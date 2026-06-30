#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

#include <chrono>

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;
static constexpr size_t kDescriptorWorkerBatchTarget = 64;
static constexpr size_t kDescriptorPendingWriteBudget = 16384;
static constexpr auto kDescriptorWorkerCoalesceWindow = std::chrono::milliseconds(1);

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

	std::unique_lock<std::mutex> lock(pipeline.mutex);
	while (pipeline.activeDescriptorBatches != 0) {
		pipeline.idle.wait(lock, [&pipeline]() {
			return pipeline.activeDescriptorBatches == 0;
		});
	}
	pipeline.copyRangeCalls++;
	pipeline.copiedDescriptors += count;
	if (!pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source)) {
		DX12DescriptorCollectTask flushTask = pipeline.pendingCopy;
		pipeline.pendingCopy = DX12DescriptorCollectTask();
		pipeline.hasPendingCopy = false;
		pipeline.flushedCopyRanges++;
		uint64_t sourceWrites = 0;
		uint64_t discardedWrites = 0;
		DX12DescriptorCollectBatch dependencyBatch;
		pipeline.CollectPendingDescriptorWritesForCopySourcesLocked(
			flushTask,
			&dependencyBatch,
			&sourceWrites);
		pipeline.DiscardPendingDescriptorWritesForCopyDestLocked(
			flushTask,
			&discardedWrites);
		pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source);
		lock.unlock();
		if (!dependencyBatch.descriptors.empty())
			pipeline.ApplyDescriptorBatch(&dependencyBatch, "CopyRangeSourceDependency");
		if (sourceWrites || discardedWrites) {
			DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineCopyDependency",
				"\"reason\":\"%s\",\"sourceWrites\":%llu,\"discardedWrites\":%llu,\"count\":%u",
				source ? source : "",
				static_cast<unsigned long long>(sourceWrites),
				static_cast<unsigned long long>(discardedWrites),
				flushTask.count);
		}
		::RecordDescriptorCopyRange(
			flushTask.device, flushTask.type, flushTask.destStart, flushTask.srcStart, flushTask.count);
		pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
		return;
	}
	lock.unlock();
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

	bool shouldFlush = false;
	std::unique_lock<std::mutex> lock(mutex);
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
	shouldFlush = pendingDescriptorWrites.size() >= kDescriptorPendingWriteBudget;
	lock.unlock();
	wake.notify_one();
	if (shouldFlush)
		FlushPendingDescriptorWrites("PendingWriteBudget");
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
			if (!stopRequested.load(std::memory_order_acquire) &&
					pendingDescriptorWrites.size() < kDescriptorWorkerBatchTarget) {
				wake.wait_for(lock, kDescriptorWorkerCoalesceWindow, [this]() {
					return stopRequested.load(std::memory_order_acquire) ||
						pendingDescriptorWrites.size() >= kDescriptorWorkerBatchTarget;
				});
			}
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
	DX12DescriptorCollectBatch dependencyBatch;
	uint64_t sourceWrites = 0;
	uint64_t discardedWrites = 0;
	std::unique_lock<std::mutex> lock(mutex);
	while (activeDescriptorBatches != 0) {
		idle.wait(lock, [this]() {
			return activeDescriptorBatches == 0;
		});
	}
	if (!hasPendingCopy) {
		lock.unlock();
		return;
	}
	task = pendingCopy;
	pendingCopy = DX12DescriptorCollectTask();
	hasPendingCopy = false;
	flushedCopyRanges++;
	CollectPendingDescriptorWritesForCopySourcesLocked(
		task,
		&dependencyBatch,
		&sourceWrites);
	DiscardPendingDescriptorWritesForCopyDestLocked(
		task,
		&discardedWrites);
	lock.unlock();
	if (!dependencyBatch.descriptors.empty())
		ApplyDescriptorBatch(&dependencyBatch, "CopyRangeSourceDependency");
	if (sourceWrites || discardedWrites) {
		DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineCopyDependency",
			"\"reason\":\"%s\",\"sourceWrites\":%llu,\"discardedWrites\":%llu,\"count\":%u",
			reason ? reason : "",
			static_cast<unsigned long long>(sourceWrites),
			static_cast<unsigned long long>(discardedWrites),
			task.count);
	}
	::RecordDescriptorCopyRange(task.device, task.type, task.destStart, task.srcStart, task.count);
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
		completedBatches.fetch_add(1, std::memory_order_relaxed);
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

bool DX12DescriptorCollectPipeline::DescriptorHandleInCopyRange(
	const DX12DescriptorCollectTask &task,
	SIZE_T cpuHandle,
	bool destinationRangeOnly)
{
	if (!task.device || task.count == 0 || cpuHandle == 0)
		return false;
	const UINT increment = task.device->GetDescriptorHandleIncrementSize(task.type);
	if (increment == 0)
		return false;
	const SIZE_T destBegin = task.destStart.ptr;
	const SIZE_T destEnd = destBegin + static_cast<SIZE_T>(task.count) * increment;
	if (cpuHandle >= destBegin && cpuHandle < destEnd &&
		((cpuHandle - destBegin) % increment) == 0)
		return true;
	if (destinationRangeOnly)
		return false;
	const SIZE_T srcBegin = task.srcStart.ptr;
	const SIZE_T srcEnd = srcBegin + static_cast<SIZE_T>(task.count) * increment;
	return cpuHandle >= srcBegin && cpuHandle < srcEnd &&
		((cpuHandle - srcBegin) % increment) == 0;
}

bool DX12DescriptorCollectPipeline::PendingCopyTouchesHandles(
	const SIZE_T *cpuHandles,
	size_t count)
{
	if (!cpuHandles || count == 0 || !hasPendingCopy)
		return false;
	for (size_t i = 0; i < count; ++i) {
		if (DescriptorHandleInCopyRange(pendingCopy, cpuHandles[i], false))
			return true;
	}
	return false;
}

void DX12DescriptorCollectPipeline::CollectPendingDescriptorWritesForHandlesLocked(
	const SIZE_T *cpuHandles,
	size_t count,
	DX12DescriptorCollectBatch *batch)
{
	if (!cpuHandles || count == 0 || !batch)
		return;
	for (size_t i = 0; i < count; ++i) {
		const SIZE_T cpuHandle = cpuHandles[i];
		if (cpuHandle == 0)
			continue;
		auto found = pendingDescriptorWrites.find(cpuHandle);
		if (found == pendingDescriptorWrites.end())
			continue;
		batch->descriptors.push_back(std::move(found->second));
		pendingDescriptorWrites.erase(found);
	}
	if (!batch->descriptors.empty()) {
		batch->sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
		batch->enqueueThreadId = GetCurrentThreadId();
		descriptorWriteBatches++;
		enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
	}
}

void DX12DescriptorCollectPipeline::CollectPendingDescriptorWritesForCopySourcesLocked(
	const DX12DescriptorCollectTask &task,
	DX12DescriptorCollectBatch *batch,
	uint64_t *sourceWrites)
{
	if (!batch || !task.device || task.count == 0)
		return;
	const UINT increment = task.device->GetDescriptorHandleIncrementSize(task.type);
	if (increment == 0)
		return;
	for (UINT i = 0; i < task.count; ++i) {
		const SIZE_T srcHandle =
			task.srcStart.ptr + static_cast<SIZE_T>(i) * increment;
		auto found = pendingDescriptorWrites.find(srcHandle);
		if (found == pendingDescriptorWrites.end())
			continue;
		batch->descriptors.push_back(std::move(found->second));
		pendingDescriptorWrites.erase(found);
		if (sourceWrites)
			++(*sourceWrites);
	}
	if (!batch->descriptors.empty()) {
		batch->sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
		batch->enqueueThreadId = GetCurrentThreadId();
		descriptorWriteBatches++;
		enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
	}
}

void DX12DescriptorCollectPipeline::DiscardPendingDescriptorWritesForCopyDestLocked(
	const DX12DescriptorCollectTask &task,
	uint64_t *discardedWrites)
{
	if (!task.device || task.count == 0)
		return;
	const UINT increment = task.device->GetDescriptorHandleIncrementSize(task.type);
	if (increment == 0)
		return;
	for (UINT i = 0; i < task.count; ++i) {
		const SIZE_T destHandle =
			task.destStart.ptr + static_cast<SIZE_T>(i) * increment;
		auto found = pendingDescriptorWrites.find(destHandle);
		if (found == pendingDescriptorWrites.end())
			continue;
		ReleaseDescriptorRecordResources(&found->second);
		pendingDescriptorWrites.erase(found);
		if (discardedWrites)
			++(*discardedWrites);
	}
}

void DX12DescriptorCollectPipeline::EnsureDescriptorWritesVisible(
	const SIZE_T *cpuHandles,
	size_t count,
	const char *reason)
{
	if (!cpuHandles || count == 0)
		return;
	for (;;) {
		DX12DescriptorCollectBatch batch;
		DX12DescriptorCollectTask copyTask;
		bool flushCopy = false;
		{
			std::unique_lock<std::mutex> lock(mutex);
			CollectPendingDescriptorWritesForHandlesLocked(cpuHandles, count, &batch);
			if (hasPendingCopy && activeDescriptorBatches != 0) {
				idle.wait(lock, [this]() {
					return activeDescriptorBatches == 0;
				});
				lock.unlock();
				continue;
			}
			if (PendingCopyTouchesHandles(cpuHandles, count)) {
				copyTask = pendingCopy;
				pendingCopy = DX12DescriptorCollectTask();
				hasPendingCopy = false;
				flushedCopyRanges++;
				flushCopy = true;
			}
			if (batch.descriptors.empty() && !flushCopy && activeDescriptorBatches == 0) {
				lock.unlock();
				return;
			}
			if (batch.descriptors.empty() && !flushCopy) {
				idle.wait(lock, [this]() {
					return activeDescriptorBatches == 0;
				});
				lock.unlock();
				continue;
			}
			lock.unlock();
		}
		if (!batch.descriptors.empty())
			ApplyDescriptorBatch(&batch, reason ? reason : "EnsureDescriptorWritesVisible");
		if (flushCopy) {
			FlushCopyTaskWithDependencies(&copyTask, reason ? reason : "EnsureDescriptorWritesVisible");
		}
		completedBatches.fetch_add(1, std::memory_order_relaxed);
	}
}

void DX12DescriptorCollectPipeline::FlushCopyTaskWithDependencies(
	DX12DescriptorCollectTask *task,
	const char *reason)
{
	if (!task || !task->device || task->count == 0)
		return;
	DX12DescriptorCollectBatch dependencyBatch;
	uint64_t sourceWrites = 0;
	uint64_t discardedWrites = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		CollectPendingDescriptorWritesForCopySourcesLocked(
			*task,
			&dependencyBatch,
			&sourceWrites);
		DiscardPendingDescriptorWritesForCopyDestLocked(
			*task,
			&discardedWrites);
	}
	if (!dependencyBatch.descriptors.empty())
		ApplyDescriptorBatch(&dependencyBatch, "CopyRangeSourceDependency");
	if (sourceWrites || discardedWrites) {
		DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineCopyDependency",
			"\"reason\":\"%s\",\"sourceWrites\":%llu,\"discardedWrites\":%llu,\"count\":%u",
			reason ? reason : "",
			static_cast<unsigned long long>(sourceWrites),
			static_cast<unsigned long long>(discardedWrites),
			task->count);
	}
	::RecordDescriptorCopyRange(task->device, task->type, task->destStart, task->srcStart, task->count);
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
