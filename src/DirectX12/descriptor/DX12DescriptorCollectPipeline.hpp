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
	Descriptor,
	CopyRange
};

class DX12DescriptorCollectTask
{
public:
	DX12DescriptorCollectTaskKind kind = DX12DescriptorCollectTaskKind::Descriptor;
	DescriptorRecord descriptor = {};
	ID3D12Device *device = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE destStart = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart = {};
	UINT count = 0;
	std::string source;
	uint64_t sequence = 0;
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
	static DX12DescriptorCollectPipeline &Get();
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

	void StartWorker();
	void StopWorker();
	bool EnqueueDescriptorTask(DescriptorRecord *record, const char *source);
	bool EnqueueCopyRangeTask(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	size_t PendingCount();
	void WorkerLoop();
	void DrainPendingOnCaller(const char *reason);
	void ProcessTask(DX12DescriptorCollectTask *task, const char *source);
	void ReleaseTask(DX12DescriptorCollectTask *task);
	void UpdateMaxDepth(size_t depth);
	void LogStats(const char *event, uint64_t sequence, size_t depth);
	void FlushCopyTaskWithDependencies(
		DX12DescriptorCollectTask *task,
		const char *reason);
	void EnsureDescriptorWritesVisible(
		const SIZE_T *cpuHandles,
		size_t count,
		const char *reason);
	bool PendingCopyTouchesHandles(
		const SIZE_T *cpuHandles,
		size_t count);
	bool DescriptorHandleInCopyRange(
		const DX12DescriptorCollectTask &task,
		SIZE_T cpuHandle,
		bool destinationRangeOnly);
	void CollectPendingDescriptorWritesForHandlesLocked(
		const SIZE_T *cpuHandles,
		size_t count,
		DX12DescriptorCollectBatch *batch);
	void CollectPendingDescriptorWritesForCopySourcesLocked(
		const DX12DescriptorCollectTask &task,
		DX12DescriptorCollectBatch *batch,
		uint64_t *sourceWrites);
	void DiscardPendingDescriptorWritesForCopyDestLocked(
		const DX12DescriptorCollectTask &task,
		uint64_t *discardedWrites);

	std::mutex mutex;
	std::condition_variable wake;
	std::condition_variable idle;
	std::thread worker;
	std::atomic<bool> started{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<uint64_t> nextSequence{1};
	std::atomic<uint64_t> enqueuedBatches{0};
	std::atomic<uint64_t> completedBatches{0};
	std::atomic<uint64_t> maxPendingDescriptorWrites{0};
	DX12DescriptorCollectTask pendingCopy = {};
	bool hasPendingCopy = false;
	std::unordered_map<SIZE_T, DescriptorRecord> pendingDescriptorWrites;
	uint64_t activeDescriptorBatches = 0;
	uint64_t descriptorWrites = 0;
	uint64_t descriptorWriteBatches = 0;
	uint64_t descriptorWriteQueued = 0;
	uint64_t descriptorWriteReplaced = 0;
	uint64_t copyRangeCalls = 0;
	uint64_t copyRangeMerged = 0;
	uint64_t copiedDescriptors = 0;
	uint64_t flushedCopyRanges = 0;
	uint64_t drainCalls = 0;
	uint64_t maxMergedCount = 0;

	void FlushPendingCopyRange(const char *reason);
	void FlushPendingDescriptorWrites(const char *reason);
	bool TryMergePendingCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	bool RetainDescriptorResourcesForAsync(DescriptorRecord *record);
	void ReleaseDescriptorBatch(DX12DescriptorCollectBatch *batch);
	bool TryStealDescriptorBatchLocked(DX12DescriptorCollectBatch *batch);
	void ApplyDescriptorBatch(DX12DescriptorCollectBatch *batch, const char *source);
	void RecordDescriptorWriteStats(const DescriptorRecord &record);
	void MaybeLogInlineStats(const char *reason);
};
