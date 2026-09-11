// DX12CommandListRuntime: per-list runtime state, binding resolution and lifecycle.
// Split from DX12CommandLists.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <d3d12.h>
#include <unordered_set>



// DX12CommandListRuntime static state
DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;


void DX12CommandListRuntime::RegisterDirectState(
	ID3D12GraphicsCommandList *commandList, DX12SimulateContext *state)
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
	auto it = shard.directStates.find(commandList);
	if (it != shard.directStates.end()) {
		DX12SimulateContext *state = it->second;
		if (state && state->commandListIdentity) {
			state->commandListIdentity->Release();
			state->commandListIdentity = nullptr;
		}
		shard.directStates.erase(it);
	}
	ReleaseSRWLockExclusive(&shard.lock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}


IUnknown *DX12CommandListRuntime::GetCommandListIdentity(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12SimulateContext *state = nullptr;
	auto directIt = shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		state = directIt->second;
	} else {
		auto it = shard.states.find(commandList);
		if (it != shard.states.end())
			state = &it->second;
	}
	if (state && state->commandListIdentity) {
		IUnknown *identity = state->commandListIdentity;
		ReleaseSRWLockShared(&shard.lock);
		return identity;
	}
	ReleaseSRWLockShared(&shard.lock);

	IUnknown *resolvedIdentity = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&resolvedIdentity))) ||
		!resolvedIdentity) {
		return commandList;
	}

	IUnknown *borrowedIdentity = resolvedIdentity;
	AcquireSRWLockExclusive(&shard.lock);
	state = nullptr;
	directIt = shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		state = directIt->second;
	} else {
		auto it = shard.states.find(commandList);
		if (it != shard.states.end())
			state = &it->second;
	}
	if (state) {
		if (!state->commandListIdentity) {
			state->commandListIdentity = resolvedIdentity;
			borrowedIdentity = resolvedIdentity;
			resolvedIdentity = nullptr;
		} else {
			borrowedIdentity = state->commandListIdentity;
		}
	}
	ReleaseSRWLockExclusive(&shard.lock);

	if (resolvedIdentity)
		resolvedIdentity->Release();
	return borrowedIdentity;
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
	DX12SimulateContext *state)
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
	DX12SimulateContext *state,
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeShard *shard,
	bool stateIsDirect)
{
	if (!state)
		return;

	// Demote the current hot entry into the history ring before overwriting
	// it. Prefer replacing an existing slot for the same list so each list
	// appears at most once; otherwise take the ring's next slot.
	if (Tls.commandList && Tls.commandList != commandList) {
		DX12CommandListRuntimeTlsCache::Slot *target = nullptr;
		for (size_t i = 0; i < DX12CommandListRuntimeTlsCache::kHistorySlotCount; ++i) {
			if (Tls.slots[i].commandList == Tls.commandList) {
				target = &Tls.slots[i];
				break;
			}
		}
		if (!target) {
			target = &Tls.slots[Tls.nextSlot];
			Tls.nextSlot = (Tls.nextSlot + 1) %
				DX12CommandListRuntimeTlsCache::kHistorySlotCount;
		}
		target->commandList = Tls.commandList;
		target->shard = Tls.shard;
		target->stateIsDirect = Tls.stateIsDirect;
		target->trackingGeneration = Tls.trackingGeneration;
		target->tracking = Tls.tracking;
		target->statePtr = Tls.statePtr;
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

DX12SimulateContext *DX12CommandListRuntime::TlsLookup(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	// Hot slot first: single-list recording threads hit here every call.
	if (Tls.commandList == commandList &&
		Tls.trackingGeneration == TrackingGenerationValue) {
		return Tls.statePtr;
	}

	// History scan with move-to-front promotion. Games interleave several
	// command lists per recording thread; a linear scan of a handful of
	// slots is far cheaper than the shard lock + unordered_map fallback.
	for (size_t i = 0; i < DX12CommandListRuntimeTlsCache::kHistorySlotCount; ++i) {
		DX12CommandListRuntimeTlsCache::Slot &slot = Tls.slots[i];
		if (slot.commandList == commandList &&
			slot.trackingGeneration == TrackingGenerationValue) {
			// Promote the hit into the hot position so the next lookup for
			// this list takes the two-load path.
			Tls.SwapHotWith(slot);
			return Tls.statePtr;
		}
	}

	return nullptr;
}

void DX12CommandListRuntime::TlsRefresh(
	DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList)
{
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*>::iterator directIt =
		shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		DX12CommandListRuntime::TlsRefreshFromState(
			directIt->second, commandList, &shard, true);
		return;
	}

	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it == shard.states.end())
		return;

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
	DX12SimulateContext *state = DX12CommandListRuntime::TlsLookup(commandList);
	if (state) {
		DX12_PROF_COUNT("Rm.RuntimeLookupTlsHits");
		out = Tls.tracking;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12_PROF_COUNT("Rm.RuntimeLookupDirectHits");
		else
			DX12_PROF_COUNT("Rm.RuntimeLookupShardHits");
		out = Tls.tracking;
		return;
	}

	DX12_PROF_COUNT("Rm.RuntimeLookupMisses");
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

DX12SimulateContext *DX12CommandListRuntime::GetStatePtrFast(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	DX12SimulateContext *state = DX12CommandListRuntime::TlsLookup(commandList);
	if (state) {
		DX12_PROF_COUNT("Rm.RuntimeLookupTlsHits");
		return state;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12_PROF_COUNT("Rm.RuntimeLookupDirectHits");
		else
			DX12_PROF_COUNT("Rm.RuntimeLookupShardHits");
		return Tls.statePtr;
	}

	DX12_PROF_COUNT("Rm.RuntimeLookupMisses");
	return nullptr;
}

void DX12CommandListRuntime::TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList)
{
	if (Tls.commandList == commandList)
		Tls.ClearHot();
	for (size_t i = 0; i < DX12CommandListRuntimeTlsCache::kHistorySlotCount; ++i) {
		DX12CommandListRuntimeTlsCache::Slot &slot = Tls.slots[i];
		if (slot.commandList == commandList) {
			slot.commandList = nullptr;
			slot.shard = nullptr;
			slot.stateIsDirect = false;
			slot.statePtr = nullptr;
			slot.trackingGeneration = 0;
		}
	}
}


void DX12CommandListRuntime::BumpTrackingGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&TrackingGenerationValue));
}

void DX12ModRefreshRuntimeStateAfterReload()
{
	// Match DX11's immediate post-reload visibility: update hot-path gates and
	// invalidate per-command-list tracking caches without waiting for Present.
	DX12HotPathState::Update();
	DX12CommandListRuntime::BumpTrackingGeneration();
}

void DX12ResolvedDrawRuntimeStateUtils::Clear(
	DX12SimulateContextDrawTemplate *state)
{
	if (!state)
		return;
	state->plan = DX12SimulateContextDrawTemplate::ResolvedPlanState();
	DX12IaHashStateCacheUtils::Clear(&state->iaHashStateCache);
	state->iaHashKeyPrefixes = {};
	state->lastDirectIaHashBuild = {};
	state->lastFullIaHashBuild = {};
	state->directIaCandidateCache =
		DX12SimulateContextDrawTemplate::DirectIaCandidateCache();
	state->directIaTargetPresence = {};
	state->fullIaTargetPresence = {};
	state->residentDrawNegativeCache = {};
	state->boundDrawResult.Reset();
	state->iaStateNegative = {};
}

void DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan(
	DX12SimulateContextDrawTemplate *state)
{
	if (!state)
		return;
	state->plan.dirty = true;
	++state->plan.invalidationSerial;
	if (!state->plan.invalidationSerial)
		state->plan.invalidationSerial = 1;
	state->boundDrawResult.Reset();
}


void DX12ResolvedDrawRuntimeStateUtils::NoteLookup(
	DX12SimulateContextDrawTemplate *state,
	bool hit)
{
	(void)state;
	(void)hit;
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
		if (it->second.commandListIdentity) {
			it->second.commandListIdentity->Release();
			it->second.commandListIdentity = nullptr;
		}
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

static void DX12RuntimeResetStateForRecording(DX12SimulateContext *state)
{
	if (!state)
		return;
	state->pipelineState = nullptr;
	state->lastWrappedPipelineState = nullptr;
	state->lastUnwrappedPipelineState = nullptr;
	state->lastWrappedComputeRootSignature = nullptr;
	state->lastUnwrappedComputeRootSignature = nullptr;
	state->lastWrappedGraphicsRootSignature = nullptr;
	state->lastUnwrappedGraphicsRootSignature = nullptr;
	state->computeBindingSerial = 0;
	// The pre-skin per-slot redirect negative cache ("this view needs no
	// redirect") is keyed by (generation, source view) and therefore survives
	// content changes by design; the full IA rebuild would wipe it, forcing
	// every frame's first matching draw to re-run the whole 32-slot pre-skin
	// lookup chain (~100-550us/frame in 32-slot scenes, measured). Preserve
	// it across the rebuild: generation and view equality still invalidate
	// entries exactly as before.
	DX12CommandListRuntime::RebuildActiveIaStatePreservingRedirectNegatives(
		&state->ia);
	state->iaEpoch = 1;
	state->iaContentEpoch = 1;

	state->cbvSrvUavHeap = nullptr;
	state->samplerHeap = nullptr;
	// OM/rasterizer shadow: a fresh command list has no render targets,
	// viewports or scissor rects bound.
	state->omRenderTargetCount = 0;
	state->omHasDepthStencil = false;
	state->viewportCount = 0;
	state->scissorCount = 0;
	state->cbvSrvUavHeapVersion = 0;
	state->lastSetHeapsCount = 0xFFFFFFFF;
	state->lastSetHeapsInput[0] = nullptr;
	state->lastSetHeapsInput[1] = nullptr;
	state->runtimeCbvSrvUavHeapResolved = false;
	state->runtimeCbvSrvUavHeap = DX12DescriptorHeapSummary();
	state->runtimeSamplerHeapResolved = false;
	state->runtimeSamplerHeap = DX12DescriptorHeapSummary();
	state->bindingQuerySerial = 1;
	state->graphicsDescriptorQuerySerial = 1;
	state->computeDescriptorQuerySerial = 1;
	state->computeDescriptorBindingVersion = 1;
	// The serial/version counters above restart for the new recording, but a
	// deterministic game replays the exact same bind sequence and lands on the
	// exact same counter values. A probe verdict or binding snapshot captured
	// under the previous recording's counters would then alias with an
	// unrelated dispatch in this one: the previous recording's last negative
	// verdict was observed swallowing the modded skinning dispatch every
	// other frame (PreSkin visibly dropping frames). Drop the counter-keyed
	// single-entry state here; the content-keyed caches (localProbeCache,
	// stable fingerprints, shared probe cache) validate by fingerprint and
	// legitimately survive Reset.
	state->resolvedCompute.probeResultValid = false;
	state->resolvedCompute.bindingsValid = false;
	state->computeRootDirtyMask = 0;
	state->computeRootUsedMask = 0;
	state->computeRootUsedMaskValid = false;
	state->graphicsRootBindingRevisions = {};
	state->computeRootBindingRevisions = {};
	state->graphicsRootTableValidCount = 0;
	state->computeRootTableValidCount = 0;
	state->graphicsRootSignature = nullptr;
	state->computeRootSignature = nullptr;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsRootTables[root] = RootTableState();
		state->computeRootTables[root] = RootTableState();
		state->graphicsRootDescriptors[root] = RootDescriptorState();
		state->computeRootDescriptors[root] = RootDescriptorState();
		// Root-constant payload arrays (64 UINT values + validity flags per
		// root) are only read while valid==true; clearing the markers avoids
		// re-zeroing ~43KB per command-list Reset.
		state->graphicsRootConstants[root].valid = false;
		state->graphicsRootConstants[root].rootParameterIndex = 0;
		state->graphicsRootConstants[root].maxSet = 0;
		state->computeRootConstants[root].valid = false;
		state->computeRootConstants[root].rootParameterIndex = 0;
		state->computeRootConstants[root].maxSet = 0;
	}
	state->psoHasOverridesGeneration = -1;
	state->psoHasShaderOverrides = true;
	state->cachedReplacementPsoKey = nullptr;
	state->cachedReplacementPso = nullptr;
	state->cachedCsHash = 0;
	state->cachedCsHashValid = false;
	state->cachedCsHashResolved = false;
	state->computeBindingHash = 0;
	state->dispatchDecision.Reset();
	state->drawDecision.Reset();
	state->staticReplacementPso = nullptr;
	state->staticReplacementReloadGeneration = 0;
	state->staticReplacementSnapshot.reset();
	state->staticTemplateReplacement.reset();
	state->staticTemplateIaApplied = false;
	state->staticTemplateSavedIa = DX12ActiveIaState();
	for (DX12ResolvedDescriptorQueryRuntimeState &query :
			state->resolvedDescriptorQueries)
		query.valid = false;
	state->resolvedGraphicsSrvs = DX12ResolvedGraphicsSrvRuntimeState();
}

void DX12CommandListRuntime::Reset(
	DX12SimulateContext *state, ID3D12PipelineState *initialState)
{
	if (!state)
		return;

	// Targeted per-recording reset instead of `*state = DX12SimulateContext()`:
	// the full assignment zeroes ~60KB of plain arrays on every Reset (the
	// profiled title resets several lists per frame - 3.8/frame measured in the
	// current scene), dominated by the 64-entry
	// root-constants value arrays. Root constant VALUES are only meaningful
	// while valid==true, so the reset only clears the validity/layout markers
	// and leaves the payload stale. Container capacities that are useful
	// across recordings (descriptor query results) are preserved by move.
	DX12RuntimeResetStateForRecording(state);

	ID3D12Device *cachedDevice = state->device;
	IUnknown *cachedCommandListIdentity = state->commandListIdentity;
	DX12CommandListTrackingCache oldCache = state->trackingCache;
	// IA caches describe immutable content and candidate results rather than a
	// single recording. Their keys already carry the required generations, so
	// preserve the working set across command-list Reset like DX11's resident
	// HackerContext state.
	DX12SimulateContextDrawTemplate persistentResolvedDraw =
		std::move(state->resolvedDraw);
	// The compute binding template and probe verdicts are kept for their
	// content-keyed caches (descriptor value IDs + fingerprints), which
	// re-validate semantically and hit when the game re-records identical
	// dispatches every frame. The counter-keyed single-entry verdict and
	// snapshot validity were already dropped in
	// DX12RuntimeResetStateForRecording: the serial/version counters restart
	// per recording, so keeping them would let a stale verdict alias an
	// unrelated dispatch that replays the same counter values.
	DX12SimulateContextComputeTemplate persistentResolvedCompute =
		std::move(state->resolvedCompute);
	std::array<DX12ResolvedDescriptorQueryRuntimeState, 6>
		persistentResolvedQueries = std::move(state->resolvedDescriptorQueries);
	// barrierScratch is documented as reused storage to avoid an allocation
	// cycle per ResourceBarrier call, but the full struct reset destroyed and
	// reallocated it on every command-list Reset. Move it out and back so its
	// capacity survives; contents are cleared below as before.
	std::vector<D3D12_RESOURCE_BARRIER> persistentBarrierScratch =
		std::move(state->barrierScratch);
	state->device = cachedDevice;
	state->commandListIdentity = cachedCommandListIdentity;
	state->pipelineState = nullptr;
	state->trackingCache = oldCache;
	state->resolvedDraw = std::move(persistentResolvedDraw);
	state->resolvedCompute = std::move(persistentResolvedCompute);
	state->resolvedDescriptorQueries = std::move(persistentResolvedQueries);
	state->barrierScratch = std::move(persistentBarrierScratch);
	state->barrierScratch.clear();
	for (DX12ResolvedDescriptorQueryRuntimeState &query :
			state->resolvedDescriptorQueries)
		query.valid = false;
	DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan(&state->resolvedDraw);
	DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);
	DX12CommandListRuntime::RememberPipelineState(state, initialState);
}

void DX12CommandListRuntime::Reset(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::Reset(fastState, initialState);
		DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
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
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12PsoDecisionCacheUtils::ClearIfReloadChanged(reloadGeneration);
	if (DX12PsoDecisionCacheUtils::TryLoadDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			&decision)) {
		DX12_PROF_COUNT("Rm.DispatchDecisionPsoCacheHits");
		return decision;
	}

	DX12_PROF_COUNT("Rm.DispatchDecisionPsoCacheMisses");
	DX12_PROF_COUNT("Rm.DispatchDecisionBuilds");

	if (!pipelineState) {
		decision.SetNoComputeShader(
			pipelineState, reloadGeneration, hasShaderOverrides, needsPreSkinProbe);
		DX12_PROF_COUNT("Rm.DispatchDecisionNoCompute");
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
		DX12_PROF_COUNT("Rm.DispatchDecisionNoCompute");
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	const uint64_t csHash = shaderInfo.cs;
	bool skipDraw = false;
	bool skipDispatch = false;
	if (hasShaderOverrides)
		DX12ModGetPipelineStateSkipFlags(
			pipelineState, &shaderInfo, &skipDraw, &skipDispatch);
	const bool shaderOverrideSkip = skipDispatch;
	const bool preSkinCandidate = needsPreSkinProbe && csHash != 0 ?
		DX12ModShouldTrackPreSkinBindingsForCs(csHash) : false;

	if (preSkinCandidate) {
		decision.SetNeedsPreSkinProbe(
			pipelineState, reloadGeneration, csHash, shaderOverrideSkip,
			hasShaderOverrides, needsPreSkinProbe);
		DX12_PROF_COUNT("Rm.DispatchDecisionPreSkinCandidate");
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
		DX12_PROF_COUNT("Rm.DispatchDecisionShaderOverrideWork");
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
	DX12_PROF_COUNT("Rm.DispatchDecisionNoPreSkinCandidate");
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
		DX12_PROF_COUNT("Rm.DrawDecisionPsoCacheHits");
		return decision;
	}

	DX12_PROF_COUNT("Rm.DrawDecisionPsoCacheMisses");
	DX12_PROF_COUNT("Rm.DrawDecisionBuilds");
	if (!DX12ModCompileDrawDecision(pipelineState, &decision)) {
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		decision.SetFromTargetPlan(
			pipelineState, reloadGeneration,
			hasShaderOverrides, hasShaderOverrides, false,
			targetPlan,
			targetPlan, DX12DrawMatchUsage(), DX12DrawMatchUsage(), false,
			DX12ModNeedsShaderDescriptorTracking());
	}
	decision.staticReplacementSnapshot =
		DX12ModFindStaticReplacementSnapshotForPso(
			pipelineState,
			reloadGeneration);
	decision.staticReplacementReady =
		decision.staticReplacementSnapshot != nullptr;

	if (!pipelineState) {
		DX12_PROF_COUNT("Rm.DrawDecisionNoPso");
	} else if (!decision.hasActiveShaderOverrides) {
		DX12_PROF_COUNT("Rm.DrawDecisionNoShaderOverride");
	} else if (!decision.hasShaderOverrides && !decision.hasShaderOverrideSkip) {
		DX12_PROF_COUNT("Rm.DrawDecisionNoDrawOverride");
	}

	switch (decision.kind) {
	case DX12CompiledDrawDecisionKind::NeedsIaHash:
		DX12_PROF_COUNT("Rm.DrawDecisionNeedsIaHash");
		break;
	case DX12CompiledDrawDecisionKind::NeedsDescriptorMatch:
		DX12_PROF_COUNT("Rm.DrawDecisionNeedsDescriptorMatch");
		break;
	case DX12CompiledDrawDecisionKind::NeedsReplacement:
		DX12_PROF_COUNT("Rm.DrawDecisionNeedsReplacement");
		break;
	case DX12CompiledDrawDecisionKind::SkipOriginal:
		DX12_PROF_COUNT("Rm.DrawDecisionSkipOriginal");
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
	DX12SimulateContext *state,
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
		DX12_PROF_COUNT("Rm.DispatchDecisionCacheHits");
		state->cachedCsHash = state->dispatchDecision.computeShaderHash;
		state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
		state->cachedCsHashResolved = true;
		return &state->dispatchDecision;
	}

	DX12_PROF_COUNT("Rm.DispatchDecisionCacheMisses");
	DX12CommandListRuntime::CompileDispatchDecision(
		state, hasShaderOverrides, needsPreSkinProbe);
	state->cachedCsHash = state->dispatchDecision.computeShaderHash;
	state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
	state->cachedCsHashResolved = true;
	return &state->dispatchDecision;
}

const DX12CompiledDrawDecision *DX12CommandListRuntime::EnsureDrawDecision(
	DX12SimulateContext *state,
	bool hasShaderOverrides)
{
	if (!state)
		return nullptr;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (state->drawDecision.resolved &&
	    state->drawDecision.pipelineState == state->pipelineState &&
	    state->drawDecision.reloadGeneration == reloadGeneration &&
	    state->drawDecision.MatchesGlobals(hasShaderOverrides)) {
		DX12_PROF_COUNT("Rm.DrawDecisionCacheHits");
		return &state->drawDecision;
	}

	DX12_PROF_COUNT("Rm.DrawDecisionCacheMisses");
	DX12CommandListRuntime::CompileDrawDecision(state, hasShaderOverrides);
	return &state->drawDecision;
}

void DX12CommandListRuntime::CompileDispatchDecision(
	DX12SimulateContext *state,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	if (!state)
		return;
	state->dispatchDecision = DX12CommandListRuntime::BuildDispatchDecision(
		state->pipelineState, hasShaderOverrides, needsPreSkinProbe);
}

void DX12CommandListRuntime::CompileDrawDecision(
	DX12SimulateContext *state,
	bool hasShaderOverrides)
{
	if (!state)
		return;
	state->drawDecision = DX12CommandListRuntime::BuildDrawDecision(
		state->pipelineState, hasShaderOverrides);
}

void DX12CommandListRuntime::InvalidateResolvedDrawPlan(
	DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan(&state->resolvedDraw);
}

void DX12CommandListRuntime::RememberPipelineState(
	DX12SimulateContext *state, ID3D12PipelineState *pipelineState)
{
	if (!state)
		return;

	const bool changed = state->pipelineState != pipelineState;
	DX12_PROF_COUNT("Rm.RuntimePsoCalls");
	if (changed)
		DX12_PROF_COUNT("Rm.RuntimePsoChanged");
	else
		DX12_PROF_COUNT("Rm.RuntimePsoNoChange");
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
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
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

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPipelineState(fastState, pipelineState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPipelineState(&state, pipelineState);
	ReleaseSRWLockExclusive(&shard.lock);
}

// Rebuild the IA state from scratch while keeping the pre-skin per-slot
// redirect negative cache: entries are keyed by (generation, source view), so
// they stay valid across command-list Reset (wiping them made every frame's
// first matching draw re-run the full 32-slot pre-skin lookup chain, measured
// at ~100-550us/frame in 32-slot scenes).
void DX12CommandListRuntime::RebuildActiveIaStatePreservingRedirectNegatives(
	DX12ActiveIaState *ia)
{
	if (!ia)
		return;
	constexpr UINT kIaSlotCount =
		ARRAYSIZE(ia->preSkinRedirectNegativeValid);
	bool savedNegativeValid[kIaSlotCount];
	UINT64 savedNegativeGeneration[kIaSlotCount];
	D3D12_VERTEX_BUFFER_VIEW savedNegativeSource[kIaSlotCount];
	for (UINT slot = 0; slot < kIaSlotCount; ++slot) {
		savedNegativeValid[slot] = ia->preSkinRedirectNegativeValid[slot];
		savedNegativeGeneration[slot] = ia->preSkinRedirectNegativeGeneration[slot];
		savedNegativeSource[slot] = ia->preSkinRedirectNegativeSource[slot];
	}
	*ia = DX12ActiveIaState();
	for (UINT slot = 0; slot < kIaSlotCount; ++slot) {
		ia->preSkinRedirectNegativeValid[slot] = savedNegativeValid[slot];
		ia->preSkinRedirectNegativeGeneration[slot] = savedNegativeGeneration[slot];
		ia->preSkinRedirectNegativeSource[slot] = savedNegativeSource[slot];
	}
}

void DX12CommandListRuntime::ResetIa(DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12CommandListRuntime::RebuildActiveIaStatePreservingRedirectNegatives(
		&state->ia);
	// Plan fix A: the command list was reset, so any live static-template IA
	// replacement must be forgotten (the driver-side IA state is gone too).
	state->staticTemplateIaApplied = false;
	state->staticTemplateReplacement.reset();
	state->staticTemplateSavedIa = DX12ActiveIaState();
	DX12CommandListRuntime::BumpIaEpoch(state);
}

void DX12CommandListRuntime::ResetIa(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::ResetIa(fastState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	DX12CommandListRuntime::ResetIa(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12CommandListRuntime::RememberIndexBuffer(
	DX12SimulateContext *state, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!state)
		return true;

	const bool newValid = view != nullptr;
	const bool same =
		state->ia.hasIndexBuffer == newValid &&
		(!newValid ||
			(state->ia.indexBuffer.BufferLocation == view->BufferLocation &&
				state->ia.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state->ia.indexBuffer.Format == view->Format));
	DX12_PROF_COUNT("Rm.RuntimeIndexBufferCalls");
	if (!same)
		DX12_PROF_COUNT("Rm.RuntimeIndexBufferChanged");
	else
		DX12_PROF_COUNT("Rm.RuntimeIndexBufferNoChange");
	if (same)
		return false;
	state->ia.hasIndexBuffer = newValid;
	state->ia.indexBuffer = view ? *view : D3D12_INDEX_BUFFER_VIEW();
	state->ia.indexBufferHashValid = false;
	state->ia.indexBufferHash = 0;
	state->ia.indexBufferHashContentGeneration = 0;
	state->ia.indexBufferHashResolveGeneration = 0;
	// Keep Mod-mode resident IA state lightweight: the current view must stay
	// resident across unrelated PSOs, but GPUVA->resource resolution is only
	// needed when a later draw actually builds an IA-targeted override key.
	state->ia.indexBufferResolved = false;
	state->ia.indexBufferResource = DX12BufferResourceSummary();
	DX12CommandListRuntime::BumpIaEpoch(state);
	return true;
}

bool DX12CommandListRuntime::RememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return true;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		return DX12CommandListRuntime::RememberIndexBuffer(fastState, view);
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	const bool changed = DX12CommandListRuntime::RememberIndexBuffer(&state, view);
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

bool DX12CommandListRuntime::RememberVertexBuffers(
	DX12SimulateContext *state,
	UINT startSlot,
	UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!state)
		return true;

	DX12ActiveIaState &ia = state->ia;
	const UINT trackedCount = startSlot < ARRAYSIZE(ia.vertexBuffers) ?
		min(count, static_cast<UINT>(ARRAYSIZE(ia.vertexBuffers)) - startSlot) : 0;
	if (trackedCount != count)
		return true;
	DX12_PROF_COUNT("Rm.RuntimeVertexBufferCalls");
	DX12_PROF_ADD_ITEMS("Rm.RuntimeVertexBufferSlots", static_cast<LONG64>(trackedCount));
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
		if (!same)
			DX12_PROF_COUNT("Rm.RuntimeVertexBufferChangedSlots");
		else
			DX12_PROF_COUNT("Rm.RuntimeVertexBufferNoChangeSlots");
		if (same)
			continue;
		ia.hasVertexBuffer[slot] = newValid;
		ia.vertexBuffers[slot] = newView;
		ia.vertexBufferHashValid[slot] = false;
		ia.vertexBufferHashes[slot] = 0;
		ia.vertexBufferHashContentGeneration[slot] = 0;
		ia.vertexBufferHashResolveGeneration[slot] = 0;
		// See RememberIndexBuffer: resident Mod state keeps the live view here and
		// resolves the backing resource lazily only for IA-targeted override work.
		ia.vertexBufferResolved[slot] = false;
		ia.vertexBufferResources[slot] = DX12BufferResourceSummary();
		changedAny = true;
	}
	if (changedAny)
		DX12CommandListRuntime::BumpIaEpoch(state);
	return changedAny;
}

bool DX12CommandListRuntime::RememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return true;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		return DX12CommandListRuntime::RememberVertexBuffers(fastState, startSlot, count, views);
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	const bool changed = DX12CommandListRuntime::RememberVertexBuffers(
		&state, startSlot, count, views);
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

bool DX12CommandListRuntime::RememberPrimitiveTopology(
	DX12SimulateContext *state, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!state)
		return true;
	const bool changed = state->ia.primitiveTopology != topology;
	DX12_PROF_COUNT("Rm.RuntimePrimitiveTopologyCalls");
	if (changed)
		DX12_PROF_COUNT("Rm.RuntimePrimitiveTopologyChanged");
	else
		DX12_PROF_COUNT("Rm.RuntimePrimitiveTopologyNoChange");
	if (!changed)
		return false;
	state->ia.primitiveTopology = topology;
	return true;
}

bool DX12CommandListRuntime::RememberPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return true;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		return DX12CommandListRuntime::RememberPrimitiveTopology(fastState, topology);
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	const bool changed = DX12CommandListRuntime::RememberPrimitiveTopology(&state, topology);
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

void DX12CommandListRuntime::BumpIaEpoch(DX12SimulateContext *state)
{
	if (!state)
		return;
	state->iaEpoch++;
	if (!state->iaEpoch)
		state->iaEpoch = 1;
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
}

void DX12CommandListRuntime::BumpIaContentEpoch(DX12SimulateContext *state)
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

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::BumpIaContentEpoch(state);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::BumpIaContentEpoch(&it->second);
	ReleaseSRWLockExclusive(&shard.lock);
}

static bool EnsureResolvedIndexBufferForTargets(
	DX12SimulateContext *state,
	bool indexed,
	const DX12IaHashTargetPlan &targetPlan)
{
	if (!state || !targetPlan.RequiresIndexBuffer(indexed) ||
	    !state->ia.hasIndexBuffer || state->ia.indexBufferResolved)
		return false;
	const D3D12_INDEX_BUFFER_VIEW &view = state->ia.indexBuffer;
	if (!view.BufferLocation || !view.SizeInBytes)
		return false;
	DX12BufferResourceSummary summary = {};
	if (!DX12ResolveBufferResourceByGpuVa(
			view.BufferLocation,
			view.SizeInBytes,
			&summary))
		return false;
	state->ia.indexBufferResource = summary;
	state->ia.indexBufferResolved = true;
	return true;
}

static bool EnsureResolvedVertexBuffersForTargets(
	DX12SimulateContext *state,
	const DX12IaHashTargetPlan &targetPlan)
{
	if (!state)
		return false;
	bool resolvedAny = false;
	for (UINT slot = 0; slot < ARRAYSIZE(state->ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) ||
		    !state->ia.hasVertexBuffer[slot] ||
		    state->ia.vertexBufferResolved[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = state->ia.vertexBuffers[slot];
		if (!view.BufferLocation || !view.SizeInBytes)
			continue;
		DX12BufferResourceSummary summary = {};
		if (!DX12ResolveBufferResourceByGpuVa(
				view.BufferLocation,
				view.SizeInBytes,
				&summary))
			continue;
		state->ia.vertexBufferResources[slot] = summary;
		state->ia.vertexBufferResolved[slot] = true;
		resolvedAny = true;
	}
	return resolvedAny;
}

static bool EnsureResolvedIaResourcesForTargets(
	DX12SimulateContext *state,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed)
{
	const bool resolvedIndex =
		EnsureResolvedIndexBufferForTargets(state, indexed, targetPlan);
	const bool resolvedVertex =
		EnsureResolvedVertexBuffersForTargets(state, targetPlan);
	return resolvedIndex || resolvedVertex;
}

static DX12IaHashTargetPlan FilterIaHashTargetPlanForDrawCounts(
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	UINT vertexCount,
	UINT indexCount)
{
	if (!targetPlan.hasTextureOverrideChecks || targetPlan.requireAllIaBuffers)
		return targetPlan;

	DX12IaHashTargetPlan filteredPlan;
	filteredPlan.ResetKnown();
	filteredPlan.hasTextureOverrideChecks = targetPlan.hasTextureOverrideChecks;
	filteredPlan.hasDescriptorTargets = targetPlan.hasDescriptorTargets;

	if (targetPlan.requireIndexBufferView || targetPlan.requireIndexBuffer) {
		if (DX12ModHasPotentialIaTextureOverrideTargetByCount(
				true,
				0,
				vertexCount,
				indexCount)) {
			filteredPlan.requireIndexBuffer = targetPlan.requireIndexBuffer;
			filteredPlan.requireIndexBufferView = targetPlan.requireIndexBufferView;
		}
	}

	for (UINT slot = 0; slot < 32; ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot))
			continue;
		if (!DX12ModHasPotentialIaTextureOverrideTargetByCount(
				false,
				slot,
				vertexCount,
				indexCount)) {
			continue;
		}
		filteredPlan.RequireVertexBuffer(slot);
	}

	return filteredPlan;
}


uint32_t DX12CommandListRuntime::GetOrBuildCachedIndexViewHash(DX12ActiveIaState *ia)
{
	if (!ia || !ia->hasIndexBuffer)
		return 0;
	if (!ia->indexBuffer.BufferLocation || !ia->indexBuffer.SizeInBytes)
		return 0;
	const UINT64 contentGeneration = DX12GetIaHashContentGeneration();
	const UINT64 resolveGeneration = DX12GetIaHashResolveGeneration();
	if (ia->indexBufferHashValid &&
	    ia->indexBufferHashContentGeneration == contentGeneration &&
	    ia->indexBufferHashResolveGeneration == resolveGeneration) {
		return ia->indexBufferHash;
	}
	ia->indexBufferHash = DX12HashIaBufferContentView(
		ia->indexBuffer.BufferLocation,
		ia->indexBuffer.SizeInBytes,
		0,
		static_cast<UINT>(ia->indexBuffer.Format),
		0);
	ia->indexBufferHashValid = true;
	ia->indexBufferHashContentGeneration = contentGeneration;
	ia->indexBufferHashResolveGeneration = resolveGeneration;
	return ia->indexBufferHash;
}

uint32_t DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(
	DX12ActiveIaState *ia,
	UINT slot)
{
	if (!ia || slot >= ARRAYSIZE(ia->vertexBuffers) || !ia->hasVertexBuffer[slot])
		return 0;
	const D3D12_VERTEX_BUFFER_VIEW &view = ia->vertexBuffers[slot];
	if (!view.BufferLocation || !view.SizeInBytes)
		return 0;
	const UINT64 contentGeneration = DX12GetIaHashContentGeneration();
	const UINT64 resolveGeneration = DX12GetIaHashResolveGeneration();
	if (ia->vertexBufferHashValid[slot] &&
	    ia->vertexBufferHashContentGeneration[slot] == contentGeneration &&
	    ia->vertexBufferHashResolveGeneration[slot] == resolveGeneration) {
		return ia->vertexBufferHashes[slot];
	}
	// Reuse cache from the backing ResourceWrapper: games that rotate a small
	// set of vertex buffers across slots/command lists rebuild the same hash
	// repeatedly because each command list invalidates its own slot entry.
	uint32_t wrapperHash = 0;
	if (DX12TryGetWrapperIaHash(
			view.BufferLocation,
			view.SizeInBytes,
			view.StrideInBytes,
			slot,
			contentGeneration,
			resolveGeneration,
			&wrapperHash)) {
		ia->vertexBufferHashes[slot] = wrapperHash;
		ia->vertexBufferHashValid[slot] = true;
		ia->vertexBufferHashContentGeneration[slot] = contentGeneration;
		ia->vertexBufferHashResolveGeneration[slot] = resolveGeneration;
		return wrapperHash;
	}
	ia->vertexBufferHashes[slot] = DX12HashIaBufferContentView(
		view.BufferLocation,
		view.SizeInBytes,
		view.StrideInBytes,
		0,
		slot);
	DX12StoreWrapperIaHash(
		view.BufferLocation,
		view.SizeInBytes,
		view.StrideInBytes,
		slot,
		contentGeneration,
		resolveGeneration,
		ia->vertexBufferHashes[slot]);
	ia->vertexBufferHashValid[slot] = true;
	ia->vertexBufferHashContentGeneration[slot] = contentGeneration;
	ia->vertexBufferHashResolveGeneration[slot] = resolveGeneration;
	return ia->vertexBufferHashes[slot];
}

bool DX12CommandListRuntime::BuildIaHashStateForTargetsCached(
	DX12ActiveIaState *ia, const DX12IaHashTargetPlan &targetPlan,
	bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	if (!ia)
		return false;
	if (!state)
		return false;
	*state = DX12IaHashState();
	state->vertexBuffers.reserve(8);
	(void)vertexCount;
	(void)indexCount;
	(void)firstVertex;
	(void)firstIndex;
	(void)baseVertex;
	const DX12IaHashTargetPlan filteredTargetPlan =
		FilterIaHashTargetPlanForDrawCounts(
			targetPlan,
			indexed,
			vertexCount,
			indexCount);

	if (filteredTargetPlan.RequiresIndexBuffer(indexed) && ia->hasIndexBuffer) {
		const bool requireIndexHash =
			filteredTargetPlan.RequiresIndexBufferHash(indexed) &&
			DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
				true,
				0,
				vertexCount,
				indexCount);
			state->hasIndexBuffer = true;
			state->indexHash = requireIndexHash ?
				GetOrBuildCachedIndexViewHash(ia) : 0;
			state->indexView = ia->indexBuffer;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia->vertexBuffers); ++slot) {
		if (!ia->hasVertexBuffer[slot] || !filteredTargetPlan.RequiresVertexBuffer(slot))
			continue;
		const bool requireVertexHash =
			DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
				false,
				slot,
				vertexCount,
				indexCount);
		DX12IaBufferHash item;
		item.slot = slot;
		item.hash = requireVertexHash ? GetOrBuildCachedVertexViewHash(ia, slot) : 0;
		item.vertexView = ia->vertexBuffers[slot];
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
		// no longer inherit unrelated resource/content churn. We keep IA binds
		// lightweight in Mod mode and only resolve GPU VA ownership when a draw
		// actually reaches IA-targeted override evaluation.
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
	DX12SimulateContext *state,
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
	const DX12IaHashTargetPlan filteredTargetPlan =
		FilterIaHashTargetPlanForDrawCounts(
			targetPlan,
			indexed,
			vertexCount,
			indexCount);
	const UINT64 targetPlanFingerprint = filteredTargetPlan.Fingerprint();
	const bool resolvedAny =
		EnsureResolvedIaResourcesForTargets(state, filteredTargetPlan, indexed);
	DX12IaHashStateCacheKey key;
	DX12IaHashKeyPrefixCacheEntry &entry =
		state->resolvedDraw.iaHashKeyPrefixes[
			cacheSlot % state->resolvedDraw.iaHashKeyPrefixes.size()];
	if (!resolvedAny &&
	    !DX12IaHashKeyPrefixCacheUtils::TryLoadStable(
			entry, state->iaEpoch, targetPlanFingerprint, indexed, &key)) {
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 globalResolveGeneration = DX12GetIaHashResolveGeneration();
		if (!DX12IaHashKeyPrefixCacheUtils::TryLoad(
				entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, &key)) {
			key = BuildIaHashStateCacheKeyForTargets(
				state->ia, state->iaContentEpoch, filteredTargetPlan, indexed,
				0, 0, 0, 0, 0);
			DX12IaHashKeyPrefixCacheUtils::Store(
				&entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, key);
		}
	} else if (resolvedAny) {
		// Plan fix E: try the prefix cache before rebuilding. The built key
		// carries exact per-resource generations for resolved targets, and
		// TryLoad only compares the global generations when the key actually
		// fell back to them (usesGlobalFallback), so fully-resolved keys stay
		// cached across unrelated buffer churn that bumps the global
		// generation every frame. Previously this branch rebuilt the key on
		// every draw.
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 globalResolveGeneration = DX12GetIaHashResolveGeneration();
		if (!DX12IaHashKeyPrefixCacheUtils::TryLoad(
				entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, &key)) {
			key = BuildIaHashStateCacheKeyForTargets(
				state->ia, state->iaContentEpoch, filteredTargetPlan, indexed,
				0, 0, 0, 0, 0);
			DX12IaHashKeyPrefixCacheUtils::Store(
				&entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, key);
		}
	}
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	// firstVertex/firstIndex/baseVertex are intentionally NOT part of the IA
	// hash key: BuildIaHashStateForTargets ignores them (the hash covers the
	// whole bound view, DX11-style), and only the candidate-match stage
	// (match_first_index etc.) consumes them. Keeping them out lets draws
	// that differ only in vertex offsets share the same IA hash cache entry.
	return key;
}

ID3D12PipelineState *DX12CommandListRuntime::GetPipelineState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state)
		return state->pipelineState;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	ID3D12PipelineState *result =
		it == shard.states.end() ? nullptr : it->second.pipelineState;
	ReleaseSRWLockShared(&shard.lock);
	return result;
}

DX12SimulateContext DX12CommandListRuntime::GetState(
	ID3D12GraphicsCommandList *commandList)
{
	DX12SimulateContext result;
	if (!commandList)
		return result;

	const DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtr(commandList);
	if (state)
		result = *state;
	return result;
}

const DX12SimulateContext *DX12CommandListRuntime::GetStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	return DX12CommandListRuntime::GetStatePtrFast(commandList);
}

static void DX12RuntimeClearRootTables(DX12SimulateContext *state)
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

static void DX12RuntimeInvalidateHeapSummary(
	bool *resolved,
	DX12DescriptorHeapSummary *summary)
{
	if (resolved)
		*resolved = false;
	if (summary)
		*summary = DX12DescriptorHeapSummary();
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

static void DX12RuntimeInvalidateRootTableResolution(
	RootTableState *table)
{
	if (!table)
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
}

static void DX12RuntimeResolveRootTable(
	DX12SimulateContext *state,
	RootTableState *table)
{
	if (!state || !table || !table->valid)
		return;
	DX12RuntimeInvalidateRootTableResolution(table);

	if (state->runtimeCbvSrvUavHeapResolved &&
			state->runtimeCbvSrvUavHeap.heap == state->cbvSrvUavHeap &&
			table->baseDescriptor.ptr >= state->runtimeCbvSrvUavHeap.gpuStart) {
		DX12RuntimeFillRootTableFromHeap(table, state->runtimeCbvSrvUavHeap);
	}
}

static void DX12RuntimeBumpBindingQuerySerial(DX12SimulateContext *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

static void DX12RuntimeBumpDescriptorQuerySerial(UINT64 *serial)
{
	if (!serial)
		return;
	if (*serial == UINT64_MAX)
		*serial = 1;
	else
		++(*serial);
}

static void DX12RuntimeBumpGraphicsDescriptorQuerySerial(
	DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12RuntimeBumpDescriptorQuerySerial(&state->graphicsDescriptorQuerySerial);
}

static void DX12RuntimeBumpRootBindingRevision(
	std::array<UINT64, MaxRootParameters> *revisions,
	UINT rootParameterIndex)
{
	if (!revisions || rootParameterIndex >= revisions->size())
		return;
	DX12RuntimeBumpDescriptorQuerySerial(&(*revisions)[rootParameterIndex]);
}

static void DX12RuntimeBumpAllRootBindingRevisions(
	std::array<UINT64, MaxRootParameters> *revisions)
{
	if (!revisions)
		return;
	for (UINT root = 0; root < revisions->size(); ++root)
		DX12RuntimeBumpRootBindingRevision(revisions, root);
}

static void DX12RuntimeBumpComputeDescriptorQuerySerial(
	DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12RuntimeBumpDescriptorQuerySerial(&state->computeDescriptorQuerySerial);
}

// Descriptor queries only depend on root parameters that can expose CBV/SRV/UAV
// bindings to the compute shader.  Keep the conservative behaviour until the
// current root signature has been parsed; once the mask is known, churn on an
// unrelated parameter must not invalidate the descriptor-query cache.
static bool DX12RuntimeComputeDescriptorQueryUsesRoot(
	const DX12SimulateContext *state, UINT rootParameterIndex)
{
	if (!state || rootParameterIndex >= MaxRootParameters ||
		!state->computeRootUsedMaskValid)
		return true;
	return (state->computeRootUsedMask & (1ull << rootParameterIndex)) != 0;
}

static void DX12RuntimeBumpComputeDescriptorBindingVersion(
	DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12RuntimeBumpDescriptorQuerySerial(
		&state->computeDescriptorBindingVersion);
}

static void DX12RuntimeBumpComputeBindingSerial(DX12SimulateContext *state)
{
	if (!state)
		return;
	++state->computeBindingSerial;
	if (!state->computeBindingSerial)
		state->computeBindingSerial = 1;
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	DX12SimulateContext *state,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!state)
		return;
	const bool changed =
		state->cbvSrvUavHeap != cbvSrvUavHeap ||
		state->samplerHeap != samplerHeap;
	if (!changed)
		return;

	state->cbvSrvUavHeap = cbvSrvUavHeap;
	state->samplerHeap = samplerHeap;
	// Cache the heap content version at bind time so the draw hot path never
	// takes the global descriptor lock (see cbvSrvUavHeapVersion comment).
	state->cbvSrvUavHeapVersion =
		DX12GetDescriptorHeapVersion(cbvSrvUavHeap);
	// Match DX11's "bind now, resolve later" policy: descriptor heap identity is
	// the hot-path state we need to preserve, while heap summaries can be filled
	// lazily the moment a draw actually needs descriptor inspection.
	DX12RuntimeInvalidateHeapSummary(
		&state->runtimeCbvSrvUavHeapResolved,
		&state->runtimeCbvSrvUavHeap);
	DX12RuntimeInvalidateHeapSummary(
		&state->runtimeSamplerHeapResolved,
		&state->runtimeSamplerHeap);
	// Resolve the heap summaries at bind time (a handful of calls per frame)
	// so the dispatch hot path never has to take the global descriptor lock
	// to map a GPU descriptor handle back to a heap. The summaries are pure
	// identity + layout data: the version field may age, but slot arithmetic
	// stays valid for the lifetime of the heap and content changes are still
	// caught by the descriptor query serial on the draw/dispatch path.
	state->runtimeCbvSrvUavHeapResolved =
		DX12RuntimeResolveHeapSummary(
			cbvSrvUavHeap,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&state->runtimeCbvSrvUavHeap);
	state->runtimeSamplerHeapResolved =
		DX12RuntimeResolveHeapSummary(
			samplerHeap,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			&state->runtimeSamplerHeap);
	DX12RuntimeClearRootTables(state);
	// Heap swap invalidates every table-based binding (DXVK: whole mask dirty).
	state->computeRootDirtyMask = ~0ull;
	DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
	DX12RuntimeBumpComputeDescriptorQuerySerial(state);
	DX12RuntimeBumpAllRootBindingRevisions(
		&state->graphicsRootBindingRevisions);
	DX12RuntimeBumpAllRootBindingRevisions(
		&state->computeRootBindingRevisions);
	DX12RuntimeBumpComputeDescriptorBindingVersion(state);
	DX12RuntimeBumpBindingQuerySerial(state);
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return;

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::SetDescriptorHeaps(state, cbvSrvUavHeap, samplerHeap);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::SetDescriptorHeaps(&it->second, cbvSrvUavHeap, samplerHeap);
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12CommandListRuntime::RememberComputeRootSignature(
	DX12SimulateContext *state,
	ID3D12RootSignature *rootSignature)
{
	if (!state)
		return true;
	if (state->computeRootSignature == rootSignature)
		return false;

	state->computeRootSignature = rootSignature;
	// Root signature change can rewrite every binding: mark all root
	// parameters dirty (DXVK: bindingsDirty = all bits).
	state->computeRootDirtyMask = ~0ull;
	state->computeRootUsedMaskValid = false;
	// Compute which root parameters this signature exposes to the shader so
	// later churn on unrelated parameters does not invalidate the binding
	// cache. Missing metadata keeps the mask invalid (conservative: any dirty
	// bit then invalidates).
	DX12RootSignatureSummary summary;
	if (rootSignature && DX12GetRootSignatureSummary(rootSignature, &summary) &&
		summary.parsed) {
		UINT64 usedMask = 0;
		for (const DX12RootParameterSummary &parameter : summary.parameters) {
			if (parameter.rootParameterIndex >= MaxRootParameters)
				continue;
			// Match CollectPreSkinBindings' affectsPreSkinBindings test:
			// descriptor tables with CBV/SRV/UAV ranges, root descriptors,
			// and root constants are observed by the probe; sampler-only
			// parameters are not, so churn on them must not invalidate.
			bool affectsPreSkinBindings =
				parameter.parameterType !=
					D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
					range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
					range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
					affectsPreSkinBindings = true;
					break;
				}
			}
			if (affectsPreSkinBindings)
				usedMask |= 1ull << parameter.rootParameterIndex;
		}
		state->computeRootUsedMask = usedMask;
		state->computeRootUsedMaskValid = true;
	}
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->computeRootTables[root] = RootTableState();
		state->computeRootDescriptors[root] = RootDescriptorState();
		state->computeRootConstants[root] = RootConstantsState();
	}
	state->computeRootTableValidCount = 0;
	DX12RuntimeBumpAllRootBindingRevisions(
		&state->computeRootBindingRevisions);
	DX12RuntimeBumpComputeBindingSerial(state);
	DX12RuntimeBumpComputeDescriptorQuerySerial(state);
	DX12RuntimeBumpComputeDescriptorBindingVersion(state);
	DX12RuntimeBumpBindingQuerySerial(state);
	return true;
}

bool DX12CommandListRuntime::RememberGraphicsRootSignature(
	DX12SimulateContext *state,
	ID3D12RootSignature *rootSignature)
{
	if (!state)
		return true;
	if (state->graphicsRootSignature == rootSignature)
		return false;

	state->graphicsRootSignature = rootSignature;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsRootTables[root] = RootTableState();
		state->graphicsRootDescriptors[root] = RootDescriptorState();
		state->graphicsRootConstants[root] = RootConstantsState();
	}
	state->graphicsRootTableValidCount = 0;
	DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
	DX12RuntimeBumpAllRootBindingRevisions(
		&state->graphicsRootBindingRevisions);
	DX12RuntimeBumpBindingQuerySerial(state);
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
	return true;
}

bool DX12CommandListRuntime::RememberGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList,
	ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return true;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		return DX12CommandListRuntime::RememberGraphicsRootSignature(
			state, rootSignature);
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	bool changed = true;
	if (it != shard.states.end()) {
		changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
			&it->second, rootSignature);
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

bool DX12CommandListRuntime::RememberComputeRootSignature(
	ID3D12GraphicsCommandList *commandList,
	ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return true;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		return DX12CommandListRuntime::RememberComputeRootSignature(
			state, rootSignature);
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	bool changed = true;
	if (it != shard.states.end()) {
		changed = DX12CommandListRuntime::RememberComputeRootSignature(
			&it->second, rootSignature);
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

bool DX12CommandListRuntime::RememberRootDescriptorTable(
	DX12SimulateContext *state,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return true;
	RootTableState &table = graphics ?
		state->graphicsRootTables[rootParameterIndex] :
		state->computeRootTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		return false;
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
	// Fill the table's runtime heap fields at bind time from the cached heap
	// summary (pure arithmetic, no global lock). The dispatch path can then
	// compute descriptor slots without reverse-mapping the GPU handle.
	DX12RuntimeResolveRootTable(state, &table);
	if (!graphics) {
		state->computeBindingSerial++;
		if (!state->computeBindingSerial)
			state->computeBindingSerial = 1;
		// DXVK-style: mark this root parameter dirty instead of relying on
		// the global serial alone. The mask lets probe/collect skip bindings
		// that the current root signature does not use.
		state->computeRootDirtyMask |= 1ull << rootParameterIndex;
		const bool affectsDescriptorQueries =
			DX12RuntimeComputeDescriptorQueryUsesRoot(
				state, rootParameterIndex);
		if (affectsDescriptorQueries)
			DX12RuntimeBumpComputeDescriptorQuerySerial(state);
		if (affectsDescriptorQueries)
			DX12RuntimeBumpComputeDescriptorBindingVersion(state);
		DX12RuntimeBumpRootBindingRevision(
			&state->computeRootBindingRevisions, rootParameterIndex);
	} else {
		DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
		DX12RuntimeBumpRootBindingRevision(
			&state->graphicsRootBindingRevisions, rootParameterIndex);
	}
	DX12RuntimeBumpBindingQuerySerial(state);
	return true;
}

bool DX12CommandListRuntime::RememberRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList)
		return true;

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		return DX12CommandListRuntime::RememberRootDescriptorTable(
			state, graphics, rootParameterIndex, baseDescriptor);
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	bool changed = true;
	if (it != shard.states.end()) {
		changed = DX12CommandListRuntime::RememberRootDescriptorTable(
			&it->second, graphics, rootParameterIndex, baseDescriptor);
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return changed;
}

void DX12CommandListRuntime::RememberGraphicsRootDescriptor(
	DX12SimulateContext *state,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;

	RootDescriptorState &descriptor =
		state->graphicsRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		return;
	}

	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
	DX12RuntimeBumpRootBindingRevision(
		&state->graphicsRootBindingRevisions, rootParameterIndex);
	DX12RuntimeBumpBindingQuerySerial(state);
}

void DX12CommandListRuntime::RememberGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberGraphicsRootDescriptor(
			state, rootParameterIndex, type, address);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberGraphicsRootDescriptor(
			&it->second, rootParameterIndex, type, address);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberComputeRootDescriptor(
	DX12SimulateContext *state,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;

	RootDescriptorState &descriptor =
		state->computeRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		return;
	}

	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	state->computeRootDirtyMask |= 1ull << rootParameterIndex;
	DX12RuntimeBumpComputeBindingSerial(state);
	if (DX12RuntimeComputeDescriptorQueryUsesRoot(
			state, rootParameterIndex))
		DX12RuntimeBumpComputeDescriptorQuerySerial(state);
	DX12RuntimeBumpRootBindingRevision(
		&state->computeRootBindingRevisions, rootParameterIndex);
	DX12RuntimeBumpBindingQuerySerial(state);
}

void DX12CommandListRuntime::RememberComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberComputeRootDescriptor(
			state, rootParameterIndex, type, address);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberComputeRootDescriptor(
			&it->second, rootParameterIndex, type, address);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
	DX12SimulateContext *state,
	UINT rootParameterIndex,
	UINT destOffset,
	UINT count,
	const void *values)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;

	RootConstantsState &constants =
		state->graphicsRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values)) {
		DX12RuntimeBumpBindingQuerySerial(state);
	}
}

void DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	UINT rootParameterIndex,
	UINT destOffset,
	UINT count,
	const void *values)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
			state, rootParameterIndex, destOffset, count, values);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
			&it->second, rootParameterIndex, destOffset, count, values);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberComputeRoot32BitConstants(
	DX12SimulateContext *state,
	UINT rootParameterIndex,
	UINT destOffset,
	UINT count,
	const void *values)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;

	RootConstantsState &constants =
		state->computeRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values)) {
		state->computeRootDirtyMask |= 1ull << rootParameterIndex;
		DX12RuntimeBumpComputeBindingSerial(state);
		DX12RuntimeBumpBindingQuerySerial(state);
	}
}

void DX12CommandListRuntime::RememberComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	UINT rootParameterIndex,
	UINT destOffset,
	UINT count,
	const void *values)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberComputeRoot32BitConstants(
			state, rootParameterIndex, destOffset, count, values);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberComputeRoot32BitConstants(
			&it->second, rootParameterIndex, destOffset, count, values);
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

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		if (cbvSrvUavHeap)
			*cbvSrvUavHeap = state->cbvSrvUavHeap;
		if (samplerHeap)
			*samplerHeap = state->samplerHeap;
		return true;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
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


void DX12CommandListLifecycle::Register(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntime::Register(commandList);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningBindingTracking()) {
		const bool inserted = DX12BindingTracker::RegisterCommandList(commandList);
		if (inserted)
			DX12BindingTracker::ResetCommandList(commandList, initialState);
	}
}

void DX12CommandListLifecycle::Unregister(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12ModForgetPreSkinOutputResizesForCommandList(commandList);
	DX12ModForgetPreSkinReplacementForCommandList(commandList);
	DX12ModForgetDrawSrvReplacementForCommandList(commandList);
	DX12ModForgetPendingPreSkinSubmissionsForCommandList(commandList);
	DX12ModForgetBundleMirrorSession(commandList);
	DX12BindingTracker::UnregisterCommandList(commandList);
	DX12HuntUnregisterCommandList(commandList);
	DX12CommandListRuntime::Unregister(commandList);
}

void DX12CommandListLifecycle::Reset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12ModForgetPreSkinOutputResizesForCommandList(commandList);
	DX12ModForgetPreSkinReplacementForCommandList(commandList);
	DX12ModForgetDrawSrvReplacementForCommandList(commandList);
	DX12ModForgetPendingPreSkinSubmissionsForCommandList(commandList);
	DX12ModForgetBundleMirrorSession(commandList);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningBindingTracking()) {
		DX12_DIAG_QPC(begin);
		DX12BindingTracker::ResetCommandList(commandList, initialState);
		DX12_PROF_CPU_STAGE(
			"CommandListReset",
			"binding_tracker_reset",
			0,
			1,
			DX12_DIAG_ELAPSED_US(begin));
	}
	// DX12CommandListRuntime::Reset (invoked by the wrapper just before this
	// lifecycle pass) already ran RememberPipelineState with the same
	// initialState and fully reset the IA/static-template state; repeating
	// either here paid a second TLS lookup plus a redundant ~10KB IA rewrite
	// per command-list Reset. Only the IA epoch bump remains unique: it
	// invalidates IA-keyed caches from the previous recording.
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides()) {
		DX12_DIAG_QPC(begin);
		DX12SimulateContext *fastState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (fastState)
			DX12CommandListRuntime::BumpIaEpoch(fastState);
		DX12_PROF_CPU_STAGE(
			"CommandListReset",
			"reset_ia",
			0,
			1,
			DX12_DIAG_ELAPSED_US(begin));
	}
	if (tracking.trackPsoState || tracking.trackHuntIa) {
		DX12_DIAG_QPC(begin);
		DX12HuntResetCommandList(commandList, initialState);
		DX12_PROF_CPU_STAGE(
			"CommandListReset",
			"hunt_reset",
			0,
			1,
			DX12_DIAG_ELAPSED_US(begin));
	}
}

void DX12CommandListLifecycle::ClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	// ClearState resets every binding including descriptor heaps: a deferred
	// draw-SRV restore refers to a binding session that no longer exists.
	// Drop it quietly; the shadow trackers are reset below.
	DX12ModForgetDrawSrvReplacementForCommandList(commandList);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "clear_state");
	if (tracking.trackBindings || DX12ModNeedsPreSkinningBindingTracking())
		DX12BindingTracker::ResetCommandList(commandList, pipelineState);
	DX12CommandListRuntime::Reset(commandList, pipelineState);
	if (DX12HuntIsEnabled())
		DX12HuntResetCommandList(commandList, pipelineState);
}

void DX12CommandListLifecycle::InvalidateAfterExecuteBundle(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	// A bundle may leave root, descriptor, PSO and IA state in the parent, but
	// its independently recorded state cannot be reconstructed by the parent
	// tracker without replaying the bundle stream. Mark the parent unknown so a
	// later Mod never mirrors stale tables or matches stale IA/PSO state.
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings || DX12ModNeedsPreSkinningBindingTracking())
		DX12BindingTracker::ResetCommandList(commandList, nullptr);
	DX12CommandListRuntime::Reset(commandList, nullptr);
	if (tracking.trackPsoState || tracking.trackHuntIa)
		DX12HuntResetCommandList(commandList, nullptr);
}
