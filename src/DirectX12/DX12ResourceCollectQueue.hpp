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
#include <vector>

#include "DX12ResourceTrackerPrivate.hpp"

enum class DX12ResourceCollectTaskKind
{
	Resource,
	ResourceBarrier,
	RootSignature,
	PsoRootSignature
};

class DX12ResourceCollectTask
{
public:
	DX12ResourceCollectTaskKind kind = DX12ResourceCollectTaskKind::Resource;
	ID3D12Resource *resource = nullptr;
	bool retainedResource = false;
	D3D12_RESOURCE_DESC desc = {};
	bool hasDesc = false;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	bool hasHeapProperties = false;
	PlacedResourceInfo placedInfo = {};
	bool hasPlacedInfo = false;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	const char *source = nullptr;
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	ID3D12RootSignature *rootSignature = nullptr;
	std::vector<uint8_t> rootSignatureBlob;
	UINT rootSignatureNodeMask = 0;
	UINT64 psoIndex = 0;
	std::string psoKind;
	ID3D12PipelineState *pipelineState = nullptr;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12ResourceCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12ResourceCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> dropped = 0;
	inline static std::atomic<uint64_t> addRefFailed = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> resourceTasksCompleted = 0;
	inline static std::atomic<uint64_t> resourceTasksElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTasksMaxElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTaskStatsSerial = 0;
	inline static uint64_t activeTasks = 0;

	static void Start();
	static void Stop();
	static bool Enqueue(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static bool EnqueueResourceBarriers(
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		const char *source);
	static bool EnqueueRootSignature(
		ID3D12RootSignature *rootSignature,
		UINT nodeMask,
		const void *blob,
		SIZE_T blobLength,
		const char *source);
	static bool EnqueuePsoRootSignature(
		UINT64 psoIndex,
		const char *kind,
		ID3D12PipelineState *pipelineState,
		ID3D12RootSignature *rootSignature,
		const char *source);
	static void DrainForSnapshot(const char *reason);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static void WorkerLoop();
	static void DrainPending(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void WaitForIdle(const char *reason);
	static void ProcessTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static bool TryAddRefBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseTaskRootSignature(DX12ResourceCollectTask *task);
	static void ReleaseTaskPsoRootSignature(DX12ResourceCollectTask *task);
	static void UpdateMaxDepth(size_t depth);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
	static void RecordResourceTaskStats(DX12ResourceCollectTask *task, const char *source, int64_t elapsedUs);
};
