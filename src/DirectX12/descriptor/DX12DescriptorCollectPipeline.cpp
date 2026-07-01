#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

#include <chrono>

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;
static constexpr size_t kDescriptorWorkerBatchTarget = 64;
static constexpr size_t kDescriptorPendingWriteBudget = 16384;
static const std::chrono::milliseconds kDescriptorWorkerCoalesceWindow(1);

DX12DescriptorCollectPipeline &DX12DescriptorCollectPipeline::Get()
{
	static DX12DescriptorCollectPipeline pipeline;
	return pipeline;
}

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *source)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	if (!pipeline.EnqueueDescriptorTask(&record, source))
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
	pipeline.StartWorker();

	std::unique_lock<std::mutex> lock(pipeline.mutex);
	pipeline.copyRangeCalls++;
	pipeline.copiedDescriptors += count;
	if (!pipeline.pendingDescriptorWrites.empty()) {
		DX12DescriptorCollectBatch batch;
		if (pipeline.TryStealDescriptorBatchLocked(&batch))
			pipeline.QueueDescriptorBatchLocked(&batch);
	}
	if (!pipeline.TryMergeQueuedCopyTaskLocked(
			device, type, destStart, srcStart, count, GetCurrentThreadId())) {
		DX12DescriptorCollectTask task;
		task.kind = DX12DescriptorCollectTaskKind::CopyRange;
		task.device = device;
		task.type = type;
		task.destStart = destStart;
		task.srcStart = srcStart;
		task.count = count;
		task.source = source ? source : "";
		task.sequence = pipeline.nextSequence.fetch_add(1, std::memory_order_relaxed);
		task.enqueueThreadId = GetCurrentThreadId();
		pipeline.queuedTasks.push_back(std::move(task));
		pipeline.enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
		if (count > pipeline.maxMergedCount)
			pipeline.maxMergedCount = count;
	}
	lock.unlock();
	pipeline.wake.notify_one();
	pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
}

void DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
	const SIZE_T *cpuHandles,
	size_t count,
	const char *reason)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.EnsureDescriptorWritesVisible(cpuHandles, count, reason ? reason : "EnsureDescriptorHandlesVisible");
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	{
		std::lock_guard<std::mutex> lock(pipeline.mutex);
		pipeline.drainCalls++;
	}
	pipeline.FlushPendingDescriptorWrites(reason ? reason : "DrainForSnapshot");
	pipeline.MaybeLogInlineStats(reason ? reason : "DrainForSnapshot");
}

void DX12DescriptorCollectPipeline::Stop()
{
	DX12DescriptorCollectPipeline &pipeline = Get();
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
		"\"event\":\"Stopped\",\"pendingDescriptorWrites\":%zu,\"activeQueuedTasks\":%llu,\"queuedTasks\":%zu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu",
		PendingCount(),
		static_cast<unsigned long long>([this]() -> uint64_t {
			std::lock_guard<std::mutex> lock(mutex);
			return activeQueuedTasks;
		}()),
		static_cast<size_t>([this]() -> size_t {
			std::lock_guard<std::mutex> lock(mutex);
			return queuedTasks.size();
		}()),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)));
}

bool DX12DescriptorCollectPipeline::EnqueueDescriptorTask(DescriptorRecord *record, const char *source)
{
	(void)source;
	if (!record || !record->cpuHandle)
		return false;
	if (ShouldTrackFullDescriptorMetadata())
		return false;

	StartWorker();
	if (!RetainDescriptorResourcesForAsync(record))
		return false;

	std::unique_lock<std::mutex> lock(mutex);
	RecordDescriptorWriteStats(*record);
	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		pendingDescriptorWrites.find(record->cpuHandle);
	if (found != pendingDescriptorWrites.end()) {
		ReleaseDescriptorRecordResources(&found->second);
		found->second = std::move(*record);
		descriptorWriteReplaced++;
	} else {
		pendingDescriptorWrites.emplace(record->cpuHandle, std::move(*record));
	}
	descriptorWriteQueued++;
	const uint64_t pending = static_cast<uint64_t>(pendingDescriptorWrites.size());
	uint64_t currentMax = maxPendingDescriptorWrites.load(std::memory_order_relaxed);
	while (pending > currentMax &&
		!maxPendingDescriptorWrites.compare_exchange_weak(
			currentMax, pending, std::memory_order_relaxed)) {
	}
	if (pendingDescriptorWrites.size() >= kDescriptorPendingWriteBudget) {
		DX12DescriptorCollectBatch batch;
		if (TryStealDescriptorBatchLocked(&batch))
			QueueDescriptorBatchLocked(&batch);
	}
	lock.unlock();
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
	return pendingDescriptorWrites.size() + queuedTasks.size() +
		static_cast<size_t>(activeQueuedTasks);
}

void DX12DescriptorCollectPipeline::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		DX12DescriptorCollectTask task;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [this]() {
				return stopRequested.load(std::memory_order_acquire) ||
					!pendingDescriptorWrites.empty() ||
					!queuedTasks.empty();
			});
			if (queuedTasks.empty() && !pendingDescriptorWrites.empty() &&
					!stopRequested.load(std::memory_order_acquire) &&
					pendingDescriptorWrites.size() < kDescriptorWorkerBatchTarget) {
				wake.wait_for(lock, kDescriptorWorkerCoalesceWindow, [this]() {
					return stopRequested.load(std::memory_order_acquire) ||
						!queuedTasks.empty() ||
						pendingDescriptorWrites.size() >= kDescriptorWorkerBatchTarget;
				});
			}
			if (queuedTasks.empty() && !pendingDescriptorWrites.empty()) {
				DX12DescriptorCollectBatch batch;
				if (TryStealDescriptorBatchLocked(&batch))
					QueueDescriptorBatchLocked(&batch);
			}
			if (queuedTasks.empty()) {
				if (stopRequested.load(std::memory_order_acquire))
					break;
				continue;
			}
			task = std::move(queuedTasks.front());
			queuedTasks.pop_front();
			activeQueuedTasks++;
		}

		ProcessTask(&task, "Worker");

		{
			std::lock_guard<std::mutex> lock(mutex);
			if (activeQueuedTasks > 0)
				activeQueuedTasks--;
		}
		completedBatches.fetch_add(1, std::memory_order_relaxed);
		ReleaseTask(&task);
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
	(void)source;
	if (!task)
		return;
	if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		if (!task->descriptors.empty())
			::RecordDescriptorBatch(&task->descriptors);
		return;
	}
	if (task->kind == DX12DescriptorCollectTaskKind::CopyRange) {
		::RecordDescriptorCopyRange(
			task->device, task->type, task->destStart, task->srcStart, task->count);
	}
}

void DX12DescriptorCollectPipeline::ReleaseTask(DX12DescriptorCollectTask *task)
{
	if (!task)
		return;
	for (DescriptorRecord &record : task->descriptors)
		ReleaseDescriptorRecordResources(&record);
	task->descriptors.clear();
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
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeQueuedTasksSnapshot = 0;
	size_t queuedTasksSnapshot = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		descriptorWritesSnapshot = descriptorWrites;
		descriptorWriteBatchesSnapshot = descriptorWriteBatches;
		descriptorWriteQueuedSnapshot = descriptorWriteQueued;
		descriptorWriteReplacedSnapshot = descriptorWriteReplaced;
		copyRangeCallsSnapshot = copyRangeCalls;
		copyRangeMergedSnapshot = copyRangeMerged;
		copiedDescriptorsSnapshot = copiedDescriptors;
		drainCallsSnapshot = drainCalls;
		maxMergedCountSnapshot = maxMergedCount;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeQueuedTasksSnapshot = activeQueuedTasks;
		queuedTasksSnapshot = queuedTasks.size();
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(descriptorWriteBatchesSnapshot),
		static_cast<unsigned long long>(descriptorWriteQueuedSnapshot),
		static_cast<unsigned long long>(descriptorWriteReplacedSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		static_cast<unsigned long long>(pendingDescriptorWritesSnapshot),
		queuedTasksSnapshot,
		static_cast<unsigned long long>(activeQueuedTasksSnapshot),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}

void DX12DescriptorCollectPipeline::FlushPendingDescriptorWrites(const char *reason)
{
	(void)reason;
	StartWorker();
	std::unique_lock<std::mutex> lock(mutex);
	if (!pendingDescriptorWrites.empty()) {
		DX12DescriptorCollectBatch batch;
		if (TryStealDescriptorBatchLocked(&batch))
			QueueDescriptorBatchLocked(&batch);
	}
	wake.notify_one();
	idle.wait(lock, [this]() {
		return pendingDescriptorWrites.empty() &&
			queuedTasks.empty() &&
			activeQueuedTasks == 0;
	});
}

void DX12DescriptorCollectPipeline::EnsureDescriptorWritesVisible(
	const SIZE_T *cpuHandles,
	size_t count,
	const char *reason)
{
	if (!cpuHandles || count == 0)
		return;
	DrainForSnapshot(reason ? reason : "EnsureDescriptorWritesVisible");
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

bool DX12DescriptorCollectPipeline::TryStealDescriptorBatchLocked(DX12DescriptorCollectBatch *batch)
{
	if (!batch || pendingDescriptorWrites.empty())
		return false;
	batch->descriptors.reserve(pendingDescriptorWrites.size());
	batch->sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
	batch->enqueueThreadId = GetCurrentThreadId();
	for (std::unordered_map<SIZE_T, DescriptorRecord>::value_type &entry : pendingDescriptorWrites)
		batch->descriptors.push_back(std::move(entry.second));
	pendingDescriptorWrites.clear();
	descriptorWriteBatches++;
	return !batch->descriptors.empty();
}

bool DX12DescriptorCollectPipeline::QueueDescriptorBatchLocked(DX12DescriptorCollectBatch *batch)
{
	if (!batch || batch->descriptors.empty())
		return false;
	DX12DescriptorCollectTask task;
	task.kind = DX12DescriptorCollectTaskKind::DescriptorBatch;
	task.descriptors = std::move(batch->descriptors);
	task.sequence = batch->sequence;
	task.enqueueThreadId = batch->enqueueThreadId;
	queuedTasks.push_back(std::move(task));
	enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool DX12DescriptorCollectPipeline::TryMergeQueuedCopyTaskLocked(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	DWORD enqueueThreadId)
{
	if (!device || count == 0 || queuedTasks.empty())
		return false;
	DX12DescriptorCollectTask &pendingCopy = queuedTasks.back();
	if (pendingCopy.kind != DX12DescriptorCollectTaskKind::CopyRange ||
			pendingCopy.device != device ||
			pendingCopy.type != type ||
			pendingCopy.enqueueThreadId != enqueueThreadId)
		return false;
	const UINT increment = device->GetDescriptorHandleIncrementSize(type);
	if (increment == 0)
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
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeQueuedTasksSnapshot = 0;
	size_t queuedTasksSnapshot = 0;
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
		drainCallsSnapshot = drainCalls;
		maxMergedCountSnapshot = maxMergedCount;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeQueuedTasksSnapshot = activeQueuedTasks;
		queuedTasksSnapshot = queuedTasks.size();
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"reason\":\"%s\",\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
		reason ? reason : "",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(descriptorWriteBatchesSnapshot),
		static_cast<unsigned long long>(descriptorWriteQueuedSnapshot),
		static_cast<unsigned long long>(descriptorWriteReplacedSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		static_cast<unsigned long long>(pendingDescriptorWritesSnapshot),
		queuedTasksSnapshot,
		static_cast<unsigned long long>(activeQueuedTasksSnapshot),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}
