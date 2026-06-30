#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;

DX12DescriptorCollectPipeline &DX12DescriptorCollectPipeline::Get()
{
	static DX12DescriptorCollectPipeline pipeline;
	return pipeline;
}

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *source)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.FlushPendingCopyRange("RecordDescriptor");
	DescriptorRecord asyncRecord = record;
	if (!pipeline.EnqueueDescriptorTask(std::move(asyncRecord), source))
		::RecordDescriptor(std::move(record));
	pipeline.MaybeLogInlineStats(source ? source : "RecordDescriptor");
}

void DX12DescriptorCollectPipeline::RecordDescriptorCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	if (!device || count == 0)
		return;

	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.FlushPendingDescriptorWrites("RecordDescriptorCopyRange");

	std::unique_lock<std::mutex> lock(pipeline.mutex);
	pipeline.copyRangeCalls++;
	pipeline.copiedDescriptors += count;
	if (!pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source)) {
		DX12DescriptorCollectTask flushTask = pipeline.pendingCopy;
		pipeline.pendingCopy = DX12DescriptorCollectTask();
		pipeline.hasPendingCopy = false;
		pipeline.flushedCopyRanges++;
		pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source);
		lock.unlock();
		::RecordDescriptorCopyRange(
			flushTask.device, flushTask.type, flushTask.destStart, flushTask.srcStart, flushTask.count);
		pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
		return;
	}
	lock.unlock();
	pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	{
		std::lock_guard<std::mutex> lock(pipeline.mutex);
		pipeline.drainCalls++;
	}
	pipeline.FlushPendingCopyRange(reason ? reason : "DrainForSnapshot");
	pipeline.FlushPendingDescriptorWrites(reason ? reason : "DrainForSnapshot");
	pipeline.MaybeLogInlineStats(reason ? reason : "DrainForSnapshot");
}

void DX12DescriptorCollectPipeline::Stop()
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.FlushPendingCopyRange("Stop");
	pipeline.FlushPendingDescriptorWrites("Stop");
	pipeline.StopWorker();
	pipeline.LogStats("StopInline", 0, 0);
}

void DX12DescriptorCollectPipeline::StartWorker()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([this]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

void DX12DescriptorCollectPipeline::StopWorker()
{
	if (!started.load(std::memory_order_acquire))
		return;

	stopRequested.store(true, std::memory_order_release);
	wake.notify_all();
	idle.notify_all();
	if (worker.joinable())
		worker.join();
	started.store(false, std::memory_order_release);

	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"Stopped\",\"pendingDescriptorWrites\":%zu,\"activeDescriptorBatches\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu",
		PendingCount(),
		static_cast<unsigned long long>([this]() -> uint64_t {
			std::lock_guard<std::mutex> lock(mutex);
			return activeDescriptorBatches;
		}()),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)));
}

bool DX12DescriptorCollectPipeline::EnqueueDescriptorTask(DescriptorRecord &&record, const char *source)
{
	(void)source;
	if (!record.cpuHandle)
		return false;
	if (ShouldTrackFullDescriptorMetadata())
		return false;

	StartWorker();
	if (!RetainDescriptorResourcesForAsync(&record))
		return false;

	std::lock_guard<std::mutex> lock(mutex);
	RecordDescriptorWriteStats(record);
	auto found = pendingDescriptorWrites.find(record.cpuHandle);
	if (found != pendingDescriptorWrites.end()) {
		ReleaseDescriptorRecordResources(&found->second);
		found->second = std::move(record);
		descriptorWriteReplaced++;
	} else {
		pendingDescriptorWrites.emplace(record.cpuHandle, std::move(record));
	}
	descriptorWriteQueued++;
	const uint64_t pending = static_cast<uint64_t>(pendingDescriptorWrites.size());
	uint64_t currentMax = maxPendingDescriptorWrites.load(std::memory_order_relaxed);
	while (pending > currentMax &&
		!maxPendingDescriptorWrites.compare_exchange_weak(
			currentMax, pending, std::memory_order_relaxed)) {
	}
	wake.notify_one();
	return true;
}

bool DX12DescriptorCollectPipeline::EnqueueCopyRangeTask(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	RecordDescriptorCopyRange(device, type, destStart, srcStart, count, source);
	return true;
}

size_t DX12DescriptorCollectPipeline::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return pendingDescriptorWrites.size() + (hasPendingCopy ? 1u : 0u);
}

void DX12DescriptorCollectPipeline::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12DescriptorCollectBatch batch;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [this]() {
				return stopRequested.load(std::memory_order_acquire) ||
					!pendingDescriptorWrites.empty();
			});
			if (pendingDescriptorWrites.empty()) {
				if (stopRequested.load(std::memory_order_acquire))
					break;
				continue;
			}
			TryStealDescriptorBatchLocked(&batch);
			activeDescriptorBatches++;
		}

		ApplyDescriptorBatch(&batch, "Worker");

		{
			std::lock_guard<std::mutex> lock(mutex);
			if (activeDescriptorBatches > 0)
				activeDescriptorBatches--;
		}
		completedBatches.fetch_add(1, std::memory_order_relaxed);
		idle.notify_all();
	}

	idle.notify_all();
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pendingDescriptorWrites\":%zu",
		GetCurrentThreadId(), PendingCount());
}

void DX12DescriptorCollectPipeline::DrainPendingOnCaller(const char *reason)
{
	DrainForSnapshot(reason);
}

void DX12DescriptorCollectPipeline::ProcessTask(DX12DescriptorCollectTask *task, const char *source)
{
	if (!task)
		return;
	if (task->kind == DX12DescriptorCollectTaskKind::Descriptor)
		RecordDescriptor(std::move(task->descriptor), source);
	else if (task->kind == DX12DescriptorCollectTaskKind::CopyRange)
		RecordDescriptorCopyRange(task->device, task->type, task->destStart, task->srcStart, task->count, source);
}

void DX12DescriptorCollectPipeline::ReleaseTask(DX12DescriptorCollectTask *task)
{
	if (!task)
		return;
	ReleaseDescriptorRecordResources(&task->descriptor);
}

void DX12DescriptorCollectPipeline::UpdateMaxDepth(size_t)
{
}

void DX12DescriptorCollectPipeline::LogStats(const char *, uint64_t, size_t)
{
	uint64_t descriptorWritesSnapshot = 0;
	uint64_t descriptorWriteBatchesSnapshot = 0;
	uint64_t descriptorWriteQueuedSnapshot = 0;
	uint64_t descriptorWriteReplacedSnapshot = 0;
	uint64_t copyRangeCallsSnapshot = 0;
	uint64_t copyRangeMergedSnapshot = 0;
	uint64_t copiedDescriptorsSnapshot = 0;
	uint64_t flushedCopyRangesSnapshot = 0;
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeDescriptorBatchesSnapshot = 0;
	bool hasPendingCopySnapshot = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		descriptorWritesSnapshot = descriptorWrites;
		descriptorWriteBatchesSnapshot = descriptorWriteBatches;
		descriptorWriteQueuedSnapshot = descriptorWriteQueued;
		descriptorWriteReplacedSnapshot = descriptorWriteReplaced;
		copyRangeCallsSnapshot = copyRangeCalls;
		copyRangeMergedSnapshot = copyRangeMerged;
		copiedDescriptorsSnapshot = copiedDescriptors;
		flushedCopyRangesSnapshot = flushedCopyRanges;
		drainCallsSnapshot = drainCalls;
		maxMergedCountSnapshot = maxMergedCount;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeDescriptorBatchesSnapshot = activeDescriptorBatches;
		hasPendingCopySnapshot = hasPendingCopy;
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"flushedCopyRanges\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"activeDescriptorBatches\":%llu,\"pendingCopy\":%u,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(descriptorWriteBatchesSnapshot),
		static_cast<unsigned long long>(descriptorWriteQueuedSnapshot),
		static_cast<unsigned long long>(descriptorWriteReplacedSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(flushedCopyRangesSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		static_cast<unsigned long long>(pendingDescriptorWritesSnapshot),
		static_cast<unsigned long long>(activeDescriptorBatchesSnapshot),
		hasPendingCopySnapshot ? 1 : 0,
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}

void DX12DescriptorCollectPipeline::FlushPendingCopyRange(const char *reason)
{
	DX12DescriptorCollectTask task;
	std::unique_lock<std::mutex> lock(mutex);
	if (!hasPendingCopy) {
		lock.unlock();
		return;
	}
	task = pendingCopy;
	pendingCopy = DX12DescriptorCollectTask();
	hasPendingCopy = false;
	flushedCopyRanges++;
	lock.unlock();
	::RecordDescriptorCopyRange(task.device, task.type, task.destStart, task.srcStart, task.count);
	(void)reason;
}

void DX12DescriptorCollectPipeline::FlushPendingDescriptorWrites(const char *reason)
{
	for (;;) {
		DX12DescriptorCollectBatch batch;
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (pendingDescriptorWrites.empty() && activeDescriptorBatches == 0) {
				lock.unlock();
				return;
			}
			if (!pendingDescriptorWrites.empty() && activeDescriptorBatches == 0) {
				TryStealDescriptorBatchLocked(&batch);
				lock.unlock();
			} else {
				idle.wait(lock, [this]() {
					return activeDescriptorBatches == 0;
				});
				lock.unlock();
				continue;
			}
		}
		ApplyDescriptorBatch(&batch, reason ? reason : "FlushPendingDescriptorWrites");
	}
}

bool DX12DescriptorCollectPipeline::TryMergePendingCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	if (!device || count == 0)
		return true;
	const UINT increment = device->GetDescriptorHandleIncrementSize(type);
	if (increment == 0)
		return false;

	if (!hasPendingCopy) {
		pendingCopy = DX12DescriptorCollectTask();
		pendingCopy.kind = DX12DescriptorCollectTaskKind::CopyRange;
		pendingCopy.device = device;
		pendingCopy.type = type;
		pendingCopy.destStart = destStart;
		pendingCopy.srcStart = srcStart;
		pendingCopy.count = count;
		pendingCopy.source = source ? source : "";
		pendingCopy.enqueueThreadId = GetCurrentThreadId();
		hasPendingCopy = true;
		if (count > maxMergedCount)
			maxMergedCount = count;
		return true;
	}

	if (pendingCopy.device != device || pendingCopy.type != type ||
			pendingCopy.enqueueThreadId != GetCurrentThreadId())
		return false;
	const SIZE_T expectedDest = pendingCopy.destStart.ptr +
		static_cast<SIZE_T>(pendingCopy.count) * increment;
	const SIZE_T expectedSrc = pendingCopy.srcStart.ptr +
		static_cast<SIZE_T>(pendingCopy.count) * increment;
	if (destStart.ptr != expectedDest || srcStart.ptr != expectedSrc)
		return false;
	const UINT64 merged = static_cast<UINT64>(pendingCopy.count) + count;
	if (merged > UINT_MAX)
		return false;
	pendingCopy.count = static_cast<UINT>(merged);
	copyRangeMerged++;
	if (pendingCopy.count > maxMergedCount)
		maxMergedCount = pendingCopy.count;
	return true;
}

bool DX12DescriptorCollectPipeline::RetainDescriptorResourcesForAsync(DescriptorRecord *record)
{
	if (!record)
		return false;
	ID3D12Resource *retainedResource = nullptr;
	if (record->resource) {
		if (!DX12ResourceSnapshotUtils::TryAddRef(record->resource))
			return false;
		retainedResource = record->resource;
	}
	ID3D12Resource *retainedCounterResource = nullptr;
	if (record->counterResource) {
		if (!DX12ResourceSnapshotUtils::TryAddRef(record->counterResource)) {
			if (retainedResource)
				DX12ResourceSnapshotUtils::Release(retainedResource);
			return false;
		}
		retainedCounterResource = record->counterResource;
	}
	record->ownsResource = retainedResource != nullptr;
	record->ownsCounterResource = retainedCounterResource != nullptr;
	return true;
}

void DX12DescriptorCollectPipeline::ReleaseDescriptorBatch(DX12DescriptorCollectBatch *batch)
{
	if (!batch)
		return;
	for (DescriptorRecord &record : batch->descriptors)
		ReleaseDescriptorRecordResources(&record);
	batch->descriptors.clear();
}

bool DX12DescriptorCollectPipeline::TryStealDescriptorBatchLocked(DX12DescriptorCollectBatch *batch)
{
	if (!batch || pendingDescriptorWrites.empty())
		return false;
	batch->descriptors.reserve(pendingDescriptorWrites.size());
	batch->sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	batch->enqueueThreadId = GetCurrentThreadId();
	for (auto &entry : pendingDescriptorWrites)
		batch->descriptors.push_back(std::move(entry.second));
	pendingDescriptorWrites.clear();
	descriptorWriteBatches++;
	enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
	return !batch->descriptors.empty();
}

void DX12DescriptorCollectPipeline::ApplyDescriptorBatch(
	DX12DescriptorCollectBatch *batch, const char *source)
{
	if (!batch || batch->descriptors.empty())
		return;
	const size_t count = batch->descriptors.size();
	uint64_t activeSnapshot = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		activeSnapshot = activeDescriptorBatches;
	}
	::RecordDescriptorBatch(&batch->descriptors);
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineBatch",
		"\"source\":\"%s\",\"seq\":%llu,\"count\":%zu,\"pending\":%zu,\"active\":%llu",
		source ? source : "",
		static_cast<unsigned long long>(batch->sequence),
		count,
		PendingCount(),
		static_cast<unsigned long long>(activeSnapshot));
	ReleaseDescriptorBatch(batch);
}

void DX12DescriptorCollectPipeline::RecordDescriptorWriteStats(const DescriptorRecord &)
{
	descriptorWrites++;
}

void DX12DescriptorCollectPipeline::MaybeLogInlineStats(const char *reason)
{
	uint64_t descriptorWritesSnapshot = 0;
	uint64_t descriptorWriteBatchesSnapshot = 0;
	uint64_t descriptorWriteQueuedSnapshot = 0;
	uint64_t descriptorWriteReplacedSnapshot = 0;
	uint64_t copyRangeCallsSnapshot = 0;
	uint64_t copyRangeMergedSnapshot = 0;
	uint64_t copiedDescriptorsSnapshot = 0;
	uint64_t flushedCopyRangesSnapshot = 0;
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeDescriptorBatchesSnapshot = 0;
	bool hasPendingCopySnapshot = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		const uint64_t total = descriptorWrites + copyRangeCalls + drainCalls;
		if (total == 0 || total % kDescriptorInlineStatsInterval != 0)
			return;
		descriptorWritesSnapshot = descriptorWrites;
		descriptorWriteBatchesSnapshot = descriptorWriteBatches;
		descriptorWriteQueuedSnapshot = descriptorWriteQueued;
		descriptorWriteReplacedSnapshot = descriptorWriteReplaced;
		copyRangeCallsSnapshot = copyRangeCalls;
		copyRangeMergedSnapshot = copyRangeMerged;
		copiedDescriptorsSnapshot = copiedDescriptors;
		flushedCopyRangesSnapshot = flushedCopyRanges;
		drainCallsSnapshot = drainCalls;
		maxMergedCountSnapshot = maxMergedCount;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeDescriptorBatchesSnapshot = activeDescriptorBatches;
		hasPendingCopySnapshot = hasPendingCopy;
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"reason\":\"%s\",\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"flushedCopyRanges\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"activeDescriptorBatches\":%llu,\"pendingCopy\":%u,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
		reason ? reason : "",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(descriptorWriteBatchesSnapshot),
		static_cast<unsigned long long>(descriptorWriteQueuedSnapshot),
		static_cast<unsigned long long>(descriptorWriteReplacedSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(flushedCopyRangesSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		static_cast<unsigned long long>(pendingDescriptorWritesSnapshot),
		static_cast<unsigned long long>(activeDescriptorBatchesSnapshot),
		hasPendingCopySnapshot ? 1 : 0,
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}
