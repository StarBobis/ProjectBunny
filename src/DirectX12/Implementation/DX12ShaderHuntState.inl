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
	DX12LogManager::Fields("DX12HuntSelect",
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
	DX12LogManager::Fields("DX12HuntSelect",
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

