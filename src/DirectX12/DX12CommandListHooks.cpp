#include "DX12CommandListHooks.h"

#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <array>
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
static SRWLOCK gIaReplacementSummaryLock = SRWLOCK_INIT;
static LONG gIaReplacementSummaryPresent = -1;
static volatile LONG gIaReplacementSummaryDraws = 0;
static volatile LONG gIaReplacementSummaryIndexedDraws = 0;
static volatile LONG gIaReplacementSummaryDispatches = 0;

volatile LONG gPerFrameTotalDraws = 0;
volatile LONG gPerFrameTotalDispatches = 0;

static SRWLOCK gHookedVTableLock = SRWLOCK_INIT;
static std::unordered_set<void*> gHookedCommandListVTables;
static SRWLOCK gRegisteredListLock = SRWLOCK_INIT;
static std::unordered_set<void*> gRegisteredCommandLists;
static SRWLOCK gKnownCommandQueueLock = SRWLOCK_INIT;
static IUnknown *gKnownCommandQueue = nullptr;

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
	shouldHook = gHookedCommandListVTables.insert(vtable).second;
	ReleaseSRWLockExclusive(&gHookedVTableLock);
	return shouldHook;
}

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

static void ForgetRegisteredCommandListPointer(void *commandList)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&gRegisteredListLock);
	gRegisteredCommandLists.erase(commandList);
	ReleaseSRWLockExclusive(&gRegisteredListLock);
}

static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
{
#if defined(_DEBUG)
	if (!DX12ShouldLogHookCall(api))
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
#else
	(void)api;
	(void)object;
	(void)fmt;
#endif
}

#if defined(_DEBUG)
#define DX12_DEBUG_VERBOSE_LOG(stmt) do { stmt; } while (0)
#else
#define DX12_DEBUG_VERBOSE_LOG(stmt) do {} while (0)
#endif

static void FlushIaReplacementExecuteSummaryIfNeeded()
{
#if defined(_DEBUG)
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

static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
{
#if defined(_DEBUG)
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12FallbackPath",
		"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
		"\"slot\":%u,\"fallback\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object, slot, fallback);
#else
	(void)api;
	(void)object;
	(void)slot;
	(void)fallback;
#endif
}

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

	if (fallback) {
		LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}

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

			// Fast path: thread-local per-slot cache
			if (slot < ARRAYSIZE(tCommandQueueOriginals) &&
				tCommandQueueOriginals[slot].target == target &&
				tCommandQueueOriginals[slot].original)
				return reinterpret_cast<T>(tCommandQueueOriginals[slot].original);

			// Medium path: per-vtable bulk cache
			if (tVtableBaseCache.vtable == vtable && slot < 256 &&
			    tVtableBaseCache.originals[slot]) {
				void *original = tVtableBaseCache.originals[slot];
				if (slot < ARRAYSIZE(tCommandQueueOriginals)) {
					tCommandQueueOriginals[slot].target = target;
					tCommandQueueOriginals[slot].original = original;
				}
				return reinterpret_cast<T>(original);
			}

			// Slow path: hash map lookup
			void *original = DX12GetOriginalFunction(target);
			if (original) {
				if (slot < ARRAYSIZE(tCommandQueueOriginals)) {
					tCommandQueueOriginals[slot].target = target;
					tCommandQueueOriginals[slot].original = original;
				}
				if (tVtableBaseCache.vtable == vtable && slot < 256) {
					tVtableBaseCache.originals[slot] = original;
				} else if (slot < 256) {
					tVtableBaseCache.vtable = vtable;
					tVtableBaseCache.originals = {};
					tVtableBaseCache.originals[slot] = original;
				}
			}
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback) {
		LogOriginalFallback(name, queue, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}

	DX12LogJsonFunc(name ? name : "ID3D12CommandQueue::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		queue, slot);
	return nullptr;
}

static void LogQueueStage(
	const char *api, const char *stage, ID3D12CommandQueue *queue,
	UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK,
	ULONGLONG elapsedMs = 0)
{
#if defined(_DEBUG)
	if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
		return;
	if (SUCCEEDED(hr) && elapsedMs < 4 && api)
		return;
	DX12LogDebugJsonFunc("DX12QueueStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
		"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), queue,
		count, fence, static_cast<unsigned long long>(value), hr,
		static_cast<unsigned long long>(elapsedMs));
#else
	(void)api;
	(void)stage;
	(void)queue;
	(void)count;
	(void)fence;
	(void)value;
	(void)hr;
	(void)elapsedMs;
#endif
}

static void LogCommandListStage(
	const char *api, const char *stage, ID3D12GraphicsCommandList *commandList,
	HRESULT hr = S_OK, ULONGLONG elapsedMs = 0)
{
#if defined(_DEBUG)
	if (SUCCEEDED(hr) && elapsedMs < 4)
		return;
	DX12LogDebugJsonFunc("DX12CommandListStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\","
		"\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), commandList,
		hr, static_cast<unsigned long long>(elapsedMs));
#else
	(void)api;
	(void)stage;
	(void)commandList;
	(void)hr;
	(void)elapsedMs;
#endif
}

static void LogCommandListStageEdge(
	const char *api, const char *stage, ID3D12GraphicsCommandList *commandList)
{
	DX12LogDebugJsonFunc("DX12CommandListStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\"",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), commandList);
}

// Per-thread vtable-base cache: avoids repeated hash map lookups
// for command lists sharing the same vtable.
struct VtableBaseCache {
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
};
static thread_local VtableBaseCache tVtableBaseCache;

template <typename T>
static T GetCommandListOriginal(
	ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
{
	if (commandList) {
		void **vtable = *reinterpret_cast<void***>(commandList);
		if (vtable) {
			void *target = vtable[slot];

			// Fast path: thread-local per-slot cache (same vtable target, same thread)
			if (slot < ARRAYSIZE(tCommandListOriginals) &&
				tCommandListOriginals[slot].target == target &&
				tCommandListOriginals[slot].original)
				return reinterpret_cast<T>(tCommandListOriginals[slot].original);

			// Medium path: per-vtable bulk cache (different slot, same vtable)
			if (tVtableBaseCache.vtable == vtable && slot < 256 &&
			    tVtableBaseCache.originals[slot]) {
				void *original = tVtableBaseCache.originals[slot];
				if (slot < ARRAYSIZE(tCommandListOriginals)) {
					tCommandListOriginals[slot].target = target;
					tCommandListOriginals[slot].original = original;
				}
				return reinterpret_cast<T>(original);
			}

			// Slow path: hash map lookup
			void *original = DX12GetOriginalFunction(target);
			if (original) {
				// Populate thread-local caches for future calls
				if (slot < ARRAYSIZE(tCommandListOriginals)) {
					tCommandListOriginals[slot].target = target;
					tCommandListOriginals[slot].original = original;
				}
				if (tVtableBaseCache.vtable == vtable && slot < 256) {
					tVtableBaseCache.originals[slot] = original;
				} else if (slot < 256) {
					tVtableBaseCache.vtable = vtable;
					tVtableBaseCache.originals = {};
					tVtableBaseCache.originals[slot] = original;
				}
			}
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback) {
		LogOriginalFallback(name, commandList, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}

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
	return callbacks;
}

// Thread-local cache for IaReplacementCallbacks: avoids 6x vtable lookup per draw.
struct ThreadLocalIaCallbacksCache {
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};
static thread_local ThreadLocalIaCallbacksCache tIaCallbacksCache;

static const DX12IaReplacementExecutorCallbacks &GetCachedIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	if (tIaCallbacksCache.commandList == commandList)
		return tIaCallbacksCache.callbacks;
	tIaCallbacksCache.commandList = commandList;
	tIaCallbacksCache.callbacks = MakeIaReplacementCallbacks(commandList);
	return tIaCallbacksCache.callbacks;
}

static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
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
	static volatile LONG firstExecuteLog = 0;
	if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
		DX12LogJsonFuncFlush("DX12ExecuteCommandListsFirst",
			"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
			queue, numCommandLists, gDX12HotPathSkipAll,
			DX12IsInternalReplay() ? "true" : "false");

	if (gDX12HotPathSkipAll) {
		PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
			DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, ExecuteCommandLists);
		if (original) {
			if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
				DX12LogJsonFuncFlush("DX12ExecuteCommandListsFirst",
					"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
					queue, numCommandLists);
			original(queue, numCommandLists, commandLists);
			if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
				DX12LogJsonFuncFlush("DX12ExecuteCommandListsFirst",
					"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
					queue, numCommandLists);
		}
		return;
	}

	if (DX12IsInternalReplay()) {
		PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
			DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, ExecuteCommandLists);
		if (original)
			original(queue, numCommandLists, commandLists);
		return;
	}

	DX12_PROFILE_SCOPE(ExecuteCommandLists);
	LogDX12Call("ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
	for (UINT i = 0; commandLists && i < numCommandLists; ++i)
		RegisterCommandList(commandLists[i], nullptr);
	PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
		DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, ExecuteCommandLists);
	if (original) {
		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogJsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		LogQueueStage("ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		ULONGLONG startTick = GetTickCount64();
#endif
		original(queue, numCommandLists, commandLists);
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogJsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		ULONGLONG originalEndTick = GetTickCount64();
		LogQueueStage("ExecuteCommandLists", "afterOriginal", queue, numCommandLists,
			nullptr, 0, S_OK, originalEndTick - startTick);
#endif
		DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
#if defined(_DEBUG)
		LogQueueStage("ExecuteCommandLists", "afterNotify", queue, numCommandLists,
			nullptr, 0, S_OK, GetTickCount64() - originalEndTick);
#endif
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
	if (DX12IsInternalReplay()) {
		PFN_QUEUE_SIGNAL original =
			DX12_QUEUE_ORIG(queue, 14, PFN_QUEUE_SIGNAL, Signal);
		return original ? original(queue, fence, value) : E_FAIL;
	}

	LogDX12Call("ID3D12CommandQueue::Signal", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_SIGNAL original =
		DX12_QUEUE_ORIG(queue, 14, PFN_QUEUE_SIGNAL, Signal);
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	LogQueueStage("Signal", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	LogQueueStage("Signal", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
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
#if defined(_DEBUG)
	LogQueueStage("Wait", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	LogQueueStage("Wait", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
	ID3D12CommandQueue *queue, UINT64 *frequency)
{
	LogDX12Call("ID3D12CommandQueue::GetTimestampFrequency", queue);
	PFN_QUEUE_GET_TIMESTAMP_FREQUENCY original =
		DX12_QUEUE_ORIG(queue, 16, PFN_QUEUE_GET_TIMESTAMP_FREQUENCY, GetTimestampFrequency);
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	LogQueueStage("GetTimestampFrequency", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, frequency);
#if defined(_DEBUG)
	LogQueueStage("GetTimestampFrequency", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
	ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp)
{
	LogDX12Call("ID3D12CommandQueue::GetClockCalibration", queue);
	PFN_QUEUE_GET_CLOCK_CALIBRATION original =
		DX12_QUEUE_ORIG(queue, 17, PFN_QUEUE_GET_CLOCK_CALIBRATION, GetClockCalibration);
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	LogQueueStage("GetClockCalibration", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, gpuTimestamp, cpuTimestamp);
#if defined(_DEBUG)
	LogQueueStage("GetClockCalibration", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
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
	if (gDX12HotPathSkipAll) {
		if (gOrigResetCommandList)
			return gOrigResetCommandList(commandList, allocator, initialState);
		return E_FAIL;
	}
	DX12_PROFILE_SCOPE(ResetCommandList);
	LogDX12Call("ID3D12GraphicsCommandList::Reset", commandList,
		" allocator=%p initialPso=%p", allocator, initialState);
	PFN_RESET_COMMAND_LIST original = DX12_CL_ORIG(commandList, 10, PFN_RESET_COMMAND_LIST, ResetCommandList);
#if defined(_DEBUG)
	ULONGLONG startTick = GetTickCount64();
	ULONGLONG stageTick = startTick;
	LogCommandListStageEdge("Reset", "beforeOriginal", commandList);
	stageTick = GetTickCount64();
#endif
	HRESULT hr = original ? original(commandList, allocator, initialState) : E_FAIL;
#if defined(_DEBUG)
	LogCommandListStage("Reset", "afterOriginal", commandList, hr, GetTickCount64() - stageTick);
#endif
	if (SUCCEEDED(hr)) {
#if defined(_DEBUG)
		stageTick = GetTickCount64();
		LogCommandListStageEdge("Reset", "beforeRuntime", commandList);
#endif
		DX12CommandListRuntimeReset(commandList, initialState);
#if defined(_DEBUG)
		LogCommandListStage("Reset", "afterRuntime", commandList, S_OK, GetTickCount64() - stageTick);
		stageTick = GetTickCount64();
		LogCommandListStageEdge("Reset", "beforeLifecycle", commandList);
#endif
		DX12CommandListLifecycleReset(commandList, initialState);
#if defined(_DEBUG)
		LogCommandListStage("Reset", "afterLifecycle", commandList, S_OK, GetTickCount64() - stageTick);
	}
	LogCommandListStage("Reset", "end", commandList, hr, GetTickCount64() - startTick);
#else
	}
#endif
	return hr;
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
	if (gDX12HotPathSkipAll) {
		if (gOrigDrawInstanced)
			gOrigDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawInstanced);

	if (DX12Profiling::ShouldCollectCounters() || DX12DrawHookFlowNeedsModWork())
		InterlockedIncrement(&gPerFrameTotalDraws);

	if (DX12IaReplacementIsExecutingInternalDraw()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	if (gDX12HotPathSkipBindings && DX12HuntIsEnabled() && !DX12DrawHookFlowNeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntShouldSkipDraw(commandList, false))
			return;
		DX12HuntRecordDraw(commandList, false);
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::DrawInstanced", commandList,
		" vertices=%u instances=%u startVertex=%u startInstance=%u",
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
			startVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntShouldSkipDraw(commandList, false))
			return;
		DX12HuntRecordDraw(commandList, false);
	}
	if (!DX12DrawHookFlowNeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DrawInstanced);
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	// Cache runtime state ptr once — GetStatePtrFast TLS-caches after first call
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntimeGetStatePtr(commandList);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		GetCachedIaReplacementCallbacks(commandList);
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
	if (gDX12HotPathSkipAll) {
		if (gOrigDrawIndexedInstanced)
			gOrigDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawIndexedInstanced);

	if (DX12Profiling::ShouldCollectCounters() || DX12DrawHookFlowNeedsModWork())
		InterlockedIncrement(&gPerFrameTotalDraws);

	if (DX12IaReplacementIsExecutingInternalDraw()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}

	if (gDX12HotPathSkipBindings && DX12HuntIsEnabled() && !DX12DrawHookFlowNeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntShouldSkipDraw(commandList, true))
			return;
		DX12HuntRecordDraw(commandList, true);
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
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
			startIndexLocation, baseVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntShouldSkipDraw(commandList, true))
			return;
		DX12HuntRecordDraw(commandList, true);
	}
	if (!DX12DrawHookFlowNeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DrawIndexedInstanced);
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	// Cache runtime state ptr once — GetStatePtrFast TLS-caches after first call
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntimeGetStatePtr(commandList);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		GetCachedIaReplacementCallbacks(commandList);
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
	if (gDX12HotPathSkipAll) {
		if (gOrigDispatch)
			gOrigDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	DX12_PROFILE_SCOPE(Dispatch);
	if (DX12Profiling::ShouldCollectCounters() ||
	    DX12ModHasActiveShaderOverrides() || DX12ModNeedsPreSkinningUavProbe())
		InterlockedIncrement(&gPerFrameTotalDispatches);

	if (DX12IsInternalReplay()) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	if (gDX12HotPathSkipBindings && DX12HuntIsEnabled() &&
	    !DX12ModHasActiveShaderOverrides() && !DX12ModNeedsPreSkinningUavProbe()) {
		DX12_PROFILE_FAST_FORWARD();
		DX12HuntRecordDispatch(commandList);
		if (DX12HuntShouldSkipDispatch(commandList))
			return;
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::Dispatch", commandList,
		" groups=%u,%u,%u", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	if (DX12HuntIsEnabled()) {
		DX12HuntRecordDispatch(commandList);
		if (DX12HuntShouldSkipDispatch(commandList))
			return;
	}
	PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, Dispatch);
	if (!DX12ModHasActiveShaderOverrides() && !DX12ModNeedsPreSkinningUavProbe()) {
		DX12_PROFILE_FAST_FORWARD();
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	if (!DX12ModHasActiveShaderOverrides() && DX12ModNeedsPreSkinningUavProbe()) {
		const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntimeGetStatePtr(commandList);
		ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState :
			DX12CommandListRuntimeGetPipelineState(commandList);
		DX12PsoShaderInfo shaderInfo = {};
		const bool hasConfiguredPreSkinCs =
			pipelineState &&
			DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) &&
			 shaderInfo.hasCS &&
			DX12ModShouldTrackPreSkinBindingsForCs(shaderInfo.cs);
		if (!hasConfiguredPreSkinCs) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12DispatchHookFlowExecute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, original);
		return;
	}
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntimeGetStatePtr(commandList);
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
	if (gDX12HotPathSkipAll) {
		if (gOrigIASetPrimitiveTopology)
			gOrigIASetPrimitiveTopology(commandList, topology);
		return;
	}
	DX12_PROFILE_SCOPE(IASetPrimitiveTopology);

	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
			" topology=%u", static_cast<UINT>(topology));
		DX12CommandListCapturePrimitiveTopology(commandList, topology);
	} else {
		if (DX12ModHasActiveShaderOverrides())
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
	if (gDX12HotPathSkipAll) {
		if (gOrigSetPipelineState)
			gOrigSetPipelineState(commandList, pipelineState);
		return;
	}
	DX12_PROFILE_SCOPE(SetPipelineState);

	if (gDX12HotPathSkipBindings && DX12HuntIsEnabled() &&
	    !DX12ModHasActiveShaderOverrides()) {
		DX12_PROFILE_FAST_FORWARD();
		DX12CommandListRuntimeRememberPipelineState(commandList, pipelineState);
		DX12HuntSetPipelineState(commandList, pipelineState);
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
	if (gDX12HotPathSkipAll) {
		if (gOrigResourceBarrier)
			gOrigResourceBarrier(commandList, numBarriers, barriers);
		return;
	}
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

#include "DX12CommandListComputeBindings.cpp"

static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (gDX12HotPathSkipAll) {
		if (gOrigSetGraphicsRootUnorderedAccessView)
			gOrigSetGraphicsRootUnorderedAccessView(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);

	if (gDX12HotPathSkipGraphicsBindings) {
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
	if (gDX12HotPathSkipAll) {
		if (gOrigIASetIndexBuffer)
			gOrigIASetIndexBuffer(commandList, view);
		return;
	}
	DX12_PROFILE_SCOPE(IASetIndexBuffer);

	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
			" gpu=0x%llx size=%u format=%u",
			view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
			view ? view->SizeInBytes : 0,
			view ? static_cast<UINT>(view->Format) : 0);
		DX12CommandListCaptureIndexBuffer(commandList, view);
	} else {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetIndexBuffer(commandList, view);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntimeRememberIndexBuffer(commandList, view);
	}

	PFN_IA_SET_INDEX_BUFFER original = DX12_CL_ORIG(commandList, 43, PFN_IA_SET_INDEX_BUFFER, IASetIndexBuffer);
	if (original)
		original(commandList, view);
}

static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (gDX12HotPathSkipAll) {
		if (gOrigIASetVertexBuffers)
			gOrigIASetVertexBuffers(commandList, startSlot, count, views);
		return;
	}
	DX12_PROFILE_SCOPE(IASetVertexBuffers);

	if (!gDX12HotPathSkipBindings) {
		LogDX12Call("ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
			" start=%u count=%u", startSlot, count);
		DX12CommandListCaptureVertexBuffers(commandList, startSlot, count, views);
	} else {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntimeRememberVertexBuffers(commandList, startSlot, count, views);
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

	// Pre-skin probe for games that dispatch compute shaders via ExecuteIndirect
	// (e.g. ZZZ).  The individual Dispatch inside ExecuteIndirect is invisible
	// to our Dispatch hook, so we intercept at the ExecuteIndirect level and
	// apply binding patches if the current compute PSO has a pre-skin CS hash.
	if (DX12ModNeedsPreSkinningUavProbe()) {
		ID3D12PipelineState *pipelineState = DX12CommandListRuntimeGetPipelineState(commandList);
		if (!pipelineState) {
			DX12Log("HookedExecuteIndirect: no PSO from runtime state\n");
		} else {
			DX12PsoShaderInfo shaderInfo = {};
			if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
				DX12Log("HookedExecuteIndirect: GetShaderInfo failed\n");
			} else if (!shaderInfo.hasCS) {
				DX12Log("HookedExecuteIndirect: PSO is not compute (hasCS=false)\n");
			} else {
				DX12Log("HookedExecuteIndirect: compute PSO cs=%016llx\n",
					static_cast<unsigned long long>(shaderInfo.cs));
				if (!DX12ModShouldTrackPreSkinBindingsForCs(shaderInfo.cs)) {
					DX12Log("HookedExecuteIndirect: CS not in pre-skin list\n");
				} else {
					DX12Log("HookedExecuteIndirect: pre-skin CS match!\n");
					std::vector<DX12CurrentComputeUavBinding> uavs, srvs, cbvs;
					std::vector<DX12CurrentRootConstants> rootConstants;
					if (DX12BindingGetCurrentComputeAll(commandList, &uavs, &srvs, &cbvs, &rootConstants)) {
						DX12Log("HookedExecuteIndirect: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
							uavs.size(), srvs.size(), cbvs.size());
						UINT originalVertexCount = 0, overrideVertexCount = 0;
						DX12PreSkinDispatchOverride dispatchOverride;
						if (DX12ModApplyPreSkinningUavReplacement(
							    commandList, shaderInfo.cs,
							    uavs, srvs, cbvs, rootConstants,
							    &originalVertexCount, &overrideVertexCount,
							    &dispatchOverride)) {
							DX12Log("HookedExecuteIndirect: pre-skin APPLIED! origVtx=%u newVtx=%u\n",
								originalVertexCount, overrideVertexCount);
						} else {
							DX12Log("HookedExecuteIndirect: pre-skin matching failed\n");
						}
					} else {
						DX12Log("HookedExecuteIndirect: no compute bindings found\n");
					}
				}
			}
		}
	}

	gOrigExecuteIndirect(commandList, commandSignature, maxCommandCount, argumentBuffer,
		argumentBufferOffset, countBuffer, countBufferOffset);

	// Restore pre-skin patches that were applied before ExecuteIndirect
	DX12ModRestorePreSkinningUavReplacement(commandList);
}

void DX12HookCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;
	DX12RememberCommandQueueForHotPath(commandQueue);
	if (!DX12HotPathHooksNeeded()) {
		DX12LogJsonFunc("DX12HotPathHookGate",
			"\"object\":\"command_queue\",\"action\":\"skip\",\"queue\":\"%p\"",
			commandQueue);
		return;
	}

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	DX12SetCommandQueue(queue);
	DX12VTableHook queueHooks[] = {
		{10, reinterpret_cast<void**>(&gOrigQueueExecuteCommandLists),
			HookedQueueExecuteCommandLists, "ID3D12CommandQueue::ExecuteCommandLists"},
	};
	DX12InstallVTableHooks(queue, queueHooks);
	queue->Release();
}

void DX12RememberCommandQueueForHotPath(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;
	AcquireSRWLockExclusive(&gKnownCommandQueueLock);
	gKnownCommandQueue = commandQueue;
	ReleaseSRWLockExclusive(&gKnownCommandQueueLock);
}

void DX12ActivateCommandQueueHotPathHooks()
{
	IUnknown *queue = nullptr;
	AcquireSRWLockShared(&gKnownCommandQueueLock);
	queue = gKnownCommandQueue;
	ReleaseSRWLockShared(&gKnownCommandQueueLock);
	if (queue)
		DX12HookCommandQueue(queue);
}

void DX12HookCommandList(IUnknown *commandList)
{
	if (!commandList)
		return;
	if (!DX12HotPathHooksNeeded())
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&baseList))))
		return;

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


