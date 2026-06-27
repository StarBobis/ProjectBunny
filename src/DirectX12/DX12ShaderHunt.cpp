#include "DX12ShaderHunt.h"

#include <Windows.h>

#include <stdint.h>

#include <set>
#include <unordered_map>
#include <vector>

#include "DX12Overlay.h"
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
	size_t selected = 0;
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
};

static SRWLOCK gHuntLock = SRWLOCK_INIT;
static bool gHuntingEnabled = false;
static volatile LONG gHuntingActive = 0;
static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12PipelineState*> gCommandListPso;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntIaState> gCommandListIa;
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
	buffer.hash = DX12HashIaBufferView(gpuVa, size, stride, format, slot);
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

static uint32_t IaHash(const HuntIaBuffer &buffer)
{
	if (!buffer.valid)
		return 0;
	return buffer.hash;
}

static void AddVisitedLocked(IaSelection &selection, uint32_t hash)
{
	if (!hash)
		return;
	selection.visited.insert(hash);
	if (!selection.visited.empty())
		selection.selected %= selection.visited.size();
}

static size_t DisplayIndexLocked(const StageSelection &stage)
{
	return stage.armed && !stage.visited.empty() ? stage.selected + 1 : 0;
}

static size_t DisplayIndexLocked(const IaSelection &selection)
{
	return selection.armed && !selection.visited.empty() ? selection.selected + 1 : 0;
}

static void AdvanceStage(StageSelection &stage, HuntCopyTarget target, int delta)
{
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
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12HuntRefreshOverlay();
}

static void AdvanceIa(IaSelection &selection, HuntCopyTarget target, int delta)
{
	AcquireSRWLockExclusive(&gHuntLock);
	selection.armed = true;
	gLastCopyTarget = target;
	if (!selection.visited.empty()) {
		const size_t size = selection.visited.size();
		if (delta < 0)
			selection.selected = (selection.selected + size - 1) % size;
		else
			selection.selected = (selection.selected + 1) % size;
	}
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12HuntRefreshOverlay();
}

static uint32_t SelectedIaHashLocked(const IaSelection &selection)
{
	if (selection.visited.empty())
		return 0;

	size_t index = selection.selected % selection.visited.size();
	auto it = selection.visited.begin();
	std::advance(it, index);
	return *it;
}

static void RecordPipelineStateUseLocked(ID3D12PipelineState *pipelineState, bool draw, bool dispatch)
{
	DX12PsoShaderInfo info = {};
	if (!pipelineState || !DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return;

	if (draw) {
		AddVisitedLocked(gVS, info.hasVS ? info.vs : 0);
		AddVisitedLocked(gPS, info.hasPS ? info.ps : 0);
	}
	if (dispatch)
		AddVisitedLocked(gCS, info.hasCS ? info.cs : 0);
}

static void RecordIaUseLocked(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	auto it = gCommandListIa.find(commandList);
	if (it == gCommandListIa.end())
		return;

	if (indexed && it->second.indexBuffer.valid)
		AddVisitedLocked(gIB, IaHash(it->second.indexBuffer));
	for (const HuntIaBuffer &buffer : it->second.vertexBuffers)
		AddVisitedLocked(gVB, IaHash(buffer));
}

static ID3D12PipelineState *GetCurrentPipelineStateLocked(ID3D12GraphicsCommandList *commandList)
{
	auto it = gCommandListPso.find(commandList);
	if (it == gCommandListPso.end())
		return nullptr;
	return it->second;
}

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gCommandListPso[commandList] = pipelineState;
	gCommandListIa.erase(commandList);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntSetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gCommandListPso[commandList] = pipelineState;
	if (gHuntingEnabled) {
		gPipelineStateBinds++;
		RecordPipelineStateUseLocked(pipelineState, true, true);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDraw(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gDrawCalls++;
	RecordPipelineStateUseLocked(GetCurrentPipelineStateLocked(commandList), true, false);
	RecordIaUseLocked(commandList, indexed);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gDispatchCalls++;
	RecordPipelineStateUseLocked(GetCurrentPipelineStateLocked(commandList), false, true);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList)
{
	if (!gHuntingEnabled || !commandList)
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gIndirectCalls++;
	RecordPipelineStateUseLocked(GetCurrentPipelineStateLocked(commandList), true, true);
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

static bool HuntShouldSkipLocked(ID3D12GraphicsCommandList *commandList, bool dispatch)
{
	if (!gHuntingEnabled)
		return false;

	DX12PsoShaderInfo info = {};
	ID3D12PipelineState *pso = GetCurrentPipelineStateLocked(commandList);
	if (!pso || !DX12GetPipelineStateShaderInfo(pso, &info))
		return false;

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

bool DX12HuntShouldSkipDraw(ID3D12GraphicsCommandList *commandList, bool indexed)
{
	if (!commandList)
		return false;
	AcquireSRWLockShared(&gHuntLock);
	bool skip = HuntShouldSkipLocked(commandList, false) || IaShouldSkipLocked(commandList, indexed);
	ReleaseSRWLockShared(&gHuntLock);
	return skip;
}

bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	AcquireSRWLockShared(&gHuntLock);
	bool skip = HuntShouldSkipLocked(commandList, true);
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

	HuntIaBuffer buffer = MakeIaView(
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

	HuntIaBuffer buffer = MakeIaView(
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
	DX12HuntRefreshOverlay();
	if (enabled) {
		DX12ShowOverlayWindowFallback();
		DX12ActivateHotPathHooks();
	} else {
		DX12CloseOverlayWindow();
	}
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
