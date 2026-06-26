#include "DX12ShaderHunt.h"

#include <Windows.h>

#include <stdint.h>

#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12Overlay.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12State.h"

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
	std::unordered_set<UINT64> visitedLookup;
	std::vector<UINT64> visited;
	size_t selected = 0;
	UINT64 selectedHash = 0;
	bool armed = false;
};

struct IaSelection
{
	std::unordered_set<uint32_t> visitedLookup;
	std::vector<uint32_t> visited;
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
	uint32_t hash = 0;
	bool valid = false;
};

struct HuntIaState
{
	HuntIaBuffer indexBuffer;
	std::vector<HuntIaBuffer> vertexBuffers;
	std::unordered_set<uint32_t> recordedIndexHashes;
	std::unordered_set<uint32_t> recordedVertexHashes;
};

struct HuntCommandListState
{
	ID3D12PipelineState *pipelineState = nullptr;
	DX12PsoShaderInfo shaderInfo = {};
	bool hasShaderInfo = false;
	bool drawRecorded = false;
	bool dispatchRecorded = false;
};

static SRWLOCK gHuntLock = SRWLOCK_INIT;
static bool gHuntingEnabled = false;
static volatile LONG gHuntingActive = 0;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntCommandListState> gCommandListState;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntIaState> gCommandListIa;
struct HuntIaViewCacheKey
{
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT stride = 0;
	UINT format = 0;
	UINT slot = 0;
	UINT role = 0;

	bool operator==(const HuntIaViewCacheKey &rhs) const
	{
		return gpuVa == rhs.gpuVa && size == rhs.size &&
			stride == rhs.stride && format == rhs.format &&
			slot == rhs.slot && role == rhs.role;
	}
};
struct HuntIaViewCacheKeyHasher
{
	size_t operator()(const HuntIaViewCacheKey &key) const
	{
		uint64_t hash = 14695981039346656037ull;
		auto append = [&hash](uint64_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		append(key.gpuVa);
		append(key.size);
		append(key.stride);
		append(key.format);
		append(key.slot);
		append(key.role);
		return static_cast<size_t>(hash);
	}
};
static std::unordered_map<HuntIaViewCacheKey, HuntIaBuffer, HuntIaViewCacheKeyHasher>
	gIaViewCache;
static SRWLOCK gIaViewCacheLock = SRWLOCK_INIT;
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
static std::unordered_set<UINT64> gFrameSeenVS;
static std::unordered_set<UINT64> gFrameSeenPS;
static std::unordered_set<UINT64> gFrameSeenCS;
static std::unordered_set<uint32_t> gFrameSeenIB;
static std::unordered_set<uint32_t> gFrameSeenVB;
static SRWLOCK gFrameSeenLock = SRWLOCK_INIT;
static DWORD gLastVisitedUpdateTick = 0;
static DWORD gLastHuntInputTick = 0;

static HuntIaBuffer ResolveIaViewCached(
	const char *role, UINT slot, UINT64 gpuVa, UINT size, UINT stride, UINT format)
{
	HuntIaBuffer buffer;
	buffer.role = role ? role : "";
	buffer.slot = slot;
	if (!gpuVa || !size)
		return buffer;

	const HuntIaViewCacheKey key = {
		gpuVa, size, stride, format, slot,
		role && role[0] == 'I' ? 1u : 2u
	};
	auto cached = gIaViewCache.find(key);
	if (cached != gIaViewCache.end())
		return cached->second;

	buffer.gpuVa = gpuVa;
	buffer.size = size;
	buffer.stride = stride;
	buffer.format = format;
	buffer.valid = true;
	DX12BufferResourceSummary summary;
	if (DX12ResolveBufferResourceByGpuVa(gpuVa, size, &summary)) {
		buffer.resolved = true;
		buffer.resource = summary.resource;
		buffer.resourceGpuVa = summary.gpuVirtualAddress;
		buffer.resourceBytes = summary.hasResourceDesc ? summary.resourceDesc.Width : 0;
		buffer.hash = DX12HashBufferResourceView(&summary, gpuVa, size);
	} else {
		buffer.hash = DX12HashBufferResourceView(nullptr, gpuVa, size);
	}

	// IA bindings are command-list hot path state. D3D12 records these bindings
	// into the list; caching identical views avoids rescanning resource metadata
	// when a game rebinds the same IB/VB hundreds of times in a burst frame.
	if (gIaViewCache.size() > 8192) {
		auto midpoint = gIaViewCache.begin();
		std::advance(midpoint, gIaViewCache.size() / 2);
		gIaViewCache.erase(gIaViewCache.begin(), midpoint);
	}
	gIaViewCache[key] = buffer;
	return buffer;
}

static HuntIaBuffer ResolveIaViewCachedThreadSafe(
	const char *role, UINT slot, UINT64 gpuVa, UINT size, UINT stride, UINT format)
{
	HuntIaBuffer empty;
	empty.role = role ? role : "";
	empty.slot = slot;
	if (!gpuVa || !size)
		return empty;

	const HuntIaViewCacheKey key = {
		gpuVa, size, stride, format, slot,
		role && role[0] == 'I' ? 1u : 2u
	};
	AcquireSRWLockShared(&gIaViewCacheLock);
	auto cached = gIaViewCache.find(key);
	if (cached != gIaViewCache.end()) {
		HuntIaBuffer buffer = cached->second;
		ReleaseSRWLockShared(&gIaViewCacheLock);
		return buffer;
	}
	ReleaseSRWLockShared(&gIaViewCacheLock);

	AcquireSRWLockExclusive(&gIaViewCacheLock);
	HuntIaBuffer buffer = ResolveIaViewCached(role, slot, gpuVa, size, stride, format);
	ReleaseSRWLockExclusive(&gIaViewCacheLock);
	return buffer;
}

static UINT64 SelectedHashLocked(const StageSelection &stage)
{
	return stage.selectedHash;
}

static bool SelectionVisibleLocked(const StageSelection &stage)
{
	return stage.selectedHash && stage.visitedLookup.find(stage.selectedHash) != stage.visitedLookup.end();
}

static bool SelectionVisibleLocked(const IaSelection &selection)
{
	return selection.selectedHash && selection.visitedLookup.find(selection.selectedHash) != selection.visitedLookup.end();
}

static void SyncSelectedHashLocked(StageSelection &stage)
{
	if (stage.visited.empty()) {
		stage.selected = 0;
		if (!stage.armed)
			stage.selectedHash = 0;
		return;
	}

	stage.selected %= stage.visited.size();
	stage.selectedHash = stage.visited[stage.selected];
}

static void ReSyncSelectionLocked(StageSelection &stage)
{
	if (!stage.armed || !stage.selectedHash) {
		if (!stage.armed && !stage.visited.empty())
			SyncSelectedHashLocked(stage);
		return;
	}

	stage.selected = 0;
	for (size_t i = 0; i < stage.visited.size(); ++i) {
		if (stage.visited[i] == stage.selectedHash) {
			stage.selected = i;
			return;
		}
	}
}

static void AddVisitedLocked(StageSelection &stage, UINT64 hash)
{
	if (!hash)
		return;
	if (stage.visitedLookup.insert(hash).second) {
		stage.visited.push_back(hash);
		if (stage.armed) {
			if (!stage.selectedHash) {
				stage.selected = stage.visited.size() - 1;
				stage.selectedHash = hash;
			} else if (stage.selectedHash == hash) {
				stage.selected = stage.visited.size() - 1;
			}
		} else if (stage.visited.size() == 1) {
			SyncSelectedHashLocked(stage);
		}
	}
}

static void TrackFrameSeenVS(UINT64 hash) { if (!hash) return; AcquireSRWLockExclusive(&gFrameSeenLock); gFrameSeenVS.insert(hash); ReleaseSRWLockExclusive(&gFrameSeenLock); }
static void TrackFrameSeenPS(UINT64 hash) { if (!hash) return; AcquireSRWLockExclusive(&gFrameSeenLock); gFrameSeenPS.insert(hash); ReleaseSRWLockExclusive(&gFrameSeenLock); }
static void TrackFrameSeenCS(UINT64 hash) { if (!hash) return; AcquireSRWLockExclusive(&gFrameSeenLock); gFrameSeenCS.insert(hash); ReleaseSRWLockExclusive(&gFrameSeenLock); }
static void TrackFrameSeenIB(uint32_t hash) { if (!hash) return; AcquireSRWLockExclusive(&gFrameSeenLock); gFrameSeenIB.insert(hash); ReleaseSRWLockExclusive(&gFrameSeenLock); }
static void TrackFrameSeenVB(uint32_t hash) { if (!hash) return; AcquireSRWLockExclusive(&gFrameSeenLock); gFrameSeenVB.insert(hash); ReleaseSRWLockExclusive(&gFrameSeenLock); }

static uint32_t IaHash(const HuntIaBuffer &buffer)
{
	if (!buffer.valid)
		return 0;
	return buffer.hash;
}

// Batch frame-seen tracking for a draw call, including on the skip path.
// Called under gHuntLock shared — takes gFrameSeenLock exclusive briefly.
static void BatchTrackFrameSeenLocked(const HuntCommandListState *state,
                                      ID3D12GraphicsCommandList *commandList,
                                      bool indexed)
{
	AcquireSRWLockExclusive(&gFrameSeenLock);
	if (state && state->hasShaderInfo) {
		const DX12PsoShaderInfo &info = state->shaderInfo;
		if (info.hasVS) gFrameSeenVS.insert(info.vs);
		if (info.hasPS) gFrameSeenPS.insert(info.ps);
	}
	auto iaIt = gCommandListIa.find(commandList);
	if (iaIt != gCommandListIa.end()) {
		if (indexed && iaIt->second.indexBuffer.valid) {
			uint32_t h = IaHash(iaIt->second.indexBuffer);
			if (h) gFrameSeenIB.insert(h);
		}
		for (const HuntIaBuffer &buf : iaIt->second.vertexBuffers) {
			uint32_t h = IaHash(buf);
			if (h) gFrameSeenVB.insert(h);
		}
	}
	ReleaseSRWLockExclusive(&gFrameSeenLock);
}

static void SyncSelectedHashLocked(IaSelection &selection)
{
	if (selection.visited.empty()) {
		selection.selected = 0;
		if (!selection.armed)
			selection.selectedHash = 0;
		return;
	}

	selection.selected %= selection.visited.size();
	selection.selectedHash = selection.visited[selection.selected];
}

static void ReSyncSelectionLocked(IaSelection &selection)
{
	if (!selection.armed || !selection.selectedHash) {
		if (!selection.armed && !selection.visited.empty())
			SyncSelectedHashLocked(selection);
		return;
	}

	selection.selected = 0;
	for (size_t i = 0; i < selection.visited.size(); ++i) {
		if (selection.visited[i] == selection.selectedHash) {
			selection.selected = i;
			return;
		}
	}
}

static void AddVisitedLocked(IaSelection &selection, uint32_t hash)
{
	if (!hash)
		return;
	if (selection.visitedLookup.insert(hash).second) {
		selection.visited.push_back(hash);
		if (selection.armed) {
			if (!selection.selectedHash) {
				selection.selected = selection.visited.size() - 1;
				selection.selectedHash = hash;
			} else if (selection.selectedHash == hash) {
				selection.selected = selection.visited.size() - 1;
			}
		} else if (selection.visited.size() == 1) {
			SyncSelectedHashLocked(selection);
		}
	}
}

static size_t DisplayIndexLocked(const StageSelection &stage)
{
	return stage.armed && SelectionVisibleLocked(stage) ? stage.selected + 1 : 0;
}

static size_t DisplayIndexLocked(const IaSelection &selection)
{
	return selection.armed && SelectionVisibleLocked(selection) ? selection.selected + 1 : 0;
}

static void AdvanceStage(StageSelection &stage, HuntCopyTarget target, int delta)
{
	AcquireSRWLockExclusive(&gHuntLock);
	stage.armed = true;
	gLastCopyTarget = target;
	gLastHuntInputTick = GetTickCount();
	if (!stage.visited.empty()) {
		const size_t size = stage.visited.size();
		if (delta < 0)
			stage.selected = (stage.selected + size - 1) % size;
		else
			stage.selected = (stage.selected + 1) % size;
		SyncSelectedHashLocked(stage);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12HuntRefreshOverlay();
}

static void AdvanceIa(IaSelection &selection, HuntCopyTarget target, int delta)
{
	AcquireSRWLockExclusive(&gHuntLock);
	selection.armed = true;
	gLastCopyTarget = target;
	gLastHuntInputTick = GetTickCount();
	if (!selection.visited.empty()) {
		const size_t size = selection.visited.size();
		if (delta < 0)
			selection.selected = (selection.selected + size - 1) % size;
		else
			selection.selected = (selection.selected + 1) % size;
		SyncSelectedHashLocked(selection);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12HuntRefreshOverlay();
}

static uint32_t SelectedIaHashLocked(const IaSelection &selection)
{
	return selection.selectedHash;
}

// Incremental update: for each stage, remove hashes from visited that were NOT
// seen this frame, and add hashes that WERE seen this frame but not yet in visited.
// This preserves selection stability while keeping the list up-to-date.
template <typename T>
static void IncrementalUpdateStage(std::unordered_set<T> &frameSeen,
                                    std::unordered_set<T> &lookup,
                                    std::vector<T> &list)
{
	std::vector<T> kept;
	kept.reserve(list.size());
	for (T hash : list) {
		if (frameSeen.find(hash) != frameSeen.end())
			kept.push_back(hash);
	}
	for (T hash : frameSeen) {
		if (lookup.find(hash) == lookup.end()) {
			lookup.insert(hash);
			kept.push_back(hash);
		}
	}
	list = std::move(kept);
	// Re-lookup from updated list
	lookup.clear();
	for (T hash : list)
		lookup.insert(hash);
}

static void IncrementalUpdateVisitedLocked()
{
	IncrementalUpdateStage(gFrameSeenVS, gVS.visitedLookup, gVS.visited);
	IncrementalUpdateStage(gFrameSeenPS, gPS.visitedLookup, gPS.visited);
	IncrementalUpdateStage(gFrameSeenCS, gCS.visitedLookup, gCS.visited);
	IncrementalUpdateStage(gFrameSeenIB, gIB.visitedLookup, gIB.visited);
	IncrementalUpdateStage(gFrameSeenVB, gVB.visitedLookup, gVB.visited);

	AcquireSRWLockExclusive(&gFrameSeenLock);
	gFrameSeenVS.clear();
	gFrameSeenPS.clear();
	gFrameSeenCS.clear();
	gFrameSeenIB.clear();
	gFrameSeenVB.clear();
	ReleaseSRWLockExclusive(&gFrameSeenLock);

	ReSyncSelectionLocked(gVS);
	ReSyncSelectionLocked(gPS);
	ReSyncSelectionLocked(gCS);
	ReSyncSelectionLocked(gIB);
	ReSyncSelectionLocked(gVB);

	for (auto &entry : gCommandListState) {
		entry.second.drawRecorded = false;
		entry.second.dispatchRecorded = false;
	}
	for (auto &entry : gCommandListIa) {
		entry.second.recordedIndexHashes.clear();
		entry.second.recordedVertexHashes.clear();
	}
}

static HuntCommandListState MakeCommandListState(ID3D12PipelineState *pipelineState)
{
	HuntCommandListState state;
	state.pipelineState = pipelineState;
	if (pipelineState)
		state.hasShaderInfo = DX12GetPipelineStateShaderInfo(pipelineState, &state.shaderInfo);
	return state;
}

static void RecordPipelineStateUseLocked(HuntCommandListState &state, bool draw, bool dispatch)
{
	if (!state.hasShaderInfo)
		return;

	const DX12PsoShaderInfo &info = state.shaderInfo;
	if (draw) {
		if (!state.drawRecorded) {
			AddVisitedLocked(gVS, info.hasVS ? info.vs : 0);
			AddVisitedLocked(gPS, info.hasPS ? info.ps : 0);
			state.drawRecorded = true;
		}
		// Frame-seen tracking is handled by BatchTrackFrameSeenLocked
		// in the draw/dispatch path — no need to double-lock here.
	}
	if (dispatch) {
		if (!state.dispatchRecorded) {
			AddVisitedLocked(gCS, info.hasCS ? info.cs : 0);
			state.dispatchRecorded = true;
		}
	}
}

static void RecordIaUseLocked(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	auto it = gCommandListIa.find(commandList);
	if (it == gCommandListIa.end())
		return;

	HuntIaState &state = it->second;
	if (indexed && state.indexBuffer.valid) {
		uint32_t hash = IaHash(state.indexBuffer);
		if (state.recordedIndexHashes.insert(hash).second)
			AddVisitedLocked(gIB, hash);
	}
	for (const HuntIaBuffer &buffer : state.vertexBuffers) {
		uint32_t hash = IaHash(buffer);
		if (!hash)
			continue;
		if (state.recordedVertexHashes.insert(hash).second)
			AddVisitedLocked(gVB, hash);
	}
}

static HuntCommandListState *GetCurrentCommandListStateLocked(ID3D12GraphicsCommandList *commandList)
{
	auto it = gCommandListState.find(commandList);
	if (it == gCommandListState.end())
		return nullptr;
	return &it->second;
}

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	HuntCommandListState state = MakeCommandListState(pipelineState);
	AcquireSRWLockExclusive(&gHuntLock);
	gCommandListState[commandList] = state;
	gCommandListIa.erase(commandList);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntSetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	auto existing = gCommandListState.find(commandList);
	if (existing != gCommandListState.end() && existing->second.pipelineState == pipelineState) {
		if (gHuntingEnabled)
			RecordPipelineStateUseLocked(existing->second, true, true);
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	HuntCommandListState state = MakeCommandListState(pipelineState);
	AcquireSRWLockExclusive(&gHuntLock);
	HuntCommandListState &stored = gCommandListState[commandList];
	stored = state;
	if (gHuntingEnabled) {
		gPipelineStateBinds++;
		RecordPipelineStateUseLocked(stored, true, true);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDraw(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gDrawCalls++;
	HuntCommandListState *state = GetCurrentCommandListStateLocked(commandList);
	if (state)
		RecordPipelineStateUseLocked(*state, true, false);
	RecordIaUseLocked(commandList, indexed);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gDispatchCalls++;
	HuntCommandListState *state = GetCurrentCommandListStateLocked(commandList);
	if (state)
		RecordPipelineStateUseLocked(*state, false, true);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gIndirectCalls++;
	HuntCommandListState *state = GetCurrentCommandListStateLocked(commandList);
	if (state)
		RecordPipelineStateUseLocked(*state, true, true);
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
		state.indexBuffer = ResolveIaViewCachedThreadSafe(
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
			buffer = ResolveIaViewCachedThreadSafe(
				"VB", slot, views[i].BufferLocation, views[i].SizeInBytes,
				views[i].StrideInBytes, 0);
		}
		state.vertexBuffers[slot] = buffer;
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

static bool HuntShouldSkipLocked(const HuntCommandListState *state, bool dispatch)
{
	if (!gHuntingEnabled || !state || !state->hasShaderInfo)
		return false;

	const DX12PsoShaderInfo &info = state->shaderInfo;
	if (dispatch) {
		UINT64 cs = SelectedHashLocked(gCS);
		return gCS.armed && cs && info.hasCS && info.cs == cs;
	}

	UINT64 vs = SelectedHashLocked(gVS);
	UINT64 ps = SelectedHashLocked(gPS);
	return (gVS.armed && vs && info.hasVS && info.vs == vs) ||
		(gPS.armed && ps && info.hasPS && info.ps == ps);
}

static bool IaShouldSkipLocked(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!gHuntingEnabled)
		return false;

	uint32_t ib = SelectedIaHashLocked(gIB);
	uint32_t vb = SelectedIaHashLocked(gVB);
	if ((!gIB.armed || !ib) && (!gVB.armed || !vb))
		return false;

	auto state = gCommandListIa.find(commandList);
	if (state == gCommandListIa.end())
		return false;

	if (indexed && gIB.armed && ib && state->second.indexBuffer.valid &&
	    IaHash(state->second.indexBuffer) == ib)
		return true;
	if (gVB.armed && vb) {
		for (const HuntIaBuffer &buffer : state->second.vertexBuffers) {
			if (IaHash(buffer) == vb)
				return true;
		}
	}

	return false;
}

bool DX12HuntProcessDraw(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!gHuntingEnabled || !commandList)
		return false;

	// Fast path: shared lock for read-only skip check.  Multiple recording
	// threads can check in parallel without serializing on the exclusive lock.
	AcquireSRWLockShared(&gHuntLock);
	const HuntCommandListState *state = GetCurrentCommandListStateLocked(commandList);
	const bool skip = HuntShouldSkipLocked(state, false) || IaShouldSkipLocked(commandList, indexed);

	// Always mark PSO/IA hashes as "seen this frame" so the incremental
	// update doesn't drop them just because the draw was skipped.
	BatchTrackFrameSeenLocked(state, commandList, indexed);

	ReleaseSRWLockShared(&gHuntLock);

	if (skip)
		return true;

	// Recording (writes) needs the exclusive lock but is a much smaller
	// fraction of the frame than the skip check alone.
	AcquireSRWLockExclusive(&gHuntLock);
	gDrawCalls++;
	state = GetCurrentCommandListStateLocked(commandList);
	if (state)
		RecordPipelineStateUseLocked(const_cast<HuntCommandListState&>(*state), true, false);
	RecordIaUseLocked(commandList, indexed);
	ReleaseSRWLockExclusive(&gHuntLock);
	return false;
}

bool DX12HuntProcessDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!gHuntingEnabled || !commandList)
		return false;

	AcquireSRWLockShared(&gHuntLock);
	const HuntCommandListState *state = GetCurrentCommandListStateLocked(commandList);
	const bool skip = HuntShouldSkipLocked(state, true);

	// Mark CS hash as seen even on skip path, to keep it in visited.
	if (state && state->hasShaderInfo && state->shaderInfo.hasCS)
		TrackFrameSeenCS(state->shaderInfo.cs);

	ReleaseSRWLockShared(&gHuntLock);

	if (skip)
		return true;

	AcquireSRWLockExclusive(&gHuntLock);
	gDispatchCalls++;
	state = GetCurrentCommandListStateLocked(commandList);
	if (state)
		RecordPipelineStateUseLocked(const_cast<HuntCommandListState&>(*state), false, true);
	ReleaseSRWLockExclusive(&gHuntLock);
	return false;
}

bool DX12HuntShouldSkipDraw(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!commandList)
		return false;
	AcquireSRWLockShared(&gHuntLock);
	bool skip = HuntShouldSkipLocked(GetCurrentCommandListStateLocked(commandList), false) ||
		IaShouldSkipLocked(commandList, indexed);
	ReleaseSRWLockShared(&gHuntLock);
	return skip;
}

bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	AcquireSRWLockShared(&gHuntLock);
	bool skip = HuntShouldSkipLocked(GetCurrentCommandListStateLocked(commandList), true);
	ReleaseSRWLockShared(&gHuntLock);
	return skip;
}

bool DX12HuntGetIaHashes(
	ID3D12GraphicsCommandList *commandList, uint32_t *ibHash,
	uint32_t *vbHashes, size_t vbHashCount, size_t *vbHashWritten)
{
	if (ibHash)
		*ibHash = 0;
	if (vbHashWritten)
		*vbHashWritten = 0;
	if (!commandList)
		return false;

	AcquireSRWLockShared(&gHuntLock);
	auto state = gCommandListIa.find(commandList);
	if (state == gCommandListIa.end()) {
		ReleaseSRWLockShared(&gHuntLock);
		return false;
	}

	if (ibHash)
		*ibHash = IaHash(state->second.indexBuffer);

	size_t written = 0;
	if (vbHashes && vbHashCount) {
		for (const HuntIaBuffer &buffer : state->second.vertexBuffers) {
			uint32_t hash = IaHash(buffer);
			if (!hash)
				continue;
			bool duplicate = false;
			for (size_t i = 0; i < written; ++i) {
				if (vbHashes[i] == hash) {
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;
			if (written >= vbHashCount)
				break;
			vbHashes[written++] = hash;
		}
	}
	if (vbHashWritten)
		*vbHashWritten = written;
	ReleaseSRWLockShared(&gHuntLock);
	return (ibHash && *ibHash) || written > 0;
}

bool DX12HuntGetIaHashState(ID3D12GraphicsCommandList *commandList, DX12IaHashState *outState)
{
	if (!outState)
		return false;
	*outState = DX12IaHashState();
	if (!commandList)
		return false;

	AcquireSRWLockShared(&gHuntLock);
	auto state = gCommandListIa.find(commandList);
	if (state == gCommandListIa.end()) {
		ReleaseSRWLockShared(&gHuntLock);
		return false;
	}

	const HuntIaBuffer &ib = state->second.indexBuffer;
	outState->indexHash = IaHash(ib);
	if (outState->indexHash) {
		outState->hasIndexBuffer = true;
		outState->indexView.BufferLocation = ib.gpuVa;
		outState->indexView.SizeInBytes = ib.size;
		outState->indexView.Format = static_cast<DXGI_FORMAT>(ib.format);
	}

	for (const HuntIaBuffer &buffer : state->second.vertexBuffers) {
		uint32_t hash = IaHash(buffer);
		if (!hash)
			continue;
		DX12IaBufferHash item;
		item.slot = buffer.slot;
		item.hash = hash;
		item.vertexView.BufferLocation = buffer.gpuVa;
		item.vertexView.SizeInBytes = buffer.size;
		item.vertexView.StrideInBytes = buffer.stride;
		outState->vertexBuffers.push_back(item);
	}
	ReleaseSRWLockShared(&gHuntLock);
	return outState->hasIndexBuffer || !outState->vertexBuffers.empty();
}

bool DX12HuntHashIndexBufferView(const D3D12_INDEX_BUFFER_VIEW *view, uint32_t *hash)
{
	if (hash)
		*hash = 0;
	if (!view || !view->BufferLocation || !view->SizeInBytes)
		return false;

	HuntIaBuffer buffer = ResolveIaViewCachedThreadSafe(
		"IB", 0, view->BufferLocation, view->SizeInBytes,
		0, static_cast<UINT>(view->Format));
	if (hash)
		*hash = IaHash(buffer);
	return IaHash(buffer) != 0;
}

bool DX12HuntHashVertexBufferView(UINT slot, const D3D12_VERTEX_BUFFER_VIEW *view, uint32_t *hash)
{
	if (hash)
		*hash = 0;
	if (!view || !view->BufferLocation || !view->SizeInBytes)
		return false;

	HuntIaBuffer buffer = ResolveIaViewCachedThreadSafe(
		"VB", slot, view->BufferLocation, view->SizeInBytes,
		view->StrideInBytes, 0);
	if (hash)
		*hash = IaHash(buffer);
	return IaHash(buffer) != 0;
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
	AcquireSRWLockExclusive(&gHuntLock);
	gHuntingEnabled = !gHuntingEnabled;
	bool enabled = gHuntingEnabled;
	InterlockedExchange(&gHuntingActive, enabled ? 1 : 0);
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12Log("DX12 hunting toggled %s\n", enabled ? "on" : "off");
	gLastHuntInputTick = GetTickCount();
	gLastVisitedUpdateTick = 0; // force immediate update on next end-frame
	DX12HuntRefreshOverlay();
	if (!enabled)
		DX12CloseOverlayWindow();
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

static void DX12HuntCopyTargetHash(HuntCopyTarget target);

void DX12HuntCopyPS() { DX12HuntCopyTargetHash(HuntCopyTarget::PS); }
void DX12HuntCopyVS() { DX12HuntCopyTargetHash(HuntCopyTarget::VS); }
void DX12HuntCopyCS() { DX12HuntCopyTargetHash(HuntCopyTarget::CS); }
void DX12HuntCopyVB() { DX12HuntCopyTargetHash(HuntCopyTarget::VB); }
void DX12HuntCopyIB() { DX12HuntCopyTargetHash(HuntCopyTarget::IB); }

static void DX12HuntCopyTargetHash(HuntCopyTarget target)
{
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

void DX12HuntEndFrame()
{
	if (!DX12HuntIsEnabled())
		return;

	// Rate-limit visited list update: default 3 seconds, 1 second while
	// the user is actively navigating (pressing hunt keys).
	const DWORD now = GetTickCount();
	if (!gLastVisitedUpdateTick)
		gLastVisitedUpdateTick = now;
	const DWORD elapsed = now - gLastVisitedUpdateTick;
	const DWORD interval = 1000;
	if (elapsed < interval) {
		// Don't update visited, don't refresh overlay text, don't reset counters.
		return;
	}

	AcquireSRWLockExclusive(&gHuntLock);
	IncrementalUpdateVisitedLocked();
	ReleaseSRWLockExclusive(&gHuntLock);
	gLastVisitedUpdateTick = now;

	// Refresh overlay NOW — before resetting counters — so it shows
	// the current frame's activity, not zeros.
	DX12HuntRefreshOverlay();

	// Reset per-frame counters for the next frame.
	AcquireSRWLockExclusive(&gHuntLock);
	gPipelineStateBinds = 0;
	gDrawCalls = 0;
	gDispatchCalls = 0;
	gIndirectCalls = 0;
	ReleaseSRWLockExclusive(&gHuntLock);
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

	gLastHuntInputTick = GetTickCount();
	gLastVisitedUpdateTick = 0; // force immediate update on next end-frame
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
		L"VS %ls %zu/%zu %016llx%ls   PS %ls %zu/%zu %016llx%ls   CS %ls %zu/%zu %016llx%ls\n"
		L"IB %ls %zu/%zu %08x%ls   VB %ls %zu/%zu %08x%ls\n"
		L"PSO binds %llu   Draw %llu Dispatch %llu Indirect %llu\n"
		L"PS:1/2/3 VS:4/5/6 IB:7/8/9 VB:/ * - CS:Del+1/2 Del+3 Num0:on/off Num+:reset",
		gHuntingEnabled ? L"on" : L"off",
		gVS.armed ? L"*" : L" ",
		DisplayIndexLocked(gVS), gVS.visited.size(),
		static_cast<unsigned long long>(gVS.armed ? vs : 0),
		(gVS.armed && vs && !SelectionVisibleLocked(gVS)) ? L" (not visible)" : L"",
		gPS.armed ? L"*" : L" ",
		DisplayIndexLocked(gPS), gPS.visited.size(),
		static_cast<unsigned long long>(gPS.armed ? ps : 0),
		(gPS.armed && ps && !SelectionVisibleLocked(gPS)) ? L" (not visible)" : L"",
		gCS.armed ? L"*" : L" ",
		DisplayIndexLocked(gCS), gCS.visited.size(),
		static_cast<unsigned long long>(gCS.armed ? cs : 0),
		(gCS.armed && cs && !SelectionVisibleLocked(gCS)) ? L" (not visible)" : L"",
		gIB.armed ? L"*" : L" ",
		DisplayIndexLocked(gIB), gIB.visited.size(),
		gIB.armed ? ib : 0,
		(gIB.armed && ib && !SelectionVisibleLocked(gIB)) ? L" (not visible)" : L"",
		gVB.armed ? L"*" : L" ",
		DisplayIndexLocked(gVB), gVB.visited.size(),
		gVB.armed ? vb : 0,
		(gVB.armed && vb && !SelectionVisibleLocked(gVB)) ? L" (not visible)" : L"",
		static_cast<unsigned long long>(gPipelineStateBinds),
		static_cast<unsigned long long>(gDrawCalls),
		static_cast<unsigned long long>(gDispatchCalls),
		static_cast<unsigned long long>(gIndirectCalls));
	ReleaseSRWLockShared(&gHuntLock);

	DX12SetOverlayStatus(status);
}
