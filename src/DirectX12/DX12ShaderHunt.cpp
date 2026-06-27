#include "DX12ShaderHunt.h"

#include <Windows.h>

#include <stdint.h>

#include <iterator>
#include <set>
#include <unordered_map>
#include <vector>

#include "DX12Overlay.h"
#include "DX12CommandListRuntime.h"
#include "DX12IaRegionHash.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12State.h"
#include "DXGIHooks.h"

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
static bool gHuntingEnabled = false;
static volatile LONG gHuntingActive = 0;
static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gCommandListPso;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntIaState> gCommandListIa;
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
		return DX12HashIndexBufferRegion(view, draw);
	}

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer.gpuVa;
	view.SizeInBytes = buffer.size;
	view.StrideInBytes = buffer.stride;
	return DX12HashVertexBufferRegion(buffer.slot, view, draw);
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
	size_t selected = 0;
	size_t count = 0;
	UINT64 hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
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
	DX12LogJsonFunc("DX12HuntSelect",
		"\"target\":\"%s\",\"selected\":%zu,\"count\":%zu,\"hash\":\"%016llx\"",
		target == HuntCopyTarget::VS ? "VS" :
		target == HuntCopyTarget::PS ? "PS" :
		target == HuntCopyTarget::CS ? "CS" : "",
		selected, count, static_cast<unsigned long long>(hash));
	DX12HuntRefreshOverlay();
}

static void AdvanceIa(IaSelection &selection, HuntCopyTarget target, int delta)
{
	size_t selected = 0;
	size_t count = 0;
	uint32_t hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
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
	DX12LogJsonFunc("DX12HuntSelect",
		"\"target\":\"%s\",\"selected\":%zu,\"count\":%zu,\"hash\":\"%08x\"",
		target == HuntCopyTarget::IB ? "IB" :
		target == HuntCopyTarget::VB ? "VB" : "",
		selected, count, hash);
	DX12HuntRefreshOverlay();
}

static ID3D12PipelineState *GetCurrentPipelineStateLocked(ID3D12GraphicsCommandList *commandList)
{
	auto it = gCommandListPso.find(commandList);
	if (it == gCommandListPso.end())
		return nullptr;
	return it->second;
}

static bool CopyIaStateLocked(ID3D12GraphicsCommandList *commandList, HuntIaState *state)
{
	if (!state)
		return false;
	auto it = gCommandListIa.find(commandList);
	if (it == gCommandListIa.end())
		return false;
	*state = it->second;
	return true;
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
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gCommandListPso[commandList] = pipelineState;
	gCommandListIa.erase(commandList);
	gCommandListVisits.erase(commandList);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntSetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gCommandListPso[commandList] = pipelineState;
	const bool enabled = gHuntingEnabled;
	if (enabled)
		gPipelineStateBinds++;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12PsoShaderInfo info = {};
	if (enabled && pipelineState && DX12GetPipelineStateShaderInfo(pipelineState, &info)) {
		AcquireSRWLockExclusive(&gHuntLock);
		if (gHuntingEnabled)
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

	DX12IaRegionDraw draw = {};
	draw.indexed = indexed;
	draw.vertexCount = vertexCount;
	draw.indexCount = indexCount;
	draw.firstVertex = firstVertex;
	draw.firstIndex = firstIndex;
	draw.baseVertex = baseVertex;

	ID3D12PipelineState *pipelineState = nullptr;
	HuntIaState iaState;
	bool hasIaState = false;
	AcquireSRWLockShared(&gHuntLock);
	pipelineState = GetCurrentPipelineStateLocked(commandList);
	hasIaState = CopyIaStateLocked(commandList, &iaState);
	ReleaseSRWLockShared(&gHuntLock);

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
	if (gHuntingEnabled) {
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
	if (gHuntingEnabled) {
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

	ID3D12PipelineState *pipelineState = nullptr;
	AcquireSRWLockShared(&gHuntLock);
	pipelineState = GetCurrentPipelineStateLocked(commandList);
	ReleaseSRWLockShared(&gHuntLock);
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (gHuntingEnabled) {
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

	ID3D12PipelineState *pipelineState = nullptr;
	AcquireSRWLockShared(&gHuntLock);
	pipelineState = GetCurrentPipelineStateLocked(commandList);
	ReleaseSRWLockShared(&gHuntLock);
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (gHuntingEnabled) {
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

void DX12HuntSetIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	HuntIaState &state = gCommandListIa[commandList];
	state.indexBuffer = HuntIaBuffer();
	if (view && view->BufferLocation && view->SizeInBytes) {
		state.indexBuffer = MakeIaView(
			"IB", 0, view->BufferLocation, view->SizeInBytes,
			0, static_cast<UINT>(view->Format));
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntSetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	HuntIaState &state = gCommandListIa[commandList];
	const UINT endSlot = startSlot + count;
	if (state.vertexBuffers.size() < endSlot)
		state.vertexBuffers.resize(endSlot);
	for (UINT i = 0; i < count; ++i) {
		const UINT slot = startSlot + i;
		HuntIaBuffer buffer;
		buffer.role = "VB";
		buffer.slot = slot;
		if (views && views[i].BufferLocation && views[i].SizeInBytes) {
			buffer = MakeIaView(
				"VB", slot, views[i].BufferLocation, views[i].SizeInBytes,
				views[i].StrideInBytes, 0);
		}
		state.vertexBuffers[slot] = buffer;
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
	DX12IaRegionDraw draw = {};
	draw.indexed = indexed;
	draw.vertexCount = vertexCount;
	draw.indexCount = indexCount;
	draw.firstVertex = firstVertex;
	draw.firstIndex = firstIndex;
	draw.baseVertex = baseVertex;

	bool enabled = false;
	bool vsArmed = false;
	bool psArmed = false;
	bool ibArmed = false;
	bool vbArmed = false;
	UINT64 selectedVS = 0;
	UINT64 selectedPS = 0;
	uint32_t selectedIB = 0;
	uint32_t selectedVB = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	HuntIaState iaState;
	bool hasIaState = false;
	AcquireSRWLockShared(&gHuntLock);
	enabled = gHuntingEnabled;
	if (enabled) {
		vsArmed = gVS.armed;
		psArmed = gPS.armed;
		ibArmed = gIB.armed;
		vbArmed = gVB.armed;
		selectedVS = SelectedHashLocked(gVS);
		selectedPS = SelectedHashLocked(gPS);
		selectedIB = SelectedIaHashLocked(gIB);
		selectedVB = SelectedIaHashLocked(gVB);
		pipelineState = GetCurrentPipelineStateLocked(commandList);
		hasIaState = CopyIaStateLocked(commandList, &iaState);
	}
	ReleaseSRWLockShared(&gHuntLock);
	if (!enabled)
		return false;

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
	ID3D12PipelineState *pipelineState = nullptr;
	AcquireSRWLockShared(&gHuntLock);
	enabled = gHuntingEnabled;
	if (enabled) {
		csArmed = gCS.armed;
		selectedCS = SelectedHashLocked(gCS);
		pipelineState = GetCurrentPipelineStateLocked(commandList);
	}
	ReleaseSRWLockShared(&gHuntLock);
	return enabled && csArmed && selectedCS &&
		PsoShouldSkip(pipelineState, true, 0, 0, selectedCS);
}

bool DX12HuntIsEnabled()
{
	return gHuntingActive != 0;
}

bool DX12HuntShouldDrawOverlay()
{
	return DX12HuntIsEnabled();
}

void DX12HuntToggle()
{
	std::unordered_map<ID3D12GraphicsCommandList*, HuntIaState> oldIa;
	IaSelection oldIB;
	IaSelection oldVB;

	AcquireSRWLockExclusive(&gHuntLock);
	gHuntingEnabled = !gHuntingEnabled;
	bool enabled = gHuntingEnabled;
	InterlockedExchange(&gHuntingActive, enabled ? 1 : 0);
	oldIa.swap(gCommandListIa);
	oldIB.visited.swap(gIB.visited);
	oldIB.lastSeenFrame.swap(gIB.lastSeenFrame);
	oldVB.visited.swap(gVB.visited);
	oldVB.lastSeenFrame.swap(gVB.lastSeenFrame);
	gIB.selected = 0;
	gIB.selectedHash = 0;
	gVB.selected = 0;
	gVB.selectedHash = 0;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12LogJsonFunc("DX12HuntToggle",
		"\"stage\":\"state\",\"enabled\":%s,\"oldIa\":%zu,\"oldIb\":%zu,\"oldVb\":%zu",
		enabled ? "true" : "false", oldIa.size(), oldIB.visited.size(), oldVB.visited.size());
	if (enabled) {
		DX12HuntRefreshOverlay();
		DX12LogJsonFunc("DX12HuntToggle", "\"stage\":\"refresh_overlay\",\"enabled\":true");
		DX12CommandListRuntimeBumpTrackingGeneration();
		DX12LogJsonFunc("DX12HuntToggle", "\"stage\":\"bump_generation\",\"enabled\":true");
		DX12ShowOverlayWindow();
		DX12LogJsonFunc("DX12HuntToggle", "\"stage\":\"show_overlay\",\"enabled\":true");
		DX12ActivateHotPathHooks();
		DX12LogJsonFunc("DX12HuntToggle", "\"stage\":\"activate_hooks\",\"enabled\":true");
	} else {
		DX12HideOverlayWindow();
		DX12LogJsonFunc("DX12HuntToggle", "\"stage\":\"hide_overlay\",\"enabled\":false");
	}
	DX12Log("DX12 hunting toggled %s\n", enabled ? "on" : "off");
}

void DX12HuntOnPresent()
{
	if (!DX12HuntIsEnabled())
		return;

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
		DX12LogJsonFunc("DX12HuntIaStats",
			"\"frame\":%llu,\"ib\":%zu,\"vb\":%zu",
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
		DX12SetOverlayStatus(L"DX12 hunting: no selected hash to copy");
		DX12HuntRefreshOverlay();
		return;
	}

	wchar_t status[128];
	if (CopyWideTextToClipboard(text))
		swprintf_s(status, L"Copied %ls hash %ls", CopyTargetName(target), text);
	else
		swprintf_s(status, L"Copy %ls hash failed", CopyTargetName(target));
	DX12SetOverlayStatus(status);
	DX12HuntRefreshOverlay();
}

void DX12HuntResetSelection()
{
	AcquireSRWLockExclusive(&gHuntLock);
	gVS.armed = false;
	gPS.armed = false;
	gCS.armed = false;
	gIB.armed = false;
	gVB.armed = false;
	gLastCopyTarget = HuntCopyTarget::None;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12SetOverlayStatus(L"DX12 hunting selection reset");
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
		gHuntingEnabled ? L"on" : L"off",
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

	DX12SetOverlayStatus(status);
}
