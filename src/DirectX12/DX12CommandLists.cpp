// Consolidated implementation module: DX12CommandLists.cpp
// Original source names are retained in #line diagnostics.

// ---- DX12CommandListHooks.cpp ----
#line 1 "DX12CommandListHooks.cpp"
#include "DX12Infrastructure.hpp"
#include "DX12Integration.hpp"

#include <d3d12.h>
#include <unordered_set>

#include "DX12Foundation.hpp"
#include "DX12RuntimeInterfaces.hpp"

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




// ---- DX12CommandListComputeBindingHookCallbacks.hpp ----
#line 1 "DX12CommandListComputeBindingHookCallbacks.hpp"
class DX12CommandListComputeBindingHookCallbacks
{
public:
	// DX11 风格：skipAll=1 时所有 hook 零开销直通。
	// PreSkin 需要 compute 绑定跟踪 → skipAll 旁路。
	static bool ShouldSkipComputeBinding()
	{
		return DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking();
	}

	static void STDMETHODCALLTYPE HookedSetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList, UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_DESCRIPTOR_HEAPS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
			if (original)
				original(commandList, count, heaps);
			return;
		}
		DX12_PROFILE_SCOPE(SetDescriptorHeaps);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(commandList, count, heaps);
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
		if (original)
			original(commandList, count, heaps);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootSignature);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootSignature);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, rootSignature, false);
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
		if (original)
			original(commandList, rootSignature);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootSignature);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootSignature);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, rootSignature, true);
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
		if (original)
			original(commandList, rootSignature);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptorTable);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, rootParameterIndex, baseDescriptor, false);
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptorTable);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, rootParameterIndex, baseDescriptor, true);
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootConstants);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_32BIT_CONSTANT original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetComputeRoot32BitConstant, "SetComputeRoot32BitConstant");
			if (original)
				original(commandList, rootParameterIndex, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, rootParameterIndex, srcData, destOffset, false);
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetComputeRoot32BitConstant, "SetComputeRoot32BitConstant");
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootConstants);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_32BIT_CONSTANT original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant, "SetGraphicsRoot32BitConstant");
			if (original)
				original(commandList, rootParameterIndex, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, rootParameterIndex, srcData, destOffset, true);
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant, PFN_SET_ROOT_32BIT_CONSTANT, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant, "SetGraphicsRoot32BitConstant");
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
		const void *srcData, UINT destOffset)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootConstants);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_32BIT_CONSTANTS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetComputeRoot32BitConstants, "SetComputeRoot32BitConstants");
			if (original)
				original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset, false);
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetComputeRoot32BitConstants, "SetComputeRoot32BitConstants");
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
		const void *srcData, UINT destOffset)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootConstants);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_32BIT_CONSTANTS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants, "SetGraphicsRoot32BitConstants");
			if (original)
				original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset, true);
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants, PFN_SET_ROOT_32BIT_CONSTANTS, DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants, "SetGraphicsRoot32BitConstants");
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootConstantBufferView, "SetComputeRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootConstantBufferView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootConstantBufferView, "SetComputeRootConstantBufferView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView, "SetGraphicsRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address, true,
			"ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView, "SetGraphicsRootConstantBufferView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootShaderResourceView, "SetComputeRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootShaderResourceView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootShaderResourceView, "SetComputeRootShaderResourceView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (DX12HotPathState::skipAll) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView, "SetGraphicsRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address, true,
			"ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView, "SetGraphicsRootShaderResourceView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}
};

#line 202 "DX12CommandListHooks.cpp"
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
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(CommandQueueHook, Original);
		hr = DX12CommandListHookState::OrigCreateCommandQueue(device, desc, riid, commandQueue);
	}
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandQueue",
		"\"hr\":\"0x%lx\",\"queue\":\"%p\"",
		hr, commandQueue ? *commandQueue : nullptr);
	{
		DX12_CREATE_TIMING_SCOPE(CommandQueueHook, Wrap);
		if (SUCCEEDED(hr) && commandQueue && *commandQueue)
			DX12CommandListHookCallbacks::RegisterCommandQueue(static_cast<IUnknown*>(*commandQueue));
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCreateCommandAllocator(
	ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12Device::CreateCommandAllocator", device, " type=%d",
		static_cast<int>(type));
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(CommandAllocatorHook, Original);
		hr = DX12CommandListHookState::OrigCreateCommandAllocator(device, type, riid, allocator);
	}
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandAllocator",
		"\"hr\":\"0x%lx\",\"allocator\":\"%p\"",
		hr, allocator ? *allocator : nullptr);
	{
		DX12_CREATE_TIMING_SCOPE(CommandAllocatorHook, Wrap);
		if (SUCCEEDED(hr) && allocator && *allocator)
			DX12Wrapper::WrapD3D12CommandAllocatorResult(riid, allocator);
	}
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
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(CommandListHook, Original);
		hr = DX12CommandListHookState::OrigCreateCommandList(
			device, nodeMask, type, allocator, initialState, riid, commandList);
	}
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandList",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	{
		DX12_CREATE_TIMING_SCOPE(CommandListHook, Record);
		if (SUCCEEDED(hr) && commandList && *commandList) {
			DX12CommandListHookRegistry::ForgetRegisteredCommandListPointer(*commandList);
			DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), initialState);
		}
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
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(CommandListHook, Original);
		hr = DX12CommandListHookState::OrigCreateCommandList1(
			device, nodeMask, type, flags, riid, commandList);
	}
	DX12FrameAnalysis::LogJsonFunc("ID3D12Device4::CreateCommandList1",
		"\"hr\":\"0x%lx\",\"commandList\":\"%p\"",
		hr, commandList ? *commandList : nullptr);
	{
		DX12_CREATE_TIMING_SCOPE(CommandListHook, Record);
		if (SUCCEEDED(hr) && commandList && *commandList) {
			DX12CommandListHookRegistry::ForgetRegisteredCommandListPointer(*commandList);
			DX12CommandListHookCallbacks::RegisterCommandList(static_cast<IUnknown*>(*commandList), nullptr);
		}
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiExecuteCommandLists);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiQueueSignal);
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
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("Signal", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	const UINT64 completedBefore =
		DX12OuterTimingProbeUtils::TimingFenceCompletedValue(fence);
	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	HRESULT hr = original(queue, fence, value);
	const UINT64 completedAfter =
		DX12OuterTimingProbeUtils::TimingFenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogQueueFenceSlice(
		"Signal",
		"hook_original",
		queue,
		fence,
		value,
		completedBefore,
		completedAfter,
		hr,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("Signal", "afterOriginal", queue, 0, fence, value, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedQueueWait(
	ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiQueueWait);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12CommandQueue::Wait", queue, " fence=%p value=%llu",
		fence, static_cast<unsigned long long>(value));
	PFN_QUEUE_WAIT original =
		DX12_QUEUE_ORIG(queue, DX12CommandListHookSlotTypes::ID3D12CommandQueue::Wait, PFN_QUEUE_WAIT, DX12CommandListHookState::OrigQueueWait, "Wait");
	if (!original)
		return E_FAIL;
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("Wait", "beforeOriginal", queue, 0, fence, value);
	ULONGLONG startTick = GetTickCount64();
#endif
	const UINT64 completedBefore =
		DX12OuterTimingProbeUtils::TimingFenceCompletedValue(fence);
	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	HRESULT hr = original(queue, fence, value);
	const UINT64 completedAfter =
		DX12OuterTimingProbeUtils::TimingFenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogQueueFenceSlice(
		"Wait",
		"hook_original",
		queue,
		fence,
		value,
		completedBefore,
		completedAfter,
		hr,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
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
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("GetTimestampFrequency", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, frequency);
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
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
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("GetClockCalibration", "beforeOriginal", queue);
	ULONGLONG startTick = GetTickCount64();
#endif
	HRESULT hr = original(queue, gpuTimestamp, cpuTimestamp);
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogQueueStage("GetClockCalibration", "afterOriginal", queue, 0, nullptr, 0, hr,
		GetTickCount64() - startTick);
#endif
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCloseCommandList(ID3D12GraphicsCommandList *commandList)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCloseCommandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Close", commandList);
	PFN_CLOSE_COMMAND_LIST original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Close, PFN_CLOSE_COMMAND_LIST, DX12CommandListHookState::OrigCloseCommandList, "CloseCommandList");
	return original ? original(commandList) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
	ID3D12PipelineState *initialState)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResetCommandList);
	if (DX12HotPathState::skipAll) {
		PFN_RESET_COMMAND_LIST original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
		return original ? original(commandList, allocator, initialState) : E_FAIL;
	}
	DX12_PROFILE_SCOPE(ResetCommandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Reset", commandList,
		" allocator=%p initialPso=%p", allocator, initialState);
	PFN_RESET_COMMAND_LIST original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	ULONGLONG startTick = GetTickCount64();
	ULONGLONG stageTick = startTick;
	DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeOriginal", commandList);
	stageTick = GetTickCount64();
#endif
	HRESULT hr = original ? original(commandList, allocator, initialState) : E_FAIL;
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
	DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterOriginal", commandList, hr, GetTickCount64() - stageTick);
#endif
	if (SUCCEEDED(hr)) {
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		stageTick = GetTickCount64();
		DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeRuntime", commandList);
#endif
		DX12CommandListRuntime::Reset(commandList, initialState);
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterRuntime", commandList, S_OK, GetTickCount64() - stageTick);
		stageTick = GetTickCount64();
		DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "beforeLifecycle", commandList);
#endif
		DX12CommandListLifecycle::Reset(commandList, initialState);
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDrawInstanced);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDispatch);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyBufferRegion);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCopyBufferRegion)
			DX12CommandListHookState::OrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyTextureRegion);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCopyTextureRegion)
			DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyTextureRegion", commandList,
		" dst=%p dstXYZ=%u,%u,%u src=%p srcBox=%p", dst, dstX, dstY, dstZ, src, srcBox);
	DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyResource);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCopyResource)
			DX12CommandListHookState::OrigCopyResource(commandList, dstResource, srcResource);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyTiles);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCopyTiles)
			DX12CommandListHookState::OrigCopyTiles(commandList, tiledResource, tileRegionStartCoordinate,
				tileRegionSize, buffer, bufferStartOffsetInBytes, flags);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResolveSubresource);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigResolveSubresource)
			DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ResolveSubresource", commandList,
		" dst=%p dstSub=%u src=%p srcSub=%u format=%u",
		dstResource, dstSubresource, srcResource, srcSubresource, static_cast<UINT>(format));
	DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
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
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigRSSetViewports)
			DX12CommandListHookState::OrigRSSetViewports(commandList, count, viewports);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::RSSetViewports", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetViewports(commandList, count, viewports);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedRSSetScissorRects(
	ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigRSSetScissorRects)
			DX12CommandListHookState::OrigRSSetScissorRects(commandList, count, rects);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::RSSetScissorRects", commandList, " count=%u", count);
	DX12CommandListHookState::OrigRSSetScissorRects(commandList, count, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedOMSetBlendFactor(
	ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4])
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigOMSetBlendFactor)
			DX12CommandListHookState::OrigOMSetBlendFactor(commandList, blendFactor);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::OMSetBlendFactor", commandList);
	DX12CommandListHookState::OrigOMSetBlendFactor(commandList, blendFactor);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedOMSetStencilRef(ID3D12GraphicsCommandList *commandList, UINT stencilRef)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigOMSetStencilRef)
			DX12CommandListHookState::OrigOMSetStencilRef(commandList, stencilRef);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::OMSetStencilRef", commandList, " stencilRef=%u", stencilRef);
	DX12CommandListHookState::OrigOMSetStencilRef(commandList, stencilRef);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiSetPipelineState);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResourceBarrier);
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
	DX12CommandListLifecycle::InvalidateAfterExecuteBundle(commandList);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSetGraphicsRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_VERTEX_BUFFERS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
		if (original)
			original(commandList, startSlot, count, views);
		return;
	}
	DX12_PROFILE_SCOPE(IASetVertexBuffers);

	D3D12_VERTEX_BUFFER_VIEW redirectedViews[
		D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	const D3D12_VERTEX_BUFFER_VIEW *activeViews = views;
	if (DX12PreSkinIaRedirectUtils::RedirectVertexBufferViews(
		    commandList,
		    startSlot,
		    count,
		    views,
		    redirectedViews,
		    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)) {
		activeViews = redirectedViews;
	}
	DX12CommandListStateFlow::CaptureVertexBuffers(
		commandList, startSlot, count, activeViews);
	PFN_IA_SET_VERTEX_BUFFERS original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
	if (original)
		original(commandList, startSlot, count, activeViews);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedSOSetTargets(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigSOSetTargets)
			DX12CommandListHookState::OrigSOSetTargets(commandList, startSlot, count, views);
		return;
	}
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigClearUnorderedAccessViewUint)
			DX12CommandListHookState::OrigClearUnorderedAccessViewUint(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
				resource, values, numRects, rects);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigClearUnorderedAccessViewFloat)
			DX12CommandListHookState::OrigClearUnorderedAccessViewFloat(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
				resource, values, numRects, rects);
		return;
	}
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
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
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigDiscardResource)
			DX12CommandListHookState::OrigDiscardResource(commandList, resource, region);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::DiscardResource", commandList, " resource=%p region=%p", resource, region);
	DX12CommandListHookState::OrigDiscardResource(commandList, resource, region);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedBeginQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigBeginQuery)
			DX12CommandListHookState::OrigBeginQuery(commandList, queryHeap, type, index);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::BeginQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigBeginQuery(commandList, queryHeap, type, index);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedEndQuery(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type, UINT index)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigEndQuery)
			DX12CommandListHookState::OrigEndQuery(commandList, queryHeap, type, index);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::EndQuery", commandList,
		" heap=%p type=%u index=%u", queryHeap, static_cast<UINT>(type), index);
	DX12CommandListHookState::OrigEndQuery(commandList, queryHeap, type, index);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedResolveQueryData(
	ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
	UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer, UINT64 alignedDestinationBufferOffset)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigResolveQueryData)
			DX12CommandListHookState::OrigResolveQueryData(commandList, queryHeap, type, startIndex, numQueries,
				destinationBuffer, alignedDestinationBufferOffset);
		return;
	}
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
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigSetPredication)
			DX12CommandListHookState::OrigSetPredication(commandList, buffer, alignedBufferOffset, operation);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SetPredication", commandList,
		" buffer=%p offset=%llu op=%u", buffer,
		static_cast<unsigned long long>(alignedBufferOffset), static_cast<UINT>(operation));
	DX12CommandListHookState::OrigSetPredication(commandList, buffer, alignedBufferOffset, operation);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListSetMarker(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCommandListSetMarker)
			DX12CommandListHookState::OrigCommandListSetMarker(commandList, metadata, data, size);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::SetMarker", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListSetMarker(commandList, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListBeginEvent(
	ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCommandListBeginEvent)
			DX12CommandListHookState::OrigCommandListBeginEvent(commandList, metadata, data, size);
		return;
	}
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::BeginEvent", commandList, " metadata=%u size=%u", metadata, size);
	DX12CommandListHookState::OrigCommandListBeginEvent(commandList, metadata, data, size);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList)
{
	if (DX12HotPathState::skipAll) {
		if (DX12CommandListHookState::OrigCommandListEndEvent)
			DX12CommandListHookState::OrigCommandListEndEvent(commandList);
		return;
	}
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
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiExecuteIndirect);
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);

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
	(void)commandQueue;
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
	(void)commandList;
}

void DX12CommandListHooks::HookCommandListCreation(IUnknown *device)
{
	(void)device;
}

// ---- DX12CommandListRuntime.cpp ----
#line 1 "DX12CommandListRuntime.cpp"
#include "DX12Foundation.hpp"

#include "DX12Infrastructure.hpp"
#include "DX12FeatureInterfaces.hpp"

DX12CommandListRuntimeShard DX12CommandListRuntime::Shards[DX12CommandListRuntime::kRuntimeShardCount];
volatile UINT64 DX12CommandListRuntime::TrackingGenerationValue = 1;
thread_local DX12CommandListRuntimeTlsCache DX12CommandListRuntime::Tls;

void DX12CommandListRuntime::RegisterDirectState(
	ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *state)
{
	if (!commandList || !state)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.directStates[commandList] = state;
	ReleaseSRWLockExclusive(&shard.lock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

void DX12CommandListRuntime::UnregisterDirectState(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.directStates.erase(commandList);
	ReleaseSRWLockExclusive(&shard.lock);
	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

DX12CommandListRuntimeState *DX12CommandListRuntime::GetDirectStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator it =
		shard.directStates.find(commandList);
	DX12CommandListRuntimeState *state = it != shard.directStates.end() ? it->second : nullptr;
	ReleaseSRWLockShared(&shard.lock);
	return state;
}

DX12CommandListRuntimeShard &DX12CommandListRuntime::ShardFor(ID3D12GraphicsCommandList *commandList)
{
	uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
	key ^= key >> 7;
	key *= 0x9e3779b97f4a7c15ull;
	return Shards[(key >> 17) & (kRuntimeShardCount - 1)];
}

void DX12CommandListRuntime::RefreshTrackingCache(DX12CommandListTrackingCache &cache)
{
	cache.generation = TrackingGenerationValue;
	cache.trackBindings = DX12CommandListStateCapture::ShouldTrackBindings();
	cache.trackIaBindings = DX12CommandListStateCapture::ShouldTrackIaBindings();
	cache.trackPipelineStateBindings = DX12CommandListStateCapture::ShouldTrackPipelineStateBindings();
	cache.recordBindingEvents = DX12CommandListStateCapture::ShouldRecordBindingEvents();
	cache.trackHuntIa = DX12CommandListStateCapture::ShouldTrackHuntIa();
	cache.trackPsoState = DX12CommandListStateCapture::ShouldTrackPsoState();
}

DX12CommandListTrackingState DX12CommandListRuntime::MakeTrackingState(
	const DX12CommandListTrackingCache &cache)
{
	DX12CommandListTrackingState state;
	state.trackBindings = cache.trackBindings;
	state.trackIaBindings = cache.trackIaBindings;
	state.trackPipelineStateBindings = cache.trackPipelineStateBindings;
	state.recordBindingEvents = cache.recordBindingEvents;
	state.trackHuntIa = cache.trackHuntIa;
	state.trackPsoState = cache.trackPsoState;
	return state;
}

DX12CommandListTrackingState DX12CommandListRuntime::GetTrackingState(
	DX12CommandListRuntimeState *state)
{
	DX12CommandListTrackingCache cache;
	if (!state) {
		DX12CommandListRuntime::RefreshTrackingCache(cache);
		return DX12CommandListRuntime::MakeTrackingState(cache);
	}

	if (state->trackingCache.generation != TrackingGenerationValue)
		DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);
	return DX12CommandListRuntime::MakeTrackingState(state->trackingCache);
}

void DX12CommandListRuntime::TlsRefreshFromState(
	DX12CommandListRuntimeState *state,
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeShard *shard,
	bool stateIsDirect)
{
	if (!state) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.stateIsDirect = false;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	Tls.commandList = commandList;
	Tls.shard = shard;
	Tls.stateIsDirect = stateIsDirect;
	Tls.statePtr = state;
	Tls.trackingGeneration = TrackingGenerationValue;

	DX12CommandListTrackingCache &mapCache = state->trackingCache;
	if (mapCache.generation != TrackingGenerationValue)
		DX12CommandListRuntime::RefreshTrackingCache(mapCache);
	Tls.tracking = mapCache;
}

void DX12CommandListRuntime::TlsRefresh(
	DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList)
{
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*>::iterator directIt =
		shard.directStates.find(commandList);
	if (directIt != shard.directStates.end()) {
		DX12CommandListRuntime::TlsRefreshFromState(
			directIt->second, commandList, &shard, true);
		return;
	}

	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it == shard.states.end()) {
		Tls.commandList = nullptr;
		Tls.shard = nullptr;
		Tls.stateIsDirect = false;
		Tls.statePtr = nullptr;
		Tls.trackingGeneration = 0;
		return;
	}

	DX12CommandListRuntime::TlsRefreshFromState(
		&it->second, commandList, &shard, false);
}

void DX12CommandListRuntime::GetTrackingCacheFast(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListTrackingCache &out)
{
	out = DX12CommandListTrackingCache();
	if (!commandList)
		return;
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupTlsHit();
		out = Tls.tracking;
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupDirectHit();
		else
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupShardHit();
		out = Tls.tracking;
		return;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupMiss();
}

DX12CommandListTrackingState DX12CommandListRuntime::GetTrackingState(
	ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListTrackingCache cache;
	if (!commandList) {
		DX12CommandListRuntime::RefreshTrackingCache(cache);
	} else {
		DX12CommandListRuntime::GetTrackingCacheFast(commandList, cache);
		if (cache.generation == 0)
			DX12CommandListRuntime::RefreshTrackingCache(cache);
	}

	return DX12CommandListRuntime::MakeTrackingState(cache);
}

DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtrFast(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	if (Tls.commandList == commandList && Tls.trackingGeneration == TrackingGenerationValue) {
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupTlsHit();
		return Tls.statePtr;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	DX12CommandListRuntime::TlsRefresh(shard, commandList);
	ReleaseSRWLockShared(&shard.lock);

	if (Tls.commandList == commandList) {
		if (Tls.stateIsDirect)
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupDirectHit();
		else
			DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupShardHit();
		return Tls.statePtr;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeLookupMiss();
	return nullptr;
}

void DX12CommandListRuntime::TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList)
{
	if (Tls.commandList != commandList)
		return;
	Tls.commandList = nullptr;
	Tls.shard = nullptr;
	Tls.stateIsDirect = false;
	Tls.statePtr = nullptr;
	Tls.trackingGeneration = 0;
}

UINT64 DX12CommandListRuntime::TrackingGeneration()
{
	return TrackingGenerationValue;
}

void DX12CommandListRuntime::BumpTrackingGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&TrackingGenerationValue));
}

void DX12CommandListRuntime::Register(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	const bool inserted = shard.states.try_emplace(commandList).second;
	ReleaseSRWLockExclusive(&shard.lock);

	if (inserted)
		DX12CommandListRuntime::BumpTrackingGeneration();
}

bool DX12CommandListRuntime::Unregister(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	bool removed = false;
	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		shard.states.erase(it);
		removed = true;
	}
	ReleaseSRWLockExclusive(&shard.lock);

	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
	if (removed) {
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12FrameAnalysis::LogJsonFunc("CommandListRuntimeUnregister",
			"\"commandList\":\"%p\"",
			commandList);
	}
	return removed;
}

void DX12CommandListRuntime::Reset(
	DX12CommandListRuntimeState *state, ID3D12PipelineState *initialState)
{
	if (!state)
		return;

	ID3D12Device *cachedDevice = state->device;
	DX12CommandListTrackingCache oldCache = state->trackingCache;
	// IA caches describe immutable content and candidate results rather than a
	// single recording. Their keys already carry the required generations, so
	// preserve the working set across command-list Reset like DX11's resident
	// HackerContext state.
	DX12ResolvedDrawRuntimeState persistentResolvedDraw =
		std::move(state->resolvedDraw);
	*state = DX12CommandListRuntimeState();
	state->device = cachedDevice;
	state->pipelineState = nullptr;
	state->trackingCache = oldCache;
	state->resolvedDraw = std::move(persistentResolvedDraw);
	DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);
	DX12CommandListRuntime::RememberPipelineState(state, initialState);
}

void DX12CommandListRuntime::Reset(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::Reset(fastState, initialState);
		DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::Reset(&state, initialState);
	ReleaseSRWLockExclusive(&shard.lock);

	DX12CommandListRuntime::TlsInvalidateIfMatch(commandList);
}

DX12CompiledDispatchDecision DX12CommandListRuntime::BuildDispatchDecision(
	ID3D12PipelineState *pipelineState,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	DX12CompiledDispatchDecision decision;
	const UINT64 reloadGeneration = DX12ModGetReloadGeneration();
	DX12PsoDecisionCacheUtils::ClearIfReloadChanged(reloadGeneration);
	if (DX12PsoDecisionCacheUtils::TryLoadDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			&decision)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPsoCacheHit();
		return decision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPsoCacheMiss();
	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionBuild();

	if (!pipelineState) {
		decision.SetNoComputeShader(
			pipelineState, reloadGeneration, hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoCompute();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	DX12PsoShaderInfo shaderInfo = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) || !shaderInfo.hasCS) {
		decision.SetNoComputeShader(
			pipelineState, reloadGeneration, hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoCompute();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	const uint64_t csHash = shaderInfo.cs;
	const bool shaderOverrideSkip = hasShaderOverrides ?
		DX12ModShouldSkipPipelineState(pipelineState, true) : false;
	const bool preSkinCandidate = needsPreSkinProbe && csHash != 0 ?
		DX12ModShouldTrackPreSkinBindingsForCs(csHash) : false;

	if (preSkinCandidate) {
		decision.SetNeedsPreSkinProbe(
			pipelineState, reloadGeneration, csHash, shaderOverrideSkip,
			hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidate();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	if (shaderOverrideSkip) {
		decision.SetNeedsShaderOverrideWork(
			pipelineState, reloadGeneration, csHash, shaderOverrideSkip,
			hasShaderOverrides, needsPreSkinProbe);
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionShaderOverrideWork();
		DX12PsoDecisionCacheUtils::StoreDispatch(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			needsPreSkinProbe,
			decision);
		return decision;
	}

	decision.SetNoPreSkinCandidate(
		pipelineState, reloadGeneration, csHash,
		hasShaderOverrides, needsPreSkinProbe);
	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionNoPreSkinCandidate();
	DX12PsoDecisionCacheUtils::StoreDispatch(
		pipelineState,
		reloadGeneration,
		hasShaderOverrides,
		needsPreSkinProbe,
		decision);
	return decision;
}

DX12CompiledDrawDecision DX12CommandListRuntime::BuildDrawDecision(
	ID3D12PipelineState *pipelineState,
	bool hasShaderOverrides)
{
	DX12CompiledDrawDecision decision;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12PsoDecisionCacheUtils::ClearIfReloadChanged(reloadGeneration);
	if (DX12PsoDecisionCacheUtils::TryLoadDraw(
			pipelineState,
			reloadGeneration,
			hasShaderOverrides,
			&decision)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionPsoCacheHit();
		return decision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionPsoCacheMiss();
	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionBuild();
	if (!DX12ModCompileDrawDecision(pipelineState, &decision)) {
		DX12IaHashTargetPlan targetPlan;
		targetPlan.RequireAll();
		decision.SetFromTargetPlan(
			pipelineState, reloadGeneration,
			hasShaderOverrides, hasShaderOverrides, false,
			targetPlan,
			targetPlan, DX12ModNeedsShaderDescriptorTracking());
	}

	if (!pipelineState) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoPso();
	} else if (!decision.hasActiveShaderOverrides) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoShaderOverride();
	} else if (!decision.hasShaderOverrides && !decision.hasShaderOverrideSkip) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNoDrawOverride();
	}

	switch (decision.kind) {
	case DX12CompiledDrawDecisionKind::NeedsIaHash:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsIaHash();
		break;
	case DX12CompiledDrawDecisionKind::NeedsDescriptorMatch:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsDescriptorMatch();
		break;
	case DX12CompiledDrawDecisionKind::NeedsReplacement:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionNeedsReplacement();
		break;
	case DX12CompiledDrawDecisionKind::SkipOriginal:
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionSkipOriginal();
		break;
	default:
		break;
	}

	DX12PsoDecisionCacheUtils::StoreDraw(
		pipelineState,
		reloadGeneration,
		hasShaderOverrides,
		decision);
	return decision;
}

const DX12CompiledDispatchDecision *DX12CommandListRuntime::EnsureDispatchDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	if (!state)
		return nullptr;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (state->dispatchDecision.resolved &&
	    state->dispatchDecision.pipelineState == state->pipelineState &&
	    state->dispatchDecision.reloadGeneration == reloadGeneration &&
	    state->dispatchDecision.MatchesGlobals(hasShaderOverrides, needsPreSkinProbe)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionCacheHit();
		state->cachedCsHash = state->dispatchDecision.computeShaderHash;
		state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
		state->cachedCsHashResolved = true;
		return &state->dispatchDecision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionCacheMiss();
	DX12CommandListRuntime::CompileDispatchDecision(
		state, hasShaderOverrides, needsPreSkinProbe);
	state->cachedCsHash = state->dispatchDecision.computeShaderHash;
	state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
	state->cachedCsHashResolved = true;
	return &state->dispatchDecision;
}

const DX12CompiledDrawDecision *DX12CommandListRuntime::EnsureDrawDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides)
{
	if (!state)
		return nullptr;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (state->drawDecision.resolved &&
	    state->drawDecision.pipelineState == state->pipelineState &&
	    state->drawDecision.reloadGeneration == reloadGeneration &&
	    state->drawDecision.MatchesGlobals(hasShaderOverrides)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheHit();
		return &state->drawDecision;
	}

	DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionCacheMiss();
	DX12CommandListRuntime::CompileDrawDecision(state, hasShaderOverrides);
	return &state->drawDecision;
}

void DX12CommandListRuntime::CompileDispatchDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides,
	bool needsPreSkinProbe)
{
	if (!state)
		return;
	state->dispatchDecision = DX12CommandListRuntime::BuildDispatchDecision(
		state->pipelineState, hasShaderOverrides, needsPreSkinProbe);
}

void DX12CommandListRuntime::CompileDrawDecision(
	DX12CommandListRuntimeState *state,
	bool hasShaderOverrides)
{
	if (!state)
		return;
	state->drawDecision = DX12CommandListRuntime::BuildDrawDecision(
		state->pipelineState, hasShaderOverrides);
}

void DX12CommandListRuntime::RememberPipelineState(
	DX12CommandListRuntimeState *state, ID3D12PipelineState *pipelineState)
{
	if (!state)
		return;

	const bool changed = state->pipelineState != pipelineState;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimePso(changed);
	if (!changed)
		return;

	state->cachedReplacementPsoKey = nullptr;
	state->cachedReplacementPso = nullptr;
	state->cachedCsHashValid = false;
	state->cachedCsHashResolved = false;
	state->cachedCsHash = 0;
	state->dispatchDecision.Reset();
	state->drawDecision.Reset();
	state->pipelineState = pipelineState;
	if (!pipelineState) {
		state->cachedCsHash = 0;
		state->cachedCsHashValid = false;
		state->cachedCsHashResolved = true;
	}

	// Resolve immutable PSO policy at the state transition, matching DX11's
	// current-shader model. Draw/dispatch then consume command-list-local state.
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	const bool shaderOverrides =
		(flags & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
	const bool preSkinProbe =
		(flags & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
	if (shaderOverrides)
		DX12CommandListRuntime::CompileDrawDecision(state, true);
	if (shaderOverrides || preSkinProbe) {
		DX12CommandListRuntime::CompileDispatchDecision(
			state, shaderOverrides, preSkinProbe);
		state->cachedCsHash = state->dispatchDecision.computeShaderHash;
		state->cachedCsHashValid = state->dispatchDecision.hasComputeShader;
		state->cachedCsHashResolved = true;
	}
}

void DX12CommandListRuntime::RememberPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPipelineState(fastState, pipelineState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPipelineState(&state, pipelineState);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::ResetIa(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->ia = DX12ActiveIaState();
	DX12CommandListRuntime::BumpIaEpoch(state);
}

void DX12CommandListRuntime::ResetIa(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::ResetIa(fastState);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::ResetIa(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberIndexBuffer(
	DX12CommandListRuntimeState *state, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!state)
		return;

	const bool newValid = view != nullptr;
	const bool same =
		state->ia.hasIndexBuffer == newValid &&
		(!newValid ||
			(state->ia.indexBuffer.BufferLocation == view->BufferLocation &&
				state->ia.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state->ia.indexBuffer.Format == view->Format));
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeIndexBuffer(!same);
	if (same)
		return;
	state->ia.hasIndexBuffer = newValid;
	state->ia.indexBuffer = view ? *view : D3D12_INDEX_BUFFER_VIEW();
	state->ia.indexBufferResolved = false;
	state->ia.indexBufferResource = DX12BufferResourceSummary();
	if (newValid && state->ia.indexBuffer.BufferLocation && state->ia.indexBuffer.SizeInBytes)
		state->ia.indexBufferResolved = DX12ResolveBufferResourceByGpuVa(
			state->ia.indexBuffer.BufferLocation,
			state->ia.indexBuffer.SizeInBytes,
			&state->ia.indexBufferResource);
	DX12CommandListRuntime::BumpIaEpoch(state);
}

void DX12CommandListRuntime::RememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberIndexBuffer(fastState, view);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberIndexBuffer(&state, view);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberVertexBuffers(
	DX12CommandListRuntimeState *state,
	UINT startSlot,
	UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!state)
		return;

	DX12ActiveIaState &ia = state->ia;
	const UINT trackedCount = startSlot < ARRAYSIZE(ia.vertexBuffers) ?
		min(count, static_cast<UINT>(ARRAYSIZE(ia.vertexBuffers)) - startSlot) : 0;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeVertexBufferCall(trackedCount);
	bool changedAny = false;
	for (UINT i = 0; i < trackedCount; ++i) {
		const UINT slot = startSlot + i;
		const D3D12_VERTEX_BUFFER_VIEW *view = views ? &views[i] : nullptr;
		D3D12_VERTEX_BUFFER_VIEW newView = view ? *view : D3D12_VERTEX_BUFFER_VIEW();
		const bool newValid = view != nullptr;
		const bool same =
			ia.hasVertexBuffer[slot] == newValid &&
			ia.vertexBuffers[slot].BufferLocation == newView.BufferLocation &&
			ia.vertexBuffers[slot].SizeInBytes == newView.SizeInBytes &&
			ia.vertexBuffers[slot].StrideInBytes == newView.StrideInBytes;
		DX12ResidentMaintenanceTelemetryUtils::RecordRuntimeVertexBufferSlot(!same);
		if (same)
			continue;
		ia.hasVertexBuffer[slot] = newValid;
		ia.vertexBuffers[slot] = newView;
		ia.vertexBufferResolved[slot] = false;
		ia.vertexBufferResources[slot] = DX12BufferResourceSummary();
		if (newValid && newView.BufferLocation && newView.SizeInBytes)
			ia.vertexBufferResolved[slot] = DX12ResolveBufferResourceByGpuVa(
				newView.BufferLocation,
				newView.SizeInBytes,
				&ia.vertexBufferResources[slot]);
		changedAny = true;
	}
	if (changedAny)
		DX12CommandListRuntime::BumpIaEpoch(state);
}

void DX12CommandListRuntime::RememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberVertexBuffers(fastState, startSlot, count, views);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberVertexBuffers(&state, startSlot, count, views);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	DX12CommandListRuntimeState *state, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!state)
		return;
	const bool changed = state->ia.primitiveTopology != topology;
	DX12ResidentMaintenanceTelemetryUtils::RecordRuntimePrimitiveTopology(changed);
	if (!changed)
		return;
	state->ia.primitiveTopology = topology;
}

void DX12CommandListRuntime::RememberPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *fastState = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (fastState) {
		DX12CommandListRuntime::RememberPrimitiveTopology(fastState, topology);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	DX12CommandListRuntimeState &state = shard.states[commandList];
	DX12CommandListRuntime::RememberPrimitiveTopology(&state, topology);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::BumpIaEpoch(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->iaEpoch++;
	if (!state->iaEpoch)
		state->iaEpoch = 1;
}

void DX12CommandListRuntime::BumpIaContentEpoch(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	state->iaContentEpoch++;
	if (!state->iaContentEpoch)
		state->iaContentEpoch = 1;
}

void DX12CommandListRuntime::BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::BumpIaContentEpoch(state);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::BumpIaContentEpoch(&it->second);
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12CommandListRuntime::BuildIaHashState(
	const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	DX12IaHashTargetPlan targetPlan;
	targetPlan.RequireAll();
	return DX12CommandListRuntime::BuildIaHashStateForTargets(
		ia, targetPlan, indexed, vertexCount, indexCount,
		firstVertex, firstIndex, baseVertex, state);
}

bool DX12CommandListRuntime::BuildIaHashStateForTargets(
	const DX12ActiveIaState &ia, const DX12IaHashTargetPlan &targetPlan,
	bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state)
{
	if (!state)
		return false;
	*state = DX12IaHashState();
	state->vertexBuffers.reserve(8);

	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer) {
		const uint32_t indexHash = targetPlan.RequiresIndexBufferHash(indexed) ?
			DX12IaRegion::HashIndexBufferView(ia.indexBuffer, draw) : 0;
		state->hasIndexBuffer = true;
		state->indexHash = indexHash;
		state->indexView = ia.indexBuffer;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		if (!targetPlan.RequiresVertexBuffer(slot))
			continue;
		const uint32_t vertexHash =
			DX12IaRegion::HashVertexBufferView(slot, ia.vertexBuffers[slot], draw);
		DX12IaBufferHash item;
		item.slot = slot;
		item.hash = vertexHash;
		item.vertexView = ia.vertexBuffers[slot];
		state->vertexBuffers.push_back(item);
	}

	return state->hasIndexBuffer || !state->vertexBuffers.empty();
}

DX12IaHashStateCacheKey DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargets(
	const DX12ActiveIaState &ia,
	UINT64 iaContentEpoch,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	UINT vertexCount,
	UINT indexCount,
	UINT firstVertex,
	UINT firstIndex,
	INT baseVertex)
{
	DX12IaHashStateCacheKey key;
	key.targetPlanFingerprint = targetPlan.Fingerprint();
	// IA hashes depend on either captured CPU content (covered by the observed
	// resource content generation below) or immutable resource/view metadata.
	// A command-list-wide Dispatch/Copy epoch cannot change either cache input.
	(void)iaContentEpoch;
	key.iaContentEpoch = 0;
	key.bufferContentGeneration = 0xcbf29ce484222325ull;
	key.bufferResolveGeneration = 0x84222325a9582d75ull;
	key.indexed = indexed;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.baseVertex = baseVertex;
	key.iaViewFingerprint = 0x84222325a9582d75ull;
	auto combineResolvedObservation = [&key](
		const DX12BufferResourceSummary &summary, UINT64 salt) {
		if (summary.resource && summary.resourceGeneration) {
			key.bufferContentGeneration =
				DX12IaHashStateCacheKeyHash::Combine(
					key.bufferContentGeneration,
					DX12GetBufferContentGenerationForResource(summary.resource) ^ salt);
			key.bufferResolveGeneration =
				DX12IaHashStateCacheKeyHash::Combine(
					key.bufferResolveGeneration,
					summary.resourceGeneration ^ salt);
			return true;
		}
		return false;
	};
	auto combineObservation = [&key, &combineResolvedObservation](
		UINT64 salt,
		const DX12BufferResourceSummary *cachedSummary) {
		if (cachedSummary && combineResolvedObservation(*cachedSummary, salt))
			return;
		// Unresolved views retain conservative global invalidation. Resolved views
		// no longer inherit unrelated resource/content churn. Draw-time key building
		// deliberately consumes wrapper IA state only; GPU VA resolution happens when
		// the IA binding changes, matching DX11-style current-state ownership.
		key.usesGlobalFallback = true;
		key.bufferContentGeneration =
			DX12IaHashStateCacheKeyHash::Combine(
				key.bufferContentGeneration,
				DX12GetIaHashContentGeneration() ^ salt);
		key.bufferResolveGeneration =
			DX12IaHashStateCacheKeyHash::Combine(
				key.bufferResolveGeneration,
				DX12GetIaHashResolveGeneration() ^ salt);
	};

	if (targetPlan.RequiresIndexBuffer(indexed)) {
		key.iaViewFingerprint ^= 0x100000001b3ull;
			key.iaViewFingerprint ^= ia.hasIndexBuffer ? 0x7f4a7c15ull : 0x51afd7edull;
			if (ia.hasIndexBuffer) {
				combineObservation(
					0x100000001b3ull,
					ia.indexBufferResolved ? &ia.indexBufferResource : nullptr);
				key.iaViewFingerprint ^= ia.indexBuffer.BufferLocation +
					0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
					(key.iaViewFingerprint >> 2);
				key.iaViewFingerprint ^= static_cast<UINT64>(ia.indexBuffer.SizeInBytes) +
				0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
				(key.iaViewFingerprint >> 2);
			key.iaViewFingerprint ^= static_cast<UINT64>(ia.indexBuffer.Format) +
				0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
				(key.iaViewFingerprint >> 2);
		}
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot))
			continue;
		key.iaViewFingerprint ^= static_cast<UINT64>(slot + 1) *
			0x9e3779b97f4a7c15ull;
		key.iaViewFingerprint ^= ia.hasVertexBuffer[slot] ? 0x27d4eb2full : 0x165667b1ull;
		if (!ia.hasVertexBuffer[slot])
			continue;
		combineObservation(
			static_cast<UINT64>(slot + 1) * 0x9e3779b97f4a7c15ull,
			ia.vertexBufferResolved[slot] ? &ia.vertexBufferResources[slot] : nullptr);
		key.iaViewFingerprint ^= ia.vertexBuffers[slot].BufferLocation +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
		key.iaViewFingerprint ^= static_cast<UINT64>(ia.vertexBuffers[slot].SizeInBytes) +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
		key.iaViewFingerprint ^= static_cast<UINT64>(ia.vertexBuffers[slot].StrideInBytes) +
			0x9e3779b97f4a7c15ull + (key.iaViewFingerprint << 6) +
			(key.iaViewFingerprint >> 2);
	}

	if (!key.iaViewFingerprint)
		key.iaViewFingerprint = 1;
	return key;
}

DX12IaHashStateCacheKey DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
	DX12CommandListRuntimeState *state,
	UINT cacheSlot,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	UINT vertexCount,
	UINT indexCount,
	UINT firstVertex,
	UINT firstIndex,
	INT baseVertex)
{
	if (!state)
		return DX12IaHashStateCacheKey();
	const UINT64 targetPlanFingerprint = targetPlan.Fingerprint();
	DX12IaHashStateCacheKey key;
	DX12IaHashKeyPrefixCacheEntry &entry =
		state->resolvedDraw.iaHashKeyPrefixes[
			cacheSlot % state->resolvedDraw.iaHashKeyPrefixes.size()];
	if (!DX12IaHashKeyPrefixCacheUtils::TryLoadStable(
			entry, state->iaEpoch, targetPlanFingerprint, indexed, &key)) {
		const UINT64 globalContentGeneration = DX12GetIaHashContentGeneration();
		const UINT64 globalResolveGeneration = DX12GetIaHashResolveGeneration();
		if (!DX12IaHashKeyPrefixCacheUtils::TryLoad(
				entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, &key)) {
			key = BuildIaHashStateCacheKeyForTargets(
				state->ia, state->iaContentEpoch, targetPlan, indexed,
				0, 0, 0, 0, 0);
			DX12IaHashKeyPrefixCacheUtils::Store(
				&entry, state->iaEpoch, targetPlanFingerprint, indexed,
				globalContentGeneration, globalResolveGeneration, key);
		}
	}
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.baseVertex = baseVertex;
	return key;
}

ID3D12PipelineState *DX12CommandListRuntime::GetPipelineState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state)
		return state->pipelineState;

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	ID3D12PipelineState *result =
		it == shard.states.end() ? nullptr : it->second.pipelineState;
	ReleaseSRWLockShared(&shard.lock);
	return result;
}

DX12CommandListRuntimeState DX12CommandListRuntime::GetState(
	ID3D12GraphicsCommandList *commandList)
{
	DX12CommandListRuntimeState result;
	if (!commandList)
		return result;

	const DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtr(commandList);
	if (state)
		result = *state;
	return result;
}

const DX12CommandListRuntimeState *DX12CommandListRuntime::GetStatePtr(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	return DX12CommandListRuntime::GetStatePtrFast(commandList);
}

static void DX12RuntimeClearRootTables(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state->graphicsRootTables[root] = RootTableState();
		state->computeRootTables[root] = RootTableState();
	}
	state->graphicsRootTableValidCount = 0;
	state->computeRootTableValidCount = 0;
}

static bool DX12RuntimeResolveHeapSummary(
	ID3D12DescriptorHeap *heap,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;
	*summary = DX12DescriptorHeapSummary();
	return DX12FindDescriptorHeapByPtr(heap, summary) &&
		summary->heap == heap &&
		summary->type == static_cast<UINT>(type) &&
		summary->increment != 0;
}

static void DX12RuntimeFillRootTableFromHeap(
	RootTableState *table,
	const DX12DescriptorHeapSummary &heap)
{
	if (!table || !table->valid ||
			heap.increment == 0 || heap.gpuStart == 0 ||
			table->baseDescriptor.ptr < heap.gpuStart) {
		return;
	}

	const UINT64 tableBaseIndex =
		(table->baseDescriptor.ptr - heap.gpuStart) / heap.increment;
	if (tableBaseIndex >= heap.numDescriptors)
		return;

	table->runtimeHeapResolved = true;
	table->runtimeHeap = heap.heap;
	table->runtimeHeapType = heap.type;
	table->runtimeHeapCpuStart = heap.cpuStart;
	table->runtimeHeapGpuStart = heap.gpuStart;
	table->runtimeHeapIncrement = heap.increment;
	table->runtimeHeapNumDescriptors = heap.numDescriptors;
	table->runtimeHeapVersion = heap.version;
	table->runtimeTableBaseIndex = tableBaseIndex;
	table->runtimeTableCpuStart =
		heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
	table->runtimeTableGpuStart =
		heap.gpuStart + tableBaseIndex * heap.increment;
}

static void DX12RuntimeResolveRootTable(
	DX12CommandListRuntimeState *state,
	RootTableState *table)
{
	if (!state || !table || !table->valid)
		return;
	table->runtimeHeapResolved = false;
	table->runtimeHeap = nullptr;
	table->runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	table->runtimeHeapCpuStart = 0;
	table->runtimeHeapGpuStart = 0;
	table->runtimeHeapIncrement = 0;
	table->runtimeHeapNumDescriptors = 0;
	table->runtimeHeapVersion = 0;
	table->runtimeTableBaseIndex = 0;
	table->runtimeTableCpuStart = 0;
	table->runtimeTableGpuStart = 0;

	if (state->runtimeCbvSrvUavHeapResolved &&
			state->runtimeCbvSrvUavHeap.heap == state->cbvSrvUavHeap &&
			table->baseDescriptor.ptr >= state->runtimeCbvSrvUavHeap.gpuStart) {
		DX12RuntimeFillRootTableFromHeap(table, state->runtimeCbvSrvUavHeap);
	}
}

static void DX12RuntimeBumpBindingQuerySerial(DX12CommandListRuntimeState *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	DX12CommandListRuntimeState *state,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!state)
		return;
	const bool changed =
		state->cbvSrvUavHeap != cbvSrvUavHeap ||
		state->samplerHeap != samplerHeap;
	state->cbvSrvUavHeap = cbvSrvUavHeap;
	state->samplerHeap = samplerHeap;
	state->runtimeCbvSrvUavHeapResolved =
		DX12RuntimeResolveHeapSummary(
			cbvSrvUavHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&state->runtimeCbvSrvUavHeap);
	if (!state->runtimeCbvSrvUavHeapResolved)
		state->runtimeCbvSrvUavHeap = DX12DescriptorHeapSummary();
	state->runtimeSamplerHeapResolved =
		DX12RuntimeResolveHeapSummary(
			samplerHeap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			&state->runtimeSamplerHeap);
	if (!state->runtimeSamplerHeapResolved)
		state->runtimeSamplerHeap = DX12DescriptorHeapSummary();
	if (changed) {
		DX12RuntimeClearRootTables(state);
		DX12RuntimeBumpBindingQuerySerial(state);
	}
}

void DX12CommandListRuntime::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::SetDescriptorHeaps(state, cbvSrvUavHeap, samplerHeap);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end())
		DX12CommandListRuntime::SetDescriptorHeaps(&it->second, cbvSrvUavHeap, samplerHeap);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12CommandListRuntime::RememberRootDescriptorTable(
	DX12CommandListRuntimeState *state,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!state || rootParameterIndex >= MaxRootParameters)
		return;
	RootTableState &table = graphics ?
		state->graphicsRootTables[rootParameterIndex] :
		state->computeRootTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		if (!table.runtimeHeapResolved)
			DX12RuntimeResolveRootTable(state, &table);
		return;
	}
	if (!table.valid) {
		if (graphics)
			++state->graphicsRootTableValidCount;
		else
			++state->computeRootTableValidCount;
	}
	table = RootTableState();
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	DX12RuntimeResolveRootTable(state, &table);
	if (!graphics) {
		state->computeBindingSerial++;
		if (!state->computeBindingSerial)
			state->computeBindingSerial = 1;
	}
	DX12RuntimeBumpBindingQuerySerial(state);
}

void DX12CommandListRuntime::RememberRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	bool graphics,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			state, graphics, rootParameterIndex, baseDescriptor);
		return;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	if (it != shard.states.end()) {
		DX12CommandListRuntime::RememberRootDescriptorTable(
			&it->second, graphics, rootParameterIndex, baseDescriptor);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12CommandListRuntime::GetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	if (cbvSrvUavHeap)
		*cbvSrvUavHeap = nullptr;
	if (samplerHeap)
		*samplerHeap = nullptr;
	if (!commandList)
		return false;

	DX12CommandListRuntimeState *state = DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (state) {
		if (cbvSrvUavHeap)
			*cbvSrvUavHeap = state->cbvSrvUavHeap;
		if (samplerHeap)
			*samplerHeap = state->samplerHeap;
		return true;
	}

	DX12CommandListRuntimeShard &shard = DX12CommandListRuntime::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState>::iterator it =
		shard.states.find(commandList);
	bool found = false;
	if (it != shard.states.end()) {
		if (cbvSrvUavHeap)
			*cbvSrvUavHeap = it->second.cbvSrvUavHeap;
		if (samplerHeap)
			*samplerHeap = it->second.samplerHeap;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	return found;
}

// ---- DX12CommandListLifecycle.cpp ----
#line 1 "DX12CommandListLifecycle.cpp"
#include "DX12Foundation.hpp"


void DX12CommandListLifecycle::Register(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntime::Register(commandList);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningUavProbe()) {
		const bool inserted = DX12BindingTracker::RegisterCommandList(commandList);
		if (inserted)
			DX12BindingTracker::ResetCommandList(commandList, initialState);
	}
}

void DX12CommandListLifecycle::Unregister(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12BindingTracker::UnregisterCommandList(commandList);
	DX12HuntUnregisterCommandList(commandList);
	DX12CommandListRuntime::Unregister(commandList);
}

void DX12CommandListLifecycle::Reset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, initialState);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, initialState);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::ResetIa(commandList);
	if (tracking.trackPsoState || tracking.trackHuntIa)
		DX12HuntResetCommandList(commandList, initialState);
}

void DX12CommandListLifecycle::ClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "clear_state");
	if (tracking.trackBindings || DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, pipelineState);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
	if (tracking.trackHuntIa)
		DX12CommandListRuntime::ResetIa(commandList);
	if (DX12HuntIsEnabled())
		DX12HuntResetCommandList(commandList, pipelineState);
}

void DX12CommandListLifecycle::InvalidateAfterExecuteBundle(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	// A bundle may leave root, descriptor, PSO and IA state in the parent, but
	// its independently recorded state cannot be reconstructed by the parent
	// tracker without replaying the bundle stream. Mark the parent unknown so a
	// later Mod never mirrors stale tables or matches stale IA/PSO state.
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings || DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, nullptr);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, nullptr);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::ResetIa(commandList);
	if (tracking.trackPsoState || tracking.trackHuntIa)
		DX12HuntResetCommandList(commandList, nullptr);
}

// ---- DX12CommandListCommandRecorder.cpp ----
#line 1 "DX12CommandListCommandRecorder.cpp"
#include "DX12Foundation.hpp"


void DX12CommandListCommandRecorder::RecordResourceBarrier(
	UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	// ResourceBarrier changes usage state, not buffer identity or content.
	// Stable IA/mod hashes depend on GPUVA, content, and view metadata, so normal
	// mod runtime does not need barrier state capture.
	if (DX12CommandListStateCapture::ShouldTrackResourceStates())
		DX12RecordResourceBarrier(numBarriers, barriers);
}

void DX12CommandListCommandRecorder::RecordExecuteBundle(
	ID3D12GraphicsCommandList *bundle,
	ID3D12PipelineState *initialState)
{
	// Bundle execution records command-list state into the parent stream. Keep
	// bundle registration here so hook code does not need to know lifecycle
	// details for secondary command lists.
	DX12CommandListLifecycle::Register(bundle, initialState);
}

void DX12CommandListCommandRecorder::RecordExecuteIndirect(
	ID3D12GraphicsCommandList *commandList)
{
	// ExecuteIndirect can expand into draw or dispatch work on the GPU. Shader
	// hunting treats it as both graphics and compute usage without inspecting
	// the command signature on the CPU hot path.
	DX12HuntRecordExecuteIndirect(commandList);
}

// ---- DX12CommandListStateCapture.cpp ----
#line 1 "DX12CommandListStateCapture.cpp"
#include "DX12Foundation.hpp"

#include "DX12Infrastructure.hpp"

bool DX12CommandListStateCapture::ShouldTrackBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy() ||
		DX12ModNeedsPreSkinningUavProbe() ||
		DX12ModNeedsShaderDescriptorTracking();
}

bool DX12CommandListStateCapture::ShouldTrackIaBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackPipelineStateBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackResourceStates()
{
	return DX12FrameAnalysis::IsCapturing() ||
		DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() ||
		DX12ShaderDumpIsBusy() ||
		DX12RuntimeState::IsHuntingMode();
}

bool DX12CommandListStateCapture::ShouldRecordBindingEvents()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy();
}

bool DX12CommandListStateCapture::ShouldTrackHuntIa()
{
	return DX12HuntIsEnabled();
}

bool DX12CommandListStateCapture::ShouldTrackPsoState()
{
	return DX12HuntIsEnabled() || DX12ModHasActiveShaderOverrides() ||
		DX12ModNeedsPreSkinningUavProbe() ||
		DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsBusy();
}

DX12CommandListTrackingState DX12CommandListStateCapture::MakeTrackingState()
{
	DX12CommandListTrackingState state;
	state.trackBindings = DX12CommandListStateCapture::ShouldTrackBindings();
	state.trackIaBindings = DX12CommandListStateCapture::ShouldTrackIaBindings();
	state.trackPipelineStateBindings = DX12CommandListStateCapture::ShouldTrackPipelineStateBindings();
	state.recordBindingEvents = DX12CommandListStateCapture::ShouldRecordBindingEvents();
	state.trackHuntIa = DX12CommandListStateCapture::ShouldTrackHuntIa();
	state.trackPsoState = DX12CommandListStateCapture::ShouldTrackPsoState();
	return state;
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	DX12CommandListRuntimeState *runtimeState)
{
	return DX12CommandListRuntime::GetTrackingState(runtimeState);
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::MakeTrackingState();
	return DX12CommandListRuntime::GetTrackingState(commandList);
}

DX12CommandListTrackingState DX12CommandListStateCapture::GetTrackingState(
	ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState)
{
	if (runtimeState)
		return DX12CommandListStateCapture::GetTrackingState(runtimeState);
	return DX12CommandListStateCapture::GetTrackingState(commandList);
}

void DX12CommandListStateCapture::PrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
}

void DX12CommandListStateCapture::PrimitiveTopology(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	D3D12_PRIMITIVE_TOPOLOGY topology)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackHuntIa) {
		if (runtimeState)
			DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
		else
			DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
	}
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetPrimitiveTopology(commandList, topology);
}

ID3D12PipelineState *DX12CommandListStateCapture::PipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	return DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
}

ID3D12PipelineState *DX12CommandListStateCapture::PipelineState(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	ID3D12PipelineState *pipelineState)
{
	ID3D12PipelineState *activePipelineState = pipelineState;
	if (DX12ModHasActiveShaderOverrides()) {
		DX12_HOTPATH_TIME_BEGIN(PsoCacheLookupTotal);
		if (runtimeState && runtimeState->cachedReplacementPsoKey == pipelineState) {
			if (runtimeState->cachedReplacementPso)
				activePipelineState = runtimeState->cachedReplacementPso;
			DX12_HOTPATH_TIME_END(PsoCacheLookupTotal);
		} else {
			DX12_HOTPATH_TIME_END(PsoCacheLookupTotal);
			ID3D12PipelineState *replacement =
				DX12ModGetReplacementPipelineState(pipelineState);
			if (runtimeState) {
				runtimeState->cachedReplacementPsoKey = pipelineState;
				runtimeState->cachedReplacementPso = replacement;
			}
			if (replacement)
				activePipelineState = replacement;
		}
	}

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackPipelineStateBindings) {
		DX12BindingTracker::SetPipelineState(commandList, activePipelineState);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordStateEvent(commandList, "set_pso");
	}
	if (tracking.trackPsoState) {
		if (runtimeState)
			DX12CommandListRuntime::RememberPipelineState(runtimeState, activePipelineState);
		else
			DX12CommandListRuntime::RememberPipelineState(commandList, activePipelineState);
	}
	if (tracking.trackHuntIa)
		DX12HuntObservePipelineState(activePipelineState);
	return activePipelineState;
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::DescriptorHeaps(commandList, runtimeState, count, heaps);
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, count, heaps);
	if (changed && tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::DescriptorHeaps(
		commandList, runtimeState, cbvSrvUavHeap, samplerHeap);
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
	if (changed && tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::ComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootSignature(commandList, runtimeState, rootSignature);
}

void DX12CommandListStateCapture::ComputeRootSignature(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	ID3D12RootSignature *rootSignature)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootSignature(commandList, rootSignature);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_signature");
}

void DX12CommandListStateCapture::GraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootSignature(commandList, runtimeState, rootSignature);
}

void DX12CommandListStateCapture::GraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	ID3D12RootSignature *rootSignature)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootSignature(commandList, rootSignature);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_signature");
}

void DX12CommandListStateCapture::ComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootDescriptorTable(
		commandList, runtimeState, rootParameterIndex, baseDescriptor);
}

void DX12CommandListStateCapture::ComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_table");
}

void DX12CommandListStateCapture::GraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootDescriptorTable(
		commandList, runtimeState, rootParameterIndex, baseDescriptor);
}

void DX12CommandListStateCapture::GraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_table");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRoot32BitConstant(
		commandList, runtimeState, rootParameterIndex, srcData, destOffset);
}

void DX12CommandListStateCapture::ComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	UINT srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constant");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRoot32BitConstant(
		commandList, runtimeState, rootParameterIndex, srcData, destOffset);
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	UINT srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constant");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRoot32BitConstants(
		commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

void DX12CommandListStateCapture::ComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	UINT num32BitValuesToSet,
	const void *srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constants");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRoot32BitConstants(
		commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	UINT num32BitValuesToSet,
	const void *srcData,
	UINT destOffset)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constants");
}

void DX12CommandListStateCapture::ComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::ComputeRootDescriptor(
		commandList, runtimeState, rootParameterIndex, type, address);
}

void DX12CommandListStateCapture::ComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetComputeRootDescriptor(commandList, rootParameterIndex, type, address);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_compute_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_compute_root_srv" : "set_compute_root_uav");
}

void DX12CommandListStateCapture::GraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::GraphicsRootDescriptor(
		commandList, runtimeState, rootParameterIndex, type, address);
}

void DX12CommandListStateCapture::GraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type,
	D3D12_GPU_VIRTUAL_ADDRESS address)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (!tracking.trackBindings)
		return;
	DX12BindingTracker::SetGraphicsRootDescriptor(commandList, rootParameterIndex, type, address);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_graphics_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_graphics_root_srv" : "set_graphics_root_uav");
}

void DX12CommandListStateCapture::IndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
}

void DX12CommandListStateCapture::IndexBuffer(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	const D3D12_INDEX_BUFFER_VIEW *view)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetIndexBuffer(commandList, view);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides()) {
		if (runtimeState)
			DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
		else
			DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
	}
}

void DX12CommandListStateCapture::VertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	DX12CommandListRuntimeState *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	DX12CommandListStateCapture::VertexBuffers(commandList, runtimeState, startSlot, count, views);
}

void DX12CommandListStateCapture::VertexBuffers(
	ID3D12GraphicsCommandList *commandList,
	DX12CommandListRuntimeState *runtimeState,
	UINT startSlot,
	UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
	if (tracking.trackIaBindings)
		DX12BindingTracker::SetVertexBuffers(commandList, startSlot, count, views);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides()) {
		if (runtimeState)
			DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
		else
			DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
	}
}
