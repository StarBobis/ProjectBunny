#include "DX12ShaderHunt.hpp"

#include <Windows.h>

#include <stdint.h>

#include <iterator>
#include <set>
#include <unordered_map>
#include <vector>

#include "DX12Overlay.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12IaRegion.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12HotPathState.hpp"
#include "DX12LogManager.hpp"
#include "DX12OverlayStatusState.hpp"
#include "DX12RuntimeModeState.hpp"
#include "DXGIHooks.hpp"

enum class HuntStage
{
	VS,
	PS,
	CS
};

enum class HuntCopyTarget
{
	None,
	VS,
	PS,
	CS,
	IB,
	VB
};

struct StageSelection
{
	std::set<UINT64> visited;
	size_t selected = 0;
	bool armed = false;
};

struct IaSelection
{
	std::set<uint32_t> visited;
	std::unordered_map<uint32_t, UINT64> lastSeenFrame;
	size_t selected = 0;
	uint32_t selectedHash = 0;
	bool armed = false;
};

struct HuntIaBuffer
{
	const char *role = "";
	D3D12_GPU_VIRTUAL_ADDRESS gpuVa = 0;
	UINT size = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	bool resolved = false;
	ID3D12Resource *resource = nullptr;
	UINT64 resourceGpuVa = 0;
	UINT64 resourceBytes = 0;
	bool valid = false;
};

struct HuntIaState
{
	HuntIaBuffer indexBuffer;
	std::vector<HuntIaBuffer> vertexBuffers;
};

struct HuntCommandListVisits
{
	std::set<UINT64> vs;
	std::set<UINT64> ps;
	std::set<UINT64> cs;
	std::set<uint32_t> ib;
	std::set<uint32_t> vb;
	UINT64 draws = 0;
	UINT64 dispatches = 0;
	UINT64 indirects = 0;
};

static SRWLOCK gHuntLock = SRWLOCK_INIT;
static volatile LONG gHuntingActive = 0;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntCommandListVisits> gCommandListVisits;
static StageSelection gVS;
static StageSelection gPS;
static StageSelection gCS;
static IaSelection gIB;
static IaSelection gVB;
static HuntCopyTarget gLastCopyTarget = HuntCopyTarget::None;
static UINT64 gPipelineStateBinds = 0;
static UINT64 gDrawCalls = 0;
static UINT64 gDispatchCalls = 0;
static UINT64 gIndirectCalls = 0;
static UINT64 gPresentFrames = 0;
static UINT64 gLastOverlayStatsFrame = 0;
static constexpr UINT64 kOverlayBufferHashLifetime = 1;

static HuntIaBuffer MakeIaView(
	const char *role, UINT slot, UINT64 gpuVa, UINT size, UINT stride, UINT format)
{
	HuntIaBuffer buffer;
	buffer.role = role ? role : "";
	buffer.slot = slot;
	if (!gpuVa || !size)
		return buffer;

	buffer.gpuVa = gpuVa;
	buffer.size = size;
	buffer.stride = stride;
	buffer.format = format;
	buffer.valid = true;
	return buffer;
}
static UINT64 SelectedHashLocked(const StageSelection &stage)
{
	if (stage.visited.empty())
		return 0;

	size_t index = stage.selected % stage.visited.size();
	auto it = stage.visited.begin();
	std::advance(it, index);
	return *it;
}

static void AddVisitedLocked(StageSelection &stage, UINT64 hash)
{
	if (!hash)
		return;
	stage.visited.insert(hash);
	if (!stage.visited.empty())
		stage.selected %= stage.visited.size();
}

static uint32_t IaRegionHash(const HuntIaBuffer &buffer, const DX12IaRegionDraw &draw)
{
	if (!buffer.valid)
		return 0;

	if (buffer.format) {
		D3D12_INDEX_BUFFER_VIEW view = {};
		view.BufferLocation = buffer.gpuVa;
		view.SizeInBytes = buffer.size;
		view.Format = static_cast<DXGI_FORMAT>(buffer.format);
		return DX12IaRegion::HashIndexBufferView(view, draw);
	}

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer.gpuVa;
	view.SizeInBytes = buffer.size;
	view.StrideInBytes = buffer.stride;
	return DX12IaRegion::HashVertexBufferView(buffer.slot, view, draw);
}

static void SyncIaSelectionPositionLocked(IaSelection &selection)
{
	if (selection.visited.empty()) {
		selection.selected = 0;
		if (!selection.armed)
			selection.selectedHash = 0;
		return;
	}
	if (selection.selectedHash) {
		auto it = selection.visited.find(selection.selectedHash);
		if (it != selection.visited.end()) {
			selection.selected = static_cast<size_t>(
				std::distance(selection.visited.begin(), it));
			return;
		}
		if (selection.armed)
			return;
	}
	if (!selection.armed)
		return;
	selection.selected %= selection.visited.size();
	auto it = selection.visited.begin();
	std::advance(it, selection.selected);
	selection.selectedHash = *it;
}

static void AddVisitedLocked(IaSelection &selection, uint32_t hash)
{
	if (!hash)
		return;
	selection.visited.insert(hash);
	selection.lastSeenFrame[hash] = gPresentFrames;
	SyncIaSelectionPositionLocked(selection);
}

static void PurgeStaleIaLocked(IaSelection &selection)
{
	for (auto it = selection.lastSeenFrame.begin(); it != selection.lastSeenFrame.end(); ) {
		const uint32_t hash = it->first;
		const UINT64 frame = it->second;
		if (gPresentFrames - frame > kOverlayBufferHashLifetime) {
			selection.visited.erase(hash);
			it = selection.lastSeenFrame.erase(it);
		} else {
			++it;
		}
	}
	if (selection.visited.empty()) {
		selection.selected = 0;
		return;
	}
	SyncIaSelectionPositionLocked(selection);
}

static void AddPsoVisitedLocked(const DX12PsoShaderInfo &info, bool draw, bool dispatch)
{
	if (draw) {
		AddVisitedLocked(gVS, info.hasVS ? info.vs : 0);
		AddVisitedLocked(gPS, info.hasPS ? info.ps : 0);
	}
	if (dispatch)
		AddVisitedLocked(gCS, info.hasCS ? info.cs : 0);
}

static size_t DisplayIndexLocked(const StageSelection &stage)
{
	return stage.armed && !stage.visited.empty() ? stage.selected + 1 : 0;
}

static size_t DisplayIndexLocked(const IaSelection &selection)
{
	if (!selection.armed || selection.visited.empty() || !selection.selectedHash)
		return 0;
	auto it = selection.visited.find(selection.selectedHash);
	if (it == selection.visited.end())
		return 0;
	return static_cast<size_t>(std::distance(selection.visited.begin(), it)) + 1;
}

static uint32_t SelectedIaHashLocked(const IaSelection &selection)
{
	return selection.selectedHash;
}

static void AdvanceStage(StageSelection &stage, HuntCopyTarget target, int delta)
{
	if (!DX12HuntIsEnabled())
		return;

	size_t selected = 0;
	size_t count = 0;
	UINT64 hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	stage.armed = true;
	gLastCopyTarget = target;
	if (!stage.visited.empty()) {
		const size_t size = stage.visited.size();
		if (delta < 0)
			stage.selected = (stage.selected + size - 1) % size;
		else
			stage.selected = (stage.selected + 1) % size;
	}
	selected = DisplayIndexLocked(stage);
	count = stage.visited.size();
	hash = SelectedHashLocked(stage);
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12LogManager::JsonFunc("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%016llx",
		target == HuntCopyTarget::VS ? "VS" :
		target == HuntCopyTarget::PS ? "PS" :
		target == HuntCopyTarget::CS ? "CS" : "",
		selected, count, static_cast<unsigned long long>(hash));
	DX12HuntRefreshOverlay();
}

static void AdvanceIa(IaSelection &selection, HuntCopyTarget target, int delta)
{
	if (!DX12HuntIsEnabled())
		return;

	size_t selected = 0;
	size_t count = 0;
	uint32_t hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	selection.armed = true;
	gLastCopyTarget = target;
	if (!selection.visited.empty()) {
		auto it = selection.selectedHash ?
			selection.visited.find(selection.selectedHash) : selection.visited.end();
		if (!selection.selectedHash) {
			it = delta < 0 ? std::prev(selection.visited.end()) : selection.visited.begin();
		} else if (it == selection.visited.end()) {
			it = selection.visited.lower_bound(selection.selectedHash);
			if (delta < 0) {
				if (it == selection.visited.begin())
					it = std::prev(selection.visited.end());
				else
					--it;
			} else if (it == selection.visited.end()) {
				it = selection.visited.begin();
			}
		} else if (delta < 0) {
			if (it == selection.visited.begin())
				it = std::prev(selection.visited.end());
			else
				--it;
		} else {
			++it;
			if (it == selection.visited.end())
				it = selection.visited.begin();
		}
		selection.selectedHash = *it;
		selection.selected = static_cast<size_t>(std::distance(selection.visited.begin(), it));
	}
	selected = DisplayIndexLocked(selection);
	count = selection.visited.size();
	hash = SelectedIaHashLocked(selection);
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12LogManager::JsonFunc("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%08x",
		target == HuntCopyTarget::IB ? "IB" :
		target == HuntCopyTarget::VB ? "VB" : "",
		selected, count, hash);
	DX12HuntRefreshOverlay();
}

static HuntIaState BuildHuntIaState(const DX12ActiveIaState &ia)
{
	HuntIaState state;
	if (ia.hasIndexBuffer && ia.indexBuffer.BufferLocation && ia.indexBuffer.SizeInBytes) {
		state.indexBuffer = MakeIaView(
			"IB", 0, ia.indexBuffer.BufferLocation, ia.indexBuffer.SizeInBytes,
			0, static_cast<UINT>(ia.indexBuffer.Format));
	}
	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
		HuntIaBuffer buffer;
		buffer.role = "VB";
		buffer.slot = slot;
		if (view.BufferLocation && view.SizeInBytes) {
			buffer = MakeIaView(
				"VB", slot, view.BufferLocation, view.SizeInBytes,
				view.StrideInBytes, 0);
		}
		if (state.vertexBuffers.size() <= slot)
			state.vertexBuffers.resize(slot + 1);
		state.vertexBuffers[slot] = buffer;
	}
	return state;
}

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
		DX12LogManager::JsonFunc("DX12ShaderHunt",
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

	const DX12CommandListRuntimeState *runtimeState =
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

	const DX12CommandListRuntimeState *runtimeState =
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

	const DX12CommandListRuntimeState *runtimeState =
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

	const DX12CommandListRuntimeState *runtimeState =
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
	const DX12CommandListRuntimeState *runtimeState =
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

	DX12LogManager::JsonFunc("DX12HuntToggle",
		"stage=state,enabled=%s,oldIa=%zu,oldIb=%zu,oldVb=%zu",
		enabled ? "true" : "false", oldIa, oldIB, oldVB);
	DX12HotPathState::Update();
	DX12LogManager::JsonFunc("DX12HuntToggle",
		"stage=hotpath_update,enabled=%s",
		enabled ? "true" : "false");
	if (enabled) {
		DX12PrimeDescriptorHistoryForHunting();
		DX12LogManager::JsonFunc("DX12HuntToggle", "stage=prime_descriptor_history,enabled=true");
		DX12HuntRefreshOverlay();
		DX12LogManager::JsonFunc("DX12HuntToggle", "stage=refresh_overlay,enabled=true");
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12LogManager::JsonFunc("DX12HuntToggle", "stage=bump_generation,enabled=true");
		DX12Overlay::Show();
		DX12LogManager::JsonFunc("DX12HuntToggle", "stage=show_overlay,enabled=true");
	} else {
		DX12Overlay::Hide();
		DX12LogManager::JsonFunc("DX12HuntToggle", "stage=hide_overlay,enabled=false");
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
		DX12LogManager::JsonFunc("DX12HuntIaStats",
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

static void FormatStageItem(
	DX12HuntListItem *item, size_t displayIndex, UINT64 hash, const StageSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %016llx", displayIndex,
		static_cast<unsigned long long>(hash));
	item->hash = hash;
	item->selected = selection.armed && SelectedHashLocked(selection) == hash;
}

static void FormatIaItem(
	DX12HuntListItem *item, size_t displayIndex, uint32_t hash, const IaSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %08x", displayIndex, hash);
	item->hash = hash;
	item->selected = selection.armed && SelectedIaHashLocked(selection) == hash;
}

size_t DX12HuntGetListItems(
	DX12HuntListCategory category, DX12HuntListItem *items, size_t itemCount)
{
	size_t total = 0;
	AcquireSRWLockShared(&gHuntLock);
	switch (category) {
	case DX12_HUNT_LIST_VS:
		total = gVS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gVS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gVS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_PS:
		total = gPS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gPS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gPS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_CS:
		total = gCS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gCS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gCS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_IB:
		total = gIB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gIB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gIB);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_VB:
		total = gVB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gVB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gVB);
				++index;
			}
		}
		break;
	default:
		break;
	}
	ReleaseSRWLockShared(&gHuntLock);
	return total;
}

template <typename Selection>
static bool SelectByIndexLocked(Selection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	auto it = selection.visited.begin();
	std::advance(it, index);
	selection.selectedHash = *it;
	return true;
}

template <>
bool SelectByIndexLocked<StageSelection>(StageSelection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	return true;
}

void DX12HuntSelectListItem(DX12HuntListCategory category, size_t index)
{
	if (!DX12HuntIsEnabled())
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			selected = SelectByIndexLocked(gVS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_PS:
			selected = SelectByIndexLocked(gPS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::PS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_CS:
			selected = SelectByIndexLocked(gCS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::CS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_IB:
			selected = SelectByIndexLocked(gIB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::IB : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_VB:
			selected = SelectByIndexLocked(gVB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VB : gLastCopyTarget;
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}

void DX12HuntSelectListHash(DX12HuntListCategory category, UINT64 hash)
{
	if (!DX12HuntIsEnabled() || !hash)
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			if (gVS.visited.find(hash) != gVS.visited.end()) {
				gVS.armed = true;
				gVS.selected = static_cast<size_t>(std::distance(gVS.visited.begin(), gVS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::VS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_PS:
			if (gPS.visited.find(hash) != gPS.visited.end()) {
				gPS.armed = true;
				gPS.selected = static_cast<size_t>(std::distance(gPS.visited.begin(), gPS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::PS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_CS:
			if (gCS.visited.find(hash) != gCS.visited.end()) {
				gCS.armed = true;
				gCS.selected = static_cast<size_t>(std::distance(gCS.visited.begin(), gCS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::CS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_IB:
			if (hash <= UINT32_MAX && gIB.visited.find(static_cast<uint32_t>(hash)) != gIB.visited.end()) {
				gIB.armed = true;
				gIB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gIB);
				gLastCopyTarget = HuntCopyTarget::IB;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_VB:
			if (hash <= UINT32_MAX && gVB.visited.find(static_cast<uint32_t>(hash)) != gVB.visited.end()) {
				gVB.armed = true;
				gVB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gVB);
				gLastCopyTarget = HuntCopyTarget::VB;
				selected = true;
			}
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}



