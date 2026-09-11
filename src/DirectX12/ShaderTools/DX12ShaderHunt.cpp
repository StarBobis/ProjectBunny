// DX12ShaderHunt: hunt state, stage/IA navigation and skip logic.
// Split from DX12ShaderTools.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>



enum class HuntStage
{
	VS,
	PS,
	CS
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
};

static SRWLOCK gHuntLock = SRWLOCK_INIT;
static volatile LONG gHuntingActive = 0;
// Sharded per-list visit accumulators. DX11 assumed single-threaded
// recording, so one global map + one global lock worked; under DX12 every
// recording thread's every draw queued on the single gHuntLock. Visits are
// keyed by command list and each access targets exactly one list, so shard
// the map by list pointer: record paths only take their shard's lock,
// cutting cross-thread contention to same-shard collisions (1/64).
static constexpr size_t kHuntVisitsShardCount = 64;
struct HuntVisitsShard
{
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, HuntCommandListVisits> entries;
};
static HuntVisitsShard gHuntVisitsShards[kHuntVisitsShardCount];

static HuntVisitsShard &HuntVisitsShardFor(ID3D12GraphicsCommandList *commandList)
{
	const UINT_PTR key = reinterpret_cast<UINT_PTR>(commandList);
	return gHuntVisitsShards[(key >> 4) % kHuntVisitsShardCount];
}

static StageSelection gVS;
static StageSelection gPS;
static StageSelection gCS;
static IaSelection gIB;
static IaSelection gVB;
static UINT64 gPresentFrames = 0;
static UINT64 gLastOverlayStatsFrame = 0;

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
	DX12_PROF_COUNT("Rm.HuntVisitedWrite");
	if (!stage.visited.empty())
		stage.selected %= stage.visited.size();
}

// DX11 parity: the visited IB/VB identity is the bound view, never the
// per-draw region. One mesh drawn with varying arguments (baseVertex, index
// ranges) collapses into a single stable entry instead of multiplying, which
// is why DX11 shows a handful of buffers per scene. The value is the same
// view-level hash mods match first (see DX12TextureOverrideMatching), so a
// copied hash is directly usable as an ib=/vb= key.
static uint32_t IaViewHash(const HuntIaBuffer &buffer)
{
	if (!buffer.valid)
		return 0;
	return DX12HashIaBufferContentView(
		buffer.gpuVa, buffer.size, buffer.stride, buffer.format, buffer.slot);
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
	// DX11 default (overlay_buffer_hash_lifetime=-1): visited buffers are
	// never purged while hunting stays on. An armed selection skips its own
	// draws, so a purged entry would vanish exactly when selected - the
	// skip/vanish/reappear feedback loop that made positions jump. Positions
	// only shift when a genuinely new hash inserts; display recomputes the
	// index from selectedHash, so nothing churns per frame.
	const bool inserted = selection.visited.insert(hash).second;
	DX12_PROF_COUNT("Rm.HuntVisitedWrite");
	if (inserted)
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

static void AdvanceStage(StageSelection &stage, const char *stageName, int delta)
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
	DX12_LOG("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%016llx",
		stageName, selected, count, static_cast<unsigned long long>(hash));
}

static void AdvanceIa(IaSelection &selection, const char *stageName, int delta)
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
	DX12_LOG("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%08x",
		stageName, selected, count, hash);
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
	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	shard.entries.erase(commandList);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	bool removed = false;
	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.entries.find(commandList);
	if (it != shard.entries.end()) {
		shard.entries.erase(it);
		removed = true;
	}
	ReleaseSRWLockExclusive(&shard.lock);

	if (removed) {
		DX12_LOG("DX12ShaderHunt",
			"stage=unregister_command_list,commandList=%p",
			commandList);
	}
}

void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState)
{
	if (!DX12HuntIsEnabled())
		return;

	// Resolve the shader info lock-free first (it does not touch hunt
	// state), then take gHuntLock once for both the counter and the
	// visited-mark instead of the old two exclusive acquisitions per bind.
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	if (hasInfo)
		AddPsoVisitedLocked(info, true, true);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

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
			ibHash = IaViewHash(iaState.indexBuffer);
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			uint32_t hash = IaViewHash(buffer);
			if (hash)
				vbHashes.push_back(hash);
		}
	}

	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = shard.entries[commandList];
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
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	// Read this list's accumulated visits under its shard lock, then merge
	// into the global selections under gHuntLock. The two locks are never
	// held together (shard first, release, then global), so there is no
	// lock-order inversion against the record paths or the toggle clear.
	HuntCommandListVisits visits;
	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.entries.find(commandList);
	if (it != shard.entries.end())
		visits = it->second;
	ReleaseSRWLockShared(&shard.lock);
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
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

	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = shard.entries[commandList];
		if (hasInfo && info.hasCS)
			visits.cs.insert(info.cs);
	}
	ReleaseSRWLockExclusive(&shard.lock);
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

	HuntVisitsShard &shard = HuntVisitsShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = shard.entries[commandList];
		if (hasInfo) {
			if (info.hasVS)
				visits.vs.insert(info.vs);
			if (info.hasPS)
				visits.ps.insert(info.ps);
			if (info.hasCS)
				visits.cs.insert(info.cs);
		}
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!commandList)
		return false;

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
	    IaViewHash(iaState.indexBuffer) == selectedIB)
		return true;
	if (vbArmed && selectedVB) {
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			if (IaViewHash(buffer) == selectedVB)
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
	return gHuntingActive != 0;
}

void DX12HuntToggle()
{
	size_t oldIa = 0;
	size_t oldIB = 0;
	size_t oldVB = 0;

	const bool enabled = InterlockedCompareExchange(&gHuntingActive, 0, 0) == 0;

	if (enabled) {
		for (HuntVisitsShard &visitsShard : gHuntVisitsShards) {
			AcquireSRWLockExclusive(&visitsShard.lock);
			oldIa += visitsShard.entries.size();
			visitsShard.entries.clear();
			ReleaseSRWLockExclusive(&visitsShard.lock);
		}
		AcquireSRWLockExclusive(&gHuntLock);
		oldIB = gIB.visited.size();
		oldVB = gVB.visited.size();
		gVS = StageSelection();
		gPS = StageSelection();
		gCS = StageSelection();
		gIB = IaSelection();
		gVB = IaSelection();
		gLastOverlayStatsFrame = gPresentFrames;
		ReleaseSRWLockExclusive(&gHuntLock);
		InterlockedExchange(&gHuntingActive, 1);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Hunting);
	} else {
		InterlockedExchange(&gHuntingActive, 0);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Mod);
	}

	DX12_LOG("DX12HuntToggle",
		"stage=state,enabled=%s,oldIa=%zu,oldIb=%zu,oldVb=%zu",
		enabled ? "true" : "false", oldIa, oldIB, oldVB);
	DX12HotPathState::Update();
	DX12_LOG("DX12HuntToggle",
		"stage=hotpath_update,enabled=%s",
		enabled ? "true" : "false");
	if (enabled) {
		DX12PrimeDescriptorHistoryForHunting();
		DX12_LOG("DX12HuntToggle", "stage=prime_descriptor_history,enabled=true");
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12_LOG("DX12HuntToggle", "stage=bump_generation,enabled=true");
	}
	DX12_LOG_TEXT("DX12 hunting toggled %s\n", enabled ? "on" : "off");
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
	ibCount = gIB.visited.size();
	vbCount = gVB.visited.size();
	if (gPresentFrames - gLastOverlayStatsFrame >= 120) {
		gLastOverlayStatsFrame = gPresentFrames;
		shouldLog = true;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (shouldLog)
		DX12_LOG("DX12HuntIaStats",
			"frame=%llu,ib=%zu,vb=%zu",
			static_cast<unsigned long long>(gPresentFrames), ibCount, vbCount);
}

void DX12HuntPreviousVS() { AdvanceStage(gVS, "VS", -1); }
void DX12HuntNextVS() { AdvanceStage(gVS, "VS", 1); }
void DX12HuntPreviousPS() { AdvanceStage(gPS, "PS", -1); }
void DX12HuntNextPS() { AdvanceStage(gPS, "PS", 1); }
void DX12HuntPreviousCS() { AdvanceStage(gCS, "CS", -1); }
void DX12HuntNextCS() { AdvanceStage(gCS, "CS", 1); }
void DX12HuntPreviousIB() { AdvanceIa(gIB, "IB", -1); }
void DX12HuntNextIB() { AdvanceIa(gIB, "IB", 1); }
void DX12HuntPreviousVB() { AdvanceIa(gVB, "VB", -1); }
void DX12HuntNextVB() { AdvanceIa(gVB, "VB", 1); }

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

void DX12HuntCopySelectedHash(HuntCopyTarget target)
{
	if (!DX12HuntIsEnabled())
		return;

	wchar_t text[32] = {};

	AcquireSRWLockShared(&gHuntLock);
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
		DX12OverlayStatusState::SetWarning(L"DX12 hunting: no selected hash to copy");
		return;
	}

	wchar_t status[128];
	if (CopyWideTextToClipboard(text))
		swprintf_s(status, L"Copied %ls hash %ls", CopyTargetName(target), text);
	else
		swprintf_s(status, L"Copy %ls hash failed", CopyTargetName(target));
	DX12OverlayStatusState::SetStatus(status);
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
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12OverlayStatusState::SetStatus(L"DX12 hunting selection reset");
}

void DX12HuntComposeOverlayText(wchar_t *buffer, size_t bufferCount)
{
	if (!buffer || !bufferCount)
		return;
	buffer[0] = 0;
	if (!DX12HuntIsEnabled())
		return;

	AcquireSRWLockShared(&gHuntLock);
	const UINT64 vs = SelectedHashLocked(gVS);
	const UINT64 ps = SelectedHashLocked(gPS);
	const UINT64 cs = SelectedHashLocked(gCS);
	const uint32_t ib = SelectedIaHashLocked(gIB);
	const uint32_t vb = SelectedIaHashLocked(gVB);

	// Line 1: per-category position/total (DX11 top line). A zero position
	// means the category is visited but not armed.
	size_t offset = 0;
	offset += swprintf_s(buffer + offset, bufferCount - offset,
		L"VS:%zu/%zu  PS:%zu/%zu  CS:%zu/%zu  IB:%zu/%zu  VB:%zu/%zu",
		DisplayIndexLocked(gVS), gVS.visited.size(),
		DisplayIndexLocked(gPS), gPS.visited.size(),
		DisplayIndexLocked(gCS), gCS.visited.size(),
		DisplayIndexLocked(gIB), gIB.visited.size(),
		DisplayIndexLocked(gVB), gVB.visited.size());

	// Line 2: the armed selections with their hashes (DX11 info lines).
	bool anyArmed = gVS.armed || gPS.armed || gCS.armed || gIB.armed || gVB.armed;
	if (anyArmed) {
		offset += swprintf_s(buffer + offset, bufferCount - offset, L"\n");
		bool first = true;
		if (gVS.armed) {
			offset += swprintf_s(buffer + offset, bufferCount - offset,
				L"VS %016llx", static_cast<unsigned long long>(vs));
			first = false;
		}
		if (gPS.armed) {
			offset += swprintf_s(buffer + offset, bufferCount - offset,
				L"%lsPS %016llx", first ? L"" : L"   ",
				static_cast<unsigned long long>(ps));
			first = false;
		}
		if (gCS.armed) {
			offset += swprintf_s(buffer + offset, bufferCount - offset,
				L"%lsCS %016llx", first ? L"" : L"   ",
				static_cast<unsigned long long>(cs));
			first = false;
		}
		if (gIB.armed) {
			offset += swprintf_s(buffer + offset, bufferCount - offset,
				L"%lsIB %08x", first ? L"" : L"   ", ib);
			first = false;
		}
		if (gVB.armed) {
			offset += swprintf_s(buffer + offset, bufferCount - offset,
				L"%lsVB %08x", first ? L"" : L"   ", vb);
		}
	}
	ReleaseSRWLockShared(&gHuntLock);
}
