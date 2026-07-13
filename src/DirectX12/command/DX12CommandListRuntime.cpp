#include "DX12CommandListRuntime.hpp"

#include "DX12CommandListStateCapture.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12IaRegion.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12PsoDecisionCacheUtils.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12HotPathState.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;

void DX12CommandListRuntime::RegisterDirectState(
	ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *state)
{
	if (!commandList || !state)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.directStates[commandList] = state;
	ReleaseSRWLockExclusive(&shard.lock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

void DX12CommandListRuntime::UnregisterDirectState(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.directStates.erase(commandList);
	ReleaseSRWLockExclusive(&shard.lock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

DX12CommandListRuntimeState *DX12CommandListRuntime::GetDirectStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator it =
		shard.directStates.find(commandList);
	DX12CommandListRuntimeState *state = it != shard.directStates.end() ? it->second : nullptr;
	ReleaseSRWLockShared(&shard.lock);
	return state;
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
	DX12CommandListRuntimeState *state,
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeShard *shard,
	bool stateIsDirect)
{
	if (!state) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.stateIsDirect = false;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	Tls.commandList = commandList;
	Tls.shard = shard;
	Tls.stateIsDirect = stateIsDirect;
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
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator directIt =
		shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		DX12CommandListRuntime::TlsRefreshFromState(
			directIt->second, commandList, &shard, true);
		return;
	}

	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it == shard.states.end()) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.stateIsDirect = false;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	DX12CommandListRuntime::TlsRefreshFromState(
		&it->second, commandList, &shard, false);
}

void DX12CommandListRuntime::GetTrackingCacheFast(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListTrackingCache &out)
{
	out = DX12CommandListTrackingCache();
	if (!commandList)
		return;
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupTlsHit();
		out = Tls.tracking;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupDirectHit();
		else
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupShardHit();
		out = Tls.tracking;
		return;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupMiss();
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
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupTlsHit();
		return Tls.statePtr;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupDirectHit();
		else
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupShardHit();
		return Tls.statePtr;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupMiss();
	return nullptr;
}

void DX12CommandListRuntime::TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList)
{
	if (Tls.commandList != commandList)
		return;
	Tls.commandList = nullptr;
	Tls.shard = nullptr;
	Tls.stateIsDirect = false;
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
	const bool inserted = shard.states.try_emplace(commandList).second;
	ReleaseSRWLockExclusive(&shard.lock);

	if (inserted)
		DX12CommandListRuntime::BumpTrackingGeneration();
}

bool DX12CommandListRuntime::Unregister(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	bool removed = false;
	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		shard.states.erase(it);
		removed = true;
	}
	ReleaseSRWLockExclusive(&shard.lock);

	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
	if (removed) {
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12FrameAnalysis::LogJsonFunc("CommandListRuntimeUnregister",
			"\"commandList\":\"%p\"",
			commandList);
	}
	return removed;
}

void DX12CommandListRuntime::Reset(
	DX12CommandListRuntimeState *state, ID3D12PipelineState *initialState)
{
	if (!state)
		return;

	ID3D12Device *cachedDevice = state->device;
	DX12CommandListTrackingCache oldCache = state->trackingCache;
	// IA caches describe immutable content and candidate results rather than a
	// single recording. Their keys already carry the required generations, so
	// preserve the working set across command-list Reset like DX11's resident
	// HackerContext state.
	DX12ResolvedDrawRuntimeState persistentResolvedDraw =
		std::move(state->resolvedDraw);
	*state = DX12CommandListRuntimeState();
	state->device = cachedDevice;
	state->pipelineState = nullptr;
	state->trackingCache = oldCache;
	state->resolvedDraw = std::move(persistentResolvedDraw);
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

DX12CompiledDispatchDecision DX12CommandListRuntime::BuildDispatchDecision(
	ID3D12PipelineState *pipelineState,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	DX12CompiledDispatchDecision decision;
	const UINT64 reloadGeneration = DX12ModGetReloadGeneration();
	DX12PsoDecisionCacheUtils::ClearIfReloadChanged(reloadGeneration);
	if (DX12PsoDecisionCacheUtils::TryLoadDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			&decision)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPsoCacheHit();
		return decision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPsoCacheMiss();
	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionBuild();

	if (!pipelineState) {
		decision.SetNoComputeShader(
			pipelineState, reloadGeneration, hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoCompute();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	DX12PsoShaderInfo shaderInfo = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) || !shaderInfo.hasCS) {
		decision.SetNoComputeShader(
			pipelineState, reloadGeneration, hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoCompute();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	const uint64_t csHash = shaderInfo.cs;
	const bool shaderOverrideSkip = hasShaderOverrides ?
		DX12ModShouldSkipPipelineState(pipelineState, true) : false;
	const bool preSkinCandidate = needsPreSkinProbe && csHash != 0 ?
		DX12ModShouldTrackPreSkinBindingsForCs(csHash) : false;

	if (preSkinCandidate) {
		decision.SetNeedsPreSkinProbe(
			pipelineState, reloadGeneration, csHash, shaderOverrideSkip,
			hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidate();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	if (shaderOverrideSkip) {
		decision.SetNeedsShaderOverrideWork(
			pipelineState, reloadGeneration, csHash, shaderOverrideSkip,
			hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionShaderOverrideWork();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	decision.SetNoPreSkinCandidate(
		pipelineState, reloadGeneration, csHash,
		hasShaderOverrides, needsPreSkinProbe);
	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoPreSkinCandidate();
	DX12PsoDecisionCacheUtils::StoreDispatch(
		pipelineState,
		reloadGeneration,
		hasShaderOverrides,
		needsPreSkinProbe,
		decision);
	return decision;
}

DX12CompiledDrawDecision DX12CommandListRuntime::BuildDrawDecision(
	ID3D12PipelineState *pipelineState,
	bool hasShaderOverrides)
{
	DX12CompiledDrawDecision decision;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12PsoDecisionCacheUtils::ClearIfReloadChanged(reloadGeneration);
	if (DX12PsoDecisionCacheUtils::TryLoadDraw(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			&decision)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionPsoCacheHit();
		return decision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionPsoCacheMiss();
	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionBuild();
	if (!DX12ModCompileDrawDecision(pipelineState, &decision)) {
		DX12IaHashTargetPlan targetPlan;
		targetPlan.RequireAll();
		decision.SetFromTargetPlan(
			pipelineState, reloadGeneration,
			hasShaderOverrides, hasShaderOverrides, false,
			targetPlan,
			targetPlan, DX12ModNeedsShaderDescriptorTracking());
	}

	if (!pipelineState) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoPso();
	} else if (!decision.hasActiveShaderOverrides) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoShaderOverride();
	} else if (!decision.hasShaderOverrides && !decision.hasShaderOverrideSkip) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoDrawOverride();
	}

	switch (decision.kind) {
	case DX12CompiledDrawDecisionKind::NeedsIaHash:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsIaHash();
		break;
	case DX12CompiledDrawDecisionKind::NeedsDescriptorMatch:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsDescriptorMatch();
		break;
	case DX12CompiledDrawDecisionKind::NeedsReplacement:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsReplacement();
		break;
	case DX12CompiledDrawDecisionKind::SkipOriginal:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionSkipOriginal();
		break;
	default:
		break;
	}

	DX12PsoDecisionCacheUtils::StoreDraw(
		pipelineState,
		reloadGeneration,
		hasShaderOverrides,
		decision);
	return decision;
}

const DX12CompiledDispatchDecision *DX12CommandListRuntime::EnsureDispatchDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	if (!state)
		return nullptr;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (state->dispatchDecision.resolved &&
	    state->dispatchDecision.pipelineState == state->pipelineState &&
	    state->dispatchDecision.reloadGeneration == reloadGeneration &&
	    state->dispatchDecision.MatchesGlobals(hasShaderOverrides, needsPreSkinProbe)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionCacheHit();
		state->cachedCsHash = state->dispatchDecision.computeShaderHash;
		state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
		state->cachedCsHashResolved = true;
		return &state->dispatchDecision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionCacheMiss();
	DX12CommandListRuntime::CompileDispatchDecision(
		state, hasShaderOverrides, needsPreSkinProbe);
	state->cachedCsHash = state->dispatchDecision.computeShaderHash;
	state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
	state->cachedCsHashResolved = true;
	return &state->dispatchDecision;
}

const DX12CompiledDrawDecision *DX12CommandListRuntime::EnsureDrawDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides)
{
	if (!state)
		return nullptr;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (state->drawDecision.resolved &&
	    state->drawDecision.pipelineState == state->pipelineState &&
	    state->drawDecision.reloadGeneration == reloadGeneration &&
	    state->drawDecision.MatchesGlobals(hasShaderOverrides)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheHit();
		return &state->drawDecision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheMiss();
	DX12CommandListRuntime::CompileDrawDecision(state, hasShaderOverrides);
	return &state->drawDecision;
}

void DX12CommandListRuntime::CompileDispatchDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	if (!state)
		return;
	state->dispatchDecision = DX12CommandListRuntime::BuildDispatchDecision(
		state->pipelineState, hasShaderOverrides, needsPreSkinProbe);
}

void DX12CommandListRuntime::CompileDrawDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides)
{
	if (!state)
		return;
	state->drawDecision = DX12CommandListRuntime::BuildDrawDecision(
		state->pipelineState, hasShaderOverrides);
}

void DX12CommandListRuntime::RememberPipelineState(
	DX12CommandListRuntimeState *state, ID3D12PipelineState *pipelineState)
{
	if (!state)
		return;

	const bool changed = state->pipelineState != pipelineState;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimePso(changed);
	if (!changed)
		return;

	state->cachedReplacementPsoKey = nullptr;
	state->cachedReplacementPso = nullptr;
	state->cachedCsHashValid = false;
	state->cachedCsHashResolved = false;
	state->cachedCsHash = 0;
	state->dispatchDecision.Reset();
	state->drawDecision.Reset();
	state->pipelineState = pipelineState;
	if (!pipelineState) {
		state->cachedCsHash = 0;
		state->cachedCsHashValid = false;
		state->cachedCsHashResolved = true;
	}

	// Resolve immutable PSO policy at the state transition, matching DX11's
	// current-shader model. Draw/dispatch then consume command-list-local state.
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	const bool shaderOverrides =
		(flags & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
	const bool preSkinProbe =
		(flags & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
	if (shaderOverrides)
		DX12CommandListRuntime::CompileDrawDecision(state, true);
	if (shaderOverrides || preSkinProbe) {
		DX12CommandListRuntime::CompileDispatchDecision(
			state, shaderOverrides, preSkinProbe);
		state->cachedCsHash = state->dispatchDecision.computeShaderHash;
		state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
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
	DX12CommandListRuntime::BumpIaEpoch(state);
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
	state->ia.indexBufferResolved = false;
	state->ia.indexBufferResource = DX12BufferResourceSummary();
	if (newValid && state->ia.indexBuffer.BufferLocation && state->ia.indexBuffer.SizeInBytes)
		state->ia.indexBufferResolved = DX12ResolveBufferResourceByGpuVa(
			state->ia.indexBuffer.BufferLocation,
			state->ia.indexBuffer.SizeInBytes,
			&state->ia.indexBufferResource);
	DX12CommandListRuntime::BumpIaEpoch(state);
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
	bool changedAny = false;
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
		ia.vertexBufferResolved[slot] = false;
		ia.vertexBufferResources[slot] = DX12BufferResourceSummary();
		if (newValid && newView.BufferLocation && newView.SizeInBytes)
			ia.vertexBufferResolved[slot] = DX12ResolveBufferResourceByGpuVa(
				newView.BufferLocation,
				newView.SizeInBytes,
				&ia.vertexBufferResources[slot]);
		changedAny = true;
	}
	if (changedAny)
		DX12CommandListRuntime::BumpIaEpoch(state);
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

void DX12CommandListRuntime::BumpIaEpoch(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->iaEpoch++;
	if (!state->iaEpoch)
		state->iaEpoch = 1;
}

void DX12CommandListRuntime::BumpIaContentEpoch(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->iaContentEpoch++;
	if (!state->iaContentEpoch)
		state->iaContentEpoch = 1;
}

void DX12CommandListRuntime::BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::BumpIaContentEpoch(state);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::BumpIaContentEpoch(&it->second);
	ReleaseSRWLockExclusive(&shard.lock);
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

DX12IaHashStateCacheKey DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargets(
	const DX12ActiveIaState &ia,
	UINT64 iaContentEpoch,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	UINT vertexCount,
	UINT indexCount,
	UINT firstVertex,
	UINT firstIndex,
	INT baseVertex)
{
	DX12IaHashStateCacheKey key;
	key.targetPlanFingerprint = targetPlan.Fingerprint();
	// IA hashes depend on either captured CPU content (covered by the observed
	// resource content generation below) or immutable resource/view metadata.
	// A command-list-wide Dispatch/Copy epoch cannot change either cache input.
	(void)iaContentEpoch;
	key.iaContentEpoch = 0;
	key.bufferContentGeneration = 0xcbf29ce484222325ull;
	key.bufferResolveGeneration = 0x84222325a9582d75ull;
	key.indexed = indexed;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.baseVertex = baseVertex;
	key.iaViewFingerprint = 0x84222325a9582d75ull;
	auto combineResolvedObservation = [&key](
		const DX12BufferResourceSummary &summary, UINT64 salt) {
		if (summary.resource && summary.resourceGeneration) {
			key.bufferContentGeneration =
				DX12IaHashStateCacheKeyHash::Combine(
					key.bufferContentGeneration,
					DX12GetBufferContentGenerationForResource(summary.resource) ^ salt);
			key.bufferResolveGeneration =
				DX12IaHashStateCacheKeyHash::Combine(
					key.bufferResolveGeneration,
					summary.resourceGeneration ^ salt);
			return true;
		}
		return false;
	};
	auto combineObservation = [&key, &combineResolvedObservation](
		UINT64 salt,
		const DX12BufferResourceSummary *cachedSummary) {
		if (cachedSummary && combineResolvedObservation(*cachedSummary, salt))
			return;
		// Unresolved views retain conservative global invalidation. Resolved views
		// no longer inherit unrelated resource/content churn. Draw-time key building
		// deliberately consumes wrapper IA state only; GPU VA resolution happens when
		// the IA binding changes, matching DX11-style current-state ownership.
		key.usesGlobalFallback = true;
		key.bufferContentGeneration =
			DX12IaHashStateCacheKeyHash::Combine(
				key.bufferContentGeneration,
				DX12GetIaHashContentGeneration() ^ salt);
		key.bufferResolveGeneration =
			DX12IaHashStateCacheKeyHash::Combine(
				key.bufferResolveGeneration,
				DX12GetIaHashResolveGeneration() ^ salt);
	};

	if (targetPlan.RequiresIndexBuffer(indexed)) {
		key.iaViewFingerprint ^= 0x100000001b3ull;
			key.iaViewFingerprint ^= ia.hasIndexBuffer ? 0x7f4a7c15ull : 0x51afd7edull;
			if (ia.hasIndexBuffer) {
				combineObservation(
					0x100000001b3ull,
					ia.indexBufferResolved ? &ia.indexBufferResource : nullptr);
				key.iaViewFingerprint ^= ia.indexBuffer.BufferLocation +
					0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
					(key.iaViewFingerprint >> 2);
				key.iaViewFingerprint ^= static_cast<UINT64>(ia.indexBuffer.SizeInBytes) +
				0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
				(key.iaViewFingerprint >> 2);
			key.iaViewFingerprint ^= static_cast<UINT64>(ia.indexBuffer.Format) +
				0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
				(key.iaViewFingerprint >> 2);
		}
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot))
			continue;
		key.iaViewFingerprint ^= static_cast<UINT64>(slot + 1) *
			0x9e3779b97f4a7c15ull;
		key.iaViewFingerprint ^= ia.hasVertexBuffer[slot] ? 0x27d4eb2full : 0x165667b1ull;
		if (!ia.hasVertexBuffer[slot])
			continue;
		combineObservation(
			static_cast<UINT64>(slot + 1) * 0x9e3779b97f4a7c15ull,
			ia.vertexBufferResolved[slot] ? &ia.vertexBufferResources[slot] : nullptr);
		key.iaViewFingerprint ^= ia.vertexBuffers[slot].BufferLocation +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
		key.iaViewFingerprint ^= static_cast<UINT64>(ia.vertexBuffers[slot].SizeInBytes) +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
		key.iaViewFingerprint ^= static_cast<UINT64>(ia.vertexBuffers[slot].StrideInBytes) +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
	}

	if (!key.iaViewFingerprint)
		key.iaViewFingerprint = 1;
	return key;
}

DX12IaHashStateCacheKey DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
	DX12CommandListRuntimeState *state,
	UINT cacheSlot,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	UINT vertexCount,
	UINT indexCount,
	UINT firstVertex,
	UINT firstIndex,
	INT baseVertex)
{
	if (!state)
		return DX12IaHashStateCacheKey();
	const UINT64 targetPlanFingerprint = targetPlan.Fingerprint();
	DX12IaHashStateCacheKey key;
	DX12IaHashKeyPrefixCacheEntry &entry =
		state->resolvedDraw.iaHashKeyPrefixes[
			cacheSlot % state->resolvedDraw.iaHashKeyPrefixes.size()];
	if (!DX12IaHashKeyPrefixCacheUtils::TryLoadStable(
			entry, state->iaEpoch, targetPlanFingerprint, indexed, &key)) {
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 globalResolveGeneration = DX12GetIaHashResolveGeneration();
		if (!DX12IaHashKeyPrefixCacheUtils::TryLoad(
				entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, &key)) {
			key = BuildIaHashStateCacheKeyForTargets(
				state->ia, state->iaContentEpoch, targetPlan, indexed,
				0, 0, 0, 0, 0);
			DX12IaHashKeyPrefixCacheUtils::Store(
				&entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, key);
		}
	}
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.baseVertex = baseVertex;
	return key;
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

static void DX12RuntimeClearRootTables(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsRootTables[root] = RootTableState();
		state->computeRootTables[root] = RootTableState();
	}
	state->graphicsRootTableValidCount = 0;
	state->computeRootTableValidCount = 0;
}

static bool DX12RuntimeResolveHeapSummary(
	ID3D12DescriptorHeap *heap,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;
	*summary = DX12DescriptorHeapSummary();
	return DX12FindDescriptorHeapByPtr(heap, summary) &&
		summary->heap == heap &&
		summary->type == static_cast<UINT>(type) &&
		summary->increment != 0;
}

static void DX12RuntimeFillRootTableFromHeap(
	RootTableState *table,
	const DX12DescriptorHeapSummary &heap)
{
	if (!table || !table->valid ||
			heap.increment == 0 || heap.gpuStart == 0 ||
			table->baseDescriptor.ptr < heap.gpuStart) {
		return;
	}

	const UINT64 tableBaseIndex =
		(table->baseDescriptor.ptr - heap.gpuStart) / heap.increment;
	if (tableBaseIndex >= heap.numDescriptors)
		return;

	table->runtimeHeapResolved = true;
	table->runtimeHeap = heap.heap;
	table->runtimeHeapType = heap.type;
	table->runtimeHeapCpuStart = heap.cpuStart;
	table->runtimeHeapGpuStart = heap.gpuStart;
	table->runtimeHeapIncrement = heap.increment;
	table->runtimeHeapNumDescriptors = heap.numDescriptors;
	table->runtimeHeapVersion = heap.version;
	table->runtimeTableBaseIndex = tableBaseIndex;
	table->runtimeTableCpuStart =
		heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
	table->runtimeTableGpuStart =
		heap.gpuStart + tableBaseIndex * heap.increment;
}

static void DX12RuntimeResolveRootTable(
	DX12CommandListRuntimeState *state,
	RootTableState *table)
{
	if (!state || !table || !table->valid)
		return;
	table->runtimeHeapResolved = false;
	table->runtimeHeap = nullptr;
	table->runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	table->runtimeHeapCpuStart = 0;
	table->runtimeHeapGpuStart = 0;
	table->runtimeHeapIncrement = 0;
	table->runtimeHeapNumDescriptors = 0;
	table->runtimeHeapVersion = 0;
	table->runtimeTableBaseIndex = 0;
	table->runtimeTableCpuStart = 0;
	table->runtimeTableGpuStart = 0;

	if (state->runtimeCbvSrvUavHeapResolved &&
			state->runtimeCbvSrvUavHeap.heap == state->cbvSrvUavHeap &&
			table->baseDescriptor.ptr >= state->runtimeCbvSrvUavHeap.gpuStart) {
		DX12RuntimeFillRootTableFromHeap(table, state->runtimeCbvSrvUavHeap);
	}
}

static void DX12RuntimeBumpBindingQuerySerial(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	DX12CommandListRuntimeState *state,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!state)
		return;
	const bool changed =
		state->cbvSrvUavHeap != cbvSrvUavHeap ||
		state->samplerHeap != samplerHeap;
	state->cbvSrvUavHeap = cbvSrvUavHeap;
	state->samplerHeap = samplerHeap;
	state->runtimeCbvSrvUavHeapResolved =
		DX12RuntimeResolveHeapSummary(
			cbvSrvUavHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&state->runtimeCbvSrvUavHeap);
	if (!state->runtimeCbvSrvUavHeapResolved)
		state->runtimeCbvSrvUavHeap = DX12DescriptorHeapSummary();
	state->runtimeSamplerHeapResolved =
		DX12RuntimeResolveHeapSummary(
			samplerHeap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			&state->runtimeSamplerHeap);
	if (!state->runtimeSamplerHeapResolved)
		state->runtimeSamplerHeap = DX12DescriptorHeapSummary();
	if (changed) {
		DX12RuntimeClearRootTables(state);
		DX12RuntimeBumpBindingQuerySerial(state);
	}
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

void DX12CommandListRuntime::RememberRootDescriptorTable(
	DX12CommandListRuntimeState *state,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;
	RootTableState &table = graphics ?
		state->graphicsRootTables[rootParameterIndex] :
		state->computeRootTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		if (!table.runtimeHeapResolved)
			DX12RuntimeResolveRootTable(state, &table);
		return;
	}
	if (!table.valid) {
		if (graphics)
			++state->graphicsRootTableValidCount;
		else
			++state->computeRootTableValidCount;
	}
	table = RootTableState();
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	DX12RuntimeResolveRootTable(state, &table);
	if (!graphics) {
		state->computeBindingSerial++;
		if (!state->computeBindingSerial)
			state->computeBindingSerial = 1;
	}
	DX12RuntimeBumpBindingQuerySerial(state);
}

void DX12CommandListRuntime::RememberRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			state, graphics, rootParameterIndex, baseDescriptor);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			&it->second, graphics, rootParameterIndex, baseDescriptor);
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
