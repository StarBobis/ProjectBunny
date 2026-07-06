#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "DX12ResourceTrackerPrivate.hpp"
#include "utils/DX12DescriptorTaskRingBufferUtils.hpp"

enum class DX12DescriptorCollectTaskKind
{
	DescriptorBatch,
	CopyRange
};

class DX12DescriptorCollectTask
{
public:
	DX12DescriptorCollectTaskKind kind = DX12DescriptorCollectTaskKind::DescriptorBatch;
	std::vector<DescriptorRecord> descriptors;
	std::vector<uint8_t> descriptorConsumed;
	ID3D12Device *device = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE destStart = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart = {};
	UINT count = 0;
	std::string source;
	uint64_t taskId = 0;
	uint64_t sequence = 0;
	bool consumed = false;
	SIZE_T trackedCpuHandleStart = 0;
	SIZE_T trackedCpuHandleEnd = 0;
	SIZE_T trackedSrcCpuHandleStart = 0;
	SIZE_T trackedSrcCpuHandleEnd = 0;
	DWORD enqueueThreadId = 0;
	std::vector<SIZE_T> indexedCpuHandles;
};

class DX12DescriptorCollectBatch
{
public:
	std::vector<DescriptorRecord> descriptors;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12DescriptorPendingTaskRef
{
public:
	uint64_t taskId = 0;
	size_t recordIndex = 0;
	bool sourceDependency = false;
};

class DX12DescriptorCollectPipeline
{
public:
	// mapsMutex protects all unordered_maps and active-task tracking.
	// Queue push/pop is lock-free via taskRingBuffer (below).
	inline static std::mutex mapsMutex;
	// Lightweight wake mutex + cv used ONLY for worker sleep/wake.
	// No data is protected by this lock.
	inline static std::mutex wakeMutex;
	inline static std::condition_variable wakeCv;
	inline static std::condition_variable idle;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> nextTaskId = 1;
	inline static std::atomic<uint64_t> enqueuedBatches = 0;
	inline static std::atomic<uint64_t> completedBatches = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> maxPendingDescriptorWrites = 0;
	// Lock-free MPSC ring buffer replaces the old std::deque + mutex.
	// Producers (game threads) push tasks lock-free.
	// Consumer (worker thread) pops tasks lock-free.
	// Capacity: 65536 tasks (power of 2, ~8 MB for shared_ptr slots).
	inline static DX12DescriptorTaskRingBufferUtils<65536> taskRingBuffer;
	inline static std::unordered_map<uint64_t, std::shared_ptr<DX12DescriptorCollectTask>> queuedTaskById;
	inline static std::unordered_map<SIZE_T, std::vector<DX12DescriptorPendingTaskRef>> pendingTaskRefsByCpuHandle;
	inline static std::unordered_map<SIZE_T, DescriptorRecord> pendingDescriptorWrites;
	inline static std::unordered_map<SIZE_T, uint64_t> pendingDescriptorSequences;
	inline static std::unordered_map<SIZE_T, uint64_t> visibleDescriptorSequences;
	inline static std::vector<SIZE_T> activeDescriptorTaskCpuHandles;
	inline static SIZE_T activeTaskDestCpuHandleStart = 0;
	inline static SIZE_T activeTaskDestCpuHandleEnd = 0;
	inline static SIZE_T activeTaskSrcCpuHandleStart = 0;
	inline static SIZE_T activeTaskSrcCpuHandleEnd = 0;
	inline static uint64_t activeQueuedTasks = 0;
	inline static uint64_t descriptorWrites = 0;
	inline static uint64_t descriptorWriteBatches = 0;
	inline static uint64_t descriptorWriteQueued = 0;
	inline static uint64_t descriptorWriteReplaced = 0;
	inline static uint64_t copyRangeCalls = 0;
	inline static uint64_t copyRangeMerged = 0;
	inline static uint64_t copiedDescriptors = 0;
	inline static uint64_t drainCalls = 0;
	inline static uint64_t maxMergedCount = 0;
	inline static uint64_t targetedInlineResolveCalls = 0;
	inline static uint64_t targetedInlineResolveBatches = 0;
	inline static uint64_t targetedInlineResolvedWrites = 0;
	inline static uint64_t targetedInlineBlockedActive = 0;
	inline static uint64_t targetedInlineBlockedQueued = 0;
	inline static uint64_t targetedInlineNoPending = 0;
	inline static uint64_t targetedQueuedPromoteCalls = 0;
	inline static uint64_t targetedQueuedPromotedDescriptorBatches = 0;
	inline static uint64_t targetedQueuedPromotedDescriptorRecords = 0;
	inline static uint64_t targetedQueuedPromotedCopyRanges = 0;
	inline static uint64_t targetedQueuedPromotedCopyDescriptors = 0;
	inline static uint64_t targetedQueuedBlockedActive = 0;
	inline static uint64_t targetedQueuedBlockedPrior = 0;
	inline static uint64_t targetedQueuedSkippedLargeCopy = 0;

	static void RecordDescriptor(DescriptorRecord &&record, const char *source);
	static void RecordDescriptorCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	static void EnsureDescriptorHandlesVisible(
		const SIZE_T *cpuHandles,
		size_t count,
		const char *reason);
	static void FindPendingDescriptorHandles(
		const SIZE_T *cpuHandles,
		size_t count,
		uint8_t *pending);
	static void CopyPendingDescriptorWrites(
		const SIZE_T *cpuHandles,
		size_t count,
		DescriptorRecord *records,
		uint8_t *found);
	static void DrainForSnapshot(const char *reason);
	static void Stop();

	static void Start();
	static size_t PendingCount();
	static void WorkerLoop();
	static bool EnqueueDescriptorTask(DescriptorRecord *record, const char *source);
	static void ProcessTask(DX12DescriptorCollectTask *task, const char *source);
	static void ReleaseTask(DX12DescriptorCollectTask *task);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
	static void EnsureDescriptorWritesVisible(
		const SIZE_T *cpuHandles,
		size_t count,
		const char *reason);
	static uint64_t RequiredSequenceForHandlesLocked(
		const SIZE_T *cpuHandles,
		size_t count);
	static uint64_t RequiredSequenceForTaskLocked(const DX12DescriptorCollectTask &task);
	static void WaitForSequence(uint64_t targetSequence, const char *reason);
	// Deprecated: merge optimization removed.  The lock-free ring buffer
	// makes individual task submission fast enough that batching via merge
	// is no longer needed.  Consecutive copy ranges are processed as
	// separate tasks by the worker.
	static bool TryMergeQueuedCopyTaskLocked(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		DWORD enqueueThreadId);
	static bool QueueDescriptorBatchLocked(DX12DescriptorCollectBatch *batch);
	// Pushes a fully-prepared task to the lock-free ring buffer.
	// The caller must have already called RegisterTaskIndexLocked
	// and inserted into queuedTaskById under mapsMutex.
	static bool QueueTaskToRingBuffer(DX12DescriptorCollectTask *task);

	// Original QueueTaskLocked kept for internal compatibility during
	// transition.  Prepares the task (taskId, sequence, indexedCpuHandles),
	// registers it in maps under mapsMutex, then pushes to ring buffer.
	static bool QueueTaskLocked(DX12DescriptorCollectTask *task);
	static void FlushPendingDescriptorWrites(const char *reason);
	static bool RetainDescriptorResourcesForAsync(DescriptorRecord *record);
	static bool TryStealDescriptorBatchLocked(DX12DescriptorCollectBatch *batch);
	static void RecordDescriptorWriteStats(const DescriptorRecord &record);
	static void MaybeLogInlineStats(const char *reason);
	static bool CpuHandleInTrackedRange(
		SIZE_T cpuHandle,
		SIZE_T rangeStart,
		SIZE_T rangeEnd);
	static bool TaskTouchesCpuHandleLocked(
		const DX12DescriptorCollectTask &task,
		SIZE_T cpuHandle);
	static bool ActiveTaskTouchesCpuHandleLocked(SIZE_T cpuHandle);
	static bool QueuedTasksTouchAnyCpuHandleLocked(
		const std::vector<SIZE_T> &cpuHandles);
	static bool TaskTouchesAnyCpuHandleLocked(
		const DX12DescriptorCollectTask &task,
		const std::vector<SIZE_T> &cpuHandles);
	static bool ActiveTaskTouchesAnyCpuHandleLocked(
		const std::vector<SIZE_T> &cpuHandles);
	static bool PriorQueuedTasksTouchAnyCpuHandleLocked(
		size_t taskIndex,
		const std::vector<SIZE_T> &cpuHandles);
	static bool CpuHandleInList(
		SIZE_T cpuHandle,
		const std::vector<SIZE_T> &cpuHandles);
	static bool BuildCopyTaskDependencyHandles(
		const DX12DescriptorCollectTask &task,
		std::vector<SIZE_T> *cpuHandles);
	static void RegisterTaskIndexLocked(DX12DescriptorCollectTask *task);
	static void RegisterIndexHandleLocked(
		DX12DescriptorCollectTask *task,
		SIZE_T cpuHandle,
		size_t recordIndex,
		bool sourceDependency);
	static void RemoveTaskIndexHandleRefLocked(
		SIZE_T cpuHandle,
		uint64_t taskId,
		size_t recordIndex,
		bool sourceDependency);
	static void UnregisterTaskIndexLocked(DX12DescriptorCollectTask *task);
	static void CleanPendingRefsForCpuHandleLocked(SIZE_T cpuHandle);
	static bool IsTaskRecordConsumed(
		const DX12DescriptorCollectTask &task,
		size_t recordIndex);
	static bool IsTaskFullyConsumed(const DX12DescriptorCollectTask &task);
	static void MarkTaskRecordConsumedAfterMove(
		DX12DescriptorCollectTask *task,
		size_t recordIndex);
	static void MarkDescriptorHandleVisibleAfterInlineLocked(
		SIZE_T cpuHandle,
		uint64_t taskSequence);
	static void MarkCopyRangeVisibleAfterInlineLocked(
		const DX12DescriptorCollectTask &task);
	static bool TryPromoteQueuedTasksForHandlesLocked(
		const std::vector<SIZE_T> &uniqueCpuHandles,
		const char *reason,
		size_t *promotedDescriptorRecords,
		size_t *promotedCopyRanges,
		size_t *promotedCopyDescriptors,
		bool *blockedByActiveTask,
		bool *blockedByPriorTask,
		bool *skippedLargeCopy);
	static bool TryPromoteHandleRefsBeforeSequenceLocked(
		SIZE_T cpuHandle,
		uint64_t beforeSequence,
		const char *reason,
		size_t *promotedDescriptorRecords,
		size_t *promotedCopyRanges,
		size_t *promotedCopyDescriptors,
		bool *blockedByActiveTask,
		bool *blockedByPriorTask,
		bool *skippedLargeCopy);
	static bool TryPromoteTaskRefLocked(
		const DX12DescriptorPendingTaskRef &taskRef,
		const char *reason,
		size_t *promotedDescriptorRecords,
		size_t *promotedCopyRanges,
		size_t *promotedCopyDescriptors,
		bool *blockedByActiveTask,
		bool *blockedByPriorTask,
		bool *skippedLargeCopy);
	static bool TryPromoteTaskDependenciesLocked(
		const DX12DescriptorCollectTask &task,
		const char *reason,
		size_t *promotedDescriptorRecords,
		size_t *promotedCopyRanges,
		size_t *promotedCopyDescriptors,
		bool *blockedByActiveTask,
		bool *blockedByPriorTask,
		bool *skippedLargeCopy);
	static void SetActiveTaskLocked(const DX12DescriptorCollectTask &task);
	static void ClearActiveTaskLocked();
	static bool BuildUniqueCpuHandleList(
		const SIZE_T *cpuHandles,
		size_t count,
		std::vector<SIZE_T> *uniqueCpuHandles);
	static bool TryResolveRequestedPendingWritesInlineLocked(
		const std::vector<SIZE_T> &uniqueCpuHandles,
		const char *reason,
		size_t *pendingWrites,
		size_t *resolvedWrites,
		bool *blockedByActiveTask,
		bool *blockedByQueuedTask);
};
