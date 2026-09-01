#pragma once

// Command-list runtime shards, TLS caches, and state capture.
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"

class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*> directStates;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext> states;
};


class DX12CommandListRuntimeTlsCache
{
public:
	// Per-thread command-list state cache. Games commonly interleave 3+
	// command lists on one recording thread (scene / shadow / UI / compute);
	// a single "previous" slot forced a shard lock + unordered_map lookup on
	// every alternation past two lists. A small move-to-front history keeps
	// the common interleave sets on the lock-free path: the hot slot is
	// checked first (two loads), then the history slots are scanned linearly
	// (cheap compared to the locked fallback).
	static constexpr size_t kHistorySlotCount = 7;

	class Slot
	{
	public:
		ID3D12GraphicsCommandList *commandList = nullptr;
		DX12CommandListRuntimeShard *shard = nullptr;
		bool stateIsDirect = false;
		UINT64 trackingGeneration = 0;
		DX12CommandListTrackingCache tracking;
		DX12SimulateContext *statePtr = nullptr;
	};

	// Hot slot, kept as direct members so the common single-list fast path
	// stays a couple of loads and compares.
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	bool stateIsDirect = false;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12SimulateContext *statePtr = nullptr;

	// History slots for additional interleaved command lists. A hit swaps
	// the matching slot into the hot position (move-to-front); a demoted hot
	// entry overwrites slots[nextSlot] ring-style, preferring an existing
	// entry for the same list so each list appears at most once.
	Slot slots[kHistorySlotCount];
	UINT32 nextSlot = 0;

	void SwapHotWith(Slot &slot)
	{
		ID3D12GraphicsCommandList *tmpCommandList = commandList;
		commandList = slot.commandList;
		slot.commandList = tmpCommandList;

		DX12CommandListRuntimeShard *tmpShard = shard;
		shard = slot.shard;
		slot.shard = tmpShard;

		bool tmpStateIsDirect = stateIsDirect;
		stateIsDirect = slot.stateIsDirect;
		slot.stateIsDirect = tmpStateIsDirect;

		UINT64 tmpTrackingGeneration = trackingGeneration;
		trackingGeneration = slot.trackingGeneration;
		slot.trackingGeneration = tmpTrackingGeneration;

		DX12CommandListTrackingCache tmpTracking = tracking;
		tracking = slot.tracking;
		slot.tracking = tmpTracking;

		DX12SimulateContext *tmpStatePtr = statePtr;
		statePtr = slot.statePtr;
		slot.statePtr = tmpStatePtr;
	}

	void ClearHot()
	{
		commandList = nullptr;
		shard = nullptr;
		stateIsDirect = false;
		trackingGeneration = 0;
		statePtr = nullptr;
	}
};


class DX12CommandListRuntime
{
public:
	static constexpr size_t kRuntimeShardCount = 64;

	static DX12CommandListRuntimeShard Shards[kRuntimeShardCount];
	static volatile UINT64 TrackingGenerationValue;
	static thread_local DX12CommandListRuntimeTlsCache Tls;

	static void RegisterDirectState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *state);
	static void UnregisterDirectState(ID3D12GraphicsCommandList *commandList);
	static IUnknown *GetCommandListIdentity(ID3D12GraphicsCommandList *commandList);

	static void BumpTrackingGeneration();
	static DX12CommandListRuntimeShard &ShardFor(ID3D12GraphicsCommandList *commandList);
	static void RefreshTrackingCache(DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState MakeTrackingState(const DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *state);
	static void TlsRefreshFromState(
		DX12SimulateContext *state,
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeShard *shard,
		bool stateIsDirect);
	static DX12SimulateContext *TlsLookup(ID3D12GraphicsCommandList *commandList);
	static void TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList);
	static void GetTrackingCacheFast(ID3D12GraphicsCommandList *commandList, DX12CommandListTrackingCache &out);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext *GetStatePtrFast(ID3D12GraphicsCommandList *commandList);
	static void TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList);

	static void Register(ID3D12GraphicsCommandList *commandList);
	static bool Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(DX12SimulateContext *state, ID3D12PipelineState *initialState);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static DX12CompiledDispatchDecision BuildDispatchDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static DX12CompiledDrawDecision BuildDrawDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides);
	static const DX12CompiledDispatchDecision *EnsureDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static const DX12CompiledDrawDecision *EnsureDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void CompileDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static void CompileDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void InvalidateResolvedDrawPlan(DX12SimulateContext *state);
	static void RememberPipelineState(DX12SimulateContext *state, ID3D12PipelineState *pipelineState);
	static void RememberPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void ResetIa(DX12SimulateContext *state);
	static void ResetIa(ID3D12GraphicsCommandList *commandList);
	// Full IA rebuild that preserves the pre-skin redirect negative cache
	// (view-keyed, generation-guarded; survives Reset by design).
	static void RebuildActiveIaStatePreservingRedirectNegatives(
		DX12ActiveIaState *ia);
	static bool RememberIndexBuffer(DX12SimulateContext *state, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberVertexBuffers(DX12SimulateContext *state, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberPrimitiveTopology(DX12SimulateContext *state, D3D12_PRIMITIVE_TOPOLOGY topology);
	static bool RememberPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void BumpIaEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList);
	static bool BuildIaHashStateForTargetsCached(DX12ActiveIaState *ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargets(const DX12ActiveIaState &ia, UINT64 iaContentEpoch, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);

	// Cross-TU cached per-view IA hash builders (DX11-style: hash saved at bind
	// time, reused at draw time, invalidated by content generation).
	static uint32_t GetOrBuildCachedIndexViewHash(DX12ActiveIaState *ia);
	static uint32_t GetOrBuildCachedVertexViewHash(DX12ActiveIaState *ia, UINT slot);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargetsCached(DX12SimulateContext *state, UINT cacheSlot, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);
	static ID3D12PipelineState *GetPipelineState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext GetState(ID3D12GraphicsCommandList *commandList);
	static const DX12SimulateContext *GetStatePtr(ID3D12GraphicsCommandList *commandList);
	static void SetDescriptorHeaps(DX12SimulateContext *state, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool RememberGraphicsRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberRootDescriptorTable(DX12SimulateContext *state, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool RememberRootDescriptorTable(ID3D12GraphicsCommandList *commandList, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void RememberGraphicsRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static bool GetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
};


class DX12CommandListStateCapture
{
public:
	static bool ShouldTrackBindings();
	static bool ShouldTrackIaBindings();
	static bool ShouldTrackPipelineStateBindings();
	static bool ShouldTrackResourceStates();
	static bool ShouldRecordBindingEvents();
	static bool ShouldTrackHuntIa();
	static bool ShouldTrackPsoState();
	static DX12CommandListTrackingState MakeTrackingState();
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *runtimeState);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState);

	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, D3D12_PRIMITIVE_TOPOLOGY topology);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12PipelineState *pipelineState);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, const D3D12_INDEX_BUFFER_VIEW *view);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
};
