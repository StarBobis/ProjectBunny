#include "DX12CommandListRuntime.hpp"

#include "DX12CommandListStateCapture.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12IaRegion.hpp"
#include "DX12ModRuntime.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"


DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;

DX12CommandListRuntimeShard &DX12CommandListRuntime::ShardFor(ID3D12GraphicsCommandList *commandList)
{
	uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
	key ^= key >> 7;
	key *= 0x9e3779b97f4a7c15ull;
	return Shards[(key >> 17) & (kRuntimeShardCount - 1)];
}

void DX12CommandListRuntime::RefreshTrackingCache(DX12CommandListTrackingCache &cache)
{
	cache.generation = TrackingGenerationValue;
	cache.trackBindings = DX12CommandListStateCapture::ShouldTrackBindings();
	cache.recordBindingEvents = DX12CommandListStateCapture::ShouldRecordBindingEvents();
	cache.trackHuntIa = DX12CommandListStateCapture::ShouldTrackHuntIa();
	cache.trackPsoState = DX12CommandListStateCapture::ShouldTrackPsoState();
}

void DX12CommandListRuntime::TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList)
{
	auto it = shard.states.find(commandList);
	if (it == shard.states.end()) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	Tls.commandList = commandList;
	Tls.shard = &shard;
	Tls.statePtr = &it->second;
	Tls.trackingGeneration = TrackingGenerationValue;

	DX12CommandListTrackingCache &mapCache = it->second.trackingCache;
	if (mapCache.generation != TrackingGenerationValue)
		DX12CommandListRuntime::RefreshTrackingCache(mapCache);
	Tls.tracking = mapCache;
}

void DX12CommandListRuntime::GetTrackingCacheFast(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListTrackingCache &out)
{
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		out = Tls.tracking;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList)
		out = Tls.tracking;
}

DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtrFast(
	ID3D12GraphicsCommandList *commandList)
{
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue)
		return Tls.statePtr;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList)
		return Tls.statePtr;
	return nullptr;
}

void DX12CommandListRuntime::TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList)
{
	if (Tls.commandList == commandList)
		Tls.commandList = nullptr;
}

UINT64 DX12CommandListRuntime::TrackingGeneration()
{
	return TrackingGenerationValue;
}

void DX12CommandListRuntime::BumpTrackingGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&TrackingGenerationValue));
}

void DX12CommandListRuntime::Register(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states.try_emplace(commandList);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::Reset(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListTrackingCache oldCache = state.trackingCache;
	state = DX12CommandListRuntimeState();
	state.pipelineState = initialState;
	state.trackingCache = oldCache;
	DX12CommandListRuntime::RefreshTrackingCache(state.trackingCache);
	ReleaseSRWLockExclusive(&shard.lock);

	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

void DX12CommandListRuntime::RememberPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states[commandList].pipelineState = pipelineState;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::ResetIa(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		fastState->ia = DX12ActiveIaState();
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	state.ia = DX12ActiveIaState();
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		fastState->ia.hasIndexBuffer = view != nullptr;
		fastState->ia.indexBuffer = view ? *view : D3D12_INDEX_BUFFER_VIEW();
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12ActiveIaState &state = shard.states[commandList].ia;
	state.hasIndexBuffer = view != nullptr;
	state.indexBuffer = view ? *view : D3D12_INDEX_BUFFER_VIEW();
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12ActiveIaState &state = fastState->ia;
		for (UINT i = 0; i < count && startSlot + i < ARRAYSIZE(state.vertexBuffers); ++i) {
			const UINT slot = startSlot + i;
			const D3D12_VERTEX_BUFFER_VIEW *view = views ? &views[i] : nullptr;
			state.hasVertexBuffer[slot] = view != nullptr;
			state.vertexBuffers[slot] = view ? *view : D3D12_VERTEX_BUFFER_VIEW();
		}
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12ActiveIaState &state = shard.states[commandList].ia;
	for (UINT i = 0; i < count && startSlot + i < ARRAYSIZE(state.vertexBuffers); ++i) {
		const UINT slot = startSlot + i;
		const D3D12_VERTEX_BUFFER_VIEW *view = views ? &views[i] : nullptr;
		state.hasVertexBuffer[slot] = view != nullptr;
		state.vertexBuffers[slot] = view ? *view : D3D12_VERTEX_BUFFER_VIEW();
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		fastState->ia.primitiveTopology = topology;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states[commandList].ia.primitiveTopology = topology;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::BumpComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		InterlockedIncrement64(
			reinterpret_cast<volatile LONG64*>(&state->computeBindingSerial));
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		InterlockedIncrement64(
			reinterpret_cast<volatile LONG64*>(&it->second.computeBindingSerial));
	ReleaseSRWLockShared(&shard.lock);
}

bool DX12CommandListRuntime::BuildIaHashState(
	const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	if (!state)
		return false;
	*state = DX12IaHashState();

	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	const uint32_t indexHash = ia.hasIndexBuffer && indexed ?
		DX12IaRegion::HashIndexBufferView(ia.indexBuffer, draw) : 0;
	if (indexHash) {
		state->hasIndexBuffer = true;
		state->indexHash = indexHash;
		state->indexView = ia.indexBuffer;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		const uint32_t vertexHash =
			DX12IaRegion::HashVertexBufferView(slot, ia.vertexBuffers[slot], draw);
		if (!vertexHash)
			continue;
		DX12IaBufferHash item;
		item.slot = slot;
		item.hash = vertexHash;
		item.vertexView = ia.vertexBuffers[slot];
		state->vertexBuffers.push_back(item);
	}

	return state->hasIndexBuffer || !state->vertexBuffers.empty();
}

ID3D12PipelineState *DX12CommandListRuntime::GetPipelineState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state)
		return state->pipelineState;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	ID3D12PipelineState *result =
		it == shard.states.end() ? nullptr : it->second.pipelineState;
	ReleaseSRWLockShared(&shard.lock);
	return result;
}

DX12CommandListRuntimeState DX12CommandListRuntime::GetState(
	ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListRuntimeState result;
	if (!commandList)
		return result;

	const DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtr(commandList);
	if (state) {
		result = *state;
		return result;
	}
	return result;
}

const DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	return DX12CommandListRuntime::GetStatePtrFast(commandList);
}


bool DX12CommandListRuntime::GetTrackBindings(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackBindings();

	DX12CommandListTrackingCache cache;
	DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
	return cache.trackBindings;
}

bool DX12CommandListRuntime::GetRecordBindingEvents(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldRecordBindingEvents();

	DX12CommandListTrackingCache cache;
	DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
	return cache.recordBindingEvents;
}

bool DX12CommandListRuntime::GetTrackHuntIa(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackHuntIa();

	DX12CommandListTrackingCache cache;
	DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
	return cache.trackHuntIa;
}

bool DX12CommandListRuntime::GetTrackPsoState(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackPsoState();

	DX12CommandListTrackingCache cache;
	DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
	return cache.trackPsoState;
}


void DX12CommandListRuntime::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		state->cbvSrvUavHeap = cbvSrvUavHeap;
		state->samplerHeap = samplerHeap;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		it->second.cbvSrvUavHeap = cbvSrvUavHeap;
		it->second.samplerHeap = samplerHeap;
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12CommandListRuntime::GetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	if (cbvSrvUavHeap)
		*cbvSrvUavHeap = nullptr;
	if (samplerHeap)
		*samplerHeap = nullptr;
	if (!commandList)
		return false;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		if (cbvSrvUavHeap)
			*cbvSrvUavHeap = state->cbvSrvUavHeap;
		if (samplerHeap)
			*samplerHeap = state->samplerHeap;
		return true;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	bool found = false;
	if (it != shard.states.end()) {
		if (cbvSrvUavHeap)
			*cbvSrvUavHeap = it->second.cbvSrvUavHeap;
		if (samplerHeap)
			*samplerHeap = it->second.samplerHeap;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	return found;
}


