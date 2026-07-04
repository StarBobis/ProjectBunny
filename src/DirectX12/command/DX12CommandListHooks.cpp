#include "DX12CommandListHooks.hpp"
#include "DX12CommandListHookSignatures.hpp"
#include "DX12CommandListHookState.hpp"
#include "DX12CommandListHookCallbacks.hpp"
#include "DX12CommandListHookDiagnostics.hpp"
#include "DX12CommandListHookRegistry.hpp"
#include "proxy/DX12Wrapper.hpp"

#include <d3d12.h>
#include <unordered_set>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListCommandRecorder.hpp"
#include "DX12CommandListStateFlow.hpp"
#include "DX12CommandListLifecycle.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12CommandQueueExecutionFlow.hpp"
#include "DX12DispatchHookFlow.hpp"
#include "DX12DrawHookFlow.hpp"
#include "DX12ExecuteIndirectRuntime.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12HookManager.hpp"
#include "DX12IaReplacementExecutor.hpp"
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

void DX12CommandListHookState::ReleaseKnownCommandQueue()
{
	IUnknown *queue = nullptr;
	AcquireSRWLockExclusive(&KnownCommandQueueLock);
	queue = KnownCommandQueue;
	KnownCommandQueue = nullptr;
	ReleaseSRWLockExclusive(&KnownCommandQueueLock);
	if (queue)
		queue->Release();
}

#define DX12_CL_ORIG(commandList, slot, type, fallback, name) \
	DX12CommandListHookRegistry::GetCommandListOriginal<type>(commandList, slot, fallback, "ID3D12GraphicsCommandList::" name)

#define DX12_QUEUE_ORIG(queue, slot, type, fallback, name) \
	DX12CommandListHookRegistry::GetCommandQueueOriginal<type>(queue, slot, fallback, "ID3D12CommandQueue::" name)



#include "hooks/DX12CommandListComputeBindingHookCallbacks.hpp"

DX12IaReplacementExecutorCallbacks DX12CommandListHookCallbacks::MakeIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	DX12IaReplacementExecutorCallbacks callbacks;
	callbacks.drawInstanced =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced, DX12IaCommandSignatures::DrawInstanced,
			DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
	callbacks.drawIndexedInstanced =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced, DX12IaCommandSignatures::DrawIndexedInstanced,
			DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
	callbacks.dispatch =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, DX12IaCommandSignatures::Dispatch,
			DX12CommandListHookState::OrigDispatch, "Dispatch");
	callbacks.setIndexBuffer =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, DX12IaCommandSignatures::SetIndexBuffer,
			DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
	callbacks.setVertexBuffers =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, DX12IaCommandSignatures::SetVertexBuffers,
			DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
	callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
	callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
	return callbacks;
}

const DX12IaReplacementExecutorCallbacks &DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(
	ID3D12GraphicsCommandList *commandList)
{
	if (DX12CommandListHookState::IaCallbacksCache.commandList == commandList)
		return DX12CommandListHookState::IaCallbacksCache.callbacks;
	DX12CommandListHookState::IaCallbacksCache.commandList = commandList;
	DX12CommandListHookState::IaCallbacksCache.callbacks = DX12CommandListHookCallbacks::MakeIaReplacementCallbacks(commandList);
	return DX12CommandListHookState::IaCallbacksCache.callbacks;
}

void DX12CommandListHookCallbacks::RegisterCommandListBase(
	ID3D12GraphicsCommandList *baseList, ID3D12PipelineState *initialState)
{
	if (!baseList)
		return;

	DX12CommandListHooks::HookCommandList(baseList);
	if (DX12CommandListHookRegistry::ShouldRegisterCommandListPointer(baseList))
		DX12CommandListLifecycle::Register(baseList, initialState);
}

void DX12CommandListHookCallbacks::RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
		DX12CommandListHookCallbacks::RegisterCommandListBase(baseList, initialState);
		baseList->Release();
	}
}

void DX12CommandListHookCallbacks::RegisterSubmittedCommandList(IUnknown *commandList)
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

void DX12CommandListHookCallbacks::RegisterCommandQueue(IUnknown *commandQueue)
{
	DX12CommandListHooks::HookCommandQueue(commandQueue);
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCreateCommandQueue(
	ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12Device::CreateCommandQueue", device, " type=%d flags=0x%x",
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

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCreateCommandAllocator(
	ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12Device::CreateCommandAllocator", device, " type=%d",
		static_cast<int>(type));
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandAllocator(device, type, riid, allocator);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandAllocator",
		"\"hr\":\"0x%lx\",\"allocator\":\"%p\"",
		hr, allocator ? *allocator : nullptr);
	if (SUCCEEDED(hr) && allocator && *allocator)
		DX12Wrapper::WrapD3D12CommandAllocatorResult(riid, allocator);
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCreateCommandList(
	ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
	REFIID riid, void **commandList)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12Device::CreateCommandList", device,
		" nodeMask=%u type=%d allocator=%p initialPso=%p",
		nodeMask, static_cast<int>(type), allocator, initialState);
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandList(device, nodeMask, type, allocator, initialState, riid, commandList);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandList",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		DX12CommandListHookRegistry::ForgetRegisteredCommandListPointer(*commandList);
		DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), initialState);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCreateCommandList1(
	ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
	D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12Device4::CreateCommandList1", device,
		" nodeMask=%u type=%d flags=0x%x",
		nodeMask, static_cast<int>(type), static_cast<UINT>(flags));
	HRESULT hr = DX12CommandListHookState::OrigCreateCommandList1(device, nodeMask, type, flags, riid, commandList);
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device4::CreateCommandList1",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	if (SUCCEEDED(hr) && commandList && *commandList) {
		DX12CommandListHookRegistry::ForgetRegisteredCommandListPointer(*commandList);
		DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), nullptr);
	}
	return hr;
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueUpdateTileMappings(
	ID3D12CommandQueue *queue, ID3D12Resource *resource, UINT numResourceRegions,
	const D3D12_TILED_RESOURCE_COORDINATE *resourceRegionStartCoordinates,
	const D3D12_TILE_REGION_SIZE *resourceRegionSizes, ID3D12Heap *heap, UINT numRanges,
	const D3D12_TILE_RANGE_FLAGS *rangeFlags, const UINT *heapRangeStartOffsets,
	const UINT *rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::UpdateTileMappings", queue,
		" resource=%p regions=%u heap=%p ranges=%u flags=0x%x",
		resource, numResourceRegions, heap, numRanges, static_cast<UINT>(flags));
	PFN_QUEUE_UPDATE_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::UpdateTileMappings, PFN_QUEUE_UPDATE_TILE_MAPPINGS, DX12CommandListHookState::OrigQueueUpdateTileMappings, "UpdateTileMappings");
	if (original)
		original(queue, resource, numResourceRegions,
			resourceRegionStartCoordinates, resourceRegionSizes, heap, numRanges,
			rangeFlags, heapRangeStartOffsets, rangeTileCounts, flags);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueCopyTileMappings(
	ID3D12CommandQueue *queue, ID3D12Resource *dstResource,
	const D3D12_TILED_RESOURCE_COORDINATE *dstRegionStartCoordinate,
	ID3D12Resource *srcResource, const D3D12_TILED_RESOURCE_COORDINATE *srcRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *regionSize, D3D12_TILE_MAPPING_FLAGS flags)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::CopyTileMappings", queue,
		" dst=%p src=%p flags=0x%x", dstResource, srcResource, static_cast<UINT>(flags));
	PFN_QUEUE_COPY_TILE_MAPPINGS original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::CopyTileMappings, PFN_QUEUE_COPY_TILE_MAPPINGS, DX12CommandListHookState::OrigQueueCopyTileMappings, "CopyTileMappings");
	if (original)
		original(queue, dstResource, dstRegionStartCoordinate,
			srcResource, srcRegionStartCoordinate, regionSize, flags);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueExecuteCommandLists(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	PFN_QUEUE_EXECUTE_COMMAND_LISTS original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::ExecuteCommandLists, PFN_QUEUE_EXECUTE_COMMAND_LISTS, DX12CommandListHookState::OrigQueueExecuteCommandLists, "ExecuteCommandLists");
	DX12CommandQueueExecutionFlow::Execute(
		queue, numCommandLists, commandLists, original,
		DX12CommandListHookCallbacks::RegisterSubmittedCommandList);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueSetMarker(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::SetMarker", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_SET_MARKER original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::SetMarker, PFN_QUEUE_SET_MARKER, DX12CommandListHookState::OrigQueueSetMarker, "SetMarker");
	if (original)
		original(queue, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueBeginEvent(
	ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::BeginEvent", queue, " metadata=%u size=%u", metadata, size);
	PFN_QUEUE_BEGIN_EVENT original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::BeginEvent, PFN_QUEUE_BEGIN_EVENT, DX12CommandListHookState::OrigQueueBeginEvent, "BeginEvent");
	if (original)
		original(queue, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueEndEvent(ID3D12CommandQueue *queue)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::EndEvent", queue);
	PFN_QUEUE_END_EVENT original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::EndEvent, PFN_QUEUE_END_EVENT, DX12CommandListHookState::OrigQueueEndEvent, "EndEvent");
	if (original)
		original(queue);
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueSignal(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	if (DX12InternalReplayState::IsActive()) {
		PFN_QUEUE_SIGNAL original =
			DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::Signal, PFN_QUEUE_SIGNAL, DX12CommandListHookState::OrigQueueSignal, "Signal");
		return original ? original(queue, fence, value) : E_FAIL;
	}

	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::Signal", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_SIGNAL original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::Signal, PFN_QUEUE_SIGNAL, DX12CommandListHookState::OrigQueueSignal, "Signal");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("Signal", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("Signal", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueWait(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::Wait", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_WAIT original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::Wait, PFN_QUEUE_WAIT, DX12CommandListHookState::OrigQueueWait, "Wait");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("Wait", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, fence, value);
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("Wait", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueGetTimestampFrequency(
	ID3D12CommandQueue *queue, UINT64 *frequency)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::GetTimestampFrequency", queue);
	PFN_QUEUE_GET_TIMESTAMP_FREQUENCY original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::GetTimestampFrequency, PFN_QUEUE_GET_TIMESTAMP_FREQUENCY, DX12CommandListHookState::OrigQueueGetTimestampFrequency, "GetTimestampFrequency");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("GetTimestampFrequency", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, frequency);
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("GetTimestampFrequency", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueGetClockCalibration(
	ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::GetClockCalibration", queue);
	PFN_QUEUE_GET_CLOCK_CALIBRATION original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::GetClockCalibration, PFN_QUEUE_GET_CLOCK_CALIBRATION, DX12CommandListHookState::OrigQueueGetClockCalibration, "GetClockCalibration");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("GetClockCalibration", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, gpuTimestamp, cpuTimestamp);
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogQueueStage("GetClockCalibration", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCloseCommandList(ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Close", commandList);
	PFN_CLOSE_COMMAND_LIST original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Close, PFN_CLOSE_COMMAND_LIST, DX12CommandListHookState::OrigCloseCommandList, "CloseCommandList");
	return original ? original(commandList) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
	ID3D12PipelineState *initialState)
{
	if (DX12HotPathState::skipAll) {
		PFN_RESET_COMMAND_LIST original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
		return original ? original(commandList, allocator, initialState) : E_FAIL;
	}
	DX12_PROFILE_SCOPE(ResetCommandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Reset", commandList,
		" allocator=%p initialPso=%p", allocator, initialState);
	PFN_RESET_COMMAND_LIST original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
#if defined(_DEBUG)
	ULONGLONG startTick = GetTickCount64();
	ULONGLONG stageTick = startTick;
	DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeOriginal", commandList);
	stageTick = GetTickCount64();
#endif
	HRESULT hr = original ? original(commandList, allocator, initialState) : E_FAIL;
#if defined(_DEBUG)
	DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterOriginal", commandList, hr, GetTickCount64() - stageTick);
#endif
	if (SUCCEEDED(hr)) {
#if defined(_DEBUG)
		stageTick = GetTickCount64();
		DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeRuntime", commandList);
#endif
		DX12CommandListRuntime::Reset(commandList, initialState);
#if defined(_DEBUG)
		DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterRuntime", commandList, S_OK, GetTickCount64() - stageTick);
		stageTick = GetTickCount64();
		DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeLifecycle", commandList);
#endif
		DX12CommandListLifecycle::Reset(commandList, initialState);
#if defined(_DEBUG)
		DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterLifecycle", commandList, S_OK, GetTickCount64() - stageTick);
	}
	DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "end", commandList, hr, GetTickCount64() - startTick);
#else
	}
#endif
	return hr;
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearState", commandList, " pso=%p", pipelineState);
	DX12CommandListLifecycle::ClearState(commandList, pipelineState);
	PFN_CLEAR_STATE original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ClearState, PFN_CLEAR_STATE, DX12CommandListHookState::OrigClearState, "ClearState");
	if (original)
		original(commandList, pipelineState);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
	UINT startVertexLocation, UINT startInstanceLocation)
{
	// 真·快速路径：skipAll 或 flags word 无 draw 级工作 → 单次位测试替代 9 个函数调用
	if (DX12HotPathState::skipAll || !(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DRAW_WORK)) {
		PFN_DRAW_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawInstanced);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::ExecuteHook(commandList, draw, iaCallbacks);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
	UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
	// 真·快速路径：skipAll 或 flags word 无 draw 级工作 → 单次位测试替代 9 个函数调用
	if (DX12HotPathState::skipAll || !(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DRAW_WORK)) {
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	DX12_PROFILE_SCOPE(DrawIndexedInstanced);
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
		indexCountPerInstance, instanceCount, startIndexLocation,
		baseVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::ExecuteHook(commandList, draw, iaCallbacks);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	// 真·快速路径：flags word 无 dispatch 级工作 → 单次位测试替代 skipAll+PreSkin 双重判断
	if (!(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DISPATCH_WORK)) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	PFN_DISPATCH original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
	DX12_PROFILE_SCOPE(Dispatch);
	DX12DispatchHookFlow::ExecuteHook(
		commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ, original);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyBufferRegion(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
	ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyBufferRegion", commandList,
		" dst=%p dstOffset=%llu src=%p srcOffset=%llu bytes=%llu",
		dstBuffer, static_cast<unsigned long long>(dstOffset), srcBuffer,
		static_cast<unsigned long long>(srcOffset), static_cast<unsigned long long>(numBytes));
	DX12CommandListHookState::OrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyTextureRegion(
	ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
	UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyTextureRegion", commandList,
		" dst=%p dstXYZ=%u,%u,%u src=%p srcBox=%p", dst, dstX, dstY, dstZ, src, srcBox);
	DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyResource", commandList,
		" dst=%p src=%p", dstResource, srcResource);
	DX12CommandListHookState::OrigCopyResource(commandList, dstResource, srcResource);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyTiles(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
	const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
	UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyTiles", commandList,
		" tiled=%p buffer=%p offset=%llu flags=0x%x", tiledResource, buffer,
		static_cast<unsigned long long>(bufferStartOffsetInBytes), static_cast<UINT>(flags));
	DX12CommandListHookState::OrigCopyTiles(commandList, tiledResource, tileRegionStartCoordinate,
		tileRegionSize, buffer, bufferStartOffsetInBytes, flags);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResolveSubresource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
	ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ResolveSubresource", commandList,
		" dst=%p dstSub=%u src=%p srcSub=%u format=%u",
		dstResource, dstSubresource, srcResource, srcSubresource, static_cast<UINT>(format));
	DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology, PFN_IA_SET_PRIMITIVE_TOPOLOGY, DX12CommandListHookState::OrigIASetPrimitiveTopology, "IASetPrimitiveTopology");
		if (original)
			original(commandList, topology);
		return;
	}
	DX12_PROFILE_SCOPE(IASetPrimitiveTopology);

	DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, topology);
	PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology, PFN_IA_SET_PRIMITIVE_TOPOLOGY, DX12CommandListHookState::OrigIASetPrimitiveTopology, "IASetPrimitiveTopology");
	if (original)
		original(commandList, topology);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedRSSetViewports(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::RSSetViewports", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetViewports(commandList, count, viewports);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedRSSetScissorRects(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::RSSetScissorRects", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetScissorRects(commandList, count, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedOMSetBlendFactor(
	ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4])
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::OMSetBlendFactor", commandList);
	DX12CommandListHookState::OrigOMSetBlendFactor(commandList, blendFactor);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedOMSetStencilRef(ID3D12GraphicsCommandList *commandList, UINT stencilRef)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::OMSetStencilRef", commandList, " stencilRef=%u", stencilRef);
	DX12CommandListHookState::OrigOMSetStencilRef(commandList, stencilRef);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (DX12HotPathState::skipAll) {
		PFN_SET_PIPELINE_STATE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
		if (original)
			original(commandList, pipelineState);
		return;
	}
	DX12_PROFILE_SCOPE(SetPipelineState);

	ID3D12PipelineState *activePipelineState =
		DX12CommandListStateFlow::CapturePipelineState(commandList, pipelineState);
	PFN_SET_PIPELINE_STATE original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
	if (original)
		original(commandList, activePipelineState);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResourceBarrier(
	ID3D12GraphicsCommandList *commandList, UINT numBarriers,
	const D3D12_RESOURCE_BARRIER *barriers)
{
	if (DX12HotPathState::skipAll) {
		PFN_RESOURCE_BARRIER original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}
	DX12_PROFILE_SCOPE(ResourceBarrier);
	DX12CommandListStateFlow::CaptureResourceBarrier(commandList, numBarriers, barriers);
	PFN_RESOURCE_BARRIER original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
	if (original)
		original(commandList, numBarriers, barriers);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedExecuteBundle(
	ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ExecuteBundle", commandList, " bundle=%p", bundle);
	DX12CommandListCommandRecorder::RecordExecuteBundle(bundle, nullptr);
	PFN_EXECUTE_BUNDLE original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteBundle, PFN_EXECUTE_BUNDLE, DX12CommandListHookState::OrigExecuteBundle, "ExecuteBundle");
	if (original)
		original(commandList, bundle);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSetGraphicsRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);

	if (DX12HotPathState::skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListStateCapture::GraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
	if (original)
		original(commandList, rootParameterIndex, address);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_INDEX_BUFFER original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
		if (original)
			original(commandList, view);
		return;
	}
	DX12_PROFILE_SCOPE(IASetIndexBuffer);

	DX12CommandListStateFlow::CaptureIndexBuffer(commandList, view);
	PFN_IA_SET_INDEX_BUFFER original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
	if (original)
		original(commandList, view);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_VERTEX_BUFFERS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
		if (original)
			original(commandList, startSlot, count, views);
		return;
	}
	DX12_PROFILE_SCOPE(IASetVertexBuffers);

	DX12CommandListStateFlow::CaptureVertexBuffers(commandList, startSlot, count, views);
	PFN_IA_SET_VERTEX_BUFFERS original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
	if (original)
		original(commandList, startSlot, count, views);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSOSetTargets(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SOSetTargets", commandList,
		" start=%u count=%u", startSlot, count);
	DX12CommandListHookState::OrigSOSetTargets(commandList, startSlot, count, views);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedOMSetRenderTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::OMSetRenderTargets", commandList,
		" rtCount=%u singleRange=%u dsv=%p", numRenderTargetDescriptors,
		singleHandleToDescriptorRange, depthStencilDescriptor);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordOutputTargets(commandList, numRenderTargetDescriptors,
			renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
	DX12CommandListHookState::OrigOMSetRenderTargets(commandList, numRenderTargetDescriptors,
		renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearDepthStencilView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
	D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearDepthStencilView", commandList,
		" dsv=0x%llx flags=0x%x rects=%u", static_cast<unsigned long long>(depthStencilView.ptr),
		static_cast<UINT>(clearFlags), numRects);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordDepthStencilClear(commandList, depthStencilView);
	DX12CommandListHookState::OrigClearDepthStencilView(commandList, depthStencilView, clearFlags, depth, stencil, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearRenderTargetView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearRenderTargetView", commandList,
		" rtv=0x%llx rects=%u", static_cast<unsigned long long>(renderTargetView.ptr), numRects);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordRenderTargetClear(commandList, renderTargetView);
	DX12CommandListHookState::OrigClearRenderTargetView(commandList, renderTargetView, colorRGBA, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearUnorderedAccessViewUint(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	DX12CommandListHookState::OrigClearUnorderedAccessViewUint(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearUnorderedAccessViewFloat(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat", commandList,
		" resource=%p gpu=0x%llx cpu=0x%llx rects=%u", resource,
		static_cast<unsigned long long>(viewGpuHandleInCurrentHeap.ptr),
		static_cast<unsigned long long>(viewCpuHandle.ptr), numRects);
	DX12CommandListHookState::OrigClearUnorderedAccessViewFloat(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
		resource, values, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDiscardResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::DiscardResource", commandList, " resource=%p region=%p", resource, region);
	DX12CommandListHookState::OrigDiscardResource(commandList, resource, region);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedBeginQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::BeginQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigBeginQuery(commandList, queryHeap, type, index);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedEndQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::EndQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigEndQuery(commandList, queryHeap, type, index);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResolveQueryData(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
	UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer, UINT64 alignedDestinationBufferOffset)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ResolveQueryData", commandList,
		" heap=%p type=%u start=%u count=%u dst=%p offset=%llu",
		queryHeap, static_cast<UINT>(type), startIndex, numQueries, destinationBuffer,
		static_cast<unsigned long long>(alignedDestinationBufferOffset));
	DX12CommandListHookState::OrigResolveQueryData(commandList, queryHeap, type, startIndex, numQueries,
		destinationBuffer, alignedDestinationBufferOffset);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSetPredication(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
	UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SetPredication", commandList,
		" buffer=%p offset=%llu op=%u", buffer,
		static_cast<unsigned long long>(alignedBufferOffset), static_cast<UINT>(operation));
	DX12CommandListHookState::OrigSetPredication(commandList, buffer, alignedBufferOffset, operation);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListSetMarker(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SetMarker", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListSetMarker(commandList, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListBeginEvent(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::BeginEvent", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListBeginEvent(commandList, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::EndEvent", commandList);
	DX12CommandListHookState::OrigCommandListEndEvent(commandList);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedExecuteIndirect(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
	UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
	ID3D12Resource *countBuffer, UINT64 countBufferOffset)
{
	PFN_EXECUTE_INDIRECT original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteIndirect, PFN_EXECUTE_INDIRECT, DX12CommandListHookState::OrigExecuteIndirect, "ExecuteIndirect");
	if (DX12HotPathState::skipAll) {
		if (original)
			original(commandList, commandSignature, maxCommandCount, argumentBuffer,
				argumentBufferOffset, countBuffer, countBufferOffset);
		return;
	}

	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ExecuteIndirect", commandList,
		" signature=%p max=%u args=%p argOffset=%llu count=%p countOffset=%llu",
		commandSignature, maxCommandCount, argumentBuffer,
		static_cast<unsigned long long>(argumentBufferOffset), countBuffer,
		static_cast<unsigned long long>(countBufferOffset));
	DX12CommandListCommandRecorder::RecordExecuteIndirect(commandList);

	DX12ExecuteIndirectRuntime::Execute(commandList, commandSignature, maxCommandCount,
		argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset, original);
}


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
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::ExecuteCommandLists, reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueExecuteCommandLists),
			DX12CommandListHookCallbacks::HookedQueueExecuteCommandLists, "ID3D12CommandQueue::ExecuteCommandLists"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::Signal, reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueSignal),
			DX12CommandListHookCallbacks::HookedQueueSignal, "ID3D12CommandQueue::Signal"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::Wait, reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueWait),
			DX12CommandListHookCallbacks::HookedQueueWait, "ID3D12CommandQueue::Wait"},
	};
	DX12HookManager::InstallVTableHooks(queue, queueHooks);
	queue->Release();
}

void DX12CommandListHooks::RememberCommandQueueForHotPath(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;
	commandQueue->AddRef();
	IUnknown *oldQueue = nullptr;
	AcquireSRWLockExclusive(&DX12CommandListHookState::KnownCommandQueueLock);
	oldQueue = DX12CommandListHookState::KnownCommandQueue;
	DX12CommandListHookState::KnownCommandQueue = commandQueue;
	ReleaseSRWLockExclusive(&DX12CommandListHookState::KnownCommandQueueLock);
	if (oldQueue)
		oldQueue->Release();
}

void DX12CommandListHooks::ActivateCommandQueueHotPathHooks()
{
	IUnknown *queue = nullptr;
	AcquireSRWLockShared(&DX12CommandListHookState::KnownCommandQueueLock);
	queue = DX12CommandListHookState::KnownCommandQueue;
	if (queue)
		queue->AddRef();
	ReleaseSRWLockShared(&DX12CommandListHookState::KnownCommandQueueLock);
	if (queue) {
		DX12CommandListHooks::HookCommandQueue(queue);
		queue->Release();
	}
}

void DX12CommandListHooks::HookCommandList(IUnknown *commandList)
{
	if (!commandList)
		return;

	ID3D12GraphicsCommandList *baseList = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&baseList))))
		return;

	void *vtable = *reinterpret_cast<void**>(baseList);
	if (!DX12CommandListHookRegistry::ShouldHookCommandListVTable(vtable)) {
		baseList->Release();
		return;
	}

	DX12VTableHook commandListHooks[] = {
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Close, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCloseCommandList),
			DX12CommandListHookCallbacks::HookedCloseCommandList, "ID3D12GraphicsCommandList::Close"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, reinterpret_cast<void**>(&DX12CommandListHookState::OrigResetCommandList),
			DX12CommandListHookCallbacks::HookedResetCommandList, "ID3D12GraphicsCommandList::Reset"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDrawInstanced),
			DX12CommandListHookCallbacks::HookedDrawInstanced, "ID3D12GraphicsCommandList::DrawInstanced"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDrawIndexedInstanced),
			DX12CommandListHookCallbacks::HookedDrawIndexedInstanced, "ID3D12GraphicsCommandList::DrawIndexedInstanced"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, reinterpret_cast<void**>(&DX12CommandListHookState::OrigDispatch),
			DX12CommandListHookCallbacks::HookedDispatch, "ID3D12GraphicsCommandList::Dispatch"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetPrimitiveTopology),
			DX12CommandListHookCallbacks::HookedIASetPrimitiveTopology, "ID3D12GraphicsCommandList::IASetPrimitiveTopology"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetPipelineState),
			DX12CommandListHookCallbacks::HookedSetPipelineState, "ID3D12GraphicsCommandList::SetPipelineState"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier, reinterpret_cast<void**>(&DX12CommandListHookState::OrigResourceBarrier),
			DX12CommandListHookCallbacks::HookedResourceBarrier, "ID3D12GraphicsCommandList::ResourceBarrier"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteBundle, reinterpret_cast<void**>(&DX12CommandListHookState::OrigExecuteBundle),
			DX12CommandListHookCallbacks::HookedExecuteBundle, "ID3D12GraphicsCommandList::ExecuteBundle"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetDescriptorHeaps),
			DX12CommandListComputeBindingHookCallbacks::HookedSetDescriptorHeaps, "ID3D12GraphicsCommandList::SetDescriptorHeaps"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootSignature),
			DX12CommandListComputeBindingHookCallbacks::HookedSetComputeRootSignature, "ID3D12GraphicsCommandList::SetComputeRootSignature"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootSignature),
			DX12CommandListComputeBindingHookCallbacks::HookedSetGraphicsRootSignature, "ID3D12GraphicsCommandList::SetGraphicsRootSignature"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootDescriptorTable),
			DX12CommandListComputeBindingHookCallbacks::HookedSetComputeRootDescriptorTable, "ID3D12GraphicsCommandList::SetComputeRootDescriptorTable"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable),
			DX12CommandListComputeBindingHookCallbacks::HookedSetGraphicsRootDescriptorTable, "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootConstantBufferView),
			DX12CommandListComputeBindingHookCallbacks::HookedSetComputeRootConstantBufferView, "ID3D12GraphicsCommandList::SetComputeRootConstantBufferView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView),
			DX12CommandListComputeBindingHookCallbacks::HookedSetGraphicsRootConstantBufferView, "ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootShaderResourceView),
			DX12CommandListComputeBindingHookCallbacks::HookedSetComputeRootShaderResourceView, "ID3D12GraphicsCommandList::SetComputeRootShaderResourceView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView),
			DX12CommandListComputeBindingHookCallbacks::HookedSetGraphicsRootShaderResourceView, "ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView),
			DX12CommandListComputeBindingHookCallbacks::HookedSetComputeRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, reinterpret_cast<void**>(&DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView),
			DX12CommandListHookCallbacks::HookedSetGraphicsRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetIndexBuffer),
			DX12CommandListHookCallbacks::HookedIASetIndexBuffer, "ID3D12GraphicsCommandList::IASetIndexBuffer"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, reinterpret_cast<void**>(&DX12CommandListHookState::OrigIASetVertexBuffers),
			DX12CommandListHookCallbacks::HookedIASetVertexBuffers, "ID3D12GraphicsCommandList::IASetVertexBuffers"},
		{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteIndirect, reinterpret_cast<void**>(&DX12CommandListHookState::OrigExecuteIndirect),
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
			{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandQueue, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandQueue),
				DX12CommandListHookCallbacks::HookedCreateCommandQueue, "ID3D12Device::CreateCommandQueue"},
			{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandAllocator, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandAllocator),
				DX12CommandListHookCallbacks::HookedCreateCommandAllocator, "ID3D12Device::CreateCommandAllocator"},
			{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandList, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList),
				DX12CommandListHookCallbacks::HookedCreateCommandList, "ID3D12Device::CreateCommandList"},
		};
		DX12HookManager::InstallVTableHooks(baseDevice, deviceHooks);
		baseDevice->Release();
	}

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{DX12CommandListHookSlotTypes::ID3D12Device4::CreateCommandList1, reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList1),
				DX12CommandListHookCallbacks::HookedCreateCommandList1, "ID3D12Device4::CreateCommandList1"},
		};
		DX12HookManager::InstallVTableHooks(device4, device4Hooks);
		device4->Release();
	}
}












