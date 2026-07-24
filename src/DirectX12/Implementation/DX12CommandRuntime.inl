DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;

std::shared_ptr<const DX12ModIaReplacement>
DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration);

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

DX12SimulateContext *DX12CommandListRuntime::GetDirectStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*>::iterator it =
		shard.directStates.find(commandList);
	DX12SimulateContext *state = it != shard.directStates.end() ? it->second : nullptr;
	ReleaseSRWLockShared(&shard.lock);
	return state;
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
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*>::iterator directIt =
		shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		DX12CommandListRuntime::TlsRefreshFromState(
			directIt->second, commandList, &shard, true);
		return;
	}

	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
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

DX12SimulateContext *DX12CommandListRuntime::GetStatePtrFast(
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
	state->directIaCandidates = {};
	state->residentDrawNegativeCache = {};
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
}

void DX12ResolvedDrawRuntimeStateUtils::EnsureScene(
	DX12SimulateContextDrawTemplate *state)
{
	// Deliberately preserve draw caches across Present/scene churn.
	// Their keys already encode reload/content/resolve/IA epochs, so DX11-style
	// fine-grained invalidation is sufficient and avoids heuristic full clears.
	(void)state;
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

void DX12CommandListRuntime::Reset(
	DX12SimulateContext *state, ID3D12PipelineState *initialState)
{
	if (!state)
		return;

	ID3D12Device *cachedDevice = state->device;
	IUnknown *cachedCommandListIdentity = state->commandListIdentity;
	DX12CommandListTrackingCache oldCache = state->trackingCache;
	// IA caches describe immutable content and candidate results rather than a
	// single recording. Their keys already carry the required generations, so
	// preserve the working set across command-list Reset like DX11's resident
	// HackerContext state.
	DX12SimulateContextDrawTemplate persistentResolvedDraw =
		std::move(state->resolvedDraw);
	*state = DX12SimulateContext();
	state->device = cachedDevice;
	state->commandListIdentity = cachedCommandListIdentity;
	state->pipelineState = nullptr;
	state->trackingCache = oldCache;
	state->resolvedDraw = std::move(persistentResolvedDraw);
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
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheHit();
		return &state->drawDecision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheMiss();
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

void DX12CommandListRuntime::ResetIa(DX12SimulateContext *state)
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

void DX12CommandListRuntime::RememberIndexBuffer(
	DX12SimulateContext *state, const D3D12_INDEX_BUFFER_VIEW *view)
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
}

void DX12CommandListRuntime::RememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberIndexBuffer(fastState, view);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	DX12CommandListRuntime::RememberIndexBuffer(&state, view);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberVertexBuffers(
	DX12SimulateContext *state,
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
}

void DX12CommandListRuntime::RememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return;

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberVertexBuffers(fastState, startSlot, count, views);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	DX12CommandListRuntime::RememberVertexBuffers(&state, startSlot, count, views);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	DX12SimulateContext *state, D3D12_PRIMITIVE_TOPOLOGY topology)
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

	DX12SimulateContext *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPrimitiveTopology(fastState, topology);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12SimulateContext &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPrimitiveTopology(&state, topology);
	ReleaseSRWLockExclusive(&shard.lock);
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
	filteredPlan.descriptorTargets = targetPlan.descriptorTargets;

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

	for (UINT slot = 0; slot < ARRAYSIZE(targetPlan.requireVertexBuffers); ++slot) {
		if (!targetPlan.requireVertexBuffers[slot])
			continue;
		if (!DX12ModHasPotentialIaTextureOverrideTargetByCount(
				false,
				slot,
				vertexCount,
				indexCount)) {
			continue;
		}
		filteredPlan.requireVertexBuffers[slot] = true;
	}

	return filteredPlan;
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

	if (filteredTargetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer) {
		const bool requireIndexHash =
			filteredTargetPlan.RequiresIndexBufferHash(indexed) &&
			DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
				true,
				0,
				vertexCount,
				indexCount);
		const uint32_t indexHash = requireIndexHash ?
		// DX11 resident mod matching keys CheckTextureOverride IB/VB tests by the
		// bound buffer/view identity plus tracked content, then uses draw-context
		// filters like match_first_index to narrow the match. Using the whole bound
		// view here keeps the hash stable across repeated draws and avoids
		// re-hashing a new draw subrange every call.
			DX12HashIaBufferContentView(
				ia.indexBuffer.BufferLocation,
				ia.indexBuffer.SizeInBytes,
				0,
				static_cast<UINT>(ia.indexBuffer.Format),
				0) : 0;
		state->hasIndexBuffer = true;
		state->indexHash = indexHash;
		state->indexView = ia.indexBuffer;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		if (!filteredTargetPlan.RequiresVertexBuffer(slot))
			continue;
		const bool requireVertexHash =
			DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
				false,
				slot,
				vertexCount,
				indexCount);
		const uint32_t vertexHash = requireVertexHash ?
			DX12HashIaBufferContentView(
				ia.vertexBuffers[slot].BufferLocation,
				ia.vertexBuffers[slot].SizeInBytes,
				ia.vertexBuffers[slot].StrideInBytes,
				0,
				slot) : 0;
		DX12IaBufferHash item;
		item.slot = slot;
		item.hash = vertexHash;
		item.vertexView = ia.vertexBuffers[slot];
		state->vertexBuffers.push_back(item);
	}

	return state->hasIndexBuffer || !state->vertexBuffers.empty();
}

static uint32_t GetOrBuildCachedIndexViewHash(DX12ActiveIaState *ia)
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

static uint32_t GetOrBuildCachedVertexViewHash(
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
	ia->vertexBufferHashes[slot] = DX12HashIaBufferContentView(
		view.BufferLocation,
		view.SizeInBytes,
		view.StrideInBytes,
		0,
		slot);
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
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 globalResolveGeneration = DX12GetIaHashResolveGeneration();
		key = BuildIaHashStateCacheKeyForTargets(
			state->ia, state->iaContentEpoch, filteredTargetPlan, indexed,
			0, 0, 0, 0, 0);
		DX12IaHashKeyPrefixCacheUtils::Store(
			&entry, state->iaEpoch, targetPlanFingerprint, indexed,
			globalContentGeneration, globalResolveGeneration, key);
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

static void DX12RuntimeBumpComputeDescriptorQuerySerial(
	DX12SimulateContext *state)
{
	if (!state)
		return;
	DX12RuntimeBumpDescriptorQuerySerial(&state->computeDescriptorQuerySerial);
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
	// Match DX11's "bind now, resolve later" policy: descriptor heap identity is
	// the hot-path state we need to preserve, while heap summaries can be filled
	// lazily the moment a draw actually needs descriptor inspection.
	DX12RuntimeInvalidateHeapSummary(
		&state->runtimeCbvSrvUavHeapResolved,
		&state->runtimeCbvSrvUavHeap);
	DX12RuntimeInvalidateHeapSummary(
		&state->runtimeSamplerHeapResolved,
		&state->runtimeSamplerHeap);
	DX12RuntimeClearRootTables(state);
	DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
	DX12RuntimeBumpComputeDescriptorQuerySerial(state);
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

void DX12CommandListRuntime::RememberComputeRootSignature(
	DX12SimulateContext *state,
	ID3D12RootSignature *rootSignature)
{
	if (!state)
		return;
	if (state->computeRootSignature == rootSignature)
		return;

	state->computeRootSignature = rootSignature;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->computeRootTables[root] = RootTableState();
		state->computeRootDescriptors[root] = RootDescriptorState();
		state->computeRootConstants[root] = RootConstantsState();
	}
	state->computeRootTableValidCount = 0;
	DX12RuntimeBumpComputeBindingSerial(state);
	DX12RuntimeBumpComputeDescriptorQuerySerial(state);
	DX12RuntimeBumpComputeDescriptorBindingVersion(state);
	DX12RuntimeBumpBindingQuerySerial(state);
}

void DX12CommandListRuntime::RememberGraphicsRootSignature(
	DX12SimulateContext *state,
	ID3D12RootSignature *rootSignature)
{
	if (!state)
		return;
	if (state->graphicsRootSignature == rootSignature)
		return;

	state->graphicsRootSignature = rootSignature;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsRootTables[root] = RootTableState();
		state->graphicsRootDescriptors[root] = RootDescriptorState();
		state->graphicsRootConstants[root] = RootConstantsState();
	}
	state->graphicsRootTableValidCount = 0;
	DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
	DX12RuntimeBumpBindingQuerySerial(state);
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
}

void DX12CommandListRuntime::RememberGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList,
	ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberGraphicsRootSignature(
			state, rootSignature);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberGraphicsRootSignature(
			&it->second, rootSignature);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberComputeRootSignature(
	ID3D12GraphicsCommandList *commandList,
	ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	DX12SimulateContext *state =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberComputeRootSignature(
			state, rootSignature);
		return;
	}

	DX12CommandListRuntimeShard &shard =
		DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberComputeRootSignature(
			&it->second, rootSignature);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberRootDescriptorTable(
	DX12SimulateContext *state,
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
	if (!graphics) {
		state->computeBindingSerial++;
		if (!state->computeBindingSerial)
			state->computeBindingSerial = 1;
		DX12RuntimeBumpComputeDescriptorQuerySerial(state);
		DX12RuntimeBumpComputeDescriptorBindingVersion(state);
	} else {
		DX12RuntimeBumpGraphicsDescriptorQuerySerial(state);
		DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
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

	DX12SimulateContext *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			state, graphics, rootParameterIndex, baseDescriptor);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			&it->second, graphics, rootParameterIndex, baseDescriptor);
	}
	ReleaseSRWLockExclusive(&shard.lock);
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
	DX12RuntimeBumpBindingQuerySerial(state);
	DX12CommandListRuntime::InvalidateResolvedDrawPlan(state);
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
	DX12RuntimeBumpComputeBindingSerial(state);
	DX12RuntimeBumpComputeDescriptorQuerySerial(state);
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

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningBindingTracking())
		DX12BindingTracker::ResetCommandList(commandList, initialState);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, initialState);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::ResetIa(commandList);
	if (tracking.trackPsoState || tracking.trackHuntIa)
		DX12HuntResetCommandList(commandList, initialState);
}

void DX12CommandListLifecycle::ClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

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



void DX12CommandListCommandRecorder::RecordResourceBarrier(
	UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	// ResourceBarrier changes usage state, not buffer identity or content.
	// Stable IA/mod hashes depend on GPUVA, content, and view metadata, so normal
	// mod runtime does not need barrier state capture.
	if (DX12CommandListStateCapture::ShouldTrackResourceStates())
		DX12RecordResourceBarrier(numBarriers, barriers);
}

void DX12CommandListCommandRecorder::RecordExecuteBundle(
	ID3D12GraphicsCommandList *bundle,
	ID3D12PipelineState *initialState)
{
	// Bundle execution records command-list state into the parent stream. Keep
	// bundle registration here so hook code does not need to know lifecycle
	// details for secondary command lists.
	DX12CommandListLifecycle::Register(bundle, initialState);
}

void DX12CommandListCommandRecorder::RecordExecuteIndirect(
	ID3D12GraphicsCommandList *commandList)
{
	// ExecuteIndirect can expand into draw or dispatch work on the GPU. Shader
	// hunting treats it as both graphics and compute usage without inspecting
	// the command signature on the CPU hot path.
	DX12HuntRecordExecuteIndirect(commandList);
}



bool DX12CommandListStateCapture::ShouldTrackBindings()
{
	const bool frameCapture = DX12FrameAnalysis::IsCapturing();
	const bool shaderDumpActive = DX12ShaderDumpIsBusy();
	// Pre-skin dispatch probing can now consume the resident runtime snapshot
	// directly, so keep the tracked binding path reserved for capture/debug
	// modes instead of charging every descriptor/root-table bind.
	return frameCapture || shaderDumpActive;
}

bool DX12CommandListStateCapture::ShouldTrackIaBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackPipelineStateBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackResourceStates()
{
	return DX12FrameAnalysis::IsCapturing() ||
		DX12ShaderDumpIsBusy() ||
		DX12HuntNeedsHotPath();
}

bool DX12CommandListStateCapture::ShouldRecordBindingEvents()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackHuntIa()
{
	return DX12HuntIsEnabled();
}

bool DX12CommandListStateCapture::ShouldTrackPsoState()
{
	const bool shaderDumpActive = DX12ShaderDumpIsBusy();
	return DX12HuntIsEnabled() || DX12ModHasActiveShaderOverrides() ||
		DX12ModNeedsPreSkinningUavProbe() ||
		shaderDumpActive;
}

DX12CommandListTrackingState DX12CommandListStateCapture::MakeTrackingState()
{
	DX12CommandListTrackingState state;
	state.trackBindings = DX12CommandListStateCapture::ShouldTrackBindings();
	state.trackIaBindings = DX12CommandListStateCapture::ShouldTrackIaBindings();
	state.trackPipelineStateBindings = DX12CommandListStateCapture::ShouldTrackPipelineStateBindings();
	state.recordBindingEvents = DX12CommandListStateCapture::ShouldRecordBindingEvents();
	state.trackHuntIa = DX12CommandListStateCapture::ShouldTrackHuntIa();
	state.trackPsoState = DX12CommandListStateCapture::ShouldTrackPsoState();
	return state;
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	DX12SimulateContext *runtimeState)
{
	return DX12CommandListRuntime::GetTrackingState(runtimeState);
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::MakeTrackingState();
	return DX12CommandListRuntime::GetTrackingState(commandList);
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState)
{
	if (runtimeState)
		return DX12CommandListStateCapture::GetTrackingState(runtimeState);
	return DX12CommandListStateCapture::GetTrackingState(commandList);
}

void DX12CommandListStateCapture::PrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
}

void DX12CommandListStateCapture::PrimitiveTopology(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	D3D12_PRIMITIVE_TOPOLOGY topology)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackHuntIa) {
		if (runtimeState)
			DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
		else
			DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
	}
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetPrimitiveTopology(commandList, topology);
}

ID3D12PipelineState *DX12CommandListStateCapture::PipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	return DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
}

ID3D12PipelineState *DX12CommandListStateCapture::PipelineState(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12PipelineState *pipelineState)
{
	ID3D12PipelineState *activePipelineState = pipelineState;
	if (DX12ModHasActiveShaderOverrides()) {
		DX12_HOTPATH_TIME_BEGIN(PsoCacheLookupTotal);
		if (runtimeState && runtimeState->cachedReplacementPsoKey == pipelineState) {
			if (runtimeState->cachedReplacementPso)
				activePipelineState = runtimeState->cachedReplacementPso;
			DX12_HOTPATH_TIME_END(PsoCacheLookupTotal);
		} else {
			DX12_HOTPATH_TIME_END(PsoCacheLookupTotal);
			ID3D12PipelineState *replacement =
				DX12ModGetReplacementPipelineState(pipelineState);
			if (runtimeState) {
				runtimeState->cachedReplacementPsoKey = pipelineState;
				runtimeState->cachedReplacementPso = replacement;
			}
			if (replacement)
				activePipelineState = replacement;
		}
	}

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackPipelineStateBindings) {
		DX12BindingTracker::SetPipelineState(commandList, activePipelineState);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordStateEvent(commandList, "set_pso");
	}
	if (tracking.trackPsoState) {
		if (runtimeState)
			DX12CommandListRuntime::RememberPipelineState(runtimeState, activePipelineState);
		else
			DX12CommandListRuntime::RememberPipelineState(commandList, activePipelineState);
	}
	if (tracking.trackHuntIa)
		DX12HuntObservePipelineState(activePipelineState);
	return activePipelineState;
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::DescriptorHeaps(commandList, runtimeState, count, heaps);
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, count, heaps);
	if (changed && tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::DescriptorHeaps(
		commandList, runtimeState, cbvSrvUavHeap, samplerHeap);
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
	if (changed && tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::ComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootSignature(commandList, runtimeState, rootSignature);
}

void DX12CommandListStateCapture::ComputeRootSignature(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12RootSignature *rootSignature)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootSignature(commandList, rootSignature);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_signature");
}

void DX12CommandListStateCapture::GraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootSignature(commandList, runtimeState, rootSignature);
}

void DX12CommandListStateCapture::GraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12RootSignature *rootSignature)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootSignature(commandList, rootSignature);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_signature");
}

void DX12CommandListStateCapture::ComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootDescriptorTable(
		commandList, runtimeState, rootParameterIndex, baseDescriptor);
}

void DX12CommandListStateCapture::ComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_table");
}

void DX12CommandListStateCapture::GraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootDescriptorTable(
		commandList, runtimeState, rootParameterIndex, baseDescriptor);
}

void DX12CommandListStateCapture::GraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_table");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRoot32BitConstant(
		commandList, runtimeState, rootParameterIndex, srcData, destOffset);
}

void DX12CommandListStateCapture::ComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	UINT srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constant");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRoot32BitConstant(
		commandList, runtimeState, rootParameterIndex, srcData, destOffset);
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	UINT srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constant");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRoot32BitConstants(
		commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

void DX12CommandListStateCapture::ComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	UINT num32BitValuesToSet,
	const void *srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constants");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRoot32BitConstants(
		commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	UINT num32BitValuesToSet,
	const void *srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constants");
}

void DX12CommandListStateCapture::ComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootDescriptor(
		commandList, runtimeState, rootParameterIndex, type, address);
}

void DX12CommandListStateCapture::ComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootDescriptor(commandList, rootParameterIndex, type, address);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_compute_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_compute_root_srv" : "set_compute_root_uav");
}

void DX12CommandListStateCapture::GraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootDescriptor(
		commandList, runtimeState, rootParameterIndex, type, address);
}

void DX12CommandListStateCapture::GraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootDescriptor(commandList, rootParameterIndex, type, address);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_graphics_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_graphics_root_srv" : "set_graphics_root_uav");
}

void DX12CommandListStateCapture::IndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
}

void DX12CommandListStateCapture::IndexBuffer(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	const D3D12_INDEX_BUFFER_VIEW *view)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetIndexBuffer(commandList, view);
	if (DX12HotPathState::NeedsResidentIaState(DX12HotPathState::HookWorkFlags)) {
		if (runtimeState)
			DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
		else
			DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
	}
}

void DX12CommandListStateCapture::VertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::VertexBuffers(commandList, runtimeState, startSlot, count, views);
}

void DX12CommandListStateCapture::VertexBuffers(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT startSlot,
	UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetVertexBuffers(commandList, startSlot, count, views);
	if (DX12HotPathState::NeedsResidentIaState(DX12HotPathState::HookWorkFlags)) {
		if (runtimeState)
			DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
		else
			DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
	}
}
