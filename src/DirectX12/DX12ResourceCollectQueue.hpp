#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "DX12ResourceTrackerPrivate.hpp"

enum class DX12ResourceCollectTaskKind
{
	Resource,
	Descriptor,
	DescriptorCopyRange,
	ResourceBarrier,
	RootSignature
};

class DX12ResourceCollectTask
{
public:
	DX12ResourceCollectTaskKind kind = DX12ResourceCollectTaskKind::Resource;
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	bool hasDesc = false;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	bool hasHeapProperties = false;
	PlacedResourceInfo placedInfo = {};
	bool hasPlacedInfo = false;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	DescriptorRecord descriptor = {};
	const char *source = nullptr;
	ID3D12Device *device = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorDestStart = {};
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorSrcStart = {};
	UINT descriptorCount = 0;
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	ID3D12RootSignature *rootSignature = nullptr;
	std::vector<uint8_t> rootSignatureBlob;
	UINT rootSignatureNodeMask = 0;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12ResourceCollectQueue
{
public:
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<DX12ResourceCollectTask> tasks;
	std::thread worker;
	std::atomic<bool> started{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<uint64_t> nextSequence{1};
	std::atomic<uint64_t> enqueued{0};
	std::atomic<uint64_t> dequeued{0};
	std::atomic<uint64_t> completed{0};
	std::atomic<uint64_t> dropped{0};
	std::atomic<uint64_t> addRefFailed{0};
	std::atomic<uint64_t> maxDepth{0};

	static DX12ResourceCollectQueue &Get();
	static void Start();
	static void Stop();
	static bool Enqueue(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static bool EnqueueDescriptor(DescriptorRecord &&record, const char *source);
	static bool EnqueueDescriptorCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
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
	static void DrainForSnapshot(const char *reason);
	static size_t PendingCount();

	void StartWorker();
	void StopWorker();
	bool EnqueueTask(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	bool EnqueueDescriptorTask(DescriptorRecord &&record, const char *source);
	bool EnqueueDescriptorCopyRangeTask(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	bool EnqueueResourceBarrierTask(
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		const char *source);
	bool EnqueueRootSignatureTask(
		ID3D12RootSignature *rootSignature,
		UINT nodeMask,
		const void *blob,
		SIZE_T blobLength,
		const char *source);
	void WorkerLoop();
	void DrainPendingOnCaller(const char *reason);
	void ProcessTask(DX12ResourceCollectTask *task, const char *source);
	void ProcessResourceTask(DX12ResourceCollectTask *task, const char *source);
	void ProcessDescriptorTask(DX12ResourceCollectTask *task, const char *source);
	void ProcessDescriptorCopyRangeTask(DX12ResourceCollectTask *task, const char *source);
	void ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source);
	void ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	bool TryAddRefBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	void ReleaseBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	void ReleaseTaskResource(DX12ResourceCollectTask *task);
	void ReleaseTaskDevice(DX12ResourceCollectTask *task);
	void ReleaseTaskDescriptor(DX12ResourceCollectTask *task);
	void ReleaseTaskRootSignature(DX12ResourceCollectTask *task);
	void UpdateMaxDepth(size_t depth);
	void LogStats(const char *event, uint64_t sequence, size_t depth);
};
