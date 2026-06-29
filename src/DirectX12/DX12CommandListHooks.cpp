#include "DX12CommandListHooks.hpp"
#include "DX12CommandListHookSignatures.hpp"
#include "DX12CommandListHookState.hpp"

#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <unordered_map>
#include <unordered_set>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListCommandRecorder.hpp"
#include "DX12CommandListLifecycle.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12DispatchHookFlow.hpp"
#include "DX12DrawHookFlow.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12HookManager.hpp"
#include "DX12IaReplacementExecutor.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12CommandQueueState.hpp"
#include "DX12HotPathState.hpp"
#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"
#include "types/DX12CommandListHookSlotTypes.hpp"

using PFN_CREATE_COMMAND_QUEUE = DX12CommandListHookSignatures::CreateCommandQueue;
using PFN_CREATE_COMMAND_ALLOCATOR = DX12CommandListHookSignatures::CreateCommandAllocator;
using PFN_CREATE_COMMAND_LIST = DX12CommandListHookSignatures::CreateCommandList;
using PFN_CREATE_COMMAND_LIST1 = DX12CommandListHookSignatures::CreateCommandList1;
using PFN_QUEUE_UPDATE_TILE_MAPPINGS = DX12CommandListHookSignatures::QueueUpdateTileMappings;
using PFN_QUEUE_COPY_TILE_MAPPINGS = DX12CommandListHookSignatures::QueueCopyTileMappings;
using PFN_QUEUE_EXECUTE_COMMAND_LISTS = DX12CommandListHookSignatures::QueueExecuteCommandLists;
using PFN_QUEUE_SET_MARKER = DX12CommandListHookSignatures::QueueSetMarker;
using PFN_QUEUE_BEGIN_EVENT = DX12CommandListHookSignatures::QueueBeginEvent;
using PFN_QUEUE_END_EVENT = DX12CommandListHookSignatures::QueueEndEvent;
using PFN_QUEUE_SIGNAL = DX12CommandListHookSignatures::QueueSignal;
using PFN_QUEUE_WAIT = DX12CommandListHookSignatures::QueueWait;
using PFN_QUEUE_GET_TIMESTAMP_FREQUENCY = DX12CommandListHookSignatures::QueueGetTimestampFrequency;
using PFN_QUEUE_GET_CLOCK_CALIBRATION = DX12CommandListHookSignatures::QueueGetClockCalibration;
using PFN_CLOSE_COMMAND_LIST = DX12CommandListHookSignatures::CloseCommandList;
using PFN_RESET_COMMAND_LIST = DX12CommandListHookSignatures::ResetCommandList;
using PFN_CLEAR_STATE = DX12CommandListHookSignatures::ClearState;
using PFN_DRAW_INSTANCED = DX12CommandListHookSignatures::DrawInstanced;
using PFN_DRAW_INDEXED_INSTANCED = DX12CommandListHookSignatures::DrawIndexedInstanced;
using PFN_DISPATCH = DX12CommandListHookSignatures::Dispatch;
using PFN_COPY_BUFFER_REGION = DX12CommandListHookSignatures::CopyBufferRegion;
using PFN_COPY_TEXTURE_REGION = DX12CommandListHookSignatures::CopyTextureRegion;
using PFN_COPY_RESOURCE = DX12CommandListHookSignatures::CopyResource;
using PFN_COPY_TILES = DX12CommandListHookSignatures::CopyTiles;
using PFN_RESOLVE_SUBRESOURCE = DX12CommandListHookSignatures::ResolveSubresource;
using PFN_IA_SET_PRIMITIVE_TOPOLOGY = DX12CommandListHookSignatures::IaSetPrimitiveTopology;
using PFN_RS_SET_VIEWPORTS = DX12CommandListHookSignatures::RsSetViewports;
using PFN_RS_SET_SCISSOR_RECTS = DX12CommandListHookSignatures::RsSetScissorRects;
using PFN_OM_SET_BLEND_FACTOR = DX12CommandListHookSignatures::OmSetBlendFactor;
using PFN_OM_SET_STENCIL_REF = DX12CommandListHookSignatures::OmSetStencilRef;
using PFN_SET_PIPELINE_STATE = DX12CommandListHookSignatures::SetPipelineState;
using PFN_RESOURCE_BARRIER = DX12CommandListHookSignatures::ResourceBarrier;
using PFN_EXECUTE_BUNDLE = DX12CommandListHookSignatures::ExecuteBundle;
using PFN_SET_DESCRIPTOR_HEAPS = DX12CommandListHookSignatures::SetDescriptorHeaps;
using PFN_SET_ROOT_SIGNATURE = DX12CommandListHookSignatures::SetRootSignature;
using PFN_SET_ROOT_DESCRIPTOR_TABLE = DX12CommandListHookSignatures::SetRootDescriptorTable;
using PFN_SET_ROOT_32BIT_CONSTANT = DX12CommandListHookSignatures::SetRoot32BitConstant;
using PFN_SET_ROOT_32BIT_CONSTANTS = DX12CommandListHookSignatures::SetRoot32BitConstants;
using PFN_SET_ROOT_GPU_VA = DX12CommandListHookSignatures::SetRootGpuVa;
using PFN_IA_SET_INDEX_BUFFER = DX12CommandListHookSignatures::IaSetIndexBuffer;
using PFN_IA_SET_VERTEX_BUFFERS = DX12CommandListHookSignatures::IaSetVertexBuffers;
using PFN_SO_SET_TARGETS = DX12CommandListHookSignatures::SoSetTargets;
using PFN_OM_SET_RENDER_TARGETS = DX12CommandListHookSignatures::OmSetRenderTargets;
using PFN_CLEAR_DEPTH_STENCIL_VIEW = DX12CommandListHookSignatures::ClearDepthStencilView;
using PFN_CLEAR_RENDER_TARGET_VIEW = DX12CommandListHookSignatures::ClearRenderTargetView;
using PFN_CLEAR_UAV_UINT = DX12CommandListHookSignatures::ClearUavUint;
using PFN_CLEAR_UAV_FLOAT = DX12CommandListHookSignatures::ClearUavFloat;
using PFN_DISCARD_RESOURCE = DX12CommandListHookSignatures::DiscardResource;
using PFN_BEGIN_QUERY = DX12CommandListHookSignatures::BeginQuery;
using PFN_END_QUERY = DX12CommandListHookSignatures::EndQuery;
using PFN_RESOLVE_QUERY_DATA = DX12CommandListHookSignatures::ResolveQueryData;
using PFN_SET_PREDICATION = DX12CommandListHookSignatures::SetPredication;
using PFN_COMMAND_LIST_SET_MARKER = DX12CommandListHookSignatures::CommandListSetMarker;
using PFN_COMMAND_LIST_BEGIN_EVENT = DX12CommandListHookSignatures::CommandListBeginEvent;
using PFN_COMMAND_LIST_END_EVENT = DX12CommandListHookSignatures::CommandListEndEvent;
using PFN_EXECUTE_INDIRECT = DX12CommandListHookSignatures::ExecuteIndirect;
DX12CommandListHookSignatures::CreateCommandQueue DX12CommandListHookState::OrigCreateCommandQueue = nullptr;
DX12CommandListHookSignatures::CreateCommandAllocator DX12CommandListHookState::OrigCreateCommandAllocator = nullptr;
DX12CommandListHookSignatures::CreateCommandList DX12CommandListHookState::OrigCreateCommandList = nullptr;
DX12CommandListHookSignatures::CreateCommandList1 DX12CommandListHookState::OrigCreateCommandList1 = nullptr;

DX12CommandListHookSignatures::QueueUpdateTileMappings DX12CommandListHookState::OrigQueueUpdateTileMappings = nullptr;
DX12CommandListHookSignatures::QueueCopyTileMappings DX12CommandListHookState::OrigQueueCopyTileMappings = nullptr;
DX12CommandListHookSignatures::QueueExecuteCommandLists DX12CommandListHookState::OrigQueueExecuteCommandLists = nullptr;
DX12CommandListHookSignatures::QueueSetMarker DX12CommandListHookState::OrigQueueSetMarker = nullptr;
DX12CommandListHookSignatures::QueueBeginEvent DX12CommandListHookState::OrigQueueBeginEvent = nullptr;
DX12CommandListHookSignatures::QueueEndEvent DX12CommandListHookState::OrigQueueEndEvent = nullptr;
DX12CommandListHookSignatures::QueueSignal DX12CommandListHookState::OrigQueueSignal = nullptr;
DX12CommandListHookSignatures::QueueWait DX12CommandListHookState::OrigQueueWait = nullptr;
DX12CommandListHookSignatures::QueueGetTimestampFrequency DX12CommandListHookState::OrigQueueGetTimestampFrequency = nullptr;
DX12CommandListHookSignatures::QueueGetClockCalibration DX12CommandListHookState::OrigQueueGetClockCalibration = nullptr;

DX12CommandListHookSignatures::CloseCommandList DX12CommandListHookState::OrigCloseCommandList = nullptr;
DX12CommandListHookSignatures::ResetCommandList DX12CommandListHookState::OrigResetCommandList = nullptr;
DX12CommandListHookSignatures::ClearState DX12CommandListHookState::OrigClearState = nullptr;
DX12CommandListHookSignatures::DrawInstanced DX12CommandListHookState::OrigDrawInstanced = nullptr;
DX12CommandListHookSignatures::DrawIndexedInstanced DX12CommandListHookState::OrigDrawIndexedInstanced = nullptr;
DX12CommandListHookSignatures::Dispatch DX12CommandListHookState::OrigDispatch = nullptr;
DX12CommandListHookSignatures::CopyBufferRegion DX12CommandListHookState::OrigCopyBufferRegion = nullptr;
DX12CommandListHookSignatures::CopyTextureRegion DX12CommandListHookState::OrigCopyTextureRegion = nullptr;
DX12CommandListHookSignatures::CopyResource DX12CommandListHookState::OrigCopyResource = nullptr;
DX12CommandListHookSignatures::CopyTiles DX12CommandListHookState::OrigCopyTiles = nullptr;
DX12CommandListHookSignatures::ResolveSubresource DX12CommandListHookState::OrigResolveSubresource = nullptr;
DX12CommandListHookSignatures::IaSetPrimitiveTopology DX12CommandListHookState::OrigIASetPrimitiveTopology = nullptr;
DX12CommandListHookSignatures::RsSetViewports DX12CommandListHookState::OrigRSSetViewports = nullptr;
DX12CommandListHookSignatures::RsSetScissorRects DX12CommandListHookState::OrigRSSetScissorRects = nullptr;
DX12CommandListHookSignatures::OmSetBlendFactor DX12CommandListHookState::OrigOMSetBlendFactor = nullptr;
DX12CommandListHookSignatures::OmSetStencilRef DX12CommandListHookState::OrigOMSetStencilRef = nullptr;
DX12CommandListHookSignatures::SetPipelineState DX12CommandListHookState::OrigSetPipelineState = nullptr;
DX12CommandListHookSignatures::ResourceBarrier DX12CommandListHookState::OrigResourceBarrier = nullptr;
DX12CommandListHookSignatures::ExecuteBundle DX12CommandListHookState::OrigExecuteBundle = nullptr;
DX12CommandListHookSignatures::SetDescriptorHeaps DX12CommandListHookState::OrigSetDescriptorHeaps = nullptr;
DX12CommandListHookSignatures::SetRootSignature DX12CommandListHookState::OrigSetComputeRootSignature = nullptr;
DX12CommandListHookSignatures::SetRootSignature DX12CommandListHookState::OrigSetGraphicsRootSignature = nullptr;
DX12CommandListHookSignatures::SetRootDescriptorTable DX12CommandListHookState::OrigSetComputeRootDescriptorTable = nullptr;
DX12CommandListHookSignatures::SetRootDescriptorTable DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstant DX12CommandListHookState::OrigSetComputeRoot32BitConstant = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstant DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstants DX12CommandListHookState::OrigSetComputeRoot32BitConstants = nullptr;
DX12CommandListHookSignatures::SetRoot32BitConstants DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootConstantBufferView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootShaderResourceView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView = nullptr;
DX12CommandListHookSignatures::SetRootGpuVa DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView = nullptr;
DX12CommandListHookSignatures::IaSetIndexBuffer DX12CommandListHookState::OrigIASetIndexBuffer = nullptr;
DX12CommandListHookSignatures::IaSetVertexBuffers DX12CommandListHookState::OrigIASetVertexBuffers = nullptr;
DX12CommandListHookSignatures::SoSetTargets DX12CommandListHookState::OrigSOSetTargets = nullptr;
DX12CommandListHookSignatures::OmSetRenderTargets DX12CommandListHookState::OrigOMSetRenderTargets = nullptr;
DX12CommandListHookSignatures::ClearDepthStencilView DX12CommandListHookState::OrigClearDepthStencilView = nullptr;
DX12CommandListHookSignatures::ClearRenderTargetView DX12CommandListHookState::OrigClearRenderTargetView = nullptr;
DX12CommandListHookSignatures::ClearUavUint DX12CommandListHookState::OrigClearUnorderedAccessViewUint = nullptr;
DX12CommandListHookSignatures::ClearUavFloat DX12CommandListHookState::OrigClearUnorderedAccessViewFloat = nullptr;
DX12CommandListHookSignatures::DiscardResource DX12CommandListHookState::OrigDiscardResource = nullptr;
DX12CommandListHookSignatures::BeginQuery DX12CommandListHookState::OrigBeginQuery = nullptr;
DX12CommandListHookSignatures::EndQuery DX12CommandListHookState::OrigEndQuery = nullptr;
DX12CommandListHookSignatures::ResolveQueryData DX12CommandListHookState::OrigResolveQueryData = nullptr;
DX12CommandListHookSignatures::SetPredication DX12CommandListHookState::OrigSetPredication = nullptr;
DX12CommandListHookSignatures::CommandListSetMarker DX12CommandListHookState::OrigCommandListSetMarker = nullptr;
DX12CommandListHookSignatures::CommandListBeginEvent DX12CommandListHookState::OrigCommandListBeginEvent = nullptr;
DX12CommandListHookSignatures::CommandListEndEvent DX12CommandListHookState::OrigCommandListEndEvent = nullptr;
DX12CommandListHookSignatures::ExecuteIndirect DX12CommandListHookState::OrigExecuteIndirect = nullptr;

SRWLOCK DX12CommandListHookState::IaReplacementSummaryLock = SRWLOCK_INIT;
LONG DX12CommandListHookState::IaReplacementSummaryPresent = -1;
volatile LONG DX12CommandListHookState::IaReplacementSummaryDraws = 0;
volatile LONG DX12CommandListHookState::IaReplacementSummaryIndexedDraws = 0;
volatile LONG DX12CommandListHookState::IaReplacementSummaryDispatches = 0;

SRWLOCK DX12CommandListHookState::HookedVTableLock = SRWLOCK_INIT;
std::unordered_set<void*> DX12CommandListHookState::HookedCommandListVTables;
SRWLOCK DX12CommandListHookState::RegisteredListLock = SRWLOCK_INIT;
std::unordered_set<void*> DX12CommandListHookState::RegisteredCommandLists;
SRWLOCK DX12CommandListHookState::KnownCommandQueueLock = SRWLOCK_INIT;
IUnknown *DX12CommandListHookState::KnownCommandQueue = nullptr;

thread_local DX12ThreadLocalCommandListOriginal DX12CommandListHookState::CommandListOriginals[64];
thread_local DX12ThreadLocalCommandListOriginal DX12CommandListHookState::CommandQueueOriginals[32];
thread_local DX12VtableBaseCache DX12CommandListHookState::VtableBaseCache;
thread_local DX12ThreadLocalIaCallbacksCache DX12CommandListHookState::IaCallbacksCache;
class DX12CommandListHookUtils
{
public:
static bool ShouldHookCommandListVTable(void *vtable)
{
	if (!vtable)
		return false;

	AcquireSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
	const bool alreadyHooked =
		DX12CommandListHookState::HookedCommandListVTables.find(vtable) != DX12CommandListHookState::HookedCommandListVTables.end();
	ReleaseSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
	if (alreadyHooked)
		return false;

	bool shouldHook = false;
	AcquireSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
	shouldHook = DX12CommandListHookState::HookedCommandListVTables.insert(vtable).second;
	ReleaseSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
	return shouldHook;
}

static bool ShouldRegisterCommandListPointer(void *commandList)
{
	if (!commandList)
		return false;

	AcquireSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
	const bool alreadyRegistered =
		DX12CommandListHookState::RegisteredCommandLists.find(commandList) != DX12CommandListHookState::RegisteredCommandLists.end();
	ReleaseSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
	if (alreadyRegistered)
		return false;

	bool shouldRegister = false;
	AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	shouldRegister = DX12CommandListHookState::RegisteredCommandLists.insert(commandList).second;
	ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	return shouldRegister;
}

static void ForgetRegisteredCommandListPointer(void *commandList)
{
	if (!commandList)
		return;

	AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	DX12CommandListHookState::RegisteredCommandLists.erase(commandList);
	ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
}

static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
{
#if defined(_DEBUG)
	if (!DX12LogManager::ShouldLogHookCall(api))
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
	DX12JsonUtils::EscapeString(apiJson, sizeof(apiJson), api ? api : "");
	DX12JsonUtils::EscapeString(detailJson, sizeof(detailJson), detail);
	DX12LogManager::DebugJsonFunc("DX12HookCall",
		"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
		apiJson, DX12RuntimeState::GetPresentCount(), object, detailJson);
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
	const LONG present = DX12RuntimeState::GetPresentCount();
	if (DX12CommandListHookState::IaReplacementSummaryPresent == present)
		return;

	AcquireSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
	if (DX12CommandListHookState::IaReplacementSummaryPresent != present) {
		const LONG previousPresent = DX12CommandListHookState::IaReplacementSummaryPresent;
		const LONG draws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDraws, 0);
		const LONG indexedDraws = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws, 0);
		const LONG dispatches = InterlockedExchange(&DX12CommandListHookState::IaReplacementSummaryDispatches, 0);
		if (previousPresent >= 0 && (draws || dispatches)) {
			DX12LogManager::DebugJsonFunc("DX12IaReplacementExecuteSummary",
				"\"present\":%ld,\"draws\":%ld,\"indexedDraws\":%ld,\"dispatches\":%ld",
				previousPresent, draws, indexedDraws, dispatches);
		}
		DX12CommandListHookState::IaReplacementSummaryPresent = present;
	}
	ReleaseSRWLockExclusive(&DX12CommandListHookState::IaReplacementSummaryLock);
#endif
}

static void RecordIaReplacementDrawExecute(bool indexed)
{
	DX12Profiling::RecordIaReplacementDraw(indexed);
#if defined(_DEBUG)
	FlushIaReplacementExecuteSummaryIfNeeded();
	InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDraws);
	if (indexed)
		InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryIndexedDraws);
#endif
}

static void RecordIaReplacementDispatchExecute()
{
	DX12Profiling::RecordIaReplacementDispatch();
#if defined(_DEBUG)
	FlushIaReplacementExecuteSummaryIfNeeded();
	InterlockedIncrement(&DX12CommandListHookState::IaReplacementSummaryDispatches);
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

static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
{
#if defined(_DEBUG)
	if (!DX12LogManager::ShouldLogHookCall(api))
		return;
	DX12LogManager::DebugJsonFunc("DX12FallbackPath",
		"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
		"\"slot\":%u,\"fallback\":\"%p\"",
		api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
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
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback) {
		LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}

	DX12LogManager::JsonFunc(name ? name : "D3D12::Unknown",
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
			if (slot < ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals) &&
				DX12CommandListHookState::CommandQueueOriginals[slot].target == target &&
				DX12CommandListHookState::CommandQueueOriginals[slot].original)
				return reinterpret_cast<T>(DX12CommandListHookState::CommandQueueOriginals[slot].original);

			// Medium path: per-vtable bulk cache
			if (DX12CommandListHookState::VtableBaseCache.vtable == vtable && slot < 256 &&
			    DX12CommandListHookState::VtableBaseCache.originals[slot]) {
				void *original = DX12CommandListHookState::VtableBaseCache.originals[slot];
				if (slot < ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals)) {
					DX12CommandListHookState::CommandQueueOriginals[slot].target = target;
					DX12CommandListHookState::CommandQueueOriginals[slot].original = original;
				}
				return reinterpret_cast<T>(original);
			}

			// Slow path: hash map lookup
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original) {
				if (slot < ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals)) {
					DX12CommandListHookState::CommandQueueOriginals[slot].target = target;
					DX12CommandListHookState::CommandQueueOriginals[slot].original = original;
				}
				if (DX12CommandListHookState::VtableBaseCache.vtable == vtable && slot < 256) {
					DX12CommandListHookState::VtableBaseCache.originals[slot] = original;
				} else if (slot < 256) {
					DX12CommandListHookState::VtableBaseCache.vtable = vtable;
					DX12CommandListHookState::VtableBaseCache.originals = {};
					DX12CommandListHookState::VtableBaseCache.originals[slot] = original;
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

	DX12LogManager::JsonFunc(name ? name : "ID3D12CommandQueue::Unknown",
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
	if (!DX12LogManager::VerboseDiagnosticsEnabled())
		return;
	if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
		return;
	if (SUCCEEDED(hr) && elapsedMs < 4 && api)
		return;
	DX12LogManager::DebugJsonFunc("DX12QueueStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
		"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
		api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), queue,
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
	if (!DX12LogManager::VerboseDiagnosticsEnabled())
		return;
	if (SUCCEEDED(hr) && elapsedMs < 4)
		return;
	DX12LogManager::DebugJsonFunc("DX12CommandListStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\","
		"\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
		api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList,
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
	if (!DX12LogManager::VerboseDiagnosticsEnabled())
		return;
	DX12LogManager::DebugJsonFunc("DX12CommandListStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"commandList\":\"%p\"",
		api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), commandList);
}

template <typename T>
static T GetCommandListOriginal(
	ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
{
	if (commandList) {
		void **vtable = *reinterpret_cast<void***>(commandList);
		if (vtable) {
			void *target = vtable[slot];

			// Fast path: thread-local per-slot cache (same vtable target, same thread)
			if (slot < ARRAYSIZE(DX12CommandListHookState::CommandListOriginals) &&
				DX12CommandListHookState::CommandListOriginals[slot].target == target &&
				DX12CommandListHookState::CommandListOriginals[slot].original)
				return reinterpret_cast<T>(DX12CommandListHookState::CommandListOriginals[slot].original);

			// Medium path: per-vtable bulk cache (different slot, same vtable)
			if (DX12CommandListHookState::VtableBaseCache.vtable == vtable && slot < 256 &&
			    DX12CommandListHookState::VtableBaseCache.originals[slot]) {
				void *original = DX12CommandListHookState::VtableBaseCache.originals[slot];
				if (slot < ARRAYSIZE(DX12CommandListHookState::CommandListOriginals)) {
					DX12CommandListHookState::CommandListOriginals[slot].target = target;
					DX12CommandListHookState::CommandListOriginals[slot].original = original;
				}
				return reinterpret_cast<T>(original);
			}

			// Slow path: hash map lookup
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original) {
				// Populate thread-local caches for future calls
				if (slot < ARRAYSIZE(DX12CommandListHookState::CommandListOriginals)) {
					DX12CommandListHookState::CommandListOriginals[slot].target = target;
					DX12CommandListHookState::CommandListOriginals[slot].original = original;
				}
				if (DX12CommandListHookState::VtableBaseCache.vtable == vtable && slot < 256) {
					DX12CommandListHookState::VtableBaseCache.originals[slot] = original;
				} else if (slot < 256) {
					DX12CommandListHookState::VtableBaseCache.vtable = vtable;
					DX12CommandListHookState::VtableBaseCache.originals = {};
					DX12CommandListHookState::VtableBaseCache.originals[slot] = original;
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

	DX12LogManager::JsonFunc(name ? name : "ID3D12GraphicsCommandList::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		commandList, slot);
	return nullptr;
}

};

#define DX12_CL_ORIG(commandList, slot, type, fallback, name) \
	DX12CommandListHookUtils::GetCommandListOriginal<type>(commandList, slot, fallback, "ID3D12GraphicsCommandList::" name)

#define DX12_QUEUE_ORIG(queue, slot, type, fallback, name) \
	DX12CommandListHookUtils::GetCommandQueueOriginal<type>(queue, slot, fallback, "ID3D12CommandQueue::" name)



#include "hooks/DX12CommandListComputeBindingHooks.hpp"

class DX12CommandListHookCallbacks
{
public:
static DX12IaReplacementExecutorCallbacks MakeIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	DX12IaReplacementExecutorCallbacks callbacks;
	callbacks.drawInstanced =
		DX12_CL_ORIG(commandList, 12, DX12IaCommandSignatures::DrawInstanced,
			DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
	callbacks.drawIndexedInstanced =
		DX12_CL_ORIG(commandList, 13, DX12IaCommandSignatures::DrawIndexedInstanced,
			DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
	callbacks.dispatch =
		DX12_CL_ORIG(commandList, 14, DX12IaCommandSignatures::Dispatch,
			DX12CommandListHookState::OrigDispatch, "Dispatch");
	callbacks.setIndexBuffer =
		DX12_CL_ORIG(commandList, 43, DX12IaCommandSignatures::SetIndexBuffer,
			DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
	callbacks.setVertexBuffers =
		DX12_CL_ORIG(commandList, 44, DX12IaCommandSignatures::SetVertexBuffers,
			DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
	callbacks.recordReplacementDraw = DX12CommandListHookUtils::RecordIaReplacementDrawExecute;
	callbacks.recordReplacementDispatch = DX12CommandListHookUtils::RecordIaReplacementDispatchExecute;
	return callbacks;
}

static const DX12IaReplacementExecutorCallbacks &GetCachedIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	if (DX12CommandListHookState::IaCallbacksCache.commandList == commandList)
		return DX12CommandListHookState::IaCallbacksCache.callbacks;
	DX12CommandListHookState::IaCallbacksCache.commandList = commandList;
	DX12CommandListHookState::IaCallbacksCache.callbacks = DX12CommandListHookCallbacks::MakeIaReplacementCallbacks(commandList);
	return DX12CommandListHookState::IaCallbacksCache.callbacks;
}

static void RegisterCommandListBase(
	ID3D12GraphicsCommandList *baseList, ID3D12PipelineState *initialState)
{
	if (!baseList)
		return;

	DX12CommandListHooks::HookCommandList(baseList);
	if (DX12CommandListHookUtils::ShouldRegisterCommandListPointer(baseList))
		DX12CommandListLifecycle::Register(baseList, initialState);
}

static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
		DX12CommandListHookCallbacks::RegisterCommandListBase(baseList, initialState);
		baseList->Release();
	}
}

static void RegisterSubmittedCommandList(IUnknown *commandList)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
		DX12CommandListHookCallbacks::RegisterCommandListBase(baseList, nullptr);
		DX12HuntPublishCommandList(baseList);
		baseList->Release();
	}
}

static void RegisterCommandQueue(IUnknown *commandQueue)
{
	DX12CommandListHooks::HookCommandQueue(commandQueue);
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
	ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12Device::CreateCommandQueue", device, " type=%d flags=0x%x",
		desc ? static_cast<int>(desc->Type) : -1,
		desc ? static_cast<UINT>(desc->Flags) : 0);
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandQueue(device, desc, riid, commandQueue);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandQueue",
		"\"hr\":\"0x%lx\",\"queue\":\"%p\"",
		hr, commandQueue ? *commandQueue : nullptr);
	if (SUCCEEDED(hr) && commandQueue && *commandQueue)
		DX12CommandListHookCallbacks::RegisterCommandQueue(static_cast<IUnknown*>(*commandQueue));
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandAllocator(
	ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12Device::CreateCommandAllocator", device, " type=%d",
		static_cast<int>(type));
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandAllocator(device, type, riid, allocator);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandAllocator",
		"\"hr\":\"0x%lx\",\"allocator\":\"%p\"",
		hr, allocator ? *allocator : nullptr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandList(
	ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
	REFIID riid, void **commandList)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12Device::CreateCommandList", device,
		" nodeMask=%u type=%d allocator=%p initialPso=%p",
		nodeMask, static_cast<int>(type), allocator, initialState);
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandList(device, nodeMask, type, allocator, initialState, riid, commandList);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandList",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		DX12CommandListHookUtils::ForgetRegisteredCommandListPointer(*commandList);
		DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), initialState);
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateCommandList1(
	ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12Device4::CreateCommandList1", device,
		" nodeMask=%u type=%d flags=0x%x",
		nodeMask, static_cast<int>(type), static_cast<UINT>(flags));
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandList1(device, nodeMask, type, flags, riid, commandList);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device4::CreateCommandList1",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		DX12CommandListHookUtils::ForgetRegisteredCommandListPointer(*commandList);
		DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), nullptr);
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
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::UpdateTileMappings", queue,
		" resource=%p regions=%u heap=%p ranges=%u flags=0x%x",
		resource, numResourceRegions, heap, numRanges, static_cast<UINT>(flags));
	PFN_QUEUE_UPDATE_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, 8, PFN_QUEUE_UPDATE_TILE_MAPPINGS, DX12CommandListHookState::OrigQueueUpdateTileMappings, "UpdateTileMappings");
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
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::CopyTileMappings", queue,
		" dst=%p src=%p flags=0x%x", dstResource, srcResource, static_cast<UINT>(flags));
	PFN_QUEUE_COPY_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, 9, PFN_QUEUE_COPY_TILE_MAPPINGS, DX12CommandListHookState::OrigQueueCopyTileMappings, "CopyTileMappings");
	if (original)
		original(queue, dstResource, dstRegionStartCoordinate,
			srcResource, srcRegionStartCoordinate, regionSize, flags);
}

static void STDMETHODCALLTYPE HookedQueueExecuteCommandLists(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	static volatile LONG firstExecuteLog = 0;
	if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
		DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
			"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
			queue, numCommandLists, DX12HotPathState::Get().skipAll,
			DX12InternalReplayState::IsActive() ? "true" : "false");

	if (DX12HotPathState::Get().skipAll) {
		PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
			DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, DX12CommandListHookState::OrigQueueExecuteCommandLists, "ExecuteCommandLists");
		if (original) {
			if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
				DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
					"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
					queue, numCommandLists);
			original(queue, numCommandLists, commandLists);
			if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
				DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
					"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
					queue, numCommandLists);
		}
		if (DX12HuntIsEnabled()) {
			for (UINT i = 0; commandLists && i < numCommandLists; ++i)
				DX12CommandListHookCallbacks::RegisterSubmittedCommandList(commandLists[i]);
		}
		return;
	}

	if (DX12InternalReplayState::IsActive()) {
		PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
			DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, DX12CommandListHookState::OrigQueueExecuteCommandLists, "ExecuteCommandLists");
		if (original)
			original(queue, numCommandLists, commandLists);
		return;
	}

	DX12_PROFILE_SCOPE(ExecuteCommandLists);
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
	PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
		DX12_QUEUE_ORIG(queue, 10, PFN_QUEUE_EXECUTE_COMMAND_LISTS, DX12CommandListHookState::OrigQueueExecuteCommandLists, "ExecuteCommandLists");
	if (original) {
		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		DX12CommandListHookUtils::LogQueueStage("ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		ULONGLONG startTick = GetTickCount64();
#endif
		original(queue, numCommandLists, commandLists);
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		ULONGLONG originalEndTick = GetTickCount64();
		DX12CommandListHookUtils::LogQueueStage("ExecuteCommandLists", "afterOriginal", queue, numCommandLists,
			nullptr, 0, S_OK, originalEndTick - startTick);
#endif
		DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
		for (UINT i = 0; commandLists && i < numCommandLists; ++i)
			DX12CommandListHookCallbacks::RegisterSubmittedCommandList(commandLists[i]);
#if defined(_DEBUG)
		DX12CommandListHookUtils::LogQueueStage("ExecuteCommandLists", "afterNotify", queue, numCommandLists,
			nullptr, 0, S_OK, GetTickCount64() - originalEndTick);
#endif
	}
}

static void STDMETHODCALLTYPE HookedQueueSetMarker(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::SetMarker", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_SET_MARKER original =
		DX12_QUEUE_ORIG(queue, 11, PFN_QUEUE_SET_MARKER, DX12CommandListHookState::OrigQueueSetMarker, "SetMarker");
	if (original)
		original(queue, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedQueueBeginEvent(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::BeginEvent", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_BEGIN_EVENT original =
		DX12_QUEUE_ORIG(queue, 12, PFN_QUEUE_BEGIN_EVENT, DX12CommandListHookState::OrigQueueBeginEvent, "BeginEvent");
	if (original)
		original(queue, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedQueueEndEvent(ID3D12CommandQueue *queue)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::EndEvent", queue);
	PFN_QUEUE_END_EVENT original =
		DX12_QUEUE_ORIG(queue, 13, PFN_QUEUE_END_EVENT, DX12CommandListHookState::OrigQueueEndEvent, "EndEvent");
	if (original)
		original(queue);
}

static HRESULT STDMETHODCALLTYPE HookedQueueSignal(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	if (DX12InternalReplayState::IsActive()) {
		PFN_QUEUE_SIGNAL original =
			DX12_QUEUE_ORIG(queue, 14, PFN_QUEUE_SIGNAL, DX12CommandListHookState::OrigQueueSignal, "Signal");
		return original ? original(queue, fence, value) : E_FAIL;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::Signal", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_SIGNAL original =
		DX12_QUEUE_ORIG(queue, 14, PFN_QUEUE_SIGNAL, DX12CommandListHookState::OrigQueueSignal, "Signal");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("Signal", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("Signal", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueWait(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::Wait", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_WAIT original =
		DX12_QUEUE_ORIG(queue, 15, PFN_QUEUE_WAIT, DX12CommandListHookState::OrigQueueWait, "Wait");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("Wait", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("Wait", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
	ID3D12CommandQueue *queue, UINT64 *frequency)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::GetTimestampFrequency", queue);
	PFN_QUEUE_GET_TIMESTAMP_FREQUENCY original =
		DX12_QUEUE_ORIG(queue, 16, PFN_QUEUE_GET_TIMESTAMP_FREQUENCY, DX12CommandListHookState::OrigQueueGetTimestampFrequency, "GetTimestampFrequency");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("GetTimestampFrequency", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, frequency);
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("GetTimestampFrequency", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
	ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12CommandQueue::GetClockCalibration", queue);
	PFN_QUEUE_GET_CLOCK_CALIBRATION original =
		DX12_QUEUE_ORIG(queue, 17, PFN_QUEUE_GET_CLOCK_CALIBRATION, DX12CommandListHookState::OrigQueueGetClockCalibration, "GetClockCalibration");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("GetClockCalibration", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, gpuTimestamp, cpuTimestamp);
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogQueueStage("GetClockCalibration", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCloseCommandList(ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::Close", commandList);
	PFN_CLOSE_COMMAND_LIST original = DX12_CL_ORIG(commandList, 9, PFN_CLOSE_COMMAND_LIST, DX12CommandListHookState::OrigCloseCommandList, "CloseCommandList");
	return original ? original(commandList) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookedResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
	ID3D12PipelineState *initialState)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_RESET_COMMAND_LIST original =
			DX12_CL_ORIG(commandList, 10, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
		return original ? original(commandList, allocator, initialState) : E_FAIL;
	}
	DX12_PROFILE_SCOPE(ResetCommandList);
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::Reset", commandList,
		" allocator=%p initialPso=%p", allocator, initialState);
	PFN_RESET_COMMAND_LIST original = DX12_CL_ORIG(commandList, 10, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
#if defined(_DEBUG)
	ULONGLONG startTick = GetTickCount64();
	ULONGLONG stageTick = startTick;
	DX12CommandListHookUtils::LogCommandListStageEdge("Reset", "beforeOriginal", commandList);
	stageTick = GetTickCount64();
#endif
	HRESULT hr = original ? original(commandList, allocator, initialState) : E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookUtils::LogCommandListStage("Reset", "afterOriginal", commandList, hr, GetTickCount64() - stageTick);
#endif
	if (SUCCEEDED(hr)) {
#if defined(_DEBUG)
		stageTick = GetTickCount64();
		DX12CommandListHookUtils::LogCommandListStageEdge("Reset", "beforeRuntime", commandList);
#endif
		DX12CommandListRuntime::Reset(commandList, initialState);
#if defined(_DEBUG)
		DX12CommandListHookUtils::LogCommandListStage("Reset", "afterRuntime", commandList, S_OK, GetTickCount64() - stageTick);
		stageTick = GetTickCount64();
		DX12CommandListHookUtils::LogCommandListStageEdge("Reset", "beforeLifecycle", commandList);
#endif
		DX12CommandListLifecycle::Reset(commandList, initialState);
#if defined(_DEBUG)
		DX12CommandListHookUtils::LogCommandListStage("Reset", "afterLifecycle", commandList, S_OK, GetTickCount64() - stageTick);
	}
	DX12CommandListHookUtils::LogCommandListStage("Reset", "end", commandList, hr, GetTickCount64() - startTick);
#else
	}
#endif
	return hr;
}

static void STDMETHODCALLTYPE HookedClearState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ClearState", commandList, " pso=%p", pipelineState);
	DX12CommandListLifecycle::ClearState(commandList, pipelineState);
	PFN_CLEAR_STATE original = DX12_CL_ORIG(commandList, 11, PFN_CLEAR_STATE, DX12CommandListHookState::OrigClearState, "ClearState");
	if (original)
		original(commandList, pipelineState);
}

static void STDMETHODCALLTYPE HookedDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
	UINT startVertexLocation, UINT startInstanceLocation)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_DRAW_INSTANCED original =
			DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawInstanced);

	if (DX12Profiling::ShouldCollectCounters() || DX12DrawHookFlow::NeedsModWork())
		DX12RuntimeState::IncrementFrameDraws();

	if (DX12IaReplacementDrawScope::IsExecuting()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	if (DX12HotPathState::Get().skipBindings && DX12HuntIsEnabled() && !DX12DrawHookFlow::NeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntShouldSkipDraw(
				commandList, false, vertexCountPerInstance, 0, startVertexLocation, 0, 0))
			return;
		DX12HuntRecordDraw(
			commandList, false, vertexCountPerInstance, 0, startVertexLocation, 0, 0);
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::DrawInstanced", commandList,
		" vertices=%u instances=%u startVertex=%u startInstance=%u",
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
			startVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntShouldSkipDraw(
				commandList, false, vertexCountPerInstance, 0, startVertexLocation, 0, 0))
			return;
		DX12HuntRecordDraw(
			commandList, false, vertexCountPerInstance, 0, startVertexLocation, 0, 0);
	}
	if (!DX12DrawHookFlow::NeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INSTANCED original = DX12_CL_ORIG(commandList, 12, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntime::GetStatePtr(commandList);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::Execute(commandList, draw, runtimeState, iaCallbacks);
}

static void STDMETHODCALLTYPE HookedDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
	UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawIndexedInstanced);

	if (DX12Profiling::ShouldCollectCounters() || DX12DrawHookFlow::NeedsModWork())
		DX12RuntimeState::IncrementFrameDraws();

	if (DX12IaReplacementDrawScope::IsExecuting()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}

	if (DX12HotPathState::Get().skipBindings && DX12HuntIsEnabled() && !DX12DrawHookFlow::NeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntShouldSkipDraw(
				commandList, true, 0, indexCountPerInstance, 0, startIndexLocation,
				baseVertexLocation))
			return;
		DX12HuntRecordDraw(
			commandList, true, 0, indexCountPerInstance, 0, startIndexLocation,
			baseVertexLocation);
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::DrawIndexedInstanced", commandList,
		" indices=%u instances=%u startIndex=%u baseVertex=%d startInstance=%u",
		indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation,
		startInstanceLocation);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
			startIndexLocation, baseVertexLocation, startInstanceLocation);
	if (DX12HuntIsEnabled()) {
		if (DX12HuntShouldSkipDraw(
				commandList, true, 0, indexCountPerInstance, 0, startIndexLocation,
				baseVertexLocation))
			return;
		DX12HuntRecordDraw(
			commandList, true, 0, indexCountPerInstance, 0, startIndexLocation,
			baseVertexLocation);
	}
	if (!DX12DrawHookFlow::NeedsModWork()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, 13, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntime::GetStatePtr(commandList);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
		indexCountPerInstance, instanceCount, startIndexLocation,
		baseVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::Execute(commandList, draw, runtimeState, iaCallbacks);
}

static void STDMETHODCALLTYPE HookedDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	DX12_PROFILE_SCOPE(Dispatch);
	if (DX12Profiling::ShouldCollectCounters() ||
	    DX12ModHasActiveShaderOverrides() || DX12ModNeedsPreSkinningUavProbe())
		DX12RuntimeState::IncrementFrameDispatches();

	if (DX12InternalReplayState::IsActive()) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	if (DX12HotPathState::Get().skipBindings && DX12HuntIsEnabled() &&
	    !DX12ModHasActiveShaderOverrides() && !DX12ModNeedsPreSkinningUavProbe()) {
		DX12_PROFILE_FAST_FORWARD();
		DX12HuntRecordDispatch(commandList);
		if (DX12HuntShouldSkipDispatch(commandList))
			return;
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::Dispatch", commandList,
		" groups=%u,%u,%u", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	if (DX12HuntIsEnabled()) {
		DX12HuntRecordDispatch(commandList);
		if (DX12HuntShouldSkipDispatch(commandList))
			return;
	}
	PFN_DISPATCH original = DX12_CL_ORIG(commandList, 14, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
	if (!DX12ModHasActiveShaderOverrides() && !DX12ModNeedsPreSkinningUavProbe()) {
		DX12_PROFILE_FAST_FORWARD();
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	if (!DX12ModHasActiveShaderOverrides() && DX12ModNeedsPreSkinningUavProbe()) {
		const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntime::GetStatePtr(commandList);
		ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState :
			DX12CommandListRuntime::GetPipelineState(commandList);
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
		DX12DispatchHookFlow::Execute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, original);
		return;
	}
	const DX12CommandListRuntimeState *runtimeState = DX12CommandListRuntime::GetStatePtr(commandList);
	DX12DispatchHookFlow::Execute(
		commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
		runtimeState, original);
}

static void STDMETHODCALLTYPE HookedCopyBufferRegion(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
	ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::CopyBufferRegion", commandList,
		" dst=%p dstOffset=%llu src=%p srcOffset=%llu bytes=%llu",
		dstBuffer, static_cast<unsigned long long>(dstOffset), srcBuffer,
		static_cast<unsigned long long>(srcOffset), static_cast<unsigned long long>(numBytes));
	DX12CommandListHookState::OrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
}

static void STDMETHODCALLTYPE HookedCopyTextureRegion(
	ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
	UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::CopyTextureRegion", commandList,
		" dst=%p dstXYZ=%u,%u,%u src=%p srcBox=%p", dst, dstX, dstY, dstZ, src, srcBox);
	DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
}

static void STDMETHODCALLTYPE HookedCopyResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::CopyResource", commandList,
		" dst=%p src=%p", dstResource, srcResource);
	DX12CommandListHookState::OrigCopyResource(commandList, dstResource, srcResource);
}

static void STDMETHODCALLTYPE HookedCopyTiles(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
	const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
	UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::CopyTiles", commandList,
		" tiled=%p buffer=%p offset=%llu flags=0x%x", tiledResource, buffer,
		static_cast<unsigned long long>(bufferStartOffsetInBytes), static_cast<UINT>(flags));
	DX12CommandListHookState::OrigCopyTiles(commandList, tiledResource, tileRegionStartCoordinate,
		tileRegionSize, buffer, bufferStartOffsetInBytes, flags);
}

static void STDMETHODCALLTYPE HookedResolveSubresource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
	ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ResolveSubresource", commandList,
		" dst=%p dstSub=%u src=%p srcSub=%u format=%u",
		dstResource, dstSubresource, srcResource, srcSubresource, static_cast<UINT>(format));
	DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
}

static void STDMETHODCALLTYPE HookedIASetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
			DX12_CL_ORIG(commandList, 20, PFN_IA_SET_PRIMITIVE_TOPOLOGY, DX12CommandListHookState::OrigIASetPrimitiveTopology, "IASetPrimitiveTopology");
		if (original)
			original(commandList, topology);
		return;
	}
	DX12_PROFILE_SCOPE(IASetPrimitiveTopology);

	if (!DX12HotPathState::Get().skipBindings) {
		DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
			" topology=%u", static_cast<UINT>(topology));
		DX12CommandListStateCapture::PrimitiveTopology(commandList, topology);
	} else {
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
	}

	PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
		DX12_CL_ORIG(commandList, 20, PFN_IA_SET_PRIMITIVE_TOPOLOGY, DX12CommandListHookState::OrigIASetPrimitiveTopology, "IASetPrimitiveTopology");
	if (original)
		original(commandList, topology);
}

static void STDMETHODCALLTYPE HookedRSSetViewports(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::RSSetViewports", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetViewports(commandList, count, viewports);
}

static void STDMETHODCALLTYPE HookedRSSetScissorRects(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::RSSetScissorRects", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetScissorRects(commandList, count, rects);
}

static void STDMETHODCALLTYPE HookedOMSetBlendFactor(
	ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4])
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::OMSetBlendFactor", commandList);
	DX12CommandListHookState::OrigOMSetBlendFactor(commandList, blendFactor);
}

static void STDMETHODCALLTYPE HookedOMSetStencilRef(ID3D12GraphicsCommandList *commandList, UINT stencilRef)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::OMSetStencilRef", commandList, " stencilRef=%u", stencilRef);
	DX12CommandListHookState::OrigOMSetStencilRef(commandList, stencilRef);
}

static void STDMETHODCALLTYPE HookedSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_PIPELINE_STATE original =
			DX12_CL_ORIG(commandList, 25, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
		if (original)
			original(commandList, pipelineState);
		return;
	}
	DX12_PROFILE_SCOPE(SetPipelineState);

	if (DX12HotPathState::Get().skipBindings && DX12HuntIsEnabled() &&
	    !DX12ModHasActiveShaderOverrides()) {
		DX12_PROFILE_FAST_FORWARD();
		DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
		DX12HuntSetPipelineState(commandList, pipelineState);
		PFN_SET_PIPELINE_STATE original = DX12_CL_ORIG(commandList, 25, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
		if (original)
			original(commandList, pipelineState);
		return;
	}

	ID3D12PipelineState *activePipelineState =
		DX12CommandListStateCapture::PipelineState(commandList, pipelineState);
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::SetPipelineState", commandList,
		" pso=%p active=%p", pipelineState, activePipelineState);
	PFN_SET_PIPELINE_STATE original = DX12_CL_ORIG(commandList, 25, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
	if (original)
		original(commandList, activePipelineState);
}

static void STDMETHODCALLTYPE HookedResourceBarrier(
	ID3D12GraphicsCommandList *commandList, UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_RESOURCE_BARRIER original =
			DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}
	DX12_PROFILE_SCOPE(ResourceBarrier);
	if (DX12InternalReplayState::IsActive()) {
		PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}

	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ResourceBarrier", commandList, " count=%u", numBarriers);
	DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
	PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, 26, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
	if (original)
		original(commandList, numBarriers, barriers);
}

static void STDMETHODCALLTYPE HookedExecuteBundle(
	ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ExecuteBundle", commandList, " bundle=%p", bundle);
	DX12CommandListCommandRecorder::RecordExecuteBundle(bundle, nullptr);
	PFN_EXECUTE_BUNDLE original = DX12_CL_ORIG(commandList, 27, PFN_EXECUTE_BUNDLE, DX12CommandListHookState::OrigExecuteBundle, "ExecuteBundle");
	if (original)
		original(commandList, bundle);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 42, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);

	if (DX12HotPathState::Get().skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 42, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListStateCapture::GraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 42, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedIASetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_IA_SET_INDEX_BUFFER original =
			DX12_CL_ORIG(commandList, 43, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
		if (original)
			original(commandList, view);
		return;
	}
	DX12_PROFILE_SCOPE(IASetIndexBuffer);

	if (!DX12HotPathState::Get().skipBindings) {
		DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
			" gpu=0x%llx size=%u format=%u",
			view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
			view ? view->SizeInBytes : 0,
			view ? static_cast<UINT>(view->Format) : 0);
		DX12CommandListStateCapture::IndexBuffer(commandList, view);
	} else {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetIndexBuffer(commandList, view);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
	}

	PFN_IA_SET_INDEX_BUFFER original = DX12_CL_ORIG(commandList, 43, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
	if (original)
		original(commandList, view);
}

static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_IA_SET_VERTEX_BUFFERS original =
			DX12_CL_ORIG(commandList, 44, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
		if (original)
			original(commandList, startSlot, count, views);
		return;
	}
	DX12_PROFILE_SCOPE(IASetVertexBuffers);

	if (!DX12HotPathState::Get().skipBindings) {
		DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
			" start=%u count=%u", startSlot, count);
		DX12CommandListStateCapture::VertexBuffers(commandList, startSlot, count, views);
	} else {
		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
	}

	PFN_IA_SET_VERTEX_BUFFERS original =
		DX12_CL_ORIG(commandList, 44, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
	if (original)
		original(commandList, startSlot, count, views);
}

static void STDMETHODCALLTYPE HookedSOSetTargets(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::SOSetTargets", commandList,
		" start=%u count=%u", startSlot, count);
	DX12CommandListHookState::OrigSOSetTargets(commandList, startSlot, count, views);
}

static void STDMETHODCALLTYPE HookedOMSetRenderTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::OMSetRenderTargets", commandList,
		" rtCount=%u singleRange=%u dsv=%p", numRenderTargetDescriptors,
		singleHandleToDescriptorRange, depthStencilDescriptor);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordOutputTargets(commandList, numRenderTargetDescriptors,
			renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
	DX12CommandListHookState::OrigOMSetRenderTargets(commandList, numRenderTargetDescriptors,
		renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
}

static void STDMETHODCALLTYPE HookedClearDepthStencilView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
	D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ClearDepthStencilView", commandList,
		" dsv=0x%llx flags=0x%x rects=%u", static_cast<unsigned long long>(depthStencilView.ptr),
		static_cast<UINT>(clearFlags), numRects);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordDepthStencilClear(commandList, depthStencilView);
	DX12CommandListHookState::OrigClearDepthStencilView(commandList, depthStencilView, clearFlags, depth, stencil, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearRenderTargetView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ClearRenderTargetView", commandList,
		" rtv=0x%llx rects=%u", static_cast<unsigned long long>(renderTargetView.ptr), numRects);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordRenderTargetClear(commandList, renderTargetView);
	DX12CommandListHookState::OrigClearRenderTargetView(commandList, renderTargetView, colorRGBA, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewUint(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	DX12CommandListHookState::OrigClearUnorderedAccessViewUint(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewFloat(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	DX12CommandListHookState::OrigClearUnorderedAccessViewFloat(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

static void STDMETHODCALLTYPE HookedDiscardResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::DiscardResource", commandList, " resource=%p region=%p", resource, region);
	DX12CommandListHookState::OrigDiscardResource(commandList, resource, region);
}

static void STDMETHODCALLTYPE HookedBeginQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::BeginQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigBeginQuery(commandList, queryHeap, type, index);
}

static void STDMETHODCALLTYPE HookedEndQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::EndQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigEndQuery(commandList, queryHeap, type, index);
}

static void STDMETHODCALLTYPE HookedResolveQueryData(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
	UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer, UINT64 alignedDestinationBufferOffset)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ResolveQueryData", commandList,
		" heap=%p type=%u start=%u count=%u dst=%p offset=%llu",
		queryHeap, static_cast<UINT>(type), startIndex, numQueries, destinationBuffer,
		static_cast<unsigned long long>(alignedDestinationBufferOffset));
	DX12CommandListHookState::OrigResolveQueryData(commandList, queryHeap, type, startIndex, numQueries,
		destinationBuffer, alignedDestinationBufferOffset);
}

static void STDMETHODCALLTYPE HookedSetPredication(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
	UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::SetPredication", commandList,
		" buffer=%p offset=%llu op=%u", buffer,
		static_cast<unsigned long long>(alignedBufferOffset), static_cast<UINT>(operation));
	DX12CommandListHookState::OrigSetPredication(commandList, buffer, alignedBufferOffset, operation);
}

static void STDMETHODCALLTYPE HookedCommandListSetMarker(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::SetMarker", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListSetMarker(commandList, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedCommandListBeginEvent(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::BeginEvent", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListBeginEvent(commandList, metadata, data, size);
}

static void STDMETHODCALLTYPE HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::EndEvent", commandList);
	DX12CommandListHookState::OrigCommandListEndEvent(commandList);
}

static void STDMETHODCALLTYPE HookedExecuteIndirect(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
	UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
	ID3D12Resource *countBuffer, UINT64 countBufferOffset)
{
	PFN_EXECUTE_INDIRECT original =
		DX12_CL_ORIG(commandList, 59, PFN_EXECUTE_INDIRECT, DX12CommandListHookState::OrigExecuteIndirect, "ExecuteIndirect");
	if (DX12HotPathState::Get().skipAll) {
		if (original)
			original(commandList, commandSignature, maxCommandCount, argumentBuffer,
				argumentBufferOffset, countBuffer, countBufferOffset);
		return;
	}

	DX12CommandListHookUtils::LogDX12Call("ID3D12GraphicsCommandList::ExecuteIndirect", commandList,
		" signature=%p max=%u args=%p argOffset=%llu count=%p countOffset=%llu",
		commandSignature, maxCommandCount, argumentBuffer,
		static_cast<unsigned long long>(argumentBufferOffset), countBuffer,
		static_cast<unsigned long long>(countBufferOffset));
	DX12CommandListCommandRecorder::RecordExecuteIndirect(commandList);

	// Pre-skin probe for games that dispatch compute shaders via ExecuteIndirect
	// (e.g. ZZZ).  The individual Dispatch inside ExecuteIndirect is invisible
	// to our Dispatch hook, so we intercept at the ExecuteIndirect level and
	// apply binding patches if the current compute PSO has a pre-skin CS hash.
	if (DX12ModNeedsPreSkinningUavProbe()) {
		ID3D12PipelineState *pipelineState = DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState) {
			DX12LogManager::Text("HookedExecuteIndirect: no PSO from runtime state\n");
		} else {
			DX12PsoShaderInfo shaderInfo = {};
			if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
				DX12LogManager::Text("HookedExecuteIndirect: GetShaderInfo failed\n");
			} else if (!shaderInfo.hasCS) {
				DX12LogManager::Text("HookedExecuteIndirect: PSO is not compute (hasCS=false)\n");
			} else {
				DX12LogManager::Text("HookedExecuteIndirect: compute PSO cs=%016llx\n",
					static_cast<unsigned long long>(shaderInfo.cs));
				if (!DX12ModShouldTrackPreSkinBindingsForCs(shaderInfo.cs)) {
					DX12LogManager::Text("HookedExecuteIndirect: CS not in pre-skin list\n");
				} else {
					DX12LogManager::Text("HookedExecuteIndirect: pre-skin CS match!\n");
					std::vector<DX12CurrentComputeUavBinding> uavs, srvs, cbvs;
					std::vector<DX12CurrentRootConstants> rootConstants;
					if (DX12BindingTracker::GetCurrentComputeAll(commandList, &uavs, &srvs, &cbvs, &rootConstants)) {
						DX12LogManager::Text("HookedExecuteIndirect: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
							uavs.size(), srvs.size(), cbvs.size());
						UINT originalVertexCount = 0, overrideVertexCount = 0;
						DX12PreSkinDispatchOverride dispatchOverride;
						if (DX12ModApplyPreSkinningUavReplacement(
							    commandList, shaderInfo.cs,
							    uavs, srvs, cbvs, rootConstants,
							    &originalVertexCount, &overrideVertexCount,
							    &dispatchOverride)) {
							DX12LogManager::Text("HookedExecuteIndirect: pre-skin APPLIED! origVtx=%u newVtx=%u\n",
								originalVertexCount, overrideVertexCount);
						} else {
							DX12LogManager::Text("HookedExecuteIndirect: pre-skin matching failed\n");
						}
					} else {
						DX12LogManager::Text("HookedExecuteIndirect: no compute bindings found\n");
					}
				}
			}
		}
	}

	if (original)
		original(commandList, commandSignature, maxCommandCount, argumentBuffer,
			argumentBufferOffset, countBuffer, countBufferOffset);

	// Restore pre-skin patches that were applied before ExecuteIndirect
	DX12ModRestorePreSkinningUavReplacement(commandList);
}

};

void DX12CommandListHooks::HookCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
	if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		static volatile LONG nonDirectLogs = 0;
		if (InterlockedIncrement(&nonDirectLogs) <= 8)
			DX12LogManager::JsonFunc("DX12CommandQueueState",
				"\"event\":\"ignore_non_direct_hotpath\",\"queue\":\"%p\",\"type\":%d",
				queue, static_cast<int>(desc.Type));
		queue->Release();
		return;
	}

	DX12CommandListHooks::RememberCommandQueueForHotPath(commandQueue);
	DX12CommandQueueState::Set(queue);
	DX12VTableHook queueHooks[] = {
		{DX12CommandQueueHookSlotTypes::ExecuteCommandLists, reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueExecuteCommandLists),
			DX12CommandListHookCallbacks::HookedQueueExecuteCommandLists, "ID3D12CommandQueue::ExecuteCommandLists"},
	};
	DX12HookManager::InstallVTableHooks(queue, queueHooks);
	queue->Release();
}

void DX12CommandListHooks::RememberCommandQueueForHotPath(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;
	AcquireSRWLockExclusive(&DX12CommandListHookState::KnownCommandQueueLock);
	DX12CommandListHookState::KnownCommandQueue = commandQueue;
	ReleaseSRWLockExclusive(&DX12CommandListHookState::KnownCommandQueueLock);
}

void DX12CommandListHooks::ActivateCommandQueueHotPathHooks()
{
	IUnknown *queue = nullptr;
	AcquireSRWLockShared(&DX12CommandListHookState::KnownCommandQueueLock);
	queue = DX12CommandListHookState::KnownCommandQueue;
	ReleaseSRWLockShared(&DX12CommandListHookState::KnownCommandQueueLock);
	if (queue)
		DX12CommandListHooks::HookCommandQueue(queue);
}

void DX12CommandListHooks::HookCommandList(IUnknown *commandList)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&baseList))))
		return;

	void *vtable = *reinterpret_cast<void**>(baseList);
	if (!DX12CommandListHookUtils::ShouldHookCommandListVTable(vtable)) {
		baseList->Release();
		return;
	}

	DX12VTableHook commandListHooks[] = {
		{DX12CommandListHookSlotTypes::Reset, reinterpret_cast<void**>(&DX12CommandListHookState::OrigResetCommandList),
			DX12CommandListHookCallbacks::HookedResetCommandList, "ID3D12GraphicsCommandList::Reset"},
		{DX12CommandListHookSlotTypes::DrawInstanced, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDrawInstanced),
			DX12CommandListHookCallbacks::HookedDrawInstanced, "ID3D12GraphicsCommandList::DrawInstanced"},
		{DX12CommandListHookSlotTypes::DrawIndexedInstanced, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDrawIndexedInstanced),
			DX12CommandListHookCallbacks::HookedDrawIndexedInstanced, "ID3D12GraphicsCommandList::DrawIndexedInstanced"},
		{DX12CommandListHookSlotTypes::Dispatch, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDispatch),
			DX12CommandListHookCallbacks::HookedDispatch, "ID3D12GraphicsCommandList::Dispatch"},
		{DX12CommandListHookSlotTypes::IaSetPrimitiveTopology, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetPrimitiveTopology),
			DX12CommandListHookCallbacks::HookedIASetPrimitiveTopology, "ID3D12GraphicsCommandList::IASetPrimitiveTopology"},
		{DX12CommandListHookSlotTypes::SetPipelineState, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetPipelineState),
			DX12CommandListHookCallbacks::HookedSetPipelineState, "ID3D12GraphicsCommandList::SetPipelineState"},
		{DX12CommandListHookSlotTypes::ResourceBarrier, reinterpret_cast<void**>(&DX12CommandListHookState::OrigResourceBarrier),
			DX12CommandListHookCallbacks::HookedResourceBarrier, "ID3D12GraphicsCommandList::ResourceBarrier"},
		{DX12CommandListHookSlotTypes::ExecuteBundle, reinterpret_cast<void**>(&DX12CommandListHookState::OrigExecuteBundle),
			DX12CommandListHookCallbacks::HookedExecuteBundle, "ID3D12GraphicsCommandList::ExecuteBundle"},
		{DX12CommandListHookSlotTypes::SetDescriptorHeaps, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetDescriptorHeaps),
			DX12CommandListComputeBindingHooks::HookedSetDescriptorHeaps, "ID3D12GraphicsCommandList::SetDescriptorHeaps"},
		{DX12CommandListHookSlotTypes::SetComputeRootSignature, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootSignature),
			DX12CommandListComputeBindingHooks::HookedSetComputeRootSignature, "ID3D12GraphicsCommandList::SetComputeRootSignature"},
		{DX12CommandListHookSlotTypes::SetGraphicsRootSignature, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootSignature),
			DX12CommandListComputeBindingHooks::HookedSetGraphicsRootSignature, "ID3D12GraphicsCommandList::SetGraphicsRootSignature"},
		{DX12CommandListHookSlotTypes::SetComputeRootDescriptorTable, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootDescriptorTable),
			DX12CommandListComputeBindingHooks::HookedSetComputeRootDescriptorTable, "ID3D12GraphicsCommandList::SetComputeRootDescriptorTable"},
		{DX12CommandListHookSlotTypes::SetGraphicsRootDescriptorTable, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable),
			DX12CommandListComputeBindingHooks::HookedSetGraphicsRootDescriptorTable, "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable"},
		{DX12CommandListHookSlotTypes::SetComputeRootConstantBufferView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootConstantBufferView),
			DX12CommandListComputeBindingHooks::HookedSetComputeRootConstantBufferView, "ID3D12GraphicsCommandList::SetComputeRootConstantBufferView"},
		{DX12CommandListHookSlotTypes::SetGraphicsRootConstantBufferView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView),
			DX12CommandListComputeBindingHooks::HookedSetGraphicsRootConstantBufferView, "ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView"},
		{DX12CommandListHookSlotTypes::SetComputeRootShaderResourceView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootShaderResourceView),
			DX12CommandListComputeBindingHooks::HookedSetComputeRootShaderResourceView, "ID3D12GraphicsCommandList::SetComputeRootShaderResourceView"},
		{DX12CommandListHookSlotTypes::SetGraphicsRootShaderResourceView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView),
			DX12CommandListComputeBindingHooks::HookedSetGraphicsRootShaderResourceView, "ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView"},
		{DX12CommandListHookSlotTypes::SetComputeRootUnorderedAccessView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView),
			DX12CommandListComputeBindingHooks::HookedSetComputeRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView"},
		{DX12CommandListHookSlotTypes::SetGraphicsRootUnorderedAccessView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView),
			DX12CommandListHookCallbacks::HookedSetGraphicsRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView"},
		{DX12CommandListHookSlotTypes::IaSetIndexBuffer, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetIndexBuffer),
			DX12CommandListHookCallbacks::HookedIASetIndexBuffer, "ID3D12GraphicsCommandList::IASetIndexBuffer"},
		{DX12CommandListHookSlotTypes::IaSetVertexBuffers, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetVertexBuffers),
			DX12CommandListHookCallbacks::HookedIASetVertexBuffers, "ID3D12GraphicsCommandList::IASetVertexBuffers"},
		{DX12CommandListHookSlotTypes::ExecuteIndirect, reinterpret_cast<void**>(&DX12CommandListHookState::OrigExecuteIndirect),
			DX12CommandListHookCallbacks::HookedExecuteIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect"},
	};
	DX12HookManager::InstallVTableHooks(baseList, commandListHooks);
	baseList->Release();
}

void DX12CommandListHooks::HookCommandListCreation(IUnknown *device)
{
	if (!device)
		return;

	ID3D12Device *baseDevice = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&baseDevice)))) {
		DX12VTableHook deviceHooks[] = {
			{DX12DeviceCommandListHookSlotTypes::CreateCommandQueue, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandQueue),
				DX12CommandListHookCallbacks::HookedCreateCommandQueue, "ID3D12Device::CreateCommandQueue"},
			{DX12DeviceCommandListHookSlotTypes::CreateCommandAllocator, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandAllocator),
				DX12CommandListHookCallbacks::HookedCreateCommandAllocator, "ID3D12Device::CreateCommandAllocator"},
			{DX12DeviceCommandListHookSlotTypes::CreateCommandList, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList),
				DX12CommandListHookCallbacks::HookedCreateCommandList, "ID3D12Device::CreateCommandList"},
		};
		DX12HookManager::InstallVTableHooks(baseDevice, deviceHooks);
		baseDevice->Release();
	}

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{DX12DeviceCommandListHookSlotTypes::Device4CreateCommandList1, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList1),
				DX12CommandListHookCallbacks::HookedCreateCommandList1, "ID3D12Device4::CreateCommandList1"},
		};
		DX12HookManager::InstallVTableHooks(device4, device4Hooks);
		device4->Release();
	}
}












