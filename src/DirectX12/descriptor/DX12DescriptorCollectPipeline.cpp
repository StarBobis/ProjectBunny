#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

#include <chrono>

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;
static constexpr size_t kDescriptorWorkerBatchTarget = 64;
static constexpr size_t kDescriptorPendingWriteBudget = 16384;
static const std::chrono::milliseconds kDescriptorWorkerCoalesceWindow(1);

static uint64_t NextDescriptorSequence()
{
	return DX12DescriptorCollectPipeline::nextSequence.fetch_add(1, std::memory_order_relaxed);
}

static bool TryComputeTrackedCpuHandleRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	UINT count,
	SIZE_T *rangeStart,
	SIZE_T *rangeEnd)
{
	if (!device || !count || !destStart.ptr || !rangeStart || !rangeEnd)
		return false;
	const UINT increment = device->GetDescriptorHandleIncrementSize(type);
	if (increment == 0)
		return false;
	const SIZE_T span = static_cast<SIZE_T>(count - 1) * static_cast<SIZE_T>(increment);
	*rangeStart = destStart.ptr;
	*rangeEnd = destStart.ptr + span;
	return true;
}

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *source)
{
	if (!EnqueueDescriptorTask(&record, source))
		::RecordDescriptor(std::move(record));
	MaybeLogInlineStats(source ? source : "RecordDescriptor");
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

	Start();

	std::unique_lock<std::mutex> lock(mutex);
	copyRangeCalls++;
	copiedDescriptors += count;
	if (!pendingDescriptorWrites.empty()) {
		DX12DescriptorCollectBatch batch;
		if (TryStealDescriptorBatchLocked(&batch))
			QueueDescriptorBatchLocked(&batch);
	}
	if (!TryMergeQueuedCopyTaskLocked(
			device, type, destStart, srcStart, count, GetCurrentThreadId())) {
		DX12DescriptorCollectTask task;
		task.kind = DX12DescriptorCollectTaskKind::CopyRange;
		task.device = device;
		task.type = type;
		task.destStart = destStart;
		task.srcStart = srcStart;
		task.count = count;
		task.source = source ? source : "";
		task.sequence = NextDescriptorSequence();
		TryComputeTrackedCpuHandleRange(
			device,
			type,
			destStart,
			count,
			&task.trackedCpuHandleStart,
			&task.trackedCpuHandleEnd);
		if (task.trackedCpuHandleStart && task.trackedCpuHandleEnd &&
			task.trackedCpuHandleEnd >= task.trackedCpuHandleStart) {
			for (UINT offset = 0; offset < count; ++offset) {
				const SIZE_T cpuHandle =
					destStart.ptr + static_cast<SIZE_T>(offset) * device->GetDescriptorHandleIncrementSize(type);
				pendingDescriptorSequences[cpuHandle] = task.sequence;
			}
		}
		task.enqueueThreadId = GetCurrentThreadId();
		queuedTasks.push_back(std::move(task));
		enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
		if (count > maxMergedCount)
			maxMergedCount = count;
	}
	lock.unlock();
	wake.notify_one();
	MaybeLogInlineStats(source ? source : "RecordCopyRange");
}

void DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
	const SIZE_T *cpuHandles,
	size_t count,
	const char *reason)
{
	EnsureDescriptorWritesVisible(
		cpuHandles, count, reason ? reason : "EnsureDescriptorHandlesVisible");
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		drainCalls++;
	}
	FlushPendingDescriptorWrites(reason ? reason : "DrainForSnapshot");
	MaybeLogInlineStats(reason ? reason : "DrainForSnapshot");
}

void DX12DescriptorCollectPipeline::Stop()
{
	FlushPendingDescriptorWrites("Stop");
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
		static_cast<unsigned long long>([]() -> uint64_t {
			std::lock_guard<std::mutex> lock(mutex);
			return activeQueuedTasks;
		}()),
		static_cast<size_t>([]() -> size_t {
			std::lock_guard<std::mutex> lock(mutex);
			return queuedTasks.size();
		}()),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)));
	LogStats("StopInline", 0, 0);
}

void DX12DescriptorCollectPipeline::Start()
{
	bool expected = false;
	if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		return;

	stopRequested.store(false, std::memory_order_release);
	worker = std::thread([]() { WorkerLoop(); });
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"Start\",\"thread\":\"%p\"",
		worker.native_handle());
}

bool DX12DescriptorCollectPipeline::EnqueueDescriptorTask(DescriptorRecord *record, const char *source)
{
	(void)source;
	if (!record || !record->cpuHandle)
		return false;
	if (DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return false;

	Start();
	if (!RetainDescriptorResourcesForAsync(record))
		return false;

	std::unique_lock<std::mutex> lock(mutex);
	RecordDescriptorWriteStats(*record);
	const uint64_t sequence = NextDescriptorSequence();
	pendingDescriptorSequences[record->cpuHandle] = sequence;
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

size_t DX12DescriptorCollectPipeline::PendingCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return pendingDescriptorWrites.size() + queuedTasks.size() +
		static_cast<size_t>(activeQueuedTasks);
}

uint64_t DX12DescriptorCollectPipeline::RequiredSequenceForHandlesLocked(
	const SIZE_T *cpuHandles,
	size_t count)
{
	if (!cpuHandles || count == 0)
		return 0;

	uint64_t targetSequence = 0;
	for (size_t i = 0; i < count; ++i) {
		const SIZE_T cpuHandle = cpuHandles[i];
		if (!cpuHandle)
			continue;
		std::unordered_map<SIZE_T, uint64_t>::const_iterator pendingFound =
			pendingDescriptorSequences.find(cpuHandle);
		if (pendingFound != pendingDescriptorSequences.end()) {
			if (pendingFound->second > targetSequence)
				targetSequence = pendingFound->second;
			continue;
		}
		std::unordered_map<SIZE_T, uint64_t>::const_iterator visibleFound =
			visibleDescriptorSequences.find(cpuHandle);
		if (visibleFound != visibleDescriptorSequences.end() &&
			visibleFound->second > targetSequence) {
			targetSequence = visibleFound->second;
		}
	}
	return targetSequence;
}

uint64_t DX12DescriptorCollectPipeline::RequiredSequenceForTaskLocked(
	const DX12DescriptorCollectTask &task)
{
	return task.sequence;
}

void DX12DescriptorCollectPipeline::WaitForSequence(uint64_t targetSequence, const char *reason)
{
	if (targetSequence == 0)
		return;

	const auto begin = std::chrono::steady_clock::now();
	const uint64_t completedBeforeWait = completedSequence.load(std::memory_order_acquire);
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (completedSequence.load(std::memory_order_acquire) < targetSequence) {
			idle.wait(lock, [targetSequence]() {
				return completedSequence.load(std::memory_order_acquire) >= targetSequence ||
					(pendingDescriptorWrites.empty() &&
						queuedTasks.empty() &&
						activeQueuedTasks == 0);
			});
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	if (elapsedUs >= 1000) {
		DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineWait",
			"\"reason\":\"%s\",\"mode\":\"sequence\",\"targetSequence\":%llu,\"completedBeforeWait\":%llu,\"completedAfterWait\":%llu,\"elapsedUs\":%lld",
			reason ? reason : "",
			static_cast<unsigned long long>(targetSequence),
			static_cast<unsigned long long>(completedBeforeWait),
			static_cast<unsigned long long>(completedSequence.load(std::memory_order_acquire)),
			static_cast<long long>(elapsedUs));
	}
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
			wake.wait(lock, []() {
				return stopRequested.load(std::memory_order_acquire) ||
					!pendingDescriptorWrites.empty() ||
					!queuedTasks.empty();
			});
			if (queuedTasks.empty() && !pendingDescriptorWrites.empty() &&
					!stopRequested.load(std::memory_order_acquire) &&
					pendingDescriptorWrites.size() < kDescriptorWorkerBatchTarget) {
				wake.wait_for(lock, kDescriptorWorkerCoalesceWindow, []() {
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
			if (task.kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
				for (const DescriptorRecord &record : task.descriptors) {
					if (!record.cpuHandle)
						continue;
					visibleDescriptorSequences[record.cpuHandle] = task.sequence;
					std::unordered_map<SIZE_T, uint64_t>::iterator foundSequence =
						pendingDescriptorSequences.find(record.cpuHandle);
					if (foundSequence != pendingDescriptorSequences.end() &&
						foundSequence->second <= task.sequence) {
						pendingDescriptorSequences.erase(foundSequence);
					}
				}
			} else if (task.kind == DX12DescriptorCollectTaskKind::CopyRange &&
				task.device && task.count != 0) {
				const UINT increment =
					task.device->GetDescriptorHandleIncrementSize(task.type);
				if (increment != 0) {
					for (UINT offset = 0; offset < task.count; ++offset) {
						const SIZE_T cpuHandle =
							task.destStart.ptr + static_cast<SIZE_T>(offset) * increment;
						visibleDescriptorSequences[cpuHandle] = task.sequence;
						std::unordered_map<SIZE_T, uint64_t>::iterator foundSequence =
							pendingDescriptorSequences.find(cpuHandle);
						if (foundSequence != pendingDescriptorSequences.end() &&
							foundSequence->second <= task.sequence) {
							pendingDescriptorSequences.erase(foundSequence);
						}
					}
				}
			}
			if (activeQueuedTasks > 0)
				activeQueuedTasks--;
		}
		completedBatches.fetch_add(1, std::memory_order_relaxed);
		completedSequence.store(task.sequence, std::memory_order_release);
		ReleaseTask(&task);
		idle.notify_all();
	}

	idle.notify_all();
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"WorkerExit\",\"threadId\":%lu,\"pendingDescriptorWrites\":%zu",
		GetCurrentThreadId(), PendingCount());
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
		"\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"completedSequence\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
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
		static_cast<unsigned long long>(completedSequence.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}

void DX12DescriptorCollectPipeline::FlushPendingDescriptorWrites(const char *reason)
{
	(void)reason;
	Start();
	std::unique_lock<std::mutex> lock(mutex);
	if (!pendingDescriptorWrites.empty()) {
		DX12DescriptorCollectBatch batch;
		if (TryStealDescriptorBatchLocked(&batch))
			QueueDescriptorBatchLocked(&batch);
	}
	wake.notify_one();
	idle.wait(lock, []() {
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
	Start();
	uint64_t targetSequence = 0;
	{
		std::unique_lock<std::mutex> lock(mutex);
		DX12DescriptorCollectBatch batch;
		bool hasRequestedPendingWrites = false;
		for (size_t i = 0; i < count; ++i) {
			const SIZE_T cpuHandle = cpuHandles[i];
			if (!cpuHandle)
				continue;
			std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
				pendingDescriptorWrites.find(cpuHandle);
			if (found == pendingDescriptorWrites.end())
				continue;
			if (!hasRequestedPendingWrites) {
				hasRequestedPendingWrites = true;
				batch.sequence = NextDescriptorSequence();
				batch.enqueueThreadId = GetCurrentThreadId();
			}
			batch.descriptors.push_back(std::move(found->second));
			pendingDescriptorWrites.erase(found);
			pendingDescriptorSequences[cpuHandle] = batch.sequence;
		}
		if (hasRequestedPendingWrites)
			QueueDescriptorBatchLocked(&batch);
		targetSequence = RequiredSequenceForHandlesLocked(cpuHandles, count);
		wake.notify_one();
	}
	WaitForSequence(targetSequence, reason ? reason : "EnsureDescriptorWritesVisible");
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
	batch->sequence = NextDescriptorSequence();
	batch->enqueueThreadId = GetCurrentThreadId();
	for (std::unordered_map<SIZE_T, DescriptorRecord>::value_type &entry : pendingDescriptorWrites) {
		pendingDescriptorSequences[entry.first] = batch->sequence;
		batch->descriptors.push_back(std::move(entry.second));
	}
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
	const uint64_t mergedSequence = NextDescriptorSequence();
	pendingCopy.sequence = mergedSequence;
	pendingCopy.count = static_cast<UINT>(merged);
	TryComputeTrackedCpuHandleRange(
		device,
		type,
		pendingCopy.destStart,
		pendingCopy.count,
		&pendingCopy.trackedCpuHandleStart,
		&pendingCopy.trackedCpuHandleEnd);
	for (UINT offset = 0; offset < count; ++offset) {
		const SIZE_T cpuHandle =
			destStart.ptr + static_cast<SIZE_T>(offset) * increment;
		pendingDescriptorSequences[cpuHandle] = mergedSequence;
	}
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
		"\"reason\":\"%s\",\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"completedSequence\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
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
		static_cast<unsigned long long>(completedSequence.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}
