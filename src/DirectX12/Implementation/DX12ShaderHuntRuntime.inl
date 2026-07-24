static bool PsoShouldSkip(ID3D12PipelineState *pipelineState, bool dispatch, UINT64 vs, UINT64 ps, UINT64 cs)
{
	DX12PsoShaderInfo info = {};
	if (!pipelineState || !DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return false;

	if (dispatch)
		return cs && info.hasCS && info.cs == cs;

	return (vs && info.hasVS && info.vs == vs) ||
		(ps && info.hasPS && info.ps == ps);
}

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	(void)pipelineState;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	gCommandListVisits.erase(commandList);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	bool removed = false;
	AcquireSRWLockExclusive(&gHuntLock);
	auto it = gCommandListVisits.find(commandList);
	if (it != gCommandListVisits.end()) {
		gCommandListVisits.erase(it);
		removed = true;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (removed) {
		DX12LogManager::Fields("DX12ShaderHunt",
			"stage=unregister_command_list,commandList=%p",
			commandList);
	}
}

void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState)
{
	if (!DX12HuntIsEnabled())
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	gPipelineStateBinds++;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12PsoShaderInfo info = {};
	if (DX12HuntIsEnabled() && pipelineState && DX12GetPipelineStateShaderInfo(pipelineState, &info)) {
		AcquireSRWLockExclusive(&gHuntLock);
		if (DX12HuntIsEnabled())
			AddPsoVisitedLocked(info, true, true);
		ReleaseSRWLockExclusive(&gHuntLock);
	}
}

void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	const HuntIaState iaState = runtimeState ?
		BuildHuntIaState(runtimeState->ia) : HuntIaState();
	const bool hasIaState = runtimeState != nullptr;

	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);
	uint32_t ibHash = 0;
	std::vector<uint32_t> vbHashes;
	if (hasIaState) {
		if (indexed && iaState.indexBuffer.valid)
			ibHash = IaRegionHash(iaState.indexBuffer, draw);
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			uint32_t hash = IaRegionHash(buffer, draw);
			if (hash)
				vbHashes.push_back(hash);
		}
	}

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.draws++;
		if (hasInfo) {
			if (info.hasVS)
				visits.vs.insert(info.vs);
			if (info.hasPS)
				visits.ps.insert(info.ps);
		}
		if (ibHash)
			visits.ib.insert(ibHash);
		for (uint32_t hash : vbHashes)
			visits.vb.insert(hash);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	HuntCommandListVisits visits;
	AcquireSRWLockExclusive(&gHuntLock);
	auto it = gCommandListVisits.find(commandList);
	if (it != gCommandListVisits.end())
		visits = it->second;
	if (DX12HuntIsEnabled()) {
		gDrawCalls += visits.draws;
		gDispatchCalls += visits.dispatches;
		gIndirectCalls += visits.indirects;
		for (UINT64 hash : visits.vs)
			AddVisitedLocked(gVS, hash);
		for (UINT64 hash : visits.ps)
			AddVisitedLocked(gPS, hash);
		for (UINT64 hash : visits.cs)
			AddVisitedLocked(gCS, hash);
		for (uint32_t hash : visits.ib)
			AddVisitedLocked(gIB, hash);
		for (uint32_t hash : visits.vb)
			AddVisitedLocked(gVB, hash);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.dispatches++;
		if (hasInfo && info.hasCS)
			visits.cs.insert(info.cs);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.indirects++;
		if (hasInfo) {
			if (info.hasVS)
				visits.vs.insert(info.vs);
			if (info.hasPS)
				visits.ps.insert(info.ps);
			if (info.hasCS)
				visits.cs.insert(info.cs);
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!commandList)
		return false;
	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	bool enabled = false;
	bool vsArmed = false;
	bool psArmed = false;
	bool ibArmed = false;
	bool vbArmed = false;
	UINT64 selectedVS = 0;
	UINT64 selectedPS = 0;
	uint32_t selectedIB = 0;
	uint32_t selectedVB = 0;
	AcquireSRWLockShared(&gHuntLock);
	enabled = DX12HuntIsEnabled();
	if (enabled) {
		vsArmed = gVS.armed;
		psArmed = gPS.armed;
		ibArmed = gIB.armed;
		vbArmed = gVB.armed;
		selectedVS = SelectedHashLocked(gVS);
		selectedPS = SelectedHashLocked(gPS);
		selectedIB = SelectedIaHashLocked(gIB);
		selectedVB = SelectedIaHashLocked(gVB);
	}
	ReleaseSRWLockShared(&gHuntLock);
	if (!enabled)
		return false;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	const HuntIaState iaState = runtimeState ?
		BuildHuntIaState(runtimeState->ia) : HuntIaState();
	const bool hasIaState = runtimeState != nullptr;
	if ((vsArmed || psArmed) && PsoShouldSkip(
			pipelineState, false,
			vsArmed ? selectedVS : 0,
			psArmed ? selectedPS : 0,
			0))
		return true;

	if (!hasIaState)
		return false;
	if (indexed && ibArmed && selectedIB && iaState.indexBuffer.valid &&
	    IaRegionHash(iaState.indexBuffer, draw) == selectedIB)
		return true;
	if (vbArmed && selectedVB) {
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			if (IaRegionHash(buffer, draw) == selectedVB)
				return true;
		}
	}
	return false;
}

bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	bool enabled = false;
	bool csArmed = false;
	UINT64 selectedCS = 0;
	AcquireSRWLockShared(&gHuntLock);
	enabled = DX12HuntIsEnabled();
	if (enabled) {
		csArmed = gCS.armed;
		selectedCS = SelectedHashLocked(gCS);
	}
	ReleaseSRWLockShared(&gHuntLock);
	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	return enabled && csArmed && selectedCS &&
		PsoShouldSkip(pipelineState, true, 0, 0, selectedCS);
}

bool DX12HuntIsEnabled()
{
	return gHuntingActive != 0;
}

bool DX12HuntNeedsHotPath()
{
	return InterlockedCompareExchange(&gHuntingActive, 0, 0) != 0;
}

bool DX12HuntShouldDrawOverlay()
{
	return DX12HuntIsEnabled();
}

void DX12HuntToggle()
{
	size_t oldIa = 0;
	size_t oldIB = 0;
	size_t oldVB = 0;

	const bool enabled = InterlockedCompareExchange(&gHuntingActive, 0, 0) == 0;

	if (enabled) {
		AcquireSRWLockExclusive(&gHuntLock);
		oldIa = gCommandListVisits.size();
		oldIB = gIB.visited.size();
		oldVB = gVB.visited.size();
		gCommandListVisits.clear();
		gVS = StageSelection();
		gPS = StageSelection();
		gCS = StageSelection();
		gIB = IaSelection();
		gVB = IaSelection();
		gLastCopyTarget = HuntCopyTarget::None;
		gPipelineStateBinds = 0;
		gDrawCalls = 0;
		gDispatchCalls = 0;
		gIndirectCalls = 0;
		gLastOverlayStatsFrame = gPresentFrames;
		ReleaseSRWLockExclusive(&gHuntLock);
		InterlockedExchange(&gHuntingActive, 1);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Hunting);
	} else {
		InterlockedExchange(&gHuntingActive, 0);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Mod);
	}

	DX12LogManager::Fields("DX12HuntToggle",
		"stage=state,enabled=%s,oldIa=%zu,oldIb=%zu,oldVb=%zu",
		enabled ? "true" : "false", oldIa, oldIB, oldVB);
	DX12HotPathState::Update();
	DX12LogManager::Fields("DX12HuntToggle",
		"stage=hotpath_update,enabled=%s",
		enabled ? "true" : "false");
	if (enabled) {
		DX12PrimeDescriptorHistoryForHunting();
		DX12LogManager::Fields("DX12HuntToggle", "stage=prime_descriptor_history,enabled=true");
		DX12HuntRefreshOverlay();
		DX12LogManager::Fields("DX12HuntToggle", "stage=refresh_overlay,enabled=true");
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12LogManager::Fields("DX12HuntToggle", "stage=bump_generation,enabled=true");
		DX12Overlay::Show();
		DX12LogManager::Fields("DX12HuntToggle", "stage=show_overlay,enabled=true");
	} else {
		DX12Overlay::Hide();
		DX12LogManager::Fields("DX12HuntToggle", "stage=hide_overlay,enabled=false");
	}
	DX12LogManager::Text("DX12 hunting toggled %s\n", enabled ? "on" : "off");
}

void DX12HuntOnPresent()
{
	if (!DX12HuntIsEnabled()) {
		return;
	}

	size_t ibCount = 0;
	size_t vbCount = 0;
	bool shouldLog = false;
	AcquireSRWLockExclusive(&gHuntLock);
	gPresentFrames++;
	PurgeStaleIaLocked(gIB);
	PurgeStaleIaLocked(gVB);
	ibCount = gIB.visited.size();
	vbCount = gVB.visited.size();
	if (gPresentFrames - gLastOverlayStatsFrame >= 120) {
		gLastOverlayStatsFrame = gPresentFrames;
		shouldLog = true;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (shouldLog)
		DX12LogManager::Fields("DX12HuntIaStats",
			"frame=%llu,ib=%zu,vb=%zu",
			static_cast<unsigned long long>(gPresentFrames), ibCount, vbCount);
}

void DX12HuntPreviousVS() { AdvanceStage(gVS, HuntCopyTarget::VS, -1); }
void DX12HuntNextVS() { AdvanceStage(gVS, HuntCopyTarget::VS, 1); }
void DX12HuntPreviousPS() { AdvanceStage(gPS, HuntCopyTarget::PS, -1); }
void DX12HuntNextPS() { AdvanceStage(gPS, HuntCopyTarget::PS, 1); }
void DX12HuntPreviousCS() { AdvanceStage(gCS, HuntCopyTarget::CS, -1); }
void DX12HuntNextCS() { AdvanceStage(gCS, HuntCopyTarget::CS, 1); }
void DX12HuntPreviousIB() { AdvanceIa(gIB, HuntCopyTarget::IB, -1); }
void DX12HuntNextIB() { AdvanceIa(gIB, HuntCopyTarget::IB, 1); }
void DX12HuntPreviousVB() { AdvanceIa(gVB, HuntCopyTarget::VB, -1); }
void DX12HuntNextVB() { AdvanceIa(gVB, HuntCopyTarget::VB, 1); }

static bool CopyWideTextToClipboard(const wchar_t *text)
{
	if (!text || !text[0])
		return false;
	if (!OpenClipboard(nullptr))
		return false;
	EmptyClipboard();
	const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!memory) {
		CloseClipboard();
		return false;
	}
	void *locked = GlobalLock(memory);
	if (!locked) {
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}
	memcpy(locked, text, bytes);
	GlobalUnlock(memory);
	if (!SetClipboardData(CF_UNICODETEXT, memory)) {
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}
	CloseClipboard();
	return true;
}

static const wchar_t *CopyTargetName(HuntCopyTarget target)
{
	switch (target) {
	case HuntCopyTarget::VS:
		return L"VS";
	case HuntCopyTarget::PS:
		return L"PS";
	case HuntCopyTarget::CS:
		return L"CS";
	case HuntCopyTarget::IB:
		return L"IB";
	case HuntCopyTarget::VB:
		return L"VB";
	default:
		return L"";
	}
}

void DX12HuntCopySelectedHash()
{
	if (!DX12HuntIsEnabled())
		return;

	wchar_t text[32] = {};
	HuntCopyTarget target = HuntCopyTarget::None;

	AcquireSRWLockShared(&gHuntLock);
	target = gLastCopyTarget;
	switch (target) {
	case HuntCopyTarget::VS:
		if (gVS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gVS)));
		break;
	case HuntCopyTarget::PS:
		if (gPS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gPS)));
		break;
	case HuntCopyTarget::CS:
		if (gCS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gCS)));
		break;
	case HuntCopyTarget::IB:
		if (gIB.armed)
			swprintf_s(text, L"%08x", SelectedIaHashLocked(gIB));
		break;
	case HuntCopyTarget::VB:
		if (gVB.armed)
			swprintf_s(text, L"%08x", SelectedIaHashLocked(gVB));
		break;
	default:
		break;
	}
	ReleaseSRWLockShared(&gHuntLock);

	if (!text[0] || wcspbrk(text, L"123456789abcdefABCDEF") == nullptr) {
		DX12OverlayStatusState::SetStatus(L"DX12 hunting: no selected hash to copy");
		DX12HuntRefreshOverlay();
		return;
	}

	wchar_t status[128];
	if (CopyWideTextToClipboard(text))
		swprintf_s(status, L"Copied %ls hash %ls", CopyTargetName(target), text);
	else
		swprintf_s(status, L"Copy %ls hash failed", CopyTargetName(target));
	DX12OverlayStatusState::SetStatus(status);
	DX12HuntRefreshOverlay();
}

void DX12HuntResetSelection()
{
	if (!DX12HuntIsEnabled())
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gVS.armed = false;
	gPS.armed = false;
	gCS.armed = false;
	gIB.armed = false;
	gVB.armed = false;
	gLastCopyTarget = HuntCopyTarget::None;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12OverlayStatusState::SetStatus(L"DX12 hunting selection reset");
	DX12HuntRefreshOverlay();
}

void DX12HuntRefreshOverlay()
{
	wchar_t status[1024];

	AcquireSRWLockShared(&gHuntLock);
	const UINT64 vs = SelectedHashLocked(gVS);
	const UINT64 ps = SelectedHashLocked(gPS);
	const UINT64 cs = SelectedHashLocked(gCS);
	const uint32_t ib = SelectedIaHashLocked(gIB);
	const uint32_t vb = SelectedIaHashLocked(gVB);
	swprintf_s(status,
		L"DX12 Hunting: %ls\n"
		L"VS %ls %zu/%zu %016llx   PS %ls %zu/%zu %016llx   CS %ls %zu/%zu %016llx\n"
		L"IB %ls %zu/%zu %08x   VB %ls %zu/%zu %08x\n"
		L"PSO binds %llu   Draw %llu Dispatch %llu Indirect %llu\n"
		L"Num0 toggle  Num+ reset  Num9 copy  4/5 VS  1/2 PS  Dec+1/2 CS  7/8 IB  Dec+7/8 VB",
		DX12HuntIsEnabled() ? L"on" : L"off",
		gVS.armed ? L"*" : L" ",
		DisplayIndexLocked(gVS), gVS.visited.size(),
		static_cast<unsigned long long>(gVS.armed ? vs : 0),
		gPS.armed ? L"*" : L" ",
		DisplayIndexLocked(gPS), gPS.visited.size(),
		static_cast<unsigned long long>(gPS.armed ? ps : 0),
		gCS.armed ? L"*" : L" ",
		DisplayIndexLocked(gCS), gCS.visited.size(),
		static_cast<unsigned long long>(gCS.armed ? cs : 0),
		gIB.armed ? L"*" : L" ",
		DisplayIndexLocked(gIB), gIB.visited.size(),
		gIB.armed ? ib : 0,
		gVB.armed ? L"*" : L" ",
		DisplayIndexLocked(gVB), gVB.visited.size(),
		gVB.armed ? vb : 0,
		static_cast<unsigned long long>(gPipelineStateBinds),
		static_cast<unsigned long long>(gDrawCalls),
		static_cast<unsigned long long>(gDispatchCalls),
		static_cast<unsigned long long>(gIndirectCalls));
	ReleaseSRWLockShared(&gHuntLock);

	DX12OverlayStatusState::SetStatus(status);
}
