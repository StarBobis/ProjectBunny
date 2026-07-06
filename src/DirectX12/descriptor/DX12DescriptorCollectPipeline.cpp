#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"

#include <chrono>

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;
static constexpr size_t kDescriptorWorkerBatchTarget = 64;
static constexpr size_t kDescriptorPendingWriteBudget = 16384;
static const std::chrono::milliseconds kDescriptorWorkerCoalesceWindow(1);

static uint64_t NextDescriptorSequence()
{
	return DX12DescriptorCollectPipeline::nextSequence.fetch_add(1, std::memory_order_relaxed);
}

static uint64_t NextDescriptorTaskId()
{
	return DX12DescriptorCollectPipeline::nextTaskId.fetch_add(1, std::memory_order_relaxed);
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
	LARGE_INTEGER runtimeBegin = DX12OuterTimingProbeUtils::Now();
	::RecordRuntimeDescriptor(CloneDescriptorRecord(record));
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"record_runtime_descriptor",
		static_cast<UINT>(DX12DescriptorCollectTaskKind::DescriptorBatch),
		1,
		DX12OuterTimingProbeUtils::ElapsedUs(runtimeBegin));
	LARGE_INTEGER enqueueBegin = DX12OuterTimingProbeUtils::Now();
	if (!EnqueueDescriptorTask(&record, source)) {
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"enqueue_descriptor_fallback",
			static_cast<UINT>(DX12DescriptorCollectTaskKind::DescriptorBatch),
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(enqueueBegin));
		LARGE_INTEGER directBegin = DX12OuterTimingProbeUtils::Now();
		::RecordDescriptor(std::move(record));
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"record_descriptor_direct",
			static_cast<UINT>(DX12DescriptorCollectTaskKind::DescriptorBatch),
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(directBegin));
	} else {
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"enqueue_descriptor",
			static_cast<UINT>(DX12DescriptorCollectTaskKind::DescriptorBatch),
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(enqueueBegin));
	}
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

	LARGE_INTEGER runtimeBegin = DX12OuterTimingProbeUtils::Now();
	::RecordRuntimeDescriptorCopyRange(device, type, destStart, srcStart, count);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"record_runtime_copy_range",
		static_cast<UINT>(DX12DescriptorCollectTaskKind::CopyRange),
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(runtimeBegin));
	Start();

	LARGE_INTEGER lockBegin = DX12OuterTimingProbeUtils::Now();
	std::unique_lock<std::mutex> lock(mapsMutex);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"copy_range_lock_wait",
		static_cast<UINT>(DX12DescriptorCollectTaskKind::CopyRange),
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(lockBegin));
	copyRangeCalls++;
	copiedDescriptors += count;
	if (!pendingDescriptorWrites.empty()) {
		LARGE_INTEGER flushBegin = DX12OuterTimingProbeUtils::Now();
		DX12DescriptorCollectBatch batch;
		size_t stolenDescriptorCount = 0;
		if (TryStealDescriptorBatchLocked(&batch)) {
			stolenDescriptorCount = batch.descriptors.size();
			QueueDescriptorBatchLocked(&batch);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"copy_range_flush_pending",
			static_cast<UINT>(DX12DescriptorCollectTaskKind::CopyRange),
			stolenDescriptorCount,
			DX12OuterTimingProbeUtils::ElapsedUs(flushBegin));
	}
	LARGE_INTEGER queueBegin = DX12OuterTimingProbeUtils::Now();
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
		TryComputeTrackedCpuHandleRange(
			device,
			type,
			srcStart,
			count,
			&task.trackedSrcCpuHandleStart,
			&task.trackedSrcCpuHandleEnd);
		if (task.trackedCpuHandleStart && task.trackedCpuHandleEnd &&
			task.trackedCpuHandleEnd >= task.trackedCpuHandleStart) {
			for (UINT offset = 0; offset < count; ++offset) {
				const SIZE_T cpuHandle =
					destStart.ptr + static_cast<SIZE_T>(offset) * device->GetDescriptorHandleIncrementSize(type);
				pendingDescriptorSequences[cpuHandle] = task.sequence;
			}
		}
		task.enqueueThreadId = GetCurrentThreadId();
		QueueTaskLocked(&task);
		enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
		if (count > maxMergedCount)
			maxMergedCount = count;
	}
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"copy_range_queue_or_merge",
		static_cast<UINT>(DX12DescriptorCollectTaskKind::CopyRange),
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(queueBegin));
	lock.unlock();
	{
		std::lock_guard<std::mutex> lk(wakeMutex);
		wakeCv.notify_one();
	}
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

void DX12DescriptorCollectPipeline::FindPendingDescriptorHandles(
	const SIZE_T *cpuHandles,
	size_t count,
	uint8_t *pending)
{
	if (!pending || count == 0)
		return;
	memset(pending, 0, count * sizeof(uint8_t));
	if (!cpuHandles)
		return;

	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	std::lock_guard<std::mutex> lock(mapsMutex);
	for (size_t index = 0; index < count; ++index) {
		const SIZE_T cpuHandle = cpuHandles[index];
		if (!cpuHandle)
			continue;
		if (pendingDescriptorSequences.find(cpuHandle) != pendingDescriptorSequences.end())
			pending[index] = 1;
	}
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"find_pending_handles",
		0,
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
}

void DX12DescriptorCollectPipeline::CopyPendingDescriptorWrites(
	const SIZE_T *cpuHandles,
	size_t count,
	DescriptorRecord *records,
	uint8_t *found)
{
	if (!cpuHandles || !records || !found || count == 0)
		return;
	memset(found, 0, count * sizeof(uint8_t));

	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	std::lock_guard<std::mutex> lock(mapsMutex);
	for (size_t index = 0; index < count; ++index) {
		const SIZE_T cpuHandle = cpuHandles[index];
		if (!cpuHandle)
			continue;
		std::unordered_map<SIZE_T, DescriptorRecord>::const_iterator pendingWrite =
			pendingDescriptorWrites.find(cpuHandle);
		if (pendingWrite == pendingDescriptorWrites.end())
			continue;
		records[index] = CloneDescriptorRecord(pendingWrite->second);
		found[index] = 1;
	}
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"copy_pending_writes",
		0,
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	{
		std::lock_guard<std::mutex> lock(mapsMutex);
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
	{
		std::lock_guard<std::mutex> lk(wakeMutex);
		wakeCv.notify_all();
	}
	idle.notify_all();
	if (worker.joinable())
		worker.join();
	started.store(false, std::memory_order_release);

	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"Stopped\",\"pendingDescriptorWrites\":%zu,\"activeQueuedTasks\":%llu,\"queuedTasks\":%zu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu",
		PendingCount(),
		static_cast<unsigned long long>([]() -> uint64_t {
			std::lock_guard<std::mutex> lock(mapsMutex);
			return activeQueuedTasks;
		}()),
		static_cast<size_t>([]() -> size_t {
			std::lock_guard<std::mutex> lock(mapsMutex);
			return taskRingBuffer.ApproximateSize();
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

	std::unique_lock<std::mutex> lock(mapsMutex);
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
	{
		std::lock_guard<std::mutex> lk(wakeMutex);
		wakeCv.notify_one();
	}
	return true;
}

size_t DX12DescriptorCollectPipeline::PendingCount()
{
	std::lock_guard<std::mutex> lock(mapsMutex);
	return pendingDescriptorWrites.size() + taskRingBuffer.ApproximateSize() +
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

	const std::chrono::steady_clock::time_point begin =
		std::chrono::steady_clock::now();
	const uint64_t completedBeforeWait = completedSequence.load(std::memory_order_acquire);
	{
		std::unique_lock<std::mutex> lock(mapsMutex);
		if (completedSequence.load(std::memory_order_acquire) < targetSequence) {
			idle.wait(lock, [targetSequence]() {
				return completedSequence.load(std::memory_order_acquire) >= targetSequence ||
					(pendingDescriptorWrites.empty() &&
						taskRingBuffer.IsEmpty() &&
						activeQueuedTasks == 0);
			});
		}
	}
	const std::chrono::steady_clock::time_point end =
		std::chrono::steady_clock::now();
	const std::chrono::microseconds elapsedDuration =
		std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
	const long long elapsedUs = static_cast<long long>(elapsedDuration.count());
	DX12OuterTimingProbeUtils::LogWorkerWait(
		"DescriptorCollectPipeline",
		reason ? reason : "",
		"sequence",
		targetSequence,
		completedSequence.load(std::memory_order_acquire),
		PendingCount(),
		static_cast<LONG64>(elapsedUs));
	if (elapsedUs >= 1000) {
		DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineWait",
			"\"reason\":\"%s\",\"mode\":\"sequence\",\"targetSequence\":%llu,\"completedBeforeWait\":%llu,\"completedAfterWait\":%llu,\"elapsedUs\":%lld",
			reason ? reason : "",
			static_cast<unsigned long long>(targetSequence),
			static_cast<unsigned long long>(completedBeforeWait),
			static_cast<unsigned long long>(completedSequence.load(std::memory_order_acquire)),
			elapsedUs);
	}
}

void DX12DescriptorCollectPipeline::WorkerLoop()
{
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"WorkerEnter\",\"threadId\":%lu",
		GetCurrentThreadId());

	for (;;) {
		std::shared_ptr<DX12DescriptorCollectTask> task;

		// Fast path: try the lock-free ring buffer first.
		task = taskRingBuffer.TryPopWithSpin(100);
		if (!task) {
			// Ring empty. Check pending descriptor writes and maybe flush.
			{
				std::lock_guard<std::mutex> lock(mapsMutex);
				if (!pendingDescriptorWrites.empty()) {
					DX12DescriptorCollectBatch batch;
					if (TryStealDescriptorBatchLocked(&batch)) {
						QueueDescriptorBatchLocked(&batch);
						// Batch tasks were just pushed to the ring buffer.
						// Fall through to retry pop outside the lock.
					}
				}
			}

			// Retry pop after potential flush.
			task = taskRingBuffer.TryPop();
			if (!task) {
				if (stopRequested.load(std::memory_order_acquire))
					break;

				// Nothing to do. Sleep waiting for producers.
				{
					std::unique_lock<std::mutex> lock(wakeMutex);
					wakeCv.wait_for(lock, std::chrono::milliseconds(1), []() {
						return stopRequested.load(std::memory_order_acquire);
					});
				}
				continue;
			}
		}

		// Got a task from the ring buffer. Update tracking under maps lock.
		UINT taskKind = 0;
		UINT64 sequence = 0;
		DWORD enqueueThreadId = 0;
		size_t itemCount = 0;
		{
			std::lock_guard<std::mutex> lock(mapsMutex);
			UnregisterTaskIndexLocked(task.get());
			queuedTaskById.erase(task->taskId);
			activeQueuedTasks++;
			SetActiveTaskLocked(*task);
			taskKind = static_cast<UINT>(task->kind);
			sequence = task->sequence;
			enqueueThreadId = task->enqueueThreadId;
			if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch)
				itemCount = task->descriptors.size();
			else if (task->kind == DX12DescriptorCollectTaskKind::CopyRange)
				itemCount = task->count;
		}

		// Process the task (no lock - same as before).
		LARGE_INTEGER taskBegin = DX12OuterTimingProbeUtils::Now();
		if (!IsTaskFullyConsumed(*task))
			ProcessTask(task.get(), "Worker");
		DX12OuterTimingProbeUtils::LogWorkerTask(
			"DescriptorCollectPipeline",
			"process_task",
			taskKind,
			sequence,
			itemCount,
			PendingCount(),
			enqueueThreadId,
			DX12OuterTimingProbeUtils::ElapsedUs(taskBegin));

		// Update visibility after processing.
		{
			std::lock_guard<std::mutex> lock(mapsMutex);
			if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
				for (size_t index = 0; index < task->indexedCpuHandles.size(); ++index) {
					const SIZE_T cpuHandle = task->indexedCpuHandles[index];
					if (!cpuHandle)
						continue;
					visibleDescriptorSequences[cpuHandle] = task->sequence;
					std::unordered_map<SIZE_T, uint64_t>::iterator foundSequence =
						pendingDescriptorSequences.find(cpuHandle);
					if (foundSequence != pendingDescriptorSequences.end() &&
						foundSequence->second <= task->sequence) {
						pendingDescriptorSequences.erase(foundSequence);
					}
				}
			} else if (task->kind == DX12DescriptorCollectTaskKind::CopyRange &&
				task->device && task->count != 0) {
				const UINT increment =
					task->device->GetDescriptorHandleIncrementSize(task->type);
				if (increment != 0) {
					for (UINT offset = 0; offset < task->count; ++offset) {
						const SIZE_T cpuHandle =
							task->destStart.ptr + static_cast<SIZE_T>(offset) * increment;
						visibleDescriptorSequences[cpuHandle] = task->sequence;
						std::unordered_map<SIZE_T, uint64_t>::iterator foundSequence =
							pendingDescriptorSequences.find(cpuHandle);
						if (foundSequence != pendingDescriptorSequences.end() &&
							foundSequence->second <= task->sequence) {
							pendingDescriptorSequences.erase(foundSequence);
						}
					}
				}
			}
			if (activeQueuedTasks > 0)
				activeQueuedTasks--;
			ClearActiveTaskLocked();
		}
		completedBatches.fetch_add(1, std::memory_order_relaxed);
		completedSequence.store(task->sequence, std::memory_order_release);
		ReleaseTask(task.get());
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
		LARGE_INTEGER compactBegin = DX12OuterTimingProbeUtils::Now();
		std::vector<DescriptorRecord> liveDescriptors;
		liveDescriptors.reserve(task->descriptors.size());
		for (size_t index = 0; index < task->descriptors.size(); ++index) {
			if (IsTaskRecordConsumed(*task, index))
				continue;
			liveDescriptors.push_back(std::move(task->descriptors[index]));
			MarkTaskRecordConsumedAfterMove(task, index);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"worker_compact_descriptor_batch",
			static_cast<UINT>(task->kind),
			task->descriptors.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(compactBegin));
		if (!liveDescriptors.empty()) {
			LARGE_INTEGER recordBegin = DX12OuterTimingProbeUtils::Now();
			::RecordDescriptorBatch(&liveDescriptors);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"DescriptorCollectPipeline",
				"worker_record_descriptor_batch",
				static_cast<UINT>(task->kind),
				liveDescriptors.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(recordBegin));
		}
		return;
	}
	if (task->kind == DX12DescriptorCollectTaskKind::CopyRange) {
		if (task->consumed)
			return;
		LARGE_INTEGER recordBegin = DX12OuterTimingProbeUtils::Now();
		::RecordDescriptorCopyRange(
			task->device, task->type, task->destStart, task->srcStart, task->count);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"worker_record_copy_range",
			static_cast<UINT>(task->kind),
			task->count,
			DX12OuterTimingProbeUtils::ElapsedUs(recordBegin));
		task->consumed = true;
	}
}

void DX12DescriptorCollectPipeline::ReleaseTask(DX12DescriptorCollectTask *task)
{
	if (!task)
		return;
	LARGE_INTEGER releaseBegin = DX12OuterTimingProbeUtils::Now();
	const size_t descriptorCount = task->descriptors.size();
	for (size_t index = 0; index < task->descriptors.size(); ++index) {
		if (IsTaskRecordConsumed(*task, index))
			continue;
		DescriptorRecord &record = task->descriptors[index];
		ReleaseDescriptorRecordResources(&record);
	}
	task->descriptors.clear();
	task->descriptorConsumed.clear();
	task->indexedCpuHandles.clear();
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"release_task",
		static_cast<UINT>(task->kind),
		descriptorCount,
		DX12OuterTimingProbeUtils::ElapsedUs(releaseBegin));
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
	uint64_t targetedInlineResolveCallsSnapshot = 0;
	uint64_t targetedInlineResolveBatchesSnapshot = 0;
	uint64_t targetedInlineResolvedWritesSnapshot = 0;
	uint64_t targetedInlineBlockedActiveSnapshot = 0;
	uint64_t targetedInlineBlockedQueuedSnapshot = 0;
	uint64_t targetedInlineNoPendingSnapshot = 0;
	uint64_t targetedQueuedPromoteCallsSnapshot = 0;
	uint64_t targetedQueuedPromotedDescriptorBatchesSnapshot = 0;
	uint64_t targetedQueuedPromotedDescriptorRecordsSnapshot = 0;
	uint64_t targetedQueuedPromotedCopyRangesSnapshot = 0;
	uint64_t targetedQueuedPromotedCopyDescriptorsSnapshot = 0;
	uint64_t targetedQueuedBlockedActiveSnapshot = 0;
	uint64_t targetedQueuedBlockedPriorSnapshot = 0;
	uint64_t targetedQueuedSkippedLargeCopySnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeQueuedTasksSnapshot = 0;
	size_t queuedTasksSnapshot = 0;
	{
		std::lock_guard<std::mutex> lock(mapsMutex);
		descriptorWritesSnapshot = descriptorWrites;
		descriptorWriteBatchesSnapshot = descriptorWriteBatches;
		descriptorWriteQueuedSnapshot = descriptorWriteQueued;
		descriptorWriteReplacedSnapshot = descriptorWriteReplaced;
		copyRangeCallsSnapshot = copyRangeCalls;
		copyRangeMergedSnapshot = copyRangeMerged;
		copiedDescriptorsSnapshot = copiedDescriptors;
		drainCallsSnapshot = drainCalls;
		maxMergedCountSnapshot = maxMergedCount;
		targetedInlineResolveCallsSnapshot = targetedInlineResolveCalls;
		targetedInlineResolveBatchesSnapshot = targetedInlineResolveBatches;
		targetedInlineResolvedWritesSnapshot = targetedInlineResolvedWrites;
		targetedInlineBlockedActiveSnapshot = targetedInlineBlockedActive;
		targetedInlineBlockedQueuedSnapshot = targetedInlineBlockedQueued;
		targetedInlineNoPendingSnapshot = targetedInlineNoPending;
		targetedQueuedPromoteCallsSnapshot = targetedQueuedPromoteCalls;
		targetedQueuedPromotedDescriptorBatchesSnapshot = targetedQueuedPromotedDescriptorBatches;
		targetedQueuedPromotedDescriptorRecordsSnapshot = targetedQueuedPromotedDescriptorRecords;
		targetedQueuedPromotedCopyRangesSnapshot = targetedQueuedPromotedCopyRanges;
		targetedQueuedPromotedCopyDescriptorsSnapshot = targetedQueuedPromotedCopyDescriptors;
		targetedQueuedBlockedActiveSnapshot = targetedQueuedBlockedActive;
		targetedQueuedBlockedPriorSnapshot = targetedQueuedBlockedPrior;
		targetedQueuedSkippedLargeCopySnapshot = targetedQueuedSkippedLargeCopy;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeQueuedTasksSnapshot = activeQueuedTasks;
		queuedTasksSnapshot = taskRingBuffer.ApproximateSize();
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"targetedInlineResolveCalls\":%llu,\"targetedInlineResolveBatches\":%llu,\"targetedInlineResolvedWrites\":%llu,\"targetedInlineBlockedActive\":%llu,\"targetedInlineBlockedQueued\":%llu,\"targetedInlineNoPending\":%llu,\"targetedQueuedPromoteCalls\":%llu,\"targetedQueuedPromotedDescriptorBatches\":%llu,\"targetedQueuedPromotedDescriptorRecords\":%llu,\"targetedQueuedPromotedCopyRanges\":%llu,\"targetedQueuedPromotedCopyDescriptors\":%llu,\"targetedQueuedBlockedActive\":%llu,\"targetedQueuedBlockedPrior\":%llu,\"targetedQueuedSkippedLargeCopy\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"completedSequence\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(descriptorWriteBatchesSnapshot),
		static_cast<unsigned long long>(descriptorWriteQueuedSnapshot),
		static_cast<unsigned long long>(descriptorWriteReplacedSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		static_cast<unsigned long long>(targetedInlineResolveCallsSnapshot),
		static_cast<unsigned long long>(targetedInlineResolveBatchesSnapshot),
		static_cast<unsigned long long>(targetedInlineResolvedWritesSnapshot),
		static_cast<unsigned long long>(targetedInlineBlockedActiveSnapshot),
		static_cast<unsigned long long>(targetedInlineBlockedQueuedSnapshot),
		static_cast<unsigned long long>(targetedInlineNoPendingSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromoteCallsSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedDescriptorBatchesSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedDescriptorRecordsSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedCopyRangesSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedCopyDescriptorsSnapshot),
		static_cast<unsigned long long>(targetedQueuedBlockedActiveSnapshot),
		static_cast<unsigned long long>(targetedQueuedBlockedPriorSnapshot),
		static_cast<unsigned long long>(targetedQueuedSkippedLargeCopySnapshot),
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
	LARGE_INTEGER lockBegin = DX12OuterTimingProbeUtils::Now();
	std::unique_lock<std::mutex> lock(mapsMutex);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"flush_lock_wait",
		0,
		0,
		DX12OuterTimingProbeUtils::ElapsedUs(lockBegin));
	if (!pendingDescriptorWrites.empty()) {
		LARGE_INTEGER queueBegin = DX12OuterTimingProbeUtils::Now();
		DX12DescriptorCollectBatch batch;
		size_t stolenDescriptorCount = 0;
		if (TryStealDescriptorBatchLocked(&batch)) {
			stolenDescriptorCount = batch.descriptors.size();
			QueueDescriptorBatchLocked(&batch);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"flush_queue_pending",
			static_cast<UINT>(DX12DescriptorCollectTaskKind::DescriptorBatch),
			stolenDescriptorCount,
			DX12OuterTimingProbeUtils::ElapsedUs(queueBegin));
	}
	{
		std::lock_guard<std::mutex> lk(wakeMutex);
		wakeCv.notify_one();
	}
	LARGE_INTEGER waitBegin = DX12OuterTimingProbeUtils::Now();
	size_t pendingBeforeWait = pendingDescriptorWrites.size() +
		taskRingBuffer.ApproximateSize() +
		static_cast<size_t>(activeQueuedTasks);
	idle.wait(lock, []() {
		return pendingDescriptorWrites.empty() &&
			taskRingBuffer.IsEmpty() &&
			activeQueuedTasks == 0;
	});
	DX12OuterTimingProbeUtils::LogWorkerWait(
		"DescriptorCollectPipeline",
		reason ? reason : "",
		"flush_idle",
		0,
		completedSequence.load(std::memory_order_acquire),
		pendingBeforeWait,
		DX12OuterTimingProbeUtils::ElapsedUs(waitBegin));
}

void DX12DescriptorCollectPipeline::EnsureDescriptorWritesVisible(
	const SIZE_T *cpuHandles,
	size_t count,
	const char *reason)
{
	if (!cpuHandles || count == 0)
		return;
	Start();
	LARGE_INTEGER uniqueBegin = DX12OuterTimingProbeUtils::Now();
	std::vector<SIZE_T> uniqueCpuHandles;
	BuildUniqueCpuHandleList(cpuHandles, count, &uniqueCpuHandles);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"ensure_build_unique_handles",
		0,
		count,
		DX12OuterTimingProbeUtils::ElapsedUs(uniqueBegin));
	size_t requestedPendingWrites = 0;
	size_t resolvedInlineWrites = 0;
	size_t promotedDescriptorRecords = 0;
	size_t promotedCopyRanges = 0;
	size_t promotedCopyDescriptors = 0;
	bool blockedByActiveTask = false;
	bool blockedByQueuedTask = false;
	bool queuedBlockedByActiveTask = false;
	bool queuedBlockedByPriorTask = false;
	bool queuedSkippedLargeCopy = false;
	size_t queuedTasksSnapshot = 0;
	uint64_t activeQueuedTasksSnapshot = 0;
	uint64_t targetSequence = 0;
	{
		LARGE_INTEGER lockBegin = DX12OuterTimingProbeUtils::Now();
		std::unique_lock<std::mutex> lock(mapsMutex);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"ensure_lock_wait",
			0,
			uniqueCpuHandles.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(lockBegin));
		LARGE_INTEGER inlineBegin = DX12OuterTimingProbeUtils::Now();
		TryResolveRequestedPendingWritesInlineLocked(
			uniqueCpuHandles,
			reason ? reason : "EnsureDescriptorWritesVisible",
			&requestedPendingWrites,
			&resolvedInlineWrites,
			&blockedByActiveTask,
			&blockedByQueuedTask);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"ensure_inline_resolve",
			0,
			uniqueCpuHandles.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(inlineBegin));
		LARGE_INTEGER promoteBegin = DX12OuterTimingProbeUtils::Now();
		TryPromoteQueuedTasksForHandlesLocked(
			uniqueCpuHandles,
			reason ? reason : "EnsureDescriptorWritesVisible",
			&promotedDescriptorRecords,
			&promotedCopyRanges,
			&promotedCopyDescriptors,
			&queuedBlockedByActiveTask,
			&queuedBlockedByPriorTask,
			&queuedSkippedLargeCopy);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"ensure_promote_queued",
			0,
			uniqueCpuHandles.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(promoteBegin));
		LARGE_INTEGER sequenceBegin = DX12OuterTimingProbeUtils::Now();
		targetSequence = RequiredSequenceForHandlesLocked(cpuHandles, count);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DescriptorCollectPipeline",
			"ensure_required_sequence",
			0,
			count,
			DX12OuterTimingProbeUtils::ElapsedUs(sequenceBegin));
		queuedTasksSnapshot = taskRingBuffer.ApproximateSize();
		activeQueuedTasksSnapshot = activeQueuedTasks;
	}
	{
		std::lock_guard<std::mutex> lk(wakeMutex);
		wakeCv.notify_one();
	}
	if (requestedPendingWrites != 0 || resolvedInlineWrites != 0 ||
			blockedByActiveTask || blockedByQueuedTask ||
			promotedDescriptorRecords != 0 ||
			promotedCopyRanges != 0 ||
			queuedBlockedByActiveTask ||
			queuedBlockedByPriorTask ||
			queuedSkippedLargeCopy) {
		DX12LogManager::JsonFunc("DX12DescriptorTargetedResolve",
			"\"reason\":\"%s\",\"requestedHandles\":%zu,\"pendingWrites\":%zu,\"inlineWrites\":%zu,\"promotedDescriptorRecords\":%zu,\"promotedCopyRanges\":%zu,\"promotedCopyDescriptors\":%zu,\"blockedByActiveTask\":%s,\"blockedByQueuedTask\":%s,\"queuedBlockedByActiveTask\":%s,\"queuedBlockedByPriorTask\":%s,\"queuedSkippedLargeCopy\":%s,\"targetSequence\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"completedSequence\":%llu",
			reason ? reason : "EnsureDescriptorWritesVisible",
			uniqueCpuHandles.size(),
			requestedPendingWrites,
			resolvedInlineWrites,
			promotedDescriptorRecords,
			promotedCopyRanges,
			promotedCopyDescriptors,
			blockedByActiveTask ? "true" : "false",
			blockedByQueuedTask ? "true" : "false",
			queuedBlockedByActiveTask ? "true" : "false",
			queuedBlockedByPriorTask ? "true" : "false",
			queuedSkippedLargeCopy ? "true" : "false",
			static_cast<unsigned long long>(targetSequence),
			queuedTasksSnapshot,
			static_cast<unsigned long long>(activeQueuedTasksSnapshot),
			static_cast<unsigned long long>(completedSequence.load(std::memory_order_acquire)));
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
	if (!QueueTaskLocked(&task))
		return false;
	enqueuedBatches.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool DX12DescriptorCollectPipeline::QueueTaskLocked(DX12DescriptorCollectTask *task)
{
	if (!task)
		return false;
	if (task->taskId == 0)
		task->taskId = NextDescriptorTaskId();
	if (task->sequence == 0)
		task->sequence = NextDescriptorSequence();
	if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		task->descriptorConsumed.assign(task->descriptors.size(), 0);
		task->indexedCpuHandles.clear();
		task->indexedCpuHandles.reserve(task->descriptors.size());
		for (size_t index = 0; index < task->descriptors.size(); ++index) {
			const DescriptorRecord &record = task->descriptors[index];
			if (record.cpuHandle)
				task->indexedCpuHandles.push_back(record.cpuHandle);
		}
	} else if (task->kind == DX12DescriptorCollectTaskKind::CopyRange) {
		task->consumed = false;
		task->indexedCpuHandles.clear();
		if (task->device && task->count != 0) {
			const UINT increment = task->device->GetDescriptorHandleIncrementSize(task->type);
			if (increment != 0) {
				task->indexedCpuHandles.reserve(task->count);
				for (UINT offset = 0; offset < task->count; ++offset) {
					const SIZE_T cpuHandle =
						task->destStart.ptr + static_cast<SIZE_T>(offset) * increment;
					if (cpuHandle)
						task->indexedCpuHandles.push_back(cpuHandle);
				}
			}
		}
	}
	std::shared_ptr<DX12DescriptorCollectTask> queuedTask =
		std::make_shared<DX12DescriptorCollectTask>(std::move(*task));
	RegisterTaskIndexLocked(queuedTask.get());
	queuedTaskById[queuedTask->taskId] = queuedTask;
	// Push to lock-free ring buffer. If full (extremely unlikely),
	// record inline as emergency fallback.
	if (!taskRingBuffer.TryPush(queuedTask)) {
		UnregisterTaskIndexLocked(queuedTask.get());
		queuedTaskById.erase(queuedTask->taskId);
		if (queuedTask->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
			::RecordDescriptorBatch(&queuedTask->descriptors);
		} else if (queuedTask->kind == DX12DescriptorCollectTaskKind::CopyRange) {
			::RecordDescriptorCopyRange(
				queuedTask->device, queuedTask->type,
				queuedTask->destStart, queuedTask->srcStart, queuedTask->count);
		}
	}
	return true;
}

bool DX12DescriptorCollectPipeline::QueueTaskToRingBuffer(DX12DescriptorCollectTask *task)
{
	if (!task)
		return false;
	std::shared_ptr<DX12DescriptorCollectTask> queuedTask =
		std::make_shared<DX12DescriptorCollectTask>(std::move(*task));
	queuedTaskById[queuedTask->taskId] = queuedTask;
	if (!taskRingBuffer.TryPush(queuedTask)) {
		queuedTaskById.erase(queuedTask->taskId);
		return false;
	}
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
	// Merge optimization removed with lock-free ring buffer.
	// Individual copy tasks are now submitted separately.
	(void)device;
	(void)type;
	(void)destStart;
	(void)srcStart;
	(void)count;
	(void)enqueueThreadId;
	return false;
}

bool DX12DescriptorCollectPipeline::CpuHandleInTrackedRange(
	SIZE_T cpuHandle,
	SIZE_T rangeStart,
	SIZE_T rangeEnd)
{
	return cpuHandle != 0 &&
		rangeStart != 0 &&
		rangeEnd >= rangeStart &&
		cpuHandle >= rangeStart &&
		cpuHandle <= rangeEnd;
}

bool DX12DescriptorCollectPipeline::TaskTouchesCpuHandleLocked(
	const DX12DescriptorCollectTask &task,
	SIZE_T cpuHandle)
{
	if (!cpuHandle)
		return false;
	if (task.kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		for (const DescriptorRecord &record : task.descriptors) {
			if (record.cpuHandle == cpuHandle)
				return true;
		}
		return false;
	}
	if (task.kind == DX12DescriptorCollectTaskKind::CopyRange) {
		if (CpuHandleInTrackedRange(
				cpuHandle,
				task.trackedCpuHandleStart,
				task.trackedCpuHandleEnd)) {
			return true;
		}
		if (CpuHandleInTrackedRange(
				cpuHandle,
				task.trackedSrcCpuHandleStart,
				task.trackedSrcCpuHandleEnd)) {
			return true;
		}
	}
	return false;
}

bool DX12DescriptorCollectPipeline::ActiveTaskTouchesCpuHandleLocked(SIZE_T cpuHandle)
{
	if (!cpuHandle)
		return false;
	for (size_t index = 0; index < activeDescriptorTaskCpuHandles.size(); ++index) {
		if (activeDescriptorTaskCpuHandles[index] == cpuHandle)
			return true;
	}
	if (CpuHandleInTrackedRange(
			cpuHandle,
			activeTaskDestCpuHandleStart,
			activeTaskDestCpuHandleEnd)) {
		return true;
	}
	if (CpuHandleInTrackedRange(
			cpuHandle,
			activeTaskSrcCpuHandleStart,
			activeTaskSrcCpuHandleEnd)) {
		return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::QueuedTasksTouchAnyCpuHandleLocked(
	const std::vector<SIZE_T> &cpuHandles)
{
	if (cpuHandles.empty())
		return false;
	for (size_t index = 0; index < cpuHandles.size(); ++index) {
		const SIZE_T cpuHandle = cpuHandles[index];
		if (!cpuHandle)
			continue;
		CleanPendingRefsForCpuHandleLocked(cpuHandle);
		std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::const_iterator found =
			pendingTaskRefsByCpuHandle.find(cpuHandle);
		if (found != pendingTaskRefsByCpuHandle.end() && !found->second.empty())
			return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::TaskTouchesAnyCpuHandleLocked(
	const DX12DescriptorCollectTask &task,
	const std::vector<SIZE_T> &cpuHandles)
{
	if (cpuHandles.empty())
		return false;
	for (size_t index = 0; index < cpuHandles.size(); ++index) {
		if (TaskTouchesCpuHandleLocked(task, cpuHandles[index]))
			return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::ActiveTaskTouchesAnyCpuHandleLocked(
	const std::vector<SIZE_T> &cpuHandles)
{
	if (cpuHandles.empty())
		return false;
	for (size_t index = 0; index < cpuHandles.size(); ++index) {
		if (ActiveTaskTouchesCpuHandleLocked(cpuHandles[index]))
			return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::PriorQueuedTasksTouchAnyCpuHandleLocked(
	size_t taskIndex,
	const std::vector<SIZE_T> &cpuHandles)
{
	(void)taskIndex;
	(void)cpuHandles;
	return false;
}

bool DX12DescriptorCollectPipeline::CpuHandleInList(
	SIZE_T cpuHandle,
	const std::vector<SIZE_T> &cpuHandles)
{
	if (!cpuHandle)
		return false;
	for (size_t index = 0; index < cpuHandles.size(); ++index) {
		if (cpuHandles[index] == cpuHandle)
			return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::BuildCopyTaskDependencyHandles(
	const DX12DescriptorCollectTask &task,
	std::vector<SIZE_T> *cpuHandles)
{
	if (!cpuHandles)
		return false;
	cpuHandles->clear();
	if (task.kind != DX12DescriptorCollectTaskKind::CopyRange ||
			!task.device ||
			task.count == 0) {
		return false;
	}
	const UINT increment = task.device->GetDescriptorHandleIncrementSize(task.type);
	if (increment == 0)
		return false;
	cpuHandles->reserve(static_cast<size_t>(task.count) * 2);
	for (UINT offset = 0; offset < task.count; ++offset) {
		const SIZE_T destHandle =
			task.destStart.ptr + static_cast<SIZE_T>(offset) * increment;
		const SIZE_T srcHandle =
			task.srcStart.ptr + static_cast<SIZE_T>(offset) * increment;
		if (destHandle && !CpuHandleInList(destHandle, *cpuHandles))
			cpuHandles->push_back(destHandle);
		if (srcHandle && !CpuHandleInList(srcHandle, *cpuHandles))
			cpuHandles->push_back(srcHandle);
	}
	return !cpuHandles->empty();
}

void DX12DescriptorCollectPipeline::RegisterTaskIndexLocked(DX12DescriptorCollectTask *task)
{
	if (!task || task->taskId == 0)
		return;
	if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		for (size_t recordIndex = 0; recordIndex < task->descriptors.size(); ++recordIndex) {
			if (IsTaskRecordConsumed(*task, recordIndex))
				continue;
			const DescriptorRecord &record = task->descriptors[recordIndex];
			RegisterIndexHandleLocked(task, record.cpuHandle, recordIndex, false);
		}
		return;
	}
	if (task->kind == DX12DescriptorCollectTaskKind::CopyRange &&
			task->device && task->count != 0) {
		const UINT increment = task->device->GetDescriptorHandleIncrementSize(task->type);
		if (increment == 0)
			return;
		for (UINT offset = 0; offset < task->count; ++offset) {
			const SIZE_T destHandle =
				task->destStart.ptr + static_cast<SIZE_T>(offset) * increment;
			const SIZE_T srcHandle =
				task->srcStart.ptr + static_cast<SIZE_T>(offset) * increment;
			RegisterIndexHandleLocked(task, destHandle, static_cast<size_t>(offset), false);
			RegisterIndexHandleLocked(task, srcHandle, static_cast<size_t>(offset), true);
		}
	}
}

void DX12DescriptorCollectPipeline::RegisterIndexHandleLocked(
	DX12DescriptorCollectTask *task,
	SIZE_T cpuHandle,
	size_t recordIndex,
	bool sourceDependency)
{
	if (!task || task->taskId == 0 || !cpuHandle)
		return;
	DX12DescriptorPendingTaskRef taskRef;
	taskRef.taskId = task->taskId;
	taskRef.recordIndex = recordIndex;
	taskRef.sourceDependency = sourceDependency;
	pendingTaskRefsByCpuHandle[cpuHandle].push_back(taskRef);
}

void DX12DescriptorCollectPipeline::RemoveTaskIndexHandleRefLocked(
	SIZE_T cpuHandle,
	uint64_t taskId,
	size_t recordIndex,
	bool sourceDependency)
{
	if (!cpuHandle || taskId == 0)
		return;
	std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::iterator found =
		pendingTaskRefsByCpuHandle.find(cpuHandle);
	if (found == pendingTaskRefsByCpuHandle.end())
		return;
	std::vector<DX12DescriptorPendingTaskRef> &refs = found->second;
	for (size_t index = 0; index < refs.size();) {
		const DX12DescriptorPendingTaskRef &taskRef = refs[index];
		if (taskRef.taskId == taskId &&
				taskRef.recordIndex == recordIndex &&
				taskRef.sourceDependency == sourceDependency) {
			refs.erase(refs.begin() + static_cast<ptrdiff_t>(index));
			continue;
		}
		++index;
	}
	if (refs.empty())
		pendingTaskRefsByCpuHandle.erase(found);
}

void DX12DescriptorCollectPipeline::UnregisterTaskIndexLocked(DX12DescriptorCollectTask *task)
{
	if (!task || task->taskId == 0)
		return;
	if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		for (size_t recordIndex = 0; recordIndex < task->descriptors.size(); ++recordIndex) {
			const DescriptorRecord &record = task->descriptors[recordIndex];
			RemoveTaskIndexHandleRefLocked(record.cpuHandle, task->taskId, recordIndex, false);
		}
		return;
	}
	if (task->kind == DX12DescriptorCollectTaskKind::CopyRange &&
			task->device && task->count != 0) {
		const UINT increment = task->device->GetDescriptorHandleIncrementSize(task->type);
		if (increment == 0)
			return;
		for (UINT offset = 0; offset < task->count; ++offset) {
			const SIZE_T destHandle =
				task->destStart.ptr + static_cast<SIZE_T>(offset) * increment;
			const SIZE_T srcHandle =
				task->srcStart.ptr + static_cast<SIZE_T>(offset) * increment;
			RemoveTaskIndexHandleRefLocked(destHandle, task->taskId, static_cast<size_t>(offset), false);
			RemoveTaskIndexHandleRefLocked(srcHandle, task->taskId, static_cast<size_t>(offset), true);
		}
	}
}

void DX12DescriptorCollectPipeline::CleanPendingRefsForCpuHandleLocked(SIZE_T cpuHandle)
{
	if (!cpuHandle)
		return;
	std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::iterator found =
		pendingTaskRefsByCpuHandle.find(cpuHandle);
	if (found == pendingTaskRefsByCpuHandle.end())
		return;
	std::vector<DX12DescriptorPendingTaskRef> &refs = found->second;
	for (size_t index = 0; index < refs.size();) {
		const DX12DescriptorPendingTaskRef &taskRef = refs[index];
		std::unordered_map<uint64_t, std::shared_ptr<DX12DescriptorCollectTask>>::iterator taskFound =
			queuedTaskById.find(taskRef.taskId);
		if (taskFound == queuedTaskById.end() || !taskFound->second) {
			refs.erase(refs.begin() + static_cast<ptrdiff_t>(index));
			continue;
		}
		const DX12DescriptorCollectTask &task = *taskFound->second;
		if (task.kind == DX12DescriptorCollectTaskKind::DescriptorBatch &&
				IsTaskRecordConsumed(task, taskRef.recordIndex)) {
			refs.erase(refs.begin() + static_cast<ptrdiff_t>(index));
			continue;
		}
		if (task.kind == DX12DescriptorCollectTaskKind::CopyRange && task.consumed) {
			refs.erase(refs.begin() + static_cast<ptrdiff_t>(index));
			continue;
		}
		++index;
	}
	if (refs.empty())
		pendingTaskRefsByCpuHandle.erase(found);
}

bool DX12DescriptorCollectPipeline::IsTaskRecordConsumed(
	const DX12DescriptorCollectTask &task,
	size_t recordIndex)
{
	return recordIndex >= task.descriptorConsumed.size() ||
		task.descriptorConsumed[recordIndex] != 0;
}

bool DX12DescriptorCollectPipeline::IsTaskFullyConsumed(const DX12DescriptorCollectTask &task)
{
	if (task.kind == DX12DescriptorCollectTaskKind::CopyRange)
		return task.consumed;
	if (task.kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		for (size_t index = 0; index < task.descriptors.size(); ++index) {
			if (!IsTaskRecordConsumed(task, index))
				return false;
		}
		return true;
	}
	return false;
}

void DX12DescriptorCollectPipeline::MarkTaskRecordConsumedAfterMove(
	DX12DescriptorCollectTask *task,
	size_t recordIndex)
{
	if (!task)
		return;
	if (recordIndex < task->descriptorConsumed.size())
		task->descriptorConsumed[recordIndex] = 1;
	if (recordIndex < task->descriptors.size()) {
		task->descriptors[recordIndex].resource = nullptr;
		task->descriptors[recordIndex].counterResource = nullptr;
		task->descriptors[recordIndex].ownsResource = false;
		task->descriptors[recordIndex].ownsCounterResource = false;
	}
}

void DX12DescriptorCollectPipeline::MarkDescriptorHandleVisibleAfterInlineLocked(
	SIZE_T cpuHandle,
	uint64_t taskSequence)
{
	if (!cpuHandle)
		return;
	visibleDescriptorSequences[cpuHandle] =
		completedSequence.load(std::memory_order_acquire);
	std::unordered_map<SIZE_T, uint64_t>::iterator foundSequence =
		pendingDescriptorSequences.find(cpuHandle);
	if (foundSequence != pendingDescriptorSequences.end() &&
			foundSequence->second <= taskSequence) {
		pendingDescriptorSequences.erase(foundSequence);
	}
}

void DX12DescriptorCollectPipeline::MarkCopyRangeVisibleAfterInlineLocked(
	const DX12DescriptorCollectTask &task)
{
	if (task.kind != DX12DescriptorCollectTaskKind::CopyRange ||
			!task.device ||
			task.count == 0) {
		return;
	}
	const UINT increment = task.device->GetDescriptorHandleIncrementSize(task.type);
	if (increment == 0)
		return;
	for (UINT offset = 0; offset < task.count; ++offset) {
		const SIZE_T cpuHandle =
			task.destStart.ptr + static_cast<SIZE_T>(offset) * increment;
		MarkDescriptorHandleVisibleAfterInlineLocked(cpuHandle, task.sequence);
	}
}

bool DX12DescriptorCollectPipeline::TryPromoteQueuedTasksForHandlesLocked(
	const std::vector<SIZE_T> &uniqueCpuHandles,
	const char *reason,
	size_t *promotedDescriptorRecords,
	size_t *promotedCopyRanges,
	size_t *promotedCopyDescriptors,
	bool *blockedByActiveTask,
	bool *blockedByPriorTask,
	bool *skippedLargeCopy)
{
	if (promotedDescriptorRecords) *promotedDescriptorRecords = 0;
	if (promotedCopyRanges) *promotedCopyRanges = 0;
	if (promotedCopyDescriptors) *promotedCopyDescriptors = 0;
	if (blockedByActiveTask) *blockedByActiveTask = false;
	if (blockedByPriorTask) *blockedByPriorTask = false;
	if (skippedLargeCopy) *skippedLargeCopy = false;
	if (uniqueCpuHandles.empty())
		return false;

	targetedQueuedPromoteCalls++;
	bool promotedAny = false;
	for (size_t handleIndex = 0; handleIndex < uniqueCpuHandles.size(); ++handleIndex) {
		const SIZE_T cpuHandle = uniqueCpuHandles[handleIndex];
		CleanPendingRefsForCpuHandleLocked(cpuHandle);
		std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::iterator found =
			pendingTaskRefsByCpuHandle.find(cpuHandle);
		if (found == pendingTaskRefsByCpuHandle.end())
			continue;
		std::vector<DX12DescriptorPendingTaskRef> refs = found->second;
		for (size_t refIndex = 0; refIndex < refs.size(); ++refIndex) {
			if (TryPromoteTaskRefLocked(
					refs[refIndex],
					reason,
					promotedDescriptorRecords,
					promotedCopyRanges,
					promotedCopyDescriptors,
					blockedByActiveTask,
					blockedByPriorTask,
					skippedLargeCopy)) {
				promotedAny = true;
			}
		}
	}
	return promotedAny;
}

bool DX12DescriptorCollectPipeline::TryPromoteHandleRefsBeforeSequenceLocked(
	SIZE_T cpuHandle,
	uint64_t beforeSequence,
	const char *reason,
	size_t *promotedDescriptorRecords,
	size_t *promotedCopyRanges,
	size_t *promotedCopyDescriptors,
	bool *blockedByActiveTask,
	bool *blockedByPriorTask,
	bool *skippedLargeCopy)
{
	if (!cpuHandle || beforeSequence == 0)
		return false;
	CleanPendingRefsForCpuHandleLocked(cpuHandle);
	std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::iterator found =
		pendingTaskRefsByCpuHandle.find(cpuHandle);
	if (found == pendingTaskRefsByCpuHandle.end())
		return false;
	std::vector<DX12DescriptorPendingTaskRef> refs = found->second;
	bool promotedAny = false;
	for (size_t index = 0; index < refs.size(); ++index) {
		std::unordered_map<uint64_t, std::shared_ptr<DX12DescriptorCollectTask>>::iterator taskFound =
			queuedTaskById.find(refs[index].taskId);
		if (taskFound == queuedTaskById.end() || !taskFound->second)
			continue;
		if (taskFound->second->sequence >= beforeSequence)
			continue;
		if (TryPromoteTaskRefLocked(
				refs[index],
				reason,
				promotedDescriptorRecords,
				promotedCopyRanges,
				promotedCopyDescriptors,
				blockedByActiveTask,
				blockedByPriorTask,
				skippedLargeCopy)) {
			promotedAny = true;
		}
	}
	return promotedAny;
}

bool DX12DescriptorCollectPipeline::TryPromoteTaskRefLocked(
	const DX12DescriptorPendingTaskRef &taskRef,
	const char *reason,
	size_t *promotedDescriptorRecords,
	size_t *promotedCopyRanges,
	size_t *promotedCopyDescriptors,
	bool *blockedByActiveTask,
	bool *blockedByPriorTask,
	bool *skippedLargeCopy)
{
	std::unordered_map<uint64_t, std::shared_ptr<DX12DescriptorCollectTask>>::iterator taskFound =
		queuedTaskById.find(taskRef.taskId);
	if (taskFound == queuedTaskById.end() || !taskFound->second)
		return false;
	DX12DescriptorCollectTask *task = taskFound->second.get();
	if (task->kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		if (taskRef.recordIndex >= task->descriptors.size() ||
				IsTaskRecordConsumed(*task, taskRef.recordIndex) ||
				taskRef.sourceDependency) {
			return false;
		}
		DescriptorRecord &record = task->descriptors[taskRef.recordIndex];
		const SIZE_T promotedHandle = record.cpuHandle;
		if (!promotedHandle)
			return false;
		if (ActiveTaskTouchesCpuHandleLocked(promotedHandle)) {
			if (blockedByActiveTask) *blockedByActiveTask = true;
			targetedQueuedBlockedActive++;
			return false;
		}
		DX12DescriptorCollectBatch promotedBatch;
		promotedBatch.sequence = task->sequence;
		promotedBatch.enqueueThreadId = GetCurrentThreadId();
		promotedBatch.descriptors.push_back(std::move(record));
		MarkTaskRecordConsumedAfterMove(task, taskRef.recordIndex);
		RemoveTaskIndexHandleRefLocked(promotedHandle, task->taskId, taskRef.recordIndex, false);
		const std::chrono::steady_clock::time_point begin =
			std::chrono::steady_clock::now();
		::RecordDescriptorBatch(&promotedBatch.descriptors);
		const std::chrono::steady_clock::time_point end =
			std::chrono::steady_clock::now();
		const std::chrono::microseconds elapsedDuration =
			std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
		const long long elapsedUs = static_cast<long long>(elapsedDuration.count());
		MarkDescriptorHandleVisibleAfterInlineLocked(promotedHandle, task->sequence);
		targetedQueuedPromotedDescriptorBatches++;
		targetedQueuedPromotedDescriptorRecords++;
		if (promotedDescriptorRecords)
			*promotedDescriptorRecords += 1;
		DX12LogManager::JsonFunc("DX12DescriptorQueuedPromote",
			"\"reason\":\"%s\",\"kind\":\"DescriptorBatch\",\"records\":%zu,\"elapsedUs\":%lld,\"taskSequence\":%llu,\"taskId\":%llu,\"queuedTasks\":%zu",
			reason ? reason : "",
			static_cast<size_t>(1),
			elapsedUs,
			static_cast<unsigned long long>(task->sequence),
			static_cast<unsigned long long>(task->taskId),
			taskRingBuffer.ApproximateSize());
		return true;
	}
	if (task->kind == DX12DescriptorCollectTaskKind::CopyRange) {
		if (task->consumed || taskRef.sourceDependency)
			return false;
		if (!task->device || task->count == 0)
			return false;
		std::vector<SIZE_T> dependencyHandles;
		if (!BuildCopyTaskDependencyHandles(*task, &dependencyHandles))
			return false;
		if (ActiveTaskTouchesAnyCpuHandleLocked(dependencyHandles)) {
			if (blockedByActiveTask) *blockedByActiveTask = true;
			targetedQueuedBlockedActive++;
			return false;
		}
		if (!TryPromoteTaskDependenciesLocked(
				*task,
				reason,
				promotedDescriptorRecords,
				promotedCopyRanges,
				promotedCopyDescriptors,
				blockedByActiveTask,
				blockedByPriorTask,
				skippedLargeCopy)) {
			if (blockedByPriorTask) *blockedByPriorTask = true;
			targetedQueuedBlockedPrior++;
			return false;
		}
		const std::chrono::steady_clock::time_point begin =
			std::chrono::steady_clock::now();
		::RecordDescriptorCopyRange(
			task->device,
			task->type,
			task->destStart,
			task->srcStart,
			task->count);
		const std::chrono::steady_clock::time_point end =
			std::chrono::steady_clock::now();
		const std::chrono::microseconds elapsedDuration =
			std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
		const long long elapsedUs = static_cast<long long>(elapsedDuration.count());
		task->consumed = true;
		UnregisterTaskIndexLocked(task);
		MarkCopyRangeVisibleAfterInlineLocked(*task);
		targetedQueuedPromotedCopyRanges++;
		targetedQueuedPromotedCopyDescriptors += task->count;
		if (promotedCopyRanges)
			*promotedCopyRanges += 1;
		if (promotedCopyDescriptors)
			*promotedCopyDescriptors += task->count;
		DX12LogManager::JsonFunc("DX12DescriptorQueuedPromote",
			"\"reason\":\"%s\",\"kind\":\"CopyRange\",\"descriptors\":%u,\"elapsedUs\":%lld,\"taskSequence\":%llu,\"taskId\":%llu,\"queuedTasks\":%zu",
			reason ? reason : "",
			task->count,
			elapsedUs,
			static_cast<unsigned long long>(task->sequence),
			static_cast<unsigned long long>(task->taskId),
			taskRingBuffer.ApproximateSize());
		return true;
	}
	return false;
}

bool DX12DescriptorCollectPipeline::TryPromoteTaskDependenciesLocked(
	const DX12DescriptorCollectTask &task,
	const char *reason,
	size_t *promotedDescriptorRecords,
	size_t *promotedCopyRanges,
	size_t *promotedCopyDescriptors,
	bool *blockedByActiveTask,
	bool *blockedByPriorTask,
	bool *skippedLargeCopy)
{
	std::vector<SIZE_T> dependencyHandles;
	if (!BuildCopyTaskDependencyHandles(task, &dependencyHandles))
		return false;
	for (size_t index = 0; index < dependencyHandles.size(); ++index) {
		TryPromoteHandleRefsBeforeSequenceLocked(
			dependencyHandles[index],
			task.sequence,
			reason,
			promotedDescriptorRecords,
			promotedCopyRanges,
			promotedCopyDescriptors,
			blockedByActiveTask,
			blockedByPriorTask,
			skippedLargeCopy);
	}
	for (size_t index = 0; index < dependencyHandles.size(); ++index) {
		CleanPendingRefsForCpuHandleLocked(dependencyHandles[index]);
		std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>>::const_iterator found =
			pendingTaskRefsByCpuHandle.find(dependencyHandles[index]);
		if (found == pendingTaskRefsByCpuHandle.end())
			continue;
		for (size_t refIndex = 0; refIndex < found->second.size(); ++refIndex) {
			std::unordered_map<uint64_t, std::shared_ptr<DX12DescriptorCollectTask>>::const_iterator taskFound =
				queuedTaskById.find(found->second[refIndex].taskId);
			if (taskFound == queuedTaskById.end() || !taskFound->second)
				continue;
			if (taskFound->second->sequence < task.sequence)
				return false;
		}
	}
	return true;
}
void DX12DescriptorCollectPipeline::SetActiveTaskLocked(
	const DX12DescriptorCollectTask &task)
{
	ClearActiveTaskLocked();
	if (task.kind == DX12DescriptorCollectTaskKind::DescriptorBatch) {
		activeDescriptorTaskCpuHandles.reserve(task.descriptors.size());
		for (size_t index = 0; index < task.descriptors.size(); ++index) {
			if (IsTaskRecordConsumed(task, index))
				continue;
			const DescriptorRecord &record = task.descriptors[index];
			if (record.cpuHandle)
				activeDescriptorTaskCpuHandles.push_back(record.cpuHandle);
		}
		return;
	}
	if (task.kind == DX12DescriptorCollectTaskKind::CopyRange) {
		activeTaskDestCpuHandleStart = task.trackedCpuHandleStart;
		activeTaskDestCpuHandleEnd = task.trackedCpuHandleEnd;
		activeTaskSrcCpuHandleStart = task.trackedSrcCpuHandleStart;
		activeTaskSrcCpuHandleEnd = task.trackedSrcCpuHandleEnd;
	}
}

void DX12DescriptorCollectPipeline::ClearActiveTaskLocked()
{
	activeDescriptorTaskCpuHandles.clear();
	activeTaskDestCpuHandleStart = 0;
	activeTaskDestCpuHandleEnd = 0;
	activeTaskSrcCpuHandleStart = 0;
	activeTaskSrcCpuHandleEnd = 0;
}

bool DX12DescriptorCollectPipeline::BuildUniqueCpuHandleList(
	const SIZE_T *cpuHandles,
	size_t count,
	std::vector<SIZE_T> *uniqueCpuHandles)
{
	if (!cpuHandles || !uniqueCpuHandles)
		return false;
	uniqueCpuHandles->clear();
	uniqueCpuHandles->reserve(count);
	for (size_t index = 0; index < count; ++index) {
		const SIZE_T cpuHandle = cpuHandles[index];
		if (!cpuHandle)
			continue;
		bool alreadyAdded = false;
		for (size_t existingIndex = 0; existingIndex < uniqueCpuHandles->size(); ++existingIndex) {
			if ((*uniqueCpuHandles)[existingIndex] == cpuHandle) {
				alreadyAdded = true;
				break;
			}
		}
		if (!alreadyAdded)
			uniqueCpuHandles->push_back(cpuHandle);
	}
	return !uniqueCpuHandles->empty();
}

bool DX12DescriptorCollectPipeline::TryResolveRequestedPendingWritesInlineLocked(
	const std::vector<SIZE_T> &uniqueCpuHandles,
	const char *reason,
	size_t *pendingWrites,
	size_t *resolvedWrites,
	bool *blockedByActiveTask,
	bool *blockedByQueuedTask)
{
	if (pendingWrites) *pendingWrites = 0;
	if (resolvedWrites) *resolvedWrites = 0;
	if (blockedByActiveTask) *blockedByActiveTask = false;
	if (blockedByQueuedTask) *blockedByQueuedTask = false;
	if (uniqueCpuHandles.empty())
		return false;

	targetedInlineResolveCalls++;
	std::vector<SIZE_T> pendingCpuHandles;
	pendingCpuHandles.reserve(uniqueCpuHandles.size());
	for (size_t index = 0; index < uniqueCpuHandles.size(); ++index) {
		const SIZE_T cpuHandle = uniqueCpuHandles[index];
		std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
			pendingDescriptorWrites.find(cpuHandle);
		if (found != pendingDescriptorWrites.end())
			pendingCpuHandles.push_back(cpuHandle);
	}
	if (pendingWrites)
		*pendingWrites = pendingCpuHandles.size();
	if (pendingCpuHandles.empty()) {
		targetedInlineNoPending++;
		return false;
	}

	bool activeBlocked = false;
	for (size_t index = 0; index < pendingCpuHandles.size(); ++index) {
		if (ActiveTaskTouchesCpuHandleLocked(pendingCpuHandles[index])) {
			activeBlocked = true;
			break;
		}
	}
	bool queuedBlocked = false;
	size_t dependencyPromotedDescriptorRecords = 0;
	size_t dependencyPromotedCopyRanges = 0;
	size_t dependencyPromotedCopyDescriptors = 0;
	bool dependencyBlockedByActiveTask = false;
	bool dependencyBlockedByPriorTask = false;
	bool dependencySkippedLargeCopy = false;
	if (!activeBlocked) {
		for (size_t index = 0; index < pendingCpuHandles.size(); ++index) {
			TryPromoteHandleRefsBeforeSequenceLocked(
				pendingCpuHandles[index],
				UINT64_MAX,
				reason,
				&dependencyPromotedDescriptorRecords,
				&dependencyPromotedCopyRanges,
				&dependencyPromotedCopyDescriptors,
				&dependencyBlockedByActiveTask,
				&dependencyBlockedByPriorTask,
				&dependencySkippedLargeCopy);
		}
		queuedBlocked = QueuedTasksTouchAnyCpuHandleLocked(pendingCpuHandles);
	} else {
		queuedBlocked = QueuedTasksTouchAnyCpuHandleLocked(pendingCpuHandles);
	}
	if (blockedByActiveTask) *blockedByActiveTask = activeBlocked;
	if (blockedByQueuedTask) *blockedByQueuedTask = queuedBlocked;

	if (activeBlocked || queuedBlocked) {
		if (activeBlocked)
			targetedInlineBlockedActive++;
		if (queuedBlocked)
			targetedInlineBlockedQueued++;

		DX12DescriptorCollectBatch fallbackBatch;
		fallbackBatch.sequence = NextDescriptorSequence();
		fallbackBatch.enqueueThreadId = GetCurrentThreadId();
		fallbackBatch.descriptors.reserve(pendingCpuHandles.size());
		for (size_t index = 0; index < pendingCpuHandles.size(); ++index) {
			const SIZE_T cpuHandle = pendingCpuHandles[index];
			std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
				pendingDescriptorWrites.find(cpuHandle);
			if (found == pendingDescriptorWrites.end())
				continue;
			fallbackBatch.descriptors.push_back(std::move(found->second));
			pendingDescriptorWrites.erase(found);
			pendingDescriptorSequences[cpuHandle] = fallbackBatch.sequence;
		}
		QueueDescriptorBatchLocked(&fallbackBatch);
		return false;
	}

	DX12DescriptorCollectBatch inlineBatch;
	inlineBatch.sequence = NextDescriptorSequence();
	inlineBatch.enqueueThreadId = GetCurrentThreadId();
	inlineBatch.descriptors.reserve(pendingCpuHandles.size());
	std::vector<SIZE_T> resolvedCpuHandles;
	resolvedCpuHandles.reserve(pendingCpuHandles.size());
	for (size_t index = 0; index < pendingCpuHandles.size(); ++index) {
		const SIZE_T cpuHandle = pendingCpuHandles[index];
		std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
			pendingDescriptorWrites.find(cpuHandle);
		if (found == pendingDescriptorWrites.end())
			continue;
		inlineBatch.descriptors.push_back(std::move(found->second));
		pendingDescriptorWrites.erase(found);
		pendingDescriptorSequences.erase(cpuHandle);
		resolvedCpuHandles.push_back(cpuHandle);
	}
	if (inlineBatch.descriptors.empty())
		return false;

	const std::chrono::steady_clock::time_point begin =
		std::chrono::steady_clock::now();
	::RecordDescriptorBatch(&inlineBatch.descriptors);
	const UINT64 visibleSequence = completedSequence.load(std::memory_order_acquire);
	for (size_t index = 0; index < resolvedCpuHandles.size(); ++index)
		visibleDescriptorSequences[resolvedCpuHandles[index]] = visibleSequence;
	const std::chrono::steady_clock::time_point end =
		std::chrono::steady_clock::now();
	const std::chrono::microseconds elapsedDuration =
		std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
	const long long elapsedUs = static_cast<long long>(elapsedDuration.count());

	targetedInlineResolveBatches++;
	targetedInlineResolvedWrites += resolvedCpuHandles.size();
	if (resolvedWrites)
		*resolvedWrites = resolvedCpuHandles.size();
	DX12LogManager::JsonFunc("DX12DescriptorTargetedInline",
		"\"reason\":\"%s\",\"resolvedWrites\":%zu,\"elapsedUs\":%lld,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"completedSequence\":%llu",
		reason ? reason : "",
		resolvedCpuHandles.size(),
		elapsedUs,
		taskRingBuffer.ApproximateSize(),
		static_cast<unsigned long long>(activeQueuedTasks),
		static_cast<unsigned long long>(visibleSequence));
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
	uint64_t targetedInlineResolveCallsSnapshot = 0;
	uint64_t targetedInlineResolveBatchesSnapshot = 0;
	uint64_t targetedInlineResolvedWritesSnapshot = 0;
	uint64_t targetedInlineBlockedActiveSnapshot = 0;
	uint64_t targetedInlineBlockedQueuedSnapshot = 0;
	uint64_t targetedInlineNoPendingSnapshot = 0;
	uint64_t targetedQueuedPromoteCallsSnapshot = 0;
	uint64_t targetedQueuedPromotedDescriptorBatchesSnapshot = 0;
	uint64_t targetedQueuedPromotedDescriptorRecordsSnapshot = 0;
	uint64_t targetedQueuedPromotedCopyRangesSnapshot = 0;
	uint64_t targetedQueuedPromotedCopyDescriptorsSnapshot = 0;
	uint64_t targetedQueuedBlockedActiveSnapshot = 0;
	uint64_t targetedQueuedBlockedPriorSnapshot = 0;
	uint64_t targetedQueuedSkippedLargeCopySnapshot = 0;
	uint64_t pendingDescriptorWritesSnapshot = 0;
	uint64_t activeQueuedTasksSnapshot = 0;
	size_t queuedTasksSnapshot = 0;
	{
		std::lock_guard<std::mutex> lock(mapsMutex);
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
		targetedInlineResolveCallsSnapshot = targetedInlineResolveCalls;
		targetedInlineResolveBatchesSnapshot = targetedInlineResolveBatches;
		targetedInlineResolvedWritesSnapshot = targetedInlineResolvedWrites;
		targetedInlineBlockedActiveSnapshot = targetedInlineBlockedActive;
		targetedInlineBlockedQueuedSnapshot = targetedInlineBlockedQueued;
		targetedInlineNoPendingSnapshot = targetedInlineNoPending;
		targetedQueuedPromoteCallsSnapshot = targetedQueuedPromoteCalls;
		targetedQueuedPromotedDescriptorBatchesSnapshot = targetedQueuedPromotedDescriptorBatches;
		targetedQueuedPromotedDescriptorRecordsSnapshot = targetedQueuedPromotedDescriptorRecords;
		targetedQueuedPromotedCopyRangesSnapshot = targetedQueuedPromotedCopyRanges;
		targetedQueuedPromotedCopyDescriptorsSnapshot = targetedQueuedPromotedCopyDescriptors;
		targetedQueuedBlockedActiveSnapshot = targetedQueuedBlockedActive;
		targetedQueuedBlockedPriorSnapshot = targetedQueuedBlockedPrior;
		targetedQueuedSkippedLargeCopySnapshot = targetedQueuedSkippedLargeCopy;
		pendingDescriptorWritesSnapshot = pendingDescriptorWrites.size();
		activeQueuedTasksSnapshot = activeQueuedTasks;
		queuedTasksSnapshot = taskRingBuffer.ApproximateSize();
	}
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"reason\":\"%s\",\"descriptorWrites\":%llu,\"descriptorWriteBatches\":%llu,\"descriptorWriteQueued\":%llu,\"descriptorWriteReplaced\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"targetedInlineResolveCalls\":%llu,\"targetedInlineResolveBatches\":%llu,\"targetedInlineResolvedWrites\":%llu,\"targetedInlineBlockedActive\":%llu,\"targetedInlineBlockedQueued\":%llu,\"targetedInlineNoPending\":%llu,\"targetedQueuedPromoteCalls\":%llu,\"targetedQueuedPromotedDescriptorBatches\":%llu,\"targetedQueuedPromotedDescriptorRecords\":%llu,\"targetedQueuedPromotedCopyRanges\":%llu,\"targetedQueuedPromotedCopyDescriptors\":%llu,\"targetedQueuedBlockedActive\":%llu,\"targetedQueuedBlockedPrior\":%llu,\"targetedQueuedSkippedLargeCopy\":%llu,\"pendingDescriptorWrites\":%llu,\"queuedTasks\":%zu,\"activeQueuedTasks\":%llu,\"enqueuedBatches\":%llu,\"completedBatches\":%llu,\"completedSequence\":%llu,\"maxPendingDescriptorWrites\":%llu,\"threadId\":%lu",
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
		static_cast<unsigned long long>(targetedInlineResolveCallsSnapshot),
		static_cast<unsigned long long>(targetedInlineResolveBatchesSnapshot),
		static_cast<unsigned long long>(targetedInlineResolvedWritesSnapshot),
		static_cast<unsigned long long>(targetedInlineBlockedActiveSnapshot),
		static_cast<unsigned long long>(targetedInlineBlockedQueuedSnapshot),
		static_cast<unsigned long long>(targetedInlineNoPendingSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromoteCallsSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedDescriptorBatchesSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedDescriptorRecordsSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedCopyRangesSnapshot),
		static_cast<unsigned long long>(targetedQueuedPromotedCopyDescriptorsSnapshot),
		static_cast<unsigned long long>(targetedQueuedBlockedActiveSnapshot),
		static_cast<unsigned long long>(targetedQueuedBlockedPriorSnapshot),
		static_cast<unsigned long long>(targetedQueuedSkippedLargeCopySnapshot),
		static_cast<unsigned long long>(pendingDescriptorWritesSnapshot),
		queuedTasksSnapshot,
		static_cast<unsigned long long>(activeQueuedTasksSnapshot),
		static_cast<unsigned long long>(enqueuedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedBatches.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(completedSequence.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(maxPendingDescriptorWrites.load(std::memory_order_relaxed)),
		GetCurrentThreadId());
}
