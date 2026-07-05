#include "DX12CommandListRuntime.hpp"

#include "DX12CommandListStateCapture.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12IaRegion.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;
std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*> DX12CommandListRuntime::gDirectStateMap;
SRWLOCK DX12CommandListRuntime::gDirectStateMapLock = SRWLOCK_INIT;

void DX12CommandListRuntime::RegisterDirectState(
	ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *state)
{
	if (!commandList || !state)
		return;

	AcquireSRWLockExclusive(&gDirectStateMapLock);
	gDirectStateMap[commandList] = state;
	ReleaseSRWLockExclusive(&gDirectStateMapLock);
}

void DX12CommandListRuntime::UnregisterDirectState(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gDirectStateMapLock);
	gDirectStateMap.erase(commandList);
	ReleaseSRWLockExclusive(&gDirectStateMapLock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

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
	cache.trackIaBindings = DX12CommandListStateCapture::ShouldTrackIaBindings();
	cache.trackPipelineStateBindings = DX12CommandListStateCapture::ShouldTrackPipelineStateBindings();
	cache.recordBindingEvents = DX12CommandListStateCapture::ShouldRecordBindingEvents();
	cache.trackHuntIa = DX12CommandListStateCapture::ShouldTrackHuntIa();
	cache.trackPsoState = DX12CommandListStateCapture::ShouldTrackPsoState();
}

DX12CommandListTrackingState DX12CommandListRuntime::MakeTrackingState(
	const DX12CommandListTrackingCache &cache)
{
	DX12CommandListTrackingState state;
	state.trackBindings = cache.trackBindings;
	state.trackIaBindings = cache.trackIaBindings;
	state.trackPipelineStateBindings = cache.trackPipelineStateBindings;
	state.recordBindingEvents = cache.recordBindingEvents;
	state.trackHuntIa = cache.trackHuntIa;
	state.trackPsoState = cache.trackPsoState;
	return state;
}

DX12CommandListTrackingState DX12CommandListRuntime::GetTrackingState(
	DX12CommandListRuntimeState *state)
{
	DX12CommandListTrackingCache cache;
	if (!state) {
		DX12CommandListRuntime::RefreshTrackingCache(cache);
		return DX12CommandListRuntime::MakeTrackingState(cache);
	}

	if (state->trackingCache.generation != TrackingGenerationValue)
		DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);
	return DX12CommandListRuntime::MakeTrackingState(state->trackingCache);
}

void DX12CommandListRuntime::TlsRefreshFromState(
	DX12CommandListRuntimeState *state, ID3D12GraphicsCommandList *commandList)
{
	if (!state) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	Tls.commandList = commandList;
	Tls.shard = nullptr;
	Tls.statePtr = state;
	Tls.trackingGeneration = TrackingGenerationValue;

	DX12CommandListTrackingCache &mapCache = state->trackingCache;
	if (mapCache.generation != TrackingGenerationValue)
		DX12CommandListRuntime::RefreshTrackingCache(mapCache);
	Tls.tracking = mapCache;
}

void DX12CommandListRuntime::TlsRefresh(
	DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList)
{
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
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
	out = DX12CommandListTrackingCache();
	if (!commandList)
		return;
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		out = Tls.tracking;
		return;
	}

	{
		DX12CommandListRuntimeState *state = nullptr;
		AcquireSRWLockShared(&gDirectStateMapLock);
		std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator it =
			gDirectStateMap.find(commandList);
		if (it != gDirectStateMap.end())
			state = it->second;
		ReleaseSRWLockShared(&gDirectStateMapLock);

		if (state) {
			DX12CommandListRuntime::TlsRefreshFromState(state, commandList);
			out = Tls.tracking;
			return;
		}
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList)
		out = Tls.tracking;
}

DX12CommandListTrackingState DX12CommandListRuntime::GetTrackingState(
	ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListTrackingCache cache;
	if (!commandList) {
		DX12CommandListRuntime::RefreshTrackingCache(cache);
	} else {
		DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
		if (cache.generation == 0)
			DX12CommandListRuntime::RefreshTrackingCache(cache);
	}

	return DX12CommandListRuntime::MakeTrackingState(cache);
}

DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtrFast(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue)
		return Tls.statePtr;

	{
		DX12CommandListRuntimeState *state = nullptr;
		AcquireSRWLockShared(&gDirectStateMapLock);
		std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator it =
			gDirectStateMap.find(commandList);
		if (it != gDirectStateMap.end())
			state = it->second;
		ReleaseSRWLockShared(&gDirectStateMapLock);

		if (state) {
			DX12CommandListRuntime::TlsRefreshFromState(state, commandList);
			return state;
		}
	}

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
	if (Tls.commandList != commandList)
		return;
	Tls.commandList = nullptr;
	Tls.shard = nullptr;
	Tls.statePtr = nullptr;
	Tls.trackingGeneration = 0;
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
	DX12CommandListRuntimeState *state, ID3D12PipelineState *initialState)
{
	if (!state)
		return;

	DX12CommandListTrackingCache oldCache = state->trackingCache;
	*state = DX12CommandListRuntimeState();
	state->pipelineState = initialState;
	state->trackingCache = oldCache;
	DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);
	DX12CommandListRuntime::RememberPipelineState(state, initialState);
}

void DX12CommandListRuntime::Reset(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::Reset(fastState, initialState);
		DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::Reset(&state, initialState);
	ReleaseSRWLockExclusive(&shard.lock);

	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

void DX12CommandListRuntime::RememberPipelineState(
	DX12CommandListRuntimeState *state, ID3D12PipelineState *pipelineState)
{
	if (!state)
		return;

	const bool changed = state->pipelineState != pipelineState;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimePso(changed);
	if (!changed && state->cachedCsHashResolved)
		return;

	if (changed) {
		state->cachedReplacementPsoKey = nullptr;
		state->cachedReplacementPso = nullptr;
		state->cachedCsHashValid = false;
		state->cachedCsHashResolved = false;
		state->cachedCsHash = 0;
	}
	state->pipelineState = pipelineState;
	if (!state->cachedCsHashResolved && pipelineState) {
		DX12PsoShaderInfo shaderInfo = {};
		if (DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
			state->cachedCsHash = shaderInfo.hasCS ? shaderInfo.cs : 0;
			state->cachedCsHashValid = shaderInfo.hasCS;
			state->cachedCsHashResolved = true;
		}
	} else if (!pipelineState) {
		state->cachedCsHash = 0;
		state->cachedCsHashValid = false;
		state->cachedCsHashResolved = true;
	}
}

void DX12CommandListRuntime::RememberPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPipelineState(fastState, pipelineState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPipelineState(&state, pipelineState);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::ResetIa(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->ia = DX12ActiveIaState();
}

void DX12CommandListRuntime::ResetIa(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::ResetIa(fastState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::ResetIa(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberIndexBuffer(
	DX12CommandListRuntimeState *state, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!state)
		return;

	const bool newValid = view != nullptr;
	const bool same =
		state->ia.hasIndexBuffer == newValid &&
		(!newValid ||
			(state->ia.indexBuffer.BufferLocation == view->BufferLocation &&
				state->ia.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state->ia.indexBuffer.Format == view->Format));
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeIndexBuffer(!same);
	if (same)
		return;
	state->ia.hasIndexBuffer = newValid;
	state->ia.indexBuffer = view ? *view : D3D12_INDEX_BUFFER_VIEW();
}

void DX12CommandListRuntime::RememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberIndexBuffer(fastState, view);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberIndexBuffer(&state, view);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberVertexBuffers(
	DX12CommandListRuntimeState *state,
	UINT startSlot,
	UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!state)
		return;

	DX12ActiveIaState &ia = state->ia;
	const UINT trackedCount = startSlot < ARRAYSIZE(ia.vertexBuffers) ?
		min(count, static_cast<UINT>(ARRAYSIZE(ia.vertexBuffers)) - startSlot) : 0;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeVertexBufferCall(trackedCount);
	for (UINT i = 0; i < trackedCount; ++i) {
		const UINT slot = startSlot + i;
		const D3D12_VERTEX_BUFFER_VIEW *view = views ? &views[i] : nullptr;
		D3D12_VERTEX_BUFFER_VIEW newView = view ? *view : D3D12_VERTEX_BUFFER_VIEW();
		const bool newValid = view != nullptr;
		const bool same =
			ia.hasVertexBuffer[slot] == newValid &&
			ia.vertexBuffers[slot].BufferLocation == newView.BufferLocation &&
			ia.vertexBuffers[slot].SizeInBytes == newView.SizeInBytes &&
			ia.vertexBuffers[slot].StrideInBytes == newView.StrideInBytes;
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeVertexBufferSlot(!same);
		if (same)
			continue;
		ia.hasVertexBuffer[slot] = newValid;
		ia.vertexBuffers[slot] = newView;
	}
}

void DX12CommandListRuntime::RememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberVertexBuffers(fastState, startSlot, count, views);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberVertexBuffers(&state, startSlot, count, views);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	DX12CommandListRuntimeState *state, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!state)
		return;
	const bool changed = state->ia.primitiveTopology != topology;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimePrimitiveTopology(changed);
	if (!changed)
		return;
	state->ia.primitiveTopology = topology;
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPrimitiveTopology(fastState, topology);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPrimitiveTopology(&state, topology);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::BumpComputeBindingSerial(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&state->computeBindingSerial));
}

void DX12CommandListRuntime::BumpComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::BumpComputeBindingSerial(state);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::BumpComputeBindingSerial(&it->second);
	ReleaseSRWLockShared(&shard.lock);
}

bool DX12CommandListRuntime::BuildIaHashState(
	const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	DX12IaHashTargetPlan targetPlan;
	targetPlan.RequireAll();
	return DX12CommandListRuntime::BuildIaHashStateForTargets(
		ia, targetPlan, indexed, vertexCount, indexCount,
		firstVertex, firstIndex, baseVertex, state);
}

bool DX12CommandListRuntime::BuildIaHashStateForTargets(
	const DX12ActiveIaState &ia, const DX12IaHashTargetPlan &targetPlan,
	bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	if (!state)
		return false;
	*state = DX12IaHashState();
	state->vertexBuffers.reserve(8);

	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer) {
		const uint32_t indexHash = targetPlan.RequiresIndexBufferHash(indexed) ?
			DX12IaRegion::HashIndexBufferView(ia.indexBuffer, draw) : 0;
		state->hasIndexBuffer = true;
		state->indexHash = indexHash;
		state->indexView = ia.indexBuffer;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		if (!targetPlan.RequiresVertexBuffer(slot))
			continue;
		const uint32_t vertexHash =
			DX12IaRegion::HashVertexBufferView(slot, ia.vertexBuffers[slot], draw);
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
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
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
	if (state)
		result = *state;
	return result;
}

const DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	return DX12CommandListRuntime::GetStatePtrFast(commandList);
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	DX12CommandListRuntimeState *state,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!state)
		return;
	state->cbvSrvUavHeap = cbvSrvUavHeap;
	state->samplerHeap = samplerHeap;
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
		DX12CommandListRuntime::SetDescriptorHeaps(state, cbvSrvUavHeap, samplerHeap);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::SetDescriptorHeaps(&it->second, cbvSrvUavHeap, samplerHeap);
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
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
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
