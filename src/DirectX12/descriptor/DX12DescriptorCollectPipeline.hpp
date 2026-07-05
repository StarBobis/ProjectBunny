#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "DX12ResourceTrackerPrivate.hpp"

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
	ID3D12Device *device = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE destStart = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart = {};
	UINT count = 0;
	std::string source;
	uint64_t sequence = 0;
	SIZE_T trackedCpuHandleStart = 0;
	SIZE_T trackedCpuHandleEnd = 0;
	SIZE_T trackedSrcCpuHandleStart = 0;
	SIZE_T trackedSrcCpuHandleEnd = 0;
	DWORD enqueueThreadId = 0;
};

class DX12DescriptorCollectBatch
{
public:
	std::vector<DescriptorRecord> descriptors;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12DescriptorCollectPipeline
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueuedBatches = 0;
	inline static std::atomic<uint64_t> completedBatches = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> maxPendingDescriptorWrites = 0;
	inline static std::deque<DX12DescriptorCollectTask> queuedTasks;
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
	static bool TryMergeQueuedCopyTaskLocked(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		DWORD enqueueThreadId);
	static bool QueueDescriptorBatchLocked(DX12DescriptorCollectBatch *batch);
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
