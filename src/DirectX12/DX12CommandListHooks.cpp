#include "DX12CommandListHooks.h"

#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>

#include "DX12BindingTracker.h"
#include "DX12CommandListCommandRecorder.h"
#include "DX12CommandListLifecycle.h"
#include "DX12CommandListRuntime.h"
#include "DX12CommandListStateCapture.h"
#include "DX12DispatchHookFlow.h"
#include "DX12DrawHookFlow.h"
#include "DX12FrameAnalysis.h"
#include "DX12HookManager.h"
#include "DX12IaReplacementExecutor.h"
#include "DX12Json.h"
#include "DX12ModRuntime.h"
#include "DX12Profiling.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"

typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMAND_QUEUE)(
	ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMAND_ALLOCATOR)(
	ID3D12Device*, D3D12_COMMAND_LIST_TYPE, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMAND_LIST)(
	ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*,
	ID3D12PipelineState*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMMAND_LIST1)(
	ID3D12Device4*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);

typedef void(STDMETHODCALLTYPE *PFN_QUEUE_UPDATE_TILE_MAPPINGS)(
	ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*,
	const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*,
	const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS);
typedef void(STDMETHODCALLTYPE *PFN_QUEUE_COPY_TILE_MAPPINGS)(
	ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*,
	ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*,
	D3D12_TILE_MAPPING_FLAGS);
typedef void(STDMETHODCALLTYPE *PFN_QUEUE_EXECUTE_COMMAND_LISTS)(
	ID3D12CommandQueue*, UINT, ID3D12CommandList *const*);
typedef void(STDMETHODCALLTYPE *PFN_QUEUE_SET_MARKER)(
	ID3D12CommandQueue*, UINT, const void*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_QUEUE_BEGIN_EVENT)(
	ID3D12CommandQueue*, UINT, const void*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_QUEUE_END_EVENT)(ID3D12CommandQueue*);
typedef HRESULT(STDMETHODCALLTYPE *PFN_QUEUE_SIGNAL)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
typedef HRESULT(STDMETHODCALLTYPE *PFN_QUEUE_WAIT)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
typedef HRESULT(STDMETHODCALLTYPE *PFN_QUEUE_GET_TIMESTAMP_FREQUENCY)(ID3D12CommandQueue*, UINT64*);
typedef HRESULT(STDMETHODCALLTYPE *PFN_QUEUE_GET_CLOCK_CALIBRATION)(ID3D12CommandQueue*, UINT64*, UINT64*);

typedef HRESULT(STDMETHODCALLTYPE *PFN_CLOSE_COMMAND_LIST)(ID3D12GraphicsCommandList*);
typedef HRESULT(STDMETHODCALLTYPE *PFN_RESET_COMMAND_LIST)(
	ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
typedef void(STDMETHODCALLTYPE *PFN_CLEAR_STATE)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
typedef void(STDMETHODCALLTYPE *PFN_DRAW_INSTANCED)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_DRAW_INDEXED_INSTANCED)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_DISPATCH)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_COPY_BUFFER_REGION)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
typedef void(STDMETHODCALLTYPE *PFN_COPY_TEXTURE_REGION)(
	ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT,
	const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
typedef void(STDMETHODCALLTYPE *PFN_COPY_RESOURCE)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
typedef void(STDMETHODCALLTYPE *PFN_COPY_TILES)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*,
	const D3D12_TILE_REGION_SIZE*, ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
typedef void(STDMETHODCALLTYPE *PFN_RESOLVE_SUBRESOURCE)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
typedef void(STDMETHODCALLTYPE *PFN_IA_SET_PRIMITIVE_TOPOLOGY)(
	ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
typedef void(STDMETHODCALLTYPE *PFN_RS_SET_VIEWPORTS)(
	ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
typedef void(STDMETHODCALLTYPE *PFN_RS_SET_SCISSOR_RECTS)(
	ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
typedef void(STDMETHODCALLTYPE *PFN_OM_SET_BLEND_FACTOR)(ID3D12GraphicsCommandList*, const FLOAT[4]);
typedef void(STDMETHODCALLTYPE *PFN_OM_SET_STENCIL_REF)(ID3D12GraphicsCommandList*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_SET_PIPELINE_STATE)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
typedef void(STDMETHODCALLTYPE *PFN_RESOURCE_BARRIER)(
	ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
typedef void(STDMETHODCALLTYPE *PFN_EXECUTE_BUNDLE)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
typedef void(STDMETHODCALLTYPE *PFN_SET_DESCRIPTOR_HEAPS)(
	ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap *const*);
typedef void(STDMETHODCALLTYPE *PFN_SET_ROOT_SIGNATURE)(
	ID3D12GraphicsCommandList*, ID3D12RootSignature*);
typedef void(STDMETHODCALLTYPE *PFN_SET_ROOT_DESCRIPTOR_TABLE)(
	ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
typedef void(STDMETHODCALLTYPE *PFN_SET_ROOT_32BIT_CONSTANT)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_SET_ROOT_32BIT_CONSTANTS)(
	ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_SET_ROOT_GPU_VA)(
	ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
typedef void(STDMETHODCALLTYPE *PFN_IA_SET_INDEX_BUFFER)(
	ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
typedef void(STDMETHODCALLTYPE *PFN_IA_SET_VERTEX_BUFFERS)(
	ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
typedef void(STDMETHODCALLTYPE *PFN_SO_SET_TARGETS)(
	ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW*);
typedef void(STDMETHODCALLTYPE *PFN_OM_SET_RENDER_TARGETS)(
	ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
	const D3D12_CPU_DESCRIPTOR_HANDLE*);
typedef void(STDMETHODCALLTYPE *PFN_CLEAR_DEPTH_STENCIL_VIEW)(
	ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8,
	UINT, const D3D12_RECT*);
typedef void(STDMETHODCALLTYPE *PFN_CLEAR_RENDER_TARGET_VIEW)(
	ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*);
typedef void(STDMETHODCALLTYPE *PFN_CLEAR_UAV_UINT)(
	ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
	ID3D12Resource*, const UINT[4], UINT, const D3D12_RECT*);
typedef void(STDMETHODCALLTYPE *PFN_CLEAR_UAV_FLOAT)(
	ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
	ID3D12Resource*, const FLOAT[4], UINT, const D3D12_RECT*);
typedef void(STDMETHODCALLTYPE *PFN_DISCARD_RESOURCE)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
typedef void(STDMETHODCALLTYPE *PFN_BEGIN_QUERY)(
	ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
typedef void(STDMETHODCALLTYPE *PFN_END_QUERY)(
	ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
typedef void(STDMETHODCALLTYPE *PFN_RESOLVE_QUERY_DATA)(
	ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT, UINT,
	ID3D12Resource*, UINT64);
typedef void(STDMETHODCALLTYPE *PFN_SET_PREDICATION)(
	ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, D3D12_PREDICATION_OP);
typedef void(STDMETHODCALLTYPE *PFN_COMMAND_LIST_SET_MARKER)(
	ID3D12GraphicsCommandList*, UINT, const void*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_COMMAND_LIST_BEGIN_EVENT)(
	ID3D12GraphicsCommandList*, UINT, const void*, UINT);
typedef void(STDMETHODCALLTYPE *PFN_COMMAND_LIST_END_EVENT)(ID3D12GraphicsCommandList*);
typedef void(STDMETHODCALLTYPE *PFN_EXECUTE_INDIRECT)(
	ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64,
	ID3D12Resource*, UINT64);

static PFN_CREATE_COMMAND_QUEUE gOrigCreateCommandQueue = nullptr;
static PFN_CREATE_COMMAND_ALLOCATOR gOrigCreateCommandAllocator = nullptr;
static PFN_CREATE_COMMAND_LIST gOrigCreateCommandList = nullptr;
static PFN_CREATE_COMMAND_LIST1 gOrigCreateCommandList1 = nullptr;

static PFN_QUEUE_UPDATE_TILE_MAPPINGS gOrigQueueUpdateTileMappings = nullptr;
static PFN_QUEUE_COPY_TILE_MAPPINGS gOrigQueueCopyTileMappings = nullptr;
static PFN_QUEUE_EXECUTE_COMMAND_LISTS gOrigQueueExecuteCommandLists = nullptr;
static PFN_QUEUE_SET_MARKER gOrigQueueSetMarker = nullptr;
static PFN_QUEUE_BEGIN_EVENT gOrigQueueBeginEvent = nullptr;
static PFN_QUEUE_END_EVENT gOrigQueueEndEvent = nullptr;
static PFN_QUEUE_SIGNAL gOrigQueueSignal = nullptr;
static PFN_QUEUE_WAIT gOrigQueueWait = nullptr;
static PFN_QUEUE_GET_TIMESTAMP_FREQUENCY gOrigQueueGetTimestampFrequency = nullptr;
static PFN_QUEUE_GET_CLOCK_CALIBRATION gOrigQueueGetClockCalibration = nullptr;

static PFN_CLOSE_COMMAND_LIST gOrigCloseCommandList = nullptr;
static PFN_RESET_COMMAND_LIST gOrigResetCommandList = nullptr;
static PFN_CLEAR_STATE gOrigClearState = nullptr;
static PFN_DRAW_INSTANCED gOrigDrawInstanced = nullptr;
static PFN_DRAW_INDEXED_INSTANCED gOrigDrawIndexedInstanced = nullptr;
static PFN_DISPATCH gOrigDispatch = nullptr;
static PFN_COPY_BUFFER_REGION gOrigCopyBufferRegion = nullptr;
static PFN_COPY_TEXTURE_REGION gOrigCopyTextureRegion = nullptr;
static PFN_COPY_RESOURCE gOrigCopyResource = nullptr;
static PFN_COPY_TILES gOrigCopyTiles = nullptr;
static PFN_RESOLVE_SUBRESOURCE gOrigResolveSubresource = nullptr;
static PFN_IA_SET_PRIMITIVE_TOPOLOGY gOrigIASetPrimitiveTopology = nullptr;
static PFN_RS_SET_VIEWPORTS gOrigRSSetViewports = nullptr;
static PFN_RS_SET_SCISSOR_RECTS gOrigRSSetScissorRects = nullptr;
static PFN_OM_SET_BLEND_FACTOR gOrigOMSetBlendFactor = nullptr;
static PFN_OM_SET_STENCIL_REF gOrigOMSetStencilRef = nullptr;
static PFN_SET_PIPELINE_STATE gOrigSetPipelineState = nullptr;
static PFN_RESOURCE_BARRIER gOrigResourceBarrier = nullptr;
static PFN_EXECUTE_BUNDLE gOrigExecuteBundle = nullptr;
static PFN_SET_DESCRIPTOR_HEAPS gOrigSetDescriptorHeaps = nullptr;
static PFN_SET_ROOT_SIGNATURE gOrigSetComputeRootSignature = nullptr;
static PFN_SET_ROOT_SIGNATURE gOrigSetGraphicsRootSignature = nullptr;
static PFN_SET_ROOT_DESCRIPTOR_TABLE gOrigSetComputeRootDescriptorTable = nullptr;
static PFN_SET_ROOT_DESCRIPTOR_TABLE gOrigSetGraphicsRootDescriptorTable = nullptr;
static PFN_SET_ROOT_32BIT_CONSTANT gOrigSetComputeRoot32BitConstant = nullptr;
static PFN_SET_ROOT_32BIT_CONSTANT gOrigSetGraphicsRoot32BitConstant = nullptr;
static PFN_SET_ROOT_32BIT_CONSTANTS gOrigSetComputeRoot32BitConstants = nullptr;
static PFN_SET_ROOT_32BIT_CONSTANTS gOrigSetGraphicsRoot32BitConstants = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetComputeRootConstantBufferView = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetGraphicsRootConstantBufferView = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetComputeRootShaderResourceView = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetGraphicsRootShaderResourceView = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetComputeRootUnorderedAccessView = nullptr;
static PFN_SET_ROOT_GPU_VA gOrigSetGraphicsRootUnorderedAccessView = nullptr;
static PFN_IA_SET_INDEX_BUFFER gOrigIASetIndexBuffer = nullptr;
static PFN_IA_SET_VERTEX_BUFFERS gOrigIASetVertexBuffers = nullptr;
static PFN_SO_SET_TARGETS gOrigSOSetTargets = nullptr;
static PFN_OM_SET_RENDER_TARGETS gOrigOMSetRenderTargets = nullptr;
static PFN_CLEAR_DEPTH_STENCIL_VIEW gOrigClearDepthStencilView = nullptr;
static PFN_CLEAR_RENDER_TARGET_VIEW gOrigClearRenderTargetView = nullptr;
static PFN_CLEAR_UAV_UINT gOrigClearUnorderedAccessViewUint = nullptr;
static PFN_CLEAR_UAV_FLOAT gOrigClearUnorderedAccessViewFloat = nullptr;
static PFN_DISCARD_RESOURCE gOrigDiscardResource = nullptr;
static PFN_BEGIN_QUERY gOrigBeginQuery = nullptr;
static PFN_END_QUERY gOrigEndQuery = nullptr;
static PFN_RESOLVE_QUERY_DATA gOrigResolveQueryData = nullptr;
static PFN_SET_PREDICATION gOrigSetPredication = nullptr;
static PFN_COMMAND_LIST_SET_MARKER gOrigCommandListSetMarker = nullptr;
static PFN_COMMAND_LIST_BEGIN_EVENT gOrigCommandListBeginEvent = nullptr;
static PFN_COMMAND_LIST_END_EVENT gOrigCommandListEndEvent = nullptr;
static PFN_EXECUTE_INDIRECT gOrigExecuteIndirect = nullptr;
static SRWLOCK gIaReplacementDedupeLock = SRWLOCK_INIT;
static std::unordered_set<uint64_t> gAutoIaReplacementSeen;
static LONG gAutoIaReplacementSeenPresent = -1;
static SRWLOCK gIaReplacementSummaryLock = SRWLOCK_INIT;
static LONG gIaReplacementSummaryPresent = -1;
static volatile LONG gIaReplacementSummaryDraws = 0;
static volatile LONG gIaReplacementSummaryIndexedDraws = 0;
static volatile LONG gIaReplacementSummaryDispatches = 0;

// --- Submit-path hook/registration dedupe ---------------------------------
//
// ID3D12CommandQueue::ExecuteCommandLists is called every frame, often with
// many command lists, and previously re-ran the full hook + lifecycle
// registration for every list on every submit. Each re-run took ~21 global
// exclusive SRW locks inside DX12HookFunction (one per vtable slot) plus extra
// COM QueryInterface calls, all serialized across the recording worker
// threads. That CPU stall on the submit path starved the GPU and is the main
// cause of the heavy frame drops / low GPU utilization.
//
// Two facts let us short-circuit cheaply and safely:
//   1. All graphics command lists created from the same device share ONE
//      vtable, so the vtable only needs hooking once. We dedupe by vtable
//      pointer, turning every later list into a single shared-lock lookup.
//   2. Lifecycle/runtime registration is idempotent, so once a command-list
//      pointer has been fully registered we can skip the whole body.
//
// Both registries use an SRW lock in shared (reader) mode on the steady-state
// hot path, allowing concurrent worker threads to proceed without contention;
// the exclusive lock is only taken on the rare first-time insert.
static SRWLOCK gHookedVTableLock = SRWLOCK_INIT;
static std::unordered_set<void*> gHookedCommandListVTables;
static SRWLOCK gRegisteredListLock = SRWLOCK_INIT;
static std::unordered_set<void*> gRegisteredCommandLists;

// Returns true if this vtable has not been hooked before (and records it).
// A shared-lock fast path keeps concurrent submits lock-free in steady state.
static bool ShouldHookCommandListVTable(void *vtable)
{
	if (!vtable)
		return false;

	AcquireSRWLockShared(&gHookedVTableLock);
	const bool alreadyHooked =
		gHookedCommandListVTables.find(vtable) != gHookedCommandListVTables.end();
	ReleaseSRWLockShared(&gHookedVTableLock);
	if (alreadyHooked)
		return false;

	bool shouldHook = false;
	AcquireSRWLockExclusive(&gHookedVTableLock);
	// Re-check under the exclusive lock to avoid a race where two threads both
	// missed in the shared section.
	shouldHook = gHookedCommandListVTables.insert(vtable).second;
	ReleaseSRWLockExclusive(&gHookedVTableLock);
	return shouldHook;
}

// Returns true the first time a command-list pointer is seen (and records it),
// so the expensive submit-path registration body runs at most once per list.
static bool ShouldRegisterCommandListPointer(void *commandList)
{
	if (!commandList)
		return false;

	AcquireSRWLockShared(&gRegisteredListLock);
	const bool alreadyRegistered =
		gRegisteredCommandLists.find(commandList) != gRegisteredCommandLists.end();
	ReleaseSRWLockShared(&gRegisteredListLock);
	if (alreadyRegistered)
		return false;

	bool shouldRegister = false;
	AcquireSRWLockExclusive(&gRegisteredListLock);
	shouldRegister = gRegisteredCommandLists.insert(commandList).second;
	ReleaseSRWLockExclusive(&gRegisteredListLock);
	return shouldRegister;
}

// Drop a recycled command-list pointer so a future list reusing the same
// address is registered (and re-hooked if its vtable differs) correctly.
static void ForgetRegisteredCommandListPointer(void *commandList)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gRegisteredListLock);
	gRegisteredCommandLists.erase(commandList);
	ReleaseSRWLockExclusive(&gRegisteredListLock);
}

static bool DX12ShouldTrackComputeBindingsForPreSkin(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || !DX12ModNeedsPreSkinningUavProbe())
		return false;

	ID3D12PipelineState *pipelineState =
		DX12CommandListRuntimeGetPipelineState(commandList);
	DX12PsoShaderInfo shaderInfo = {};
	return pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) &&
		shaderInfo.hasCS &&
		DX12ModShouldTrackPreSkinBindingsForCs(shaderInfo.cs);
}

static void UpdateIaTextureCandidateFlag(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || !DX12ModHasActiveTextureOverrides()) {
		DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, false);
		return;
	}

	DX12IaHashState iaState;
	bool mayHaveCandidate = false;
	if (DX12HuntGetIaHashState(commandList, &iaState)) {
		if (iaState.hasIndexBuffer) {
			mayHaveCandidate = DX12ModIaHashMayHaveTextureOverrideCandidate(
				iaState.indexHash, true, 0);
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			if (mayHaveCandidate)
				break;
			mayHaveCandidate = DX12ModIaHashMayHaveTextureOverrideCandidate(
				buffer.hash, false, buffer.slot);
		}
	}
	DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, mayHaveCandidate);
}

static bool TestIaTextureCandidateHash(uint32_t hash, bool indexed, UINT slot)
{
	if (!hash)
		return false;
	const bool hit = DX12ModIaHashMayHaveTextureOverrideCandidate(hash, indexed, slot) ||
		DX12ModIaHashMayHaveTextureOverrideCandidate(0, indexed, slot);
	DX12Profiling::RecordIaCandidateTest(hit);
	return hit;
}

static bool UpdateIaTextureCandidateFromIndexView(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList || !DX12ModHasActiveTextureOverrides()) {
		DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, false);
		return false;
	}

	uint32_t hash = 0;
	const bool candidate =
		DX12HuntHashIndexBufferView(view, &hash) &&
		TestIaTextureCandidateHash(hash, true, 0);
	if (candidate)
		DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, true);
	return candidate;
}

static bool UpdateIaTextureCandidateFromVertexViews(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || !DX12ModHasActiveTextureOverrides()) {
		DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, false);
		return false;
	}

	bool candidate = false;
	for (UINT i = 0; i < count; ++i) {
		uint32_t hash = 0;
		const UINT slot = startSlot + i;
		if (DX12HuntHashVertexBufferView(slot, views ? &views[i] : nullptr, &hash) &&
		    TestIaTextureCandidateHash(hash, false, slot)) {
			candidate = true;
			break;
		}
	}
	if (candidate)
		DX12CommandListRuntimeSetMayHaveIaTextureCandidate(commandList, true);
	return candidate;
}

static bool DX12ShouldBypassIaTextureDrawWork(ID3D12GraphicsCommandList *commandList)
{
	return DX12ModHasActiveTextureOverrides() &&
		!DX12ModHasActiveShaderOverrides() &&
		!DX12ModNeedsPresentReplacement() &&
		!DX12HuntIsEnabled() &&
		!DX12CommandListRuntimeMayHaveIaTextureCandidate(commandList);
}

static void SyncHuntIaFromRuntimeState(
	ID3D12GraphicsCommandList *commandList, const DX12ActiveIaState &ia)
{
	DX12Profiling::RecordIaHuntIaUpdate();
	DX12HuntSetIndexBuffer(commandList, ia.hasIndexBuffer ? &ia.indexBuffer : nullptr);

	// Candidate windows must be exact, including slots that became unbound.
	// Sending the full 32-slot IA view snapshot prevents stale Hunt VB hashes
	// from surviving after the game clears or reuses a slot.
	D3D12_VERTEX_BUFFER_VIEW views[ARRAYSIZE(ia.vertexBuffers)] = {};
	for (UINT i = 0; i < ARRAYSIZE(ia.vertexBuffers); ++i)
		views[i] = ia.hasVertexBuffer[i] ? ia.vertexBuffers[i] : D3D12_VERTEX_BUFFER_VIEW();
	DX12Profiling::RecordIaHuntIaUpdate();
	DX12HuntSetVertexBuffers(commandList, 0, ARRAYSIZE(views), views);
}

static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
{
	if (DX12IsInternalReplay())
		return;

	char detail[1024];
	detail[0] = '\0';
	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(detail, sizeof(detail), fmt, args);
		va_end(args);
	}

	char apiJson[256];
	char detailJson[2048];
	DX12JsonEscapeString(apiJson, sizeof(apiJson), api ? api : "");
	DX12JsonEscapeString(detailJson, sizeof(detailJson), detail);
	DX12LogDebugJsonFunc("DX12HookCall",
		"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
		apiJson, DX12GetPresentCount(), object, detailJson);
}

#if defined(_DEBUG)
#define DX12_DEBUG_VERBOSE_LOG(stmt) do { stmt; } while (0)
#else
#define DX12_DEBUG_VERBOSE_LOG(stmt) do {} while (0)
#endif

static void FlushIaReplacementExecuteSummaryIfNeeded()
{
#if defined(_DEBUG)
	// Keep IA replacement execution summaries disabled by default on the hot
	// path. Per-frame JSON logging can dominate CPU time in debug builds and
	// make the GPU look underfed while replacement itself is inexpensive.
	constexpr bool kLogIaReplacementExecuteSummary = false;
	if (!kLogIaReplacementExecuteSummary)
		return;
	const LONG present = DX12GetPresentCount();
	if (gIaReplacementSummaryPresent == present)
		return;

	AcquireSRWLockExclusive(&gIaReplacementSummaryLock);
	if (gIaReplacementSummaryPresent != present) {
		const LONG previousPresent = gIaReplacementSummaryPresent;
		const LONG draws = InterlockedExchange(&gIaReplacementSummaryDraws, 0);
		const LONG indexedDraws = InterlockedExchange(&gIaReplacementSummaryIndexedDraws, 0);
		const LONG dispatches = InterlockedExchange(&gIaReplacementSummaryDispatches, 0);
		if (previousPresent >= 0 && (draws || dispatches)) {
			DX12LogDebugJsonFunc("DX12IaReplacementExecuteSummary",
				"\"present\":%ld,\"draws\":%ld,\"indexedDraws\":%ld,\"dispatches\":%ld",
				previousPresent, draws, indexedDraws, dispatches);
		}
		gIaReplacementSummaryPresent = present;
	}
	ReleaseSRWLockExclusive(&gIaReplacementSummaryLock);
#endif
}

static void RecordIaReplacementDrawExecute(bool indexed)
{
	DX12Profiling::RecordIaReplacementDraw(indexed);
#if defined(_DEBUG)
	FlushIaReplacementExecuteSummaryIfNeeded();
	InterlockedIncrement(&gIaReplacementSummaryDraws);
	if (indexed)
		InterlockedIncrement(&gIaReplacementSummaryIndexedDraws);
#endif
}

static void RecordIaReplacementDispatchExecute()
{
	DX12Profiling::RecordIaReplacementDispatch();
#if defined(_DEBUG)
	FlushIaReplacementExecuteSummaryIfNeeded();
	InterlockedIncrement(&gIaReplacementSummaryDispatches);
#endif
}

static void ResetAutoIaReplacementDedupe()
{
	AcquireSRWLockExclusive(&gIaReplacementDedupeLock);
	gAutoIaReplacementSeen.clear();
	gAutoIaReplacementSeenPresent = DX12GetPresentCount();
	ReleaseSRWLockExclusive(&gIaReplacementDedupeLock);
}

static uint64_t HashCombine64(uint64_t hash, uint64_t value)
{
	hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
	return hash;
}

static bool ShouldSuppressAutoIaReplacement(
	ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, int32_t baseVertex,
	uint32_t firstInstance, const DX12ModIaReplacement &replacement)
{
	if (!commandList || !replacement.skip)
		return false;
	if (!replacement.draws.empty() || !replacement.dispatches.empty() ||
	    !replacement.postCommandLists.empty())
		return false;

	ID3D12PipelineState *pipelineState = DX12CommandListRuntimeGetPipelineState(commandList);
	DX12PsoShaderInfo shaderInfo = {};
	DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo);

	uint64_t key = 14695981039346656037ull;
	key = HashCombine64(key, iaState.hasIndexBuffer ? iaState.indexHash : 0);
	key = HashCombine64(key, vertexCount);
	key = HashCombine64(key, indexCount);
	key = HashCombine64(key, instanceCount);
	key = HashCombine64(key, firstVertex);
	key = HashCombine64(key, firstIndex);
	key = HashCombine64(key, static_cast<uint32_t>(baseVertex));
	key = HashCombine64(key, firstInstance);
	key = HashCombine64(key, shaderInfo.hasVS ? shaderInfo.vs : 0);
	key = HashCombine64(key, shaderInfo.hasPS ? shaderInfo.ps : 0);
	key = HashCombine64(key, replacement.skip ? 1 : 0);
	key = HashCombine64(key, replacement.hasIndexBuffer ?
		replacement.indexBuffer.BufferLocation : 0);
	key = HashCombine64(key, replacement.hasIndexBuffer ?
		replacement.indexBuffer.SizeInBytes : 0);
	key = HashCombine64(key, replacement.vertexBufferStartSlot);
	key = HashCombine64(key, replacement.vertexBuffers.size());
	for (const D3D12_VERTEX_BUFFER_VIEW &vb : replacement.vertexBuffers) {
		key = HashCombine64(key, vb.BufferLocation);
		key = HashCombine64(key, vb.SizeInBytes);
		key = HashCombine64(key, vb.StrideInBytes);
	}
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		key = HashCombine64(key, draw.indexed ? 1 : 0);
		key = HashCombine64(key, draw.count);
		key = HashCombine64(key, draw.start);
		key = HashCombine64(key, static_cast<uint32_t>(draw.baseVertex));
		key = HashCombine64(key, draw.fromCaller ? 1 : 0);
	}
	for (const DX12ModIaReplacement::DispatchCall &dispatch : replacement.dispatches) {
		key = HashCombine64(key, dispatch.groupsX);
		key = HashCombine64(key, dispatch.groupsY);
		key = HashCombine64(key, dispatch.groupsZ);
	}
	for (const std::wstring &list : replacement.postCommandLists) {
		for (wchar_t ch : list)
			key = HashCombine64(key, static_cast<uint32_t>(ch));
	}

	const LONG present = DX12GetPresentCount();
	AcquireSRWLockExclusive(&gIaReplacementDedupeLock);
	if (gAutoIaReplacementSeenPresent != present) {
		gAutoIaReplacementSeen.clear();
		gAutoIaReplacementSeenPresent = present;
	}
	const bool inserted = gAutoIaReplacementSeen.insert(key).second;
	ReleaseSRWLockExclusive(&gIaReplacementDedupeLock);

#if defined(_DEBUG)
	if (!inserted) {
		DX12LogDebugJsonFunc("DX12IaReplacementSuppressed",
			"\"reason\":\"duplicate_auto_ia\",\"key\":\"%016llx\",\"ib\":\"%08x\",\"vertexCount\":%u,\"indexCount\":%u,\"instanceCount\":%u,\"firstVertex\":%u,\"firstIndex\":%u,\"draws\":%zu,\"pso\":\"%p\",\"vs\":\"%016llx\",\"ps\":\"%016llx\"",
			static_cast<unsigned long long>(key),
			iaState.hasIndexBuffer ? iaState.indexHash : 0,
			vertexCount, indexCount, instanceCount, firstVertex, firstIndex,
			replacement.draws.size(), pipelineState,
			static_cast<unsigned long long>(shaderInfo.hasVS ? shaderInfo.vs : 0),
			static_cast<unsigned long long>(shaderInfo.hasPS ? shaderInfo.ps : 0));
	}
#endif
	return !inserted;
}

static void FormatVertexBufferViews(
	char *text, size_t textCount, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!text || textCount == 0)
		return;
	text[0] = '\0';
	if (!views || count == 0)
		return;

	const UINT maxLogged = count > 8 ? 8 : count;
	size_t used = 0;
	for (UINT i = 0; i < maxLogged && used < textCount; ++i) {
		int written = sprintf_s(text + used, textCount - used,
			"%s%u:gpu=0x%llx,size=%u,stride=%u",
			i ? ";" : "",
			startSlot + i,
			static_cast<unsigned long long>(views[i].BufferLocation),
			views[i].SizeInBytes,
			views[i].StrideInBytes);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
	}
	if (count > maxLogged && used < textCount)
		sprintf_s(text + used, textCount - used, ";...");
}

struct ThreadLocalCommandListOriginal
{
	void *target = nullptr;
	void *original = nullptr;
};

static thread_local ThreadLocalCommandListOriginal tCommandListOriginals[64];
static thread_local ThreadLocalCommandListOriginal tCommandQueueOriginals[32];

template <typename T>
static T GetVTableOriginal(void *object, UINT slot, T fallback, const char *name)
{
	if (object) {
		void **vtable = *reinterpret_cast<void***>(object);
		if (vtable) {
			void *target = vtable[slot];
			void *original = DX12GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback)
		return fallback;

	DX12LogJsonFunc(name ? name : "D3D12::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		object, slot);
	return nullptr;
}

template <typename T>
static T GetCommandQueueOriginal(ID3D12CommandQueue *queue, UINT slot, T fallback, const char *name)
{
	if (queue) {
		void **vtable = *reinterpret_cast<void***>(queue);
		if (vtable) {
			void *target = vtable[slot];
			if (slot < ARRAYSIZE(tCommandQueueOriginals) &&
				tCommandQueueOriginals[slot].target == target &&
				tCommandQueueOriginals[slot].original)
				return reinterpret_cast<T>(tCommandQueueOriginals[slot].original);

			void *original = DX12GetOriginalFunction(target);
			if (slot < ARRAYSIZE(tCommandQueueOriginals) && original) {
				tCommandQueueOriginals[slot].target = target;
				tCommandQueueOriginals[slot].original = original;
			}
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback)
		return fallback;

	DX12LogJsonFunc(name ? name : "ID3D12CommandQueue::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		queue, slot);
	return nullptr;
}

static void LogQueueStage(
	const char *api, const char *stage, ID3D12CommandQueue *queue,
	UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK)
{
	DX12LogDebugJsonFuncFlush("DX12QueueStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
		"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\"",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), queue,
		count, fence, static_cast<unsigned long long>(value), hr);
}

template <typename T>
static T GetCommandListOriginal(
	ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
{
	if (commandList) {
		void **vtable = *reinterpret_cast<void***>(commandList);
		if (vtable) {
			void *target = vtable[slot];
			if (slot < ARRAYSIZE(tCommandListOriginals) &&
				tCommandListOriginals[slot].target == target &&
				tCommandListOriginals[slot].original)
				return reinterpret_cast<T>(tCommandListOriginals[slot].original);

			void *original = DX12GetOriginalFunction(target);
			if (slot < ARRAYSIZE(tCommandListOriginals) && original) {
				tCommandListOriginals[slot].target = target;
				tCommandListOriginals[slot].original = original;
			}
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback)
		return fallback;

	DX12LogJsonFunc(name ? name : "ID3D12GraphicsCommandList::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		commandList, slot);
	return nullptr;
}

#define DX12_CL_ORIG(commandList, slot, type, name) \
	GetCommandListOriginal<type>(commandList, slot, gOrig##name, "ID3D12GraphicsCommandList::" #name)

#define DX12_QUEUE_ORIG(queue, slot, type, name) \
	GetCommandQueueOriginal<type>(queue, slot, gOrigQueue##name, "ID3D12CommandQueue::" #name)

static DX12IaReplacementExecutorCallbacks MakeIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	DX12IaReplacementExecutorCallbacks callbacks;
	callbacks.drawInstanced =
		DX12_CL_ORIG(commandList, 12, DX12IaExecDrawInstanced, DrawInstanced);
	callbacks.drawIndexedInstanced =
		DX12_CL_ORIG(commandList, 13, DX12IaExecDrawIndexedInstanced, DrawIndexedInstanced);
	callbacks.dispatch =
		DX12_CL_ORIG(commandList, 14, DX12IaExecDispatch, Dispatch);
	callbacks.setIndexBuffer =
		DX12_CL_ORIG(commandList, 43, DX12IaExecSetIndexBuffer, IASetIndexBuffer);
	callbacks.setVertexBuffers =
		DX12_CL_ORIG(commandList, 44, DX12IaExecSetVertexBuffers, IASetVertexBuffers);
	callbacks.recordReplacementDraw = RecordIaReplacementDrawExecute;
	callbacks.recordReplacementDispatch = RecordIaReplacementDispatchExecute;
	callbacks.shouldSuppressAutoReplacement = ShouldSuppressAutoIaReplacement;
	return callbacks;
}

static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
		// Skip the whole hook + lifecycle registration body for command lists
		// we have already processed. ExecuteCommandLists re-submits the same
		// lists every frame, so without this guard each submit re-took dozens
		// of global exclusive locks per list and starved the GPU. The vtable
		// hook itself is independently deduped inside DX12HookCommandList, so a
		// recycled pointer that slips through still cannot double-hook.
		if (ShouldRegisterCommandListPointer(baseList)) {
			DX12HookCommandList(baseList);
			DX12CommandListLifecycleRegister(baseList, initialState);
		}
		baseList->Release();
	}
}

static void RegisterCommandQueue(IUnknown *commandQueue)
{
	DX12HookCommandQueue(commandQueue);
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
	ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue)
{
	LogDX12Call("ID3D12Device::CreateCommandQueue", device, " type=%d flags=0x%x",
		desc ? static_cast<int>(desc->Type) : -1,
		desc ? static_cast<UINT>(desc->Flags) : 0);
	HRESULT hr = gOrigCreateCommandQueue(device, desc, riid, commandQueue);
	DX12FrameAnalysisLogJsonFunc("ID3D12Device::CreateCommandQueue",
		"\"hr\":\"0x%lx\",\"queue\":\"%p\"",
		hr, commandQueue ? *commandQueue : nullptr);
	if (SUCCEEDED(hr) && commandQueue && *commandQueue)
		RegisterCommandQueue(static_cast<IUnknown*>(*commandQueue));
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandAllocator(
	ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator)
{
	LogDX12Call("ID3D12Device::CreateCommandAllocator", device, " type=%d",
		static_cast<int>(type));
	HRESULT hr = gOrigCreateCommandAllocator(device, type, riid, allocator);
	DX12FrameAnalysisLogJsonFunc("ID3D12Device::CreateCommandAllocator",
		"\"hr\":\"0x%lx\",\"allocator\":\"%p\"",
		hr, allocator ? *allocator : nullptr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandList(
	ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
	REFIID riid, void **commandList)
{
	LogDX12Call("ID3D12Device::CreateCommandList", device,
		" nodeMask=%u type=%d allocator=%p initialPso=%p",
		nodeMask, static_cast<int>(type), allocator, initialState);
	HRESULT hr = gOrigCreateCommandList(device, nodeMask, type, allocator, initialState, riid, commandList);
	DX12FrameAnalysisLogJsonFunc("ID3D12Device::CreateCommandList",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		// Creation is the authoritative registration point. Forget any stale
		// dedupe entry first so a recycled pointer is always re-registered
		// here with its real initial PSO (the submit-path dedupe only exists
		// to skip repeat work, never to suppress a genuine new list).
		ForgetRegisteredCommandListPointer(*commandList);
		RegisterCommandList(static_cast<IUnknown*>(*commandList), initialState);
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandList1(
	ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList)
{
	LogDX12Call("ID3D12Device4::CreateCommandList1", device,
		" nodeMask=%u type=%d flags=0x%x",
		nodeMask, static_cast<int>(type), static_cast<UINT>(flags));
	HRESULT hr = gOrigCreateCommandList1(device, nodeMask, type, flags, riid, commandList);
	DX12FrameAnalysisLogJsonFunc("ID3D12Device4::CreateCommandList1",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		// See HookedCreateCommandList: clear any stale dedupe entry so a
		// recycled pointer is re-registered fresh at creation time.
		ForgetRegisteredCommandListPointer(*commandList);
		RegisterCommandList(static_cast<IUnknown*>(*commandList), nullptr);
	}
	return hr;
}

static void STDMETHODCALLTYPE HookedQueueUpdateTileMappings(
	ID3D12CommandQueue *queue, ID3D12Resource *resource, UINT numResourceRegions,
	const D3D12_TILED_RESOURCE_COORDINATE *resourceRegionStartCoordinates,
	const D3D12_TILE_REGION_SIZE *resourceRegionSizes, ID3D12Heap *heap, UINT numRanges,
	const D3D12_TILE_RANGE_FLAGS *rangeFlags, const UINT *heapRangeStartOffsets,
	const UINT *rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags)
{
	LogDX12Call("ID3D12CommandQueue::UpdateTileMappings", queue,
		" resource=%p regions=%u heap=%p ranges=%u flags=0x%x",
		resource, numResourceRegions, heap, numRanges, static_cast<UINT>(flags));
	PFN_QUEUE_UPDATE_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, 8, PFN_QUEUE_UPDATE_TILE_MAPPINGS, UpdateTileMappings);
	if (original)
		original(queue, resource, numResourceRegions,
			resourceRegionStartCoordinates, resourceRegionSizes, heap, numRanges,
			rangeFlags, heapRangeStartOffsets, rangeTileCounts, flags);
}

static void STDMETHODCALLTYPE HookedQueueCopyTileMappings(
	ID3D12CommandQueue *queue, ID3D12Resource *dstResource,
	const D3D12_TILED_RESOURCE_COORDINATE *dstRegionStartCoordinate,
	ID3D12Resource *srcResource, const D3D12_TILED_RESOURCE_COORDINATE *srcRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *regionSize, D3D12_TILE_MAPPING_FLAGS flags)
{
	LogDX12Call("ID3D12CommandQueue::CopyTileMappings", queue,
		" dst=%p src=%p flags=0x%x", dstResource, srcResource, static_cast<UINT>(flags));
	PFN_QUEUE_COPY_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, 9, PFN_QUEUE_COPY_TILE_MAPPINGS, CopyTileMappings);
	if (original)
		original(queue, dstResource, dstRegionStartCoordinate,
			srcResource, srcRegionStartCoordinate, regionSize, flags);
}

static void STDMETHODCALLTYPE HookedQueueExecuteCommandLists(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	DX12_PROFILE_SCOPE(ExecuteCommandLists);
	LogDX12Call("ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
	for (UINT i = 0; commandLists && i < numCommandLists; ++i)
		RegisterCommandList(commandLists[i], nullptr);
	PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
		DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, ExecuteCommandLists);
	if (original) {
		LogQueueStage("ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		original(queue, numCommandLists, commandLists);
		LogQueueStage("ExecuteCommandLists", "afterOriginal", queue, numCommandLists);
	}
}

static void STDMETHODCALLTYPE HookedQueueSetMarker(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	LogDX12Call("ID3D12CommandQueue::SetMarker", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_SET_MARKER original =
		DX12_QUEUE_ORIG(queue, 11, PFN_QUEUE_SET_MARKER, SetMarker);
	if (original)
		original(queue, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedQueueBeginEvent(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	LogDX12Call("ID3D12CommandQueue::BeginEvent", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_BEGIN_EVENT original =
		DX12_QUEUE_ORIG(queue, 12, PFN_QUEUE_BEGIN_EVENT, BeginEvent);
	if (original)
		original(queue, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedQueueEndEvent(ID3D12CommandQueue *queue)
{
	LogDX12Call("ID3D12CommandQueue::EndEvent", queue);
	PFN_QUEUE_END_EVENT original =
		DX12_QUEUE_ORIG(queue, 13, PFN_QUEUE_END_EVENT, EndEvent);
	if (original)
		original(queue);
}

static HRESULT STDMETHODCALLTYPE HookedQueueSignal(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	LogDX12Call("ID3D12CommandQueue::Signal", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_SIGNAL original =
		DX12_QUEUE_ORIG(queue, 14, PFN_QUEUE_SIGNAL, Signal);
	if (!original)
		return E_FAIL;
	LogQueueStage("Signal", "beforeOriginal", queue, 0, fence, value);
	HRESULT hr = original(queue, fence, value);
	LogQueueStage("Signal", "afterOriginal", queue, 0, fence, value, hr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueWait(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	LogDX12Call("ID3D12CommandQueue::Wait", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_WAIT original =
		DX12_QUEUE_ORIG(queue, 15, PFN_QUEUE_WAIT, Wait);
	if (!original)
		return E_FAIL;
	LogQueueStage("Wait", "beforeOriginal", queue, 0, fence, value);
	HRESULT hr = original(queue, fence, value);
	LogQueueStage("Wait", "afterOriginal", queue, 0, fence, value, hr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
	ID3D12CommandQueue *queue, UINT64 *frequency)
{
	LogDX12Call("ID3D12CommandQueue::GetTimestampFrequency", queue);
	PFN_QUEUE_GET_TIMESTAMP_FREQUENCY original =
		DX12_QUEUE_ORIG(queue, 16, PFN_QUEUE_GET_TIMESTAMP_FREQUENCY, GetTimestampFrequency);
	return original ? original(queue, frequency) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
	ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp)
{
	LogDX12Call("ID3D12CommandQueue::GetClockCalibration", queue);
	PFN_QUEUE_GET_CLOCK_CALIBRATION original =
		DX12_QUEUE_ORIG(queue, 17, PFN_QUEUE_GET_CLOCK_CALIBRATION, GetClockCalibration);
	return original ? original(queue, gpuTimestamp, cpuTimestamp) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookedCloseCommandList(ID3D12GraphicsCommandList *commandList)
{
	LogDX12Call("ID3D12GraphicsCommandList::Close", commandList);
	PFN_CLOSE_COMMAND_LIST original = DX12_CL_ORIG(commandList, 9, PFN_CLOSE_COMMAND_LIST, CloseCommandList);
	return original ? original(commandList) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookedResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
	ID3D12PipelineState *initialState)
{
	DX12_PROFILE_SCOPE(ResetCommandList);
	LogDX12Call("ID3D12GraphicsCommandList::Reset", commandList,
		" allocator=%p initialPso=%p", allocator, initialState);
	DX12CommandListLifecycleReset(commandList, initialState);
	// IA replacement dedupe is now cleared once per frame at Present time
	// (via tracking generation bump) instead of on every Reset.  Resetting
	// it here was redundant because the dedupe is keyed by present count,
	// which does not change across command-list resets within a frame.
	DX12CommandListRuntimeReset(commandList, initialState);
	PFN_RESET_COMMAND_LIST original = DX12_CL_ORIG(commandList, 10, PFN_RESET_COMMAND_LIST, ResetCommandList);
	return original ? original(commandList, allocator, initialState) : E_FAIL;
}

static void STDMETHODCALLTYPE HookedClearState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	LogDX12Call("ID3D12GraphicsCommandList::ClearState", commandList, " pso=%p", pipelineState);
	DX12CommandListLifecycleClearState(commandList, pipelineState);
	PFN_CLEAR_STATE original = DX12_CL_ORIG(commandList, 11, PFN_CLEAR_STATE, ClearState);
	if (original)
		original(commandList, pipelineState);
}

static void STDMETHODCALLTYPE HookedDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
	UINT startVertexLocation, UINT startInstanceLocation)
{
	DX12_PROFILE_SCOPE(DrawInstanced);

	// Fast-forward: when no mod/hunt/capture work is needed, skip ALL tracking
	// and call the original with a single predictable branch.
	if (gDX12HotPathSkipAll) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	if (DX12IaReplacementIsExecutingInternalDraw()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::DrawInstanced", commandList,
		" vertices=%u instances=%u startVertex=%u startInstance=%u",
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	// Use cached tracking predicates to avoid re-evaluating global flags on
	// every draw call.  The cache is refreshed at Reset time.
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
			startVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntProcessDraw(commandList, false))
			return;
	}
	if (!DX12DrawHookFlowNeedsModWork()) {
		// D3D12 draw calls only append commands to the current command list.
		// When no mod-side draw work is active, stay as thin as the API call
		// itself and avoid runtime-state snapshots on every draw.
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	if (DX12ShouldBypassIaTextureDrawWork(commandList)) {
		// IASet hooks keep a cheap "may match" bit for texture overrides.
		// D3D12 Draw* calls only record draw commands, so negative IA candidates
		// can bypass the heavier replacement flow without changing command order.
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	const DX12CommandListRuntimeState runtimeState = DX12CommandListRuntimeGetState(commandList);
	const DX12IaReplacementExecutorCallbacks iaCallbacks =
		MakeIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = {};
	draw.vertexCount = vertexCountPerInstance;
	draw.instanceCount = instanceCount;
	draw.firstVertex = startVertexLocation;
	draw.firstInstance = startInstanceLocation;
	DX12DrawHookFlowExecute(commandList, draw, runtimeState, iaCallbacks);
}

static void STDMETHODCALLTYPE HookedDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
	UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
	DX12_PROFILE_SCOPE(DrawIndexedInstanced);

	// Fast-forward: when no mod/hunt/capture work is needed, skip ALL tracking.
	if (gDX12HotPathSkipAll) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}

	if (DX12IaReplacementIsExecutingInternalDraw()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::DrawIndexedInstanced", commandList,
		" indices=%u instances=%u startIndex=%u baseVertex=%d startInstance=%u",
		indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation,
		startInstanceLocation);
	// Use cached tracking predicates to avoid re-evaluating global flags on
	// every draw call.  The cache is refreshed at Reset time.
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
			startIndexLocation, baseVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntProcessDraw(commandList, true))
			return;
	}
	if (!DX12DrawHookFlowNeedsModWork()) {
		// Keep the common no-override path lock-free after hunting decisions.
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	if (DX12ShouldBypassIaTextureDrawWork(commandList)) {
		// IASet-derived negative candidate state avoids per-draw IA hash reads
		// for the overwhelmingly common non-target mesh path.
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	const DX12CommandListRuntimeState runtimeState = DX12CommandListRuntimeGetState(commandList);
	const DX12IaReplacementExecutorCallbacks iaCallbacks =
		MakeIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = {};
	draw.indexed = true;
	draw.indexCount = indexCountPerInstance;
	draw.instanceCount = instanceCount;
	draw.firstIndex = startIndexLocation;
	draw.baseVertex = baseVertexLocation;
	draw.firstInstance = startInstanceLocation;
	DX12DrawHookFlowExecute(commandList, draw, runtimeState, iaCallbacks);
}

static void STDMETHODCALLTYPE HookedDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	DX12_PROFILE_SCOPE(Dispatch);
	if (DX12IsInternalReplay()) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	// Fast-forward: when no mod/hunt/capture work is needed, skip ALL tracking.
	if (gDX12HotPathSkipAll) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::Dispatch", commandList,
		" groups=%u,%u,%u", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	// Use cached tracking predicate to avoid re-evaluating global flags on
	// every dispatch call.  The cache is refreshed at Reset time.
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntProcessDispatch(commandList))
			return;
	}
	PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
	if (!DX12ModHasActiveShaderOverrides() && !DX12ModNeedsPreSkinningUavProbe()) {
		// Dispatch only records compute work into the current D3D12 command list.
		// When no compute override or UAV pre-skin probe can use our tracked state,
		// avoid the runtime-state lock/hash lookup on this high-frequency path.
		DX12_PROFILE_FAST_FORWARD();
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	if (!DX12ModHasActiveShaderOverrides() && DX12ModNeedsPreSkinningUavProbe()) {
		ID3D12PipelineState *pipelineState =
			DX12CommandListRuntimeGetPipelineState(commandList);
		DX12PsoShaderInfo shaderInfo = {};
		const bool hasConfiguredPreSkinCs =
			pipelineState &&
			DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) &&
			shaderInfo.hasCS &&
			DX12ModShouldTrackPreSkinBindingsForCs(shaderInfo.cs);
		if (!hasConfiguredPreSkinCs) {
			// Most game dispatches are unrelated to pre-skinning. D3D12 command
			// lists record dispatches sequentially, so probing every compute
			// PSO in a compute-heavy scene can dominate the frame even though no
			// replacement can match. Only enter the heavy dispatch flow when the
			// current CS is one of the configured pre-skin candidates.
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
	}
	const DX12CommandListRuntimeState runtimeState = DX12CommandListRuntimeGetState(commandList);
	DX12DispatchHookFlowExecute(
		commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
		runtimeState, original);
}

static void STDMETHODCALLTYPE HookedCopyBufferRegion(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
	ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes)
{
	LogDX12Call("ID3D12GraphicsCommandList::CopyBufferRegion", commandList,
		" dst=%p dstOffset=%llu src=%p srcOffset=%llu bytes=%llu",
		dstBuffer, static_cast<unsigned long long>(dstOffset), srcBuffer,
		static_cast<unsigned long long>(srcOffset), static_cast<unsigned long long>(numBytes));
	gOrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
}

static void STDMETHODCALLTYPE HookedCopyTextureRegion(
	ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
	UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	LogDX12Call("ID3D12GraphicsCommandList::CopyTextureRegion", commandList,
		" dst=%p dstXYZ=%u,%u,%u src=%p srcBox=%p", dst, dstX, dstY, dstZ, src, srcBox);
	gOrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
}

static void STDMETHODCALLTYPE HookedCopyResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource)
{
	LogDX12Call("ID3D12GraphicsCommandList::CopyResource", commandList,
		" dst=%p src=%p", dstResource, srcResource);
	gOrigCopyResource(commandList, dstResource, srcResource);
}

static void STDMETHODCALLTYPE HookedCopyTiles(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
	const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
	UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags)
{
	LogDX12Call("ID3D12GraphicsCommandList::CopyTiles", commandList,
		" tiled=%p buffer=%p offset=%llu flags=0x%x", tiledResource, buffer,
		static_cast<unsigned long long>(bufferStartOffsetInBytes), static_cast<UINT>(flags));
	gOrigCopyTiles(commandList, tiledResource, tileRegionStartCoordinate,
		tileRegionSize, buffer, bufferStartOffsetInBytes, flags);
}

static void STDMETHODCALLTYPE HookedResolveSubresource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
	ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format)
{
	LogDX12Call("ID3D12GraphicsCommandList::ResolveSubresource", commandList,
		" dst=%p dstSub=%u src=%p srcSub=%u format=%u",
		dstResource, dstSubresource, srcResource, srcSubresource, static_cast<UINT>(format));
	gOrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
}

static void STDMETHODCALLTYPE HookedIASetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	DX12_PROFILE_SCOPE(IASetPrimitiveTopology);

	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
			" topology=%u", static_cast<UINT>(topology));
		DX12CommandListCapturePrimitiveTopology(commandList, topology);
	} else {
		if (DX12ModHasActiveTextureOverrides() || DX12HuntIsEnabled())
			DX12CommandListRuntimeRememberPrimitiveTopology(commandList, topology);
	}

	PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
		DX12_CL_ORIG(commandList, 20, PFN_IA_SET_PRIMITIVE_TOPOLOGY, IASetPrimitiveTopology);
	if (original)
		original(commandList, topology);
}

static void STDMETHODCALLTYPE HookedRSSetViewports(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports)
{
	LogDX12Call("ID3D12GraphicsCommandList::RSSetViewports", commandList, " count=%u", count);
	gOrigRSSetViewports(commandList, count, viewports);
}

static void STDMETHODCALLTYPE HookedRSSetScissorRects(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects)
{
	LogDX12Call("ID3D12GraphicsCommandList::RSSetScissorRects", commandList, " count=%u", count);
	gOrigRSSetScissorRects(commandList, count, rects);
}

static void STDMETHODCALLTYPE HookedOMSetBlendFactor(
	ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4])
{
	LogDX12Call("ID3D12GraphicsCommandList::OMSetBlendFactor", commandList);
	gOrigOMSetBlendFactor(commandList, blendFactor);
}

static void STDMETHODCALLTYPE HookedOMSetStencilRef(ID3D12GraphicsCommandList *commandList, UINT stencilRef)
{
	LogDX12Call("ID3D12GraphicsCommandList::OMSetStencilRef", commandList, " stencilRef=%u", stencilRef);
	gOrigOMSetStencilRef(commandList, stencilRef);
}

static void STDMETHODCALLTYPE HookedSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12_PROFILE_SCOPE(SetPipelineState);

	// Fast-forward: skip PSO replacement and all tracking when idle.
	if (gDX12HotPathSkipAll) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_PIPELINE_STATE original = DX12_CL_ORIG(commandList, 25, PFN_SET_PIPELINE_STATE, SetPipelineState);
		if (original)
			original(commandList, pipelineState);
		return;
	}

	ID3D12PipelineState *activePipelineState =
		DX12CommandListCapturePipelineState(commandList, pipelineState);
	LogDX12Call("ID3D12GraphicsCommandList::SetPipelineState", commandList,
		" pso=%p active=%p", pipelineState, activePipelineState);
	PFN_SET_PIPELINE_STATE original = DX12_CL_ORIG(commandList, 25, PFN_SET_PIPELINE_STATE, SetPipelineState);
	if (original)
		original(commandList, activePipelineState);
}

static void STDMETHODCALLTYPE HookedResourceBarrier(
	ID3D12GraphicsCommandList *commandList, UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers)
{
	DX12_PROFILE_SCOPE(ResourceBarrier);
	if (DX12IsInternalReplay()) {
		PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, ResourceBarrier);
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, ResourceBarrier);
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::ResourceBarrier", commandList, " count=%u", numBarriers);
	DX12CommandListCommandRecordResourceBarrier(numBarriers, barriers);
	PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, ResourceBarrier);
	if (original)
		original(commandList, numBarriers, barriers);
}

static void STDMETHODCALLTYPE HookedExecuteBundle(
	ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle)
{
	LogDX12Call("ID3D12GraphicsCommandList::ExecuteBundle", commandList, " bundle=%p", bundle);
	DX12CommandListCommandRecordExecuteBundle(bundle, nullptr);
	PFN_EXECUTE_BUNDLE original = DX12_CL_ORIG(commandList, 27, PFN_EXECUTE_BUNDLE, ExecuteBundle);
	if (original)
		original(commandList, bundle);
}

static void STDMETHODCALLTYPE HookedSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	DX12_PROFILE_SCOPE(SetDescriptorHeaps);
	if (DX12IsInternalReplay()) {
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
		if (original)
			original(commandList, count, heaps);
		return;
	}

	// Always track descriptor-heap pointers in RuntimeState — two unconditional
	// pointer writes on the TLS fast path.  Texture-override matching reads
	// them without activating the full BindingTracker (the former #1 CPU sink).
	ID3D12DescriptorHeap *cbvSrvUav = nullptr;
	ID3D12DescriptorHeap *sampler = nullptr;
	if (heaps) {
		for (UINT i = 0; i < count; ++i) {
			if (!heaps[i])
				continue;
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				cbvSrvUav = heaps[i];
			else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				sampler = heaps[i];
		}
	}
	DX12CommandListRuntimeSetDescriptorHeaps(commandList, cbvSrvUav, sampler);

	// Fast-forward: skip the heavy BindingTracker whenever binding capture is
	// disabled. SetDescriptorHeaps is a frequent D3D12 command-list binding
	// call, and texture override matching only needs the lightweight runtime
	// heap pointers written above.
	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
		if (original)
			original(commandList, count, heaps);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList, " count=%u", count);
	DX12CommandListCaptureDescriptorHeaps(commandList, count, heaps);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_DESCRIPTOR_HEAPS original =
		DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
	if (original)
		original(commandList, count, heaps);
}

static void STDMETHODCALLTYPE HookedSetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12_PROFILE_SCOPE(SetComputeRootSignature);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		// Pre-skinning match_cs probing reads compute root state from the
		// BindingTracker. Keep that tracker warm only while the current compute
		// PSO is one of the configured pre-skin candidates; unrelated compute
		// passes must stay as thin as the native D3D12 binding call.
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList))
			DX12BindingSetComputeRootSignature(commandList, rootSignature);
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 29, PFN_SET_ROOT_SIGNATURE, SetComputeRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootSignature", commandList,
		" rootSignature=%p", rootSignature);
	DX12CommandListCaptureComputeRootSignature(commandList, rootSignature);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_SIGNATURE original =
		DX12_CL_ORIG(commandList, 29, PFN_SET_ROOT_SIGNATURE, SetComputeRootSignature);
	if (original)
		original(commandList, rootSignature);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12_PROFILE_SCOPE(SetGraphicsRootSignature);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 30, PFN_SET_ROOT_SIGNATURE, SetGraphicsRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootSignature", commandList,
		" rootSignature=%p", rootSignature);
	DX12CommandListCaptureGraphicsRootSignature(commandList, rootSignature);
	PFN_SET_ROOT_SIGNATURE original =
		DX12_CL_ORIG(commandList, 30, PFN_SET_ROOT_SIGNATURE, SetGraphicsRootSignature);
	if (original)
		original(commandList, rootSignature);
}

static void STDMETHODCALLTYPE HookedSetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
	if (DX12IsInternalReplay()) {
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList))
			DX12BindingSetComputeRootDescriptorTable(
				commandList, rootParameterIndex, baseDescriptor);
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootDescriptorTable", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex,
		static_cast<unsigned long long>(baseDescriptor.ptr));
	DX12CommandListCaptureComputeRootDescriptorTable(
		commandList, rootParameterIndex, baseDescriptor);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_DESCRIPTOR_TABLE original =
		DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
	if (original)
		original(commandList, rootParameterIndex, baseDescriptor);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 32, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetGraphicsRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex,
		static_cast<unsigned long long>(baseDescriptor.ptr));
	DX12CommandListCaptureGraphicsRootDescriptorTable(
		commandList, rootParameterIndex, baseDescriptor);
	PFN_SET_ROOT_DESCRIPTOR_TABLE original =
		DX12_CL_ORIG(commandList, 32, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetGraphicsRootDescriptorTable);
	if (original)
		original(commandList, rootParameterIndex, baseDescriptor);
}

static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList)) {
			// Some pre-skin producers drive dispatch selection through root
			// constants only, so candidate compute PSOs still need these values.
			DX12BindingSetComputeRoot32BitConstant(
				commandList, rootParameterIndex, destOffset, srcData);
		}
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, 33, PFN_SET_ROOT_32BIT_CONSTANT, SetComputeRoot32BitConstant);
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRoot32BitConstant", commandList,
		" root=%u destOffset=%u", rootParameterIndex, destOffset);
	DX12CommandListCaptureComputeRoot32BitConstant(
		commandList, rootParameterIndex, srcData, destOffset);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_32BIT_CONSTANT original =
		DX12_CL_ORIG(commandList, 33, PFN_SET_ROOT_32BIT_CONSTANT, SetComputeRoot32BitConstant);
	if (original)
		original(commandList, rootParameterIndex, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant", commandList,
		" root=%u destOffset=%u", rootParameterIndex, destOffset);
	DX12CommandListCaptureGraphicsRoot32BitConstant(
		commandList, rootParameterIndex, srcData, destOffset);
	PFN_SET_ROOT_32BIT_CONSTANT original =
		DX12_CL_ORIG(commandList, 34, PFN_SET_ROOT_32BIT_CONSTANT, SetGraphicsRoot32BitConstant);
	if (original)
		original(commandList, rootParameterIndex, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList)) {
			// Mirror candidate pre-skin root constants into the tracker; all
			// other compute passes avoid the lock-heavy tracker update.
			DX12BindingSetComputeRoot32BitConstants(
				commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
		}
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, 35, PFN_SET_ROOT_32BIT_CONSTANTS, SetComputeRoot32BitConstants);
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRoot32BitConstants", commandList,
		" root=%u values=%u destOffset=%u", rootParameterIndex, num32BitValuesToSet, destOffset);
	DX12CommandListCaptureComputeRoot32BitConstants(
		commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_32BIT_CONSTANTS original =
		DX12_CL_ORIG(commandList, 35, PFN_SET_ROOT_32BIT_CONSTANTS, SetComputeRoot32BitConstants);
	if (original)
		original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants", commandList,
		" root=%u values=%u destOffset=%u", rootParameterIndex, num32BitValuesToSet, destOffset);
	DX12CommandListCaptureGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	PFN_SET_ROOT_32BIT_CONSTANTS original =
		DX12_CL_ORIG(commandList, 36, PFN_SET_ROOT_32BIT_CONSTANTS, SetGraphicsRoot32BitConstants);
	if (original)
		original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetComputeRootConstantBufferView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList))
			DX12BindingSetComputeRootDescriptor(
				commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address);
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 37, PFN_SET_ROOT_GPU_VA, SetComputeRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootConstantBufferView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 37, PFN_SET_ROOT_GPU_VA, SetComputeRootConstantBufferView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootConstantBufferView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 38, PFN_SET_ROOT_GPU_VA, SetGraphicsRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureGraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 38, PFN_SET_ROOT_GPU_VA, SetGraphicsRootConstantBufferView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetComputeRootShaderResourceView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList))
			DX12BindingSetComputeRootDescriptor(
				commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address);
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 39, PFN_SET_ROOT_GPU_VA, SetComputeRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootShaderResourceView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 39, PFN_SET_ROOT_GPU_VA, SetComputeRootShaderResourceView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootShaderResourceView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 40, PFN_SET_ROOT_GPU_VA, SetGraphicsRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureGraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 40, PFN_SET_ROOT_GPU_VA, SetGraphicsRootShaderResourceView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetComputeRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ShouldTrackComputeBindingsForPreSkin(commandList))
			DX12BindingSetComputeRootDescriptor(
				commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 41, PFN_SET_ROOT_GPU_VA, SetComputeRootUnorderedAccessView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 41, PFN_SET_ROOT_GPU_VA, SetComputeRootUnorderedAccessView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);

	if (gDX12HotPathSkipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 42, PFN_SET_ROOT_GPU_VA, SetGraphicsRootUnorderedAccessView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureGraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 42, PFN_SET_ROOT_GPU_VA, SetGraphicsRootUnorderedAccessView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedIASetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	DX12_PROFILE_SCOPE(IASetIndexBuffer);

	// When gDX12HotPathSkipBindings is set, skip the heavy BindingTracker but
	// keep the lightweight RuntimeState tracking.  Texture-override matching
	// needs the current IB/VB state and reads it from RuntimeState (TLS path).
	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
			" gpu=0x%llx size=%u format=%u",
			view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
			view ? view->SizeInBytes : 0,
			view ? static_cast<UINT>(view->Format) : 0);
		DX12CommandListCaptureIndexBuffer(commandList, view);
	} else {
		// Fast path: only lightweight tracking for mod matching.
		// Texture override matching reads IA hashes from the Hunt module's
		// gCommandListIa AND buffer views from RuntimeState.  Both must be
		// updated even when the BindingTracker is skipped.
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ModHasActiveTextureOverrides()) {
			const bool hadCandidate =
				DX12CommandListRuntimeMayHaveIaTextureCandidate(commandList);
			DX12CommandListRuntimeRememberIndexBuffer(commandList, view);
			// D3D12 IASet* calls only record buffer views into the command list.
			// In the steady state most views cannot match any TextureOverride,
			// so hash this one view first and only populate Hunt's full IA map
			// when the hash can actually be consumed by the draw replacement path.
			if (DX12HuntIsEnabled()) {
				DX12Profiling::RecordIaHuntIaUpdate();
				DX12HuntSetIndexBuffer(commandList, view);
			} else if (hadCandidate ||
			    UpdateIaTextureCandidateFromIndexView(commandList, view)) {
				SyncHuntIaFromRuntimeState(
					commandList, DX12CommandListRuntimeGetState(commandList).ia);
				UpdateIaTextureCandidateFlag(commandList);
			}
		} else if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetIndexBuffer(commandList, view);
		}
	}

	PFN_IA_SET_INDEX_BUFFER original = DX12_CL_ORIG(commandList, 43, PFN_IA_SET_INDEX_BUFFER, IASetIndexBuffer);
	if (original)
		original(commandList, view);
}

static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	DX12_PROFILE_SCOPE(IASetVertexBuffers);

	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
			" start=%u count=%u", startSlot, count);
		DX12CommandListCaptureVertexBuffers(commandList, startSlot, count, views);
	} else {
		// Fast path: only lightweight tracking for mod matching.
		DX12_PROFILE_FAST_FORWARD();
		if (DX12ModHasActiveTextureOverrides()) {
			const bool hadCandidate =
				DX12CommandListRuntimeMayHaveIaTextureCandidate(commandList);
			DX12CommandListRuntimeRememberVertexBuffers(commandList, startSlot, count, views);
			// Keep the IASet hot path local unless the changed view is a real
			// TextureOverride candidate. Full Hunt IA state is still maintained
			// while shader hunting is active so manual selection remains exact.
			if (DX12HuntIsEnabled()) {
				DX12Profiling::RecordIaHuntIaUpdate();
				DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
			} else if (hadCandidate || UpdateIaTextureCandidateFromVertexViews(
				   commandList, startSlot, count, views)) {
				SyncHuntIaFromRuntimeState(
					commandList, DX12CommandListRuntimeGetState(commandList).ia);
				UpdateIaTextureCandidateFlag(commandList);
			}
		} else if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
		}
	}

	PFN_IA_SET_VERTEX_BUFFERS original =
		DX12_CL_ORIG(commandList, 44, PFN_IA_SET_VERTEX_BUFFERS, IASetVertexBuffers);
	if (original)
		original(commandList, startSlot, count, views);
}

static void STDMETHODCALLTYPE HookedSOSetTargets(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
	LogDX12Call("ID3D12GraphicsCommandList::SOSetTargets", commandList,
		" start=%u count=%u", startSlot, count);
	gOrigSOSetTargets(commandList, startSlot, count, views);
}

static void STDMETHODCALLTYPE HookedOMSetRenderTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	LogDX12Call("ID3D12GraphicsCommandList::OMSetRenderTargets", commandList,
		" rtCount=%u singleRange=%u dsv=%p", numRenderTargetDescriptors,
		singleHandleToDescriptorRange, depthStencilDescriptor);
	gOrigOMSetRenderTargets(commandList, numRenderTargetDescriptors,
		renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
}

static void STDMETHODCALLTYPE HookedClearDepthStencilView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
	D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects, const D3D12_RECT *rects)
{
	LogDX12Call("ID3D12GraphicsCommandList::ClearDepthStencilView", commandList,
		" dsv=0x%llx flags=0x%x rects=%u", static_cast<unsigned long long>(depthStencilView.ptr),
		static_cast<UINT>(clearFlags), numRects);
	gOrigClearDepthStencilView(commandList, depthStencilView, clearFlags, depth, stencil, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearRenderTargetView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects)
{
	LogDX12Call("ID3D12GraphicsCommandList::ClearRenderTargetView", commandList,
		" rtv=0x%llx rects=%u", static_cast<unsigned long long>(renderTargetView.ptr), numRects);
	gOrigClearRenderTargetView(commandList, renderTargetView, colorRGBA, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewUint(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	gOrigClearUnorderedAccessViewUint(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewFloat(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	gOrigClearUnorderedAccessViewFloat(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

static void STDMETHODCALLTYPE HookedDiscardResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
	LogDX12Call("ID3D12GraphicsCommandList::DiscardResource", commandList, " resource=%p region=%p", resource, region);
	gOrigDiscardResource(commandList, resource, region);
}

static void STDMETHODCALLTYPE HookedBeginQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	LogDX12Call("ID3D12GraphicsCommandList::BeginQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	gOrigBeginQuery(commandList, queryHeap, type, index);
}

static void STDMETHODCALLTYPE HookedEndQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	LogDX12Call("ID3D12GraphicsCommandList::EndQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	gOrigEndQuery(commandList, queryHeap, type, index);
}

static void STDMETHODCALLTYPE HookedResolveQueryData(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
	UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer, UINT64 alignedDestinationBufferOffset)
{
	LogDX12Call("ID3D12GraphicsCommandList::ResolveQueryData", commandList,
		" heap=%p type=%u start=%u count=%u dst=%p offset=%llu",
		queryHeap, static_cast<UINT>(type), startIndex, numQueries, destinationBuffer,
		static_cast<unsigned long long>(alignedDestinationBufferOffset));
	gOrigResolveQueryData(commandList, queryHeap, type, startIndex, numQueries,
		destinationBuffer, alignedDestinationBufferOffset);
}

static void STDMETHODCALLTYPE HookedSetPredication(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
	UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation)
{
	LogDX12Call("ID3D12GraphicsCommandList::SetPredication", commandList,
		" buffer=%p offset=%llu op=%u", buffer,
		static_cast<unsigned long long>(alignedBufferOffset), static_cast<UINT>(operation));
	gOrigSetPredication(commandList, buffer, alignedBufferOffset, operation);
}

static void STDMETHODCALLTYPE HookedCommandListSetMarker(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	LogDX12Call("ID3D12GraphicsCommandList::SetMarker", commandList, " metadata=%u size=%u", metadata, size);
	gOrigCommandListSetMarker(commandList, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedCommandListBeginEvent(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	LogDX12Call("ID3D12GraphicsCommandList::BeginEvent", commandList, " metadata=%u size=%u", metadata, size);
	gOrigCommandListBeginEvent(commandList, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList)
{
	LogDX12Call("ID3D12GraphicsCommandList::EndEvent", commandList);
	gOrigCommandListEndEvent(commandList);
}

static void STDMETHODCALLTYPE HookedExecuteIndirect(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
	UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
	ID3D12Resource *countBuffer, UINT64 countBufferOffset)
{
	LogDX12Call("ID3D12GraphicsCommandList::ExecuteIndirect", commandList,
		" signature=%p max=%u args=%p argOffset=%llu count=%p countOffset=%llu",
		commandSignature, maxCommandCount, argumentBuffer,
		static_cast<unsigned long long>(argumentBufferOffset), countBuffer,
		static_cast<unsigned long long>(countBufferOffset));
	DX12CommandListCommandRecordExecuteIndirect(commandList);
	gOrigExecuteIndirect(commandList, commandSignature, maxCommandCount, argumentBuffer,
		argumentBufferOffset, countBuffer, countBufferOffset);
}

void DX12HookCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	DX12SetCommandQueue(queue);
	DX12VTableHook queueHooks[] = {
		{8, reinterpret_cast<void**>(&gOrigQueueUpdateTileMappings),
			HookedQueueUpdateTileMappings, "ID3D12CommandQueue::UpdateTileMappings"},
		{9, reinterpret_cast<void**>(&gOrigQueueCopyTileMappings),
			HookedQueueCopyTileMappings, "ID3D12CommandQueue::CopyTileMappings"},
		{10, reinterpret_cast<void**>(&gOrigQueueExecuteCommandLists),
			HookedQueueExecuteCommandLists, "ID3D12CommandQueue::ExecuteCommandLists"},
		{11, reinterpret_cast<void**>(&gOrigQueueSetMarker),
			HookedQueueSetMarker, "ID3D12CommandQueue::SetMarker"},
		{12, reinterpret_cast<void**>(&gOrigQueueBeginEvent),
			HookedQueueBeginEvent, "ID3D12CommandQueue::BeginEvent"},
		{13, reinterpret_cast<void**>(&gOrigQueueEndEvent),
			HookedQueueEndEvent, "ID3D12CommandQueue::EndEvent"},
		{14, reinterpret_cast<void**>(&gOrigQueueSignal),
			HookedQueueSignal, "ID3D12CommandQueue::Signal"},
		{15, reinterpret_cast<void**>(&gOrigQueueWait),
			HookedQueueWait, "ID3D12CommandQueue::Wait"},
		{16, reinterpret_cast<void**>(&gOrigQueueGetTimestampFrequency),
			HookedQueueGetTimestampFrequency, "ID3D12CommandQueue::GetTimestampFrequency"},
		{17, reinterpret_cast<void**>(&gOrigQueueGetClockCalibration),
			HookedQueueGetClockCalibration, "ID3D12CommandQueue::GetClockCalibration"},
	};
	DX12InstallVTableHooks(queue, queueHooks);
	queue->Release();
}

void DX12HookCommandList(IUnknown *commandList)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&baseList))))
		return;

	// All command lists from the same device share one vtable. Hook each
	// distinct vtable only once; subsequent lists hit the shared-lock fast
	// path in ShouldHookCommandListVTable and skip the ~21 expensive
	// per-slot hook attempts entirely.
	void *vtable = *reinterpret_cast<void**>(baseList);
	if (!ShouldHookCommandListVTable(vtable)) {
		baseList->Release();
		return;
	}

	DX12VTableHook commandListHooks[] = {
		{10, reinterpret_cast<void**>(&gOrigResetCommandList),
			HookedResetCommandList, "ID3D12GraphicsCommandList::Reset"},
		{12, reinterpret_cast<void**>(&gOrigDrawInstanced),
			HookedDrawInstanced, "ID3D12GraphicsCommandList::DrawInstanced"},
		{13, reinterpret_cast<void**>(&gOrigDrawIndexedInstanced),
			HookedDrawIndexedInstanced, "ID3D12GraphicsCommandList::DrawIndexedInstanced"},
		{14, reinterpret_cast<void**>(&gOrigDispatch),
			HookedDispatch, "ID3D12GraphicsCommandList::Dispatch"},
		{20, reinterpret_cast<void**>(&gOrigIASetPrimitiveTopology),
			HookedIASetPrimitiveTopology, "ID3D12GraphicsCommandList::IASetPrimitiveTopology"},
		{25, reinterpret_cast<void**>(&gOrigSetPipelineState),
			HookedSetPipelineState, "ID3D12GraphicsCommandList::SetPipelineState"},
		{26, reinterpret_cast<void**>(&gOrigResourceBarrier),
			HookedResourceBarrier, "ID3D12GraphicsCommandList::ResourceBarrier"},
		{27, reinterpret_cast<void**>(&gOrigExecuteBundle),
			HookedExecuteBundle, "ID3D12GraphicsCommandList::ExecuteBundle"},
		{28, reinterpret_cast<void**>(&gOrigSetDescriptorHeaps),
			HookedSetDescriptorHeaps, "ID3D12GraphicsCommandList::SetDescriptorHeaps"},
		{29, reinterpret_cast<void**>(&gOrigSetComputeRootSignature),
			HookedSetComputeRootSignature, "ID3D12GraphicsCommandList::SetComputeRootSignature"},
		{30, reinterpret_cast<void**>(&gOrigSetGraphicsRootSignature),
			HookedSetGraphicsRootSignature, "ID3D12GraphicsCommandList::SetGraphicsRootSignature"},
		{31, reinterpret_cast<void**>(&gOrigSetComputeRootDescriptorTable),
			HookedSetComputeRootDescriptorTable, "ID3D12GraphicsCommandList::SetComputeRootDescriptorTable"},
		{32, reinterpret_cast<void**>(&gOrigSetGraphicsRootDescriptorTable),
			HookedSetGraphicsRootDescriptorTable, "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable"},
		{37, reinterpret_cast<void**>(&gOrigSetComputeRootConstantBufferView),
			HookedSetComputeRootConstantBufferView, "ID3D12GraphicsCommandList::SetComputeRootConstantBufferView"},
		{38, reinterpret_cast<void**>(&gOrigSetGraphicsRootConstantBufferView),
			HookedSetGraphicsRootConstantBufferView, "ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView"},
		{39, reinterpret_cast<void**>(&gOrigSetComputeRootShaderResourceView),
			HookedSetComputeRootShaderResourceView, "ID3D12GraphicsCommandList::SetComputeRootShaderResourceView"},
		{40, reinterpret_cast<void**>(&gOrigSetGraphicsRootShaderResourceView),
			HookedSetGraphicsRootShaderResourceView, "ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView"},
		{41, reinterpret_cast<void**>(&gOrigSetComputeRootUnorderedAccessView),
			HookedSetComputeRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView"},
		{42, reinterpret_cast<void**>(&gOrigSetGraphicsRootUnorderedAccessView),
			HookedSetGraphicsRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView"},
		{43, reinterpret_cast<void**>(&gOrigIASetIndexBuffer),
			HookedIASetIndexBuffer, "ID3D12GraphicsCommandList::IASetIndexBuffer"},
		{44, reinterpret_cast<void**>(&gOrigIASetVertexBuffers),
			HookedIASetVertexBuffers, "ID3D12GraphicsCommandList::IASetVertexBuffers"},
		{59, reinterpret_cast<void**>(&gOrigExecuteIndirect),
			HookedExecuteIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect"},
	};
	DX12InstallVTableHooks(baseList, commandListHooks);
	baseList->Release();
}

void DX12HookCommandListCreation(IUnknown *device)
{
	if (!device)
		return;

	ID3D12Device *baseDevice = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&baseDevice)))) {
		DX12VTableHook deviceHooks[] = {
			{8, reinterpret_cast<void**>(&gOrigCreateCommandQueue),
				HookedCreateCommandQueue, "ID3D12Device::CreateCommandQueue"},
			{9, reinterpret_cast<void**>(&gOrigCreateCommandAllocator),
				HookedCreateCommandAllocator, "ID3D12Device::CreateCommandAllocator"},
			{12, reinterpret_cast<void**>(&gOrigCreateCommandList),
				HookedCreateCommandList, "ID3D12Device::CreateCommandList"},
		};
		DX12InstallVTableHooks(baseDevice, deviceHooks);
		baseDevice->Release();
	}

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{52, reinterpret_cast<void**>(&gOrigCreateCommandList1),
				HookedCreateCommandList1, "ID3D12Device4::CreateCommandList1"},
		};
		DX12InstallVTableHooks(device4, device4Hooks);
		device4->Release();
	}
}


