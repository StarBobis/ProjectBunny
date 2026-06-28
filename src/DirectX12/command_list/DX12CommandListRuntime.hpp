#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <unordered_map>
#include <vector>

class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};

class DX12IaHashState
{
public:
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
};

class DX12CommandListTrackingCache
{
public:
	UINT64 generation = 0;
	bool trackBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
};

class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState> states;
};

class DX12CommandListRuntimeTlsCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12CommandListRuntimeState *statePtr = nullptr;
};

class DX12CommandListRuntime
{
public:
	static constexpr size_t kRuntimeShardCount = 64;

	static DX12CommandListRuntimeShard Shards[kRuntimeShardCount];
	static volatile UINT64 TrackingGenerationValue;
	static thread_local DX12CommandListRuntimeTlsCache Tls;

	static UINT64 TrackingGeneration();
	static void BumpTrackingGeneration();
	static DX12CommandListRuntimeShard &ShardFor(ID3D12GraphicsCommandList *commandList);
	static void RefreshTrackingCache(DX12CommandListTrackingCache &cache);
	static void TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList);
	static void GetTrackingCacheFast(ID3D12GraphicsCommandList *commandList, DX12CommandListTrackingCache &out);
	static DX12CommandListRuntimeState *GetStatePtrFast(ID3D12GraphicsCommandList *commandList);
	static void TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList);

	static void Register(ID3D12GraphicsCommandList *commandList);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void RememberPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void ResetIa(ID3D12GraphicsCommandList *commandList);
	static void RememberIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void RememberVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void RememberPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void BumpComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
	static bool BuildIaHashState(const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static ID3D12PipelineState *GetPipelineState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListRuntimeState GetState(ID3D12GraphicsCommandList *commandList);
	static const DX12CommandListRuntimeState *GetStatePtr(ID3D12GraphicsCommandList *commandList);
	static void SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool GetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
	static bool GetTrackBindings(ID3D12GraphicsCommandList *commandList);
	static bool GetRecordBindingEvents(ID3D12GraphicsCommandList *commandList);
	static bool GetTrackHuntIa(ID3D12GraphicsCommandList *commandList);
	static bool GetTrackPsoState(ID3D12GraphicsCommandList *commandList);
};

