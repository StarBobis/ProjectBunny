


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




class DX12CommandListComputeBindingHookCallbacks
{
public:
	// As in DX11, skipAll forwards hooks without tracking work.
	// Probe-only PreSkin still needs the live compute state, but it can use
	// the lightweight runtime resident path instead of the heavy tracker.
	static bool ShouldSkipComputeBinding()
	{
		return DX12HotPathState::skipAll &&
			!(DX12HotPathState::HookWorkFlags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
	}

	static bool ShouldSkipGraphicsBinding()
	{
		return DX12HotPathState::skipGraphicsBindings != 0;
	}

	static void STDMETHODCALLTYPE HookedSetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList, UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12_PROFILE_FUNCTION(SetDescriptorHeaps);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_DESCRIPTOR_HEAPS original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
			if (original)
				original(commandList, count, heaps);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(commandList, count, heaps);
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps, PFN_SET_DESCRIPTOR_HEAPS, DX12CommandListHookState::OrigSetDescriptorHeaps, "SetDescriptorHeaps");
		if (original)
			original(commandList, count, heaps);
	}

	static void STDMETHODCALLTYPE HookedSetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootSignature);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetGraphicsRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootSignature);
		if (ShouldSkipGraphicsBinding()) {
			PFN_SET_ROOT_SIGNATURE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature, PFN_SET_ROOT_SIGNATURE, DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
			if (original)
				original(commandList, rootSignature);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetComputeRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptorTable);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetGraphicsRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptorTable);
		if (ShouldSkipGraphicsBinding()) {
			PFN_SET_ROOT_DESCRIPTOR_TABLE original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable, PFN_SET_ROOT_DESCRIPTOR_TABLE, DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
			if (original)
				original(commandList, rootParameterIndex, baseDescriptor);
			return;
		}
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
		if (ShouldSkipGraphicsBinding()) {
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
		if (ShouldSkipGraphicsBinding()) {
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
		DX12_PROFILE_FUNCTION(SetComputeRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootConstantBufferView, "SetComputeRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetGraphicsRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (ShouldSkipGraphicsBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView, "SetGraphicsRootConstantBufferView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetComputeRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootShaderResourceView, "SetComputeRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetGraphicsRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		if (ShouldSkipGraphicsBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView, "SetGraphicsRootShaderResourceView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
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
		DX12_PROFILE_FUNCTION(SetComputeRootUnorderedAccessView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		if (ShouldSkipComputeBinding()) {
			PFN_SET_ROOT_GPU_VA original =
				DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
			if (original)
				original(commandList, rootParameterIndex, address);
			return;
		}
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address, false,
			"ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView");
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView, "SetComputeRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
	}
};

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

	DX12CommandListHooks::HookCommandList(commandList);
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
	const bool huntEnabled = DX12HuntIsEnabled();
	const bool captureActive =
		DX12CaptureLedger::CurrentCaptureSessionId() != 0;
	if (!huntEnabled && !captureActive)
		return;

	DX12CommandListHooks::HookCommandList(commandList);
	ID3D12GraphicsCommandList *baseList = nullptr;
	if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&baseList)))) {
		DX12CommandListHookCallbacks::RegisterCommandListBase(baseList, nullptr);
		if (huntEnabled)
			DX12HuntPublishCommandList(baseList);
		if (captureActive)
			DX12BindingTracker::SubmitCommandList(baseList);
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
	DX12_PROFILE_FUNCTION(ResetCommandList);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResetCommandList);
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	const bool needsPresentResetWork =
		(flags & DX12HotPathState::FLAG_PRESENT_REPLACEMENT) != 0;
	if (DX12HotPathState::skipAll && !needsPresentResetWork) {
		PFN_RESET_COMMAND_LIST original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Reset, PFN_RESET_COMMAND_LIST, DX12CommandListHookState::OrigResetCommandList, "ResetCommandList");
		return original ? original(commandList, allocator, initialState) : E_FAIL;
	}
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
		stageTick = GetTickCount64();
		DX12CommandListHookDiagnostics::LogCommandListStageEdge("Reset", "skipPresentRuntime", commandList);
#endif
		(void)needsPresentResetWork;
		// Do not claim Present replacement work from Reset. Engines commonly
		// reset direct command lists well before those lists are actually used,
		// and the first far-ahead Reset would otherwise consume the current
		// Present token and push the effect out by many frames.
#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12CommandListHookDiagnostics::LogCommandListStage("Reset", "afterPresentRuntime", commandList, S_OK, GetTickCount64() - stageTick);
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
	DX12_PROFILE_FUNCTION(DrawInstanced);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDrawInstanced);
	const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
	// DX11-style forward path: no draw work means no runtime-state lookup.
	if (DX12HotPathState::skipAll ||
		!(drawFlags & DX12HotPathState::MASK_DRAW_WORK)) {
		PFN_DRAW_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (DX12HotPathState::CanResidentDrawFastReject(drawFlags, runtimeState)) {
		PFN_DRAW_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced, PFN_DRAW_INSTANCED, DX12CommandListHookState::OrigDrawInstanced, "DrawInstanced");
		if (original)
			original(commandList, vertexCountPerInstance, instanceCount,
				startVertexLocation, startInstanceLocation);
		return;
	}
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
		vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, iaCallbacks);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
	UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
	DX12_PROFILE_FUNCTION(DrawIndexedInstanced);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
	const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
	// DX11-style forward path: no draw work means no runtime-state lookup.
	if (DX12HotPathState::skipAll ||
		!(drawFlags & DX12HotPathState::MASK_DRAW_WORK)) {
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (DX12HotPathState::CanResidentDrawFastReject(drawFlags, runtimeState)) {
		PFN_DRAW_INDEXED_INSTANCED original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced, PFN_DRAW_INDEXED_INSTANCED, DX12CommandListHookState::OrigDrawIndexedInstanced, "DrawIndexedInstanced");
		if (original)
			original(commandList, indexCountPerInstance, instanceCount,
				startIndexLocation, baseVertexLocation, startInstanceLocation);
		return;
	}
	const DX12IaReplacementExecutorCallbacks &iaCallbacks =
		DX12CommandListHookCallbacks::GetCachedIaReplacementCallbacks(commandList);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
		indexCountPerInstance, instanceCount, startIndexLocation,
		baseVertexLocation, startInstanceLocation);
	DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, iaCallbacks);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	DX12_PROFILE_FUNCTION(Dispatch);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiDispatch);
	// Fast path: one bit test replaces separate skipAll and PreSkin checks.
	if (!(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DISPATCH_WORK)) {
		PFN_DISPATCH original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
		if (original)
			original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	PFN_DISPATCH original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch, PFN_DISPATCH, DX12CommandListHookState::OrigDispatch, "Dispatch");
	DX12DispatchHookFlow::ExecuteHook(
		commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ, original);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyBufferRegion(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
	ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes)
{
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	if (DX12HotPathState::skipAll ||
		(!DX12HotPathState::NeedsPreSkinProbe(flags) &&
		 !DX12HotPathState::NeedsIaBufferContentTracking(flags))) {
		if (DX12CommandListHookState::OrigCopyBufferRegion)
			DX12CommandListHookState::OrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyBufferRegion);
	if (DX12HotPathState::NeedsPreSkinProbe(flags))
		DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyBufferRegion", commandList,
		" dst=%p dstOffset=%llu src=%p srcOffset=%llu bytes=%llu",
		dstBuffer, static_cast<unsigned long long>(dstOffset), srcBuffer,
		static_cast<unsigned long long>(srcOffset), static_cast<unsigned long long>(numBytes));
	DX12CommandListHookState::OrigCopyBufferRegion(commandList, dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
	if (DX12HotPathState::NeedsIaBufferContentTracking(flags) &&
		!DX12CopyBufferContentRange(
			dstBuffer,
			dstOffset,
			srcBuffer,
			srcOffset,
			numBytes)) {
		DX12InvalidateBufferContent(dstBuffer);
	}
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyTextureRegion(
	ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
	UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	if (DX12HotPathState::skipAll ||
		!DX12HotPathState::NeedsPreSkinProbe(DX12HotPathState::HookWorkFlags)) {
		if (DX12CommandListHookState::OrigCopyTextureRegion)
			DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyTextureRegion);
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyTextureRegion", commandList,
		" dst=%p dstXYZ=%u,%u,%u src=%p srcBox=%p", dst, dstX, dstY, dstZ, src, srcBox);
	DX12CommandListHookState::OrigCopyTextureRegion(commandList, dst, dstX, dstY, dstZ, src, srcBox);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyResource(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource)
{
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	if (DX12HotPathState::skipAll ||
		(!DX12HotPathState::NeedsPreSkinProbe(flags) &&
		 !DX12HotPathState::NeedsIaBufferContentTracking(flags))) {
		if (DX12CommandListHookState::OrigCopyResource)
			DX12CommandListHookState::OrigCopyResource(commandList, dstResource, srcResource);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyResource);
	if (DX12HotPathState::NeedsPreSkinProbe(flags))
		DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::CopyResource", commandList,
		" dst=%p src=%p", dstResource, srcResource);
	DX12CommandListHookState::OrigCopyResource(commandList, dstResource, srcResource);
	if (DX12HotPathState::NeedsIaBufferContentTracking(flags) &&
		dstResource && srcResource) {
		const D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();
		const D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
		if (dstDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		    srcDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		    !DX12CopyBufferContent(dstResource, srcResource)) {
			DX12InvalidateBufferContent(dstResource);
		}
	}
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedCopyTiles(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
	const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
	const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
	UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags)
{
	if (DX12HotPathState::skipAll ||
		!DX12HotPathState::NeedsPreSkinProbe(DX12HotPathState::HookWorkFlags)) {
		if (DX12CommandListHookState::OrigCopyTiles)
			DX12CommandListHookState::OrigCopyTiles(commandList, tiledResource, tileRegionStartCoordinate,
				tileRegionSize, buffer, bufferStartOffsetInBytes, flags);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiCopyTiles);
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
	if (DX12HotPathState::skipAll ||
		!DX12HotPathState::NeedsPreSkinProbe(DX12HotPathState::HookWorkFlags)) {
		if (DX12CommandListHookState::OrigResolveSubresource)
			DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResolveSubresource);
	DX12CommandListRuntime::BumpIaContentEpoch(commandList);
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ResolveSubresource", commandList,
		" dst=%p dstSub=%u src=%p srcSub=%u format=%u",
		dstResource, dstSubresource, srcResource, srcSubresource, static_cast<UINT>(format));
	DX12CommandListHookState::OrigResolveSubresource(commandList, dstResource, dstSubresource, srcResource, srcSubresource, format);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	DX12_PROFILE_FUNCTION(IASetPrimitiveTopology);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_PRIMITIVE_TOPOLOGY original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology, PFN_IA_SET_PRIMITIVE_TOPOLOGY, DX12CommandListHookState::OrigIASetPrimitiveTopology, "IASetPrimitiveTopology");
		if (original)
			original(commandList, topology);
		return;
	}

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
	DX12_PROFILE_FUNCTION(SetPipelineState);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiSetPipelineState);
	if (DX12HotPathState::skipAll) {
		PFN_SET_PIPELINE_STATE original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState, PFN_SET_PIPELINE_STATE, DX12CommandListHookState::OrigSetPipelineState, "SetPipelineState");
		if (original)
			original(commandList, pipelineState);
		return;
	}

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
	DX12_PROFILE_FUNCTION(ResourceBarrier);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiResourceBarrier);
	if (DX12HotPathState::skipAll) {
		PFN_RESOURCE_BARRIER original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier, PFN_RESOURCE_BARRIER, DX12CommandListHookState::OrigResourceBarrier, "ResourceBarrier");
		if (original)
			original(commandList, numBarriers, barriers);
		return;
	}
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
	DX12_PROFILE_FUNCTION(SetGraphicsRootUnorderedAccessView);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
	if (DX12HotPathState::skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	if (DX12HotPathState::skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address, true,
		"ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView");
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView, PFN_SET_ROOT_GPU_VA, DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView, "SetGraphicsRootUnorderedAccessView");
	if (original)
		original(commandList, rootParameterIndex, address);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	DX12_PROFILE_FUNCTION(IASetIndexBuffer);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_INDEX_BUFFER original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
		if (original)
			original(commandList, view);
		return;
	}

	DX12CommandListStateFlow::CaptureIndexBuffer(commandList, view);
	PFN_IA_SET_INDEX_BUFFER original = DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer, PFN_IA_SET_INDEX_BUFFER, DX12CommandListHookState::OrigIASetIndexBuffer, "IASetIndexBuffer");
	if (original)
		original(commandList, view);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedIASetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	DX12_PROFILE_FUNCTION(IASetVertexBuffers);
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
	if (DX12HotPathState::skipAll) {
		PFN_IA_SET_VERTEX_BUFFERS original =
			DX12_CL_ORIG(commandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers, PFN_IA_SET_VERTEX_BUFFERS, DX12CommandListHookState::OrigIASetVertexBuffers, "IASetVertexBuffers");
		if (original)
			original(commandList, startSlot, count, views);
		return;
	}

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
	if (DX12HotPathState::NeedsBindingEventRecording(
			DX12HotPathState::HookWorkFlags)) {
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordOutputTargets(commandList, numRenderTargetDescriptors,
				renderTargetDescriptors, singleHandleToDescriptorRange, depthStencilDescriptor);
	}
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
	if (DX12HotPathState::NeedsBindingEventRecording(
			DX12HotPathState::HookWorkFlags)) {
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordDepthStencilClear(commandList, depthStencilView);
	}
	DX12CommandListHookState::OrigClearDepthStencilView(commandList, depthStencilView, clearFlags, depth, stencil, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearRenderTargetView(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects)
{
	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearRenderTargetView", commandList,
		" rtv=0x%llx rects=%u", static_cast<unsigned long long>(renderTargetView.ptr), numRects);
	if (DX12HotPathState::NeedsBindingEventRecording(
			DX12HotPathState::HookWorkFlags)) {
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordRenderTargetClear(commandList, renderTargetView);
	}
	DX12CommandListHookState::OrigClearRenderTargetView(commandList, renderTargetView, colorRGBA, numRects, rects);
}

void STDMETHODCALLTYPE DX12CommandListHookCallbacks::HookedClearUnorderedAccessViewUint(
	ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
	UINT numRects, const D3D12_RECT *rects)
{
	if (DX12HotPathState::skipAll ||
		!DX12HotPathState::NeedsPreSkinProbe(DX12HotPathState::HookWorkFlags)) {
		if (DX12CommandListHookState::OrigClearUnorderedAccessViewUint)
			DX12CommandListHookState::OrigClearUnorderedAccessViewUint(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
				resource, values, numRects, rects);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
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
	if (DX12HotPathState::skipAll ||
		!DX12HotPathState::NeedsPreSkinProbe(DX12HotPathState::HookWorkFlags)) {
		if (DX12CommandListHookState::OrigClearUnorderedAccessViewFloat)
			DX12CommandListHookState::OrigClearUnorderedAccessViewFloat(commandList, viewGpuHandleInCurrentHeap, viewCpuHandle,
				resource, values, numRects, rects);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
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
	const UINT64 flags = DX12HotPathState::HookWorkFlags;
	const bool needsPreSkinProbe =
		(flags & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
	const bool huntEnabled =
		(flags & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;
	if (DX12HotPathState::skipAll || (!needsPreSkinProbe && !huntEnabled)) {
		if (original)
			original(commandList, commandSignature, maxCommandCount, argumentBuffer,
				argumentBufferOffset, countBuffer, countBufferOffset);
		return;
	}
	DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
		DX12OuterTimingProbeUtils::ApiExecuteIndirect);

	DX12CommandListHookDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ExecuteIndirect", commandList,
		" signature=%p max=%u args=%p argOffset=%llu count=%p countOffset=%llu",
		commandSignature, maxCommandCount, argumentBuffer,
		static_cast<unsigned long long>(argumentBufferOffset), countBuffer,
		static_cast<unsigned long long>(countBufferOffset));
	if (huntEnabled)
		DX12CommandListCommandRecorder::RecordExecuteIndirect(commandList);

	DX12ExecuteIndirectRuntime::Execute(commandList, commandSignature, maxCommandCount,
		argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset, original);
}

void DX12DeviceChildHooks::Install(IUnknown *object)
{
	if (!object)
		return;

	ID3D12DeviceChild *deviceChild = nullptr;
	if (FAILED(object->QueryInterface(IID_PPV_ARGS(&deviceChild))))
		return;

	DX12VTableHook hook = {
		GetDeviceSlot,
		reinterpret_cast<void**>(&OriginalGetDevice),
		reinterpret_cast<void*>(HookedGetDevice),
		"ID3D12DeviceChild::GetDevice",
	};
	DX12HookManager::InstallVTableHook(deviceChild, hook);
	deviceChild->Release();
}

HRESULT STDMETHODCALLTYPE DX12DeviceChildHooks::HookedGetDevice(
	ID3D12DeviceChild *object, REFIID riid, void **device)
{
	if (!object || !device)
		return E_POINTER;

	void **vtable = DX12HookManager::GetVTable(object);
	GetDevice original = vtable ? reinterpret_cast<GetDevice>(
		DX12HookRegistry::GetOriginalFunction(vtable[GetDeviceSlot])) : nullptr;
	if (!original)
		return E_FAIL;

	HRESULT hr = original(object, riid, device);
	if (SUCCEEDED(hr) && *device)
		DX12Wrapper::WrapD3D12DeviceResult(riid, device);
	return hr;
}


void DX12CommandListHooks::HookCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	DX12VTableHook hooks[] = {
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::UpdateTileMappings,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueUpdateTileMappings),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueUpdateTileMappings), "ID3D12CommandQueue::UpdateTileMappings"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::CopyTileMappings,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueCopyTileMappings),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueCopyTileMappings), "ID3D12CommandQueue::CopyTileMappings"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::ExecuteCommandLists,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueExecuteCommandLists),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueExecuteCommandLists), "ID3D12CommandQueue::ExecuteCommandLists"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::SetMarker,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueSetMarker),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueSetMarker), "ID3D12CommandQueue::SetMarker"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::BeginEvent,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueBeginEvent),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueBeginEvent), "ID3D12CommandQueue::BeginEvent"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::EndEvent,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueEndEvent),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueEndEvent), "ID3D12CommandQueue::EndEvent"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::Signal,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueSignal),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueSignal), "ID3D12CommandQueue::Signal"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::Wait,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueWait),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueWait), "ID3D12CommandQueue::Wait"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::GetTimestampFrequency,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueGetTimestampFrequency),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueGetTimestampFrequency), "ID3D12CommandQueue::GetTimestampFrequency"},
		{DX12CommandListHookSlotTypes::ID3D12CommandQueue::GetClockCalibration,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigQueueGetClockCalibration),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedQueueGetClockCalibration), "ID3D12CommandQueue::GetClockCalibration"},
	};
	DX12HookManager::InstallVTableHooks(queue, hooks, ARRAYSIZE(hooks));
	DX12CommandQueueState::Set(queue);
	RememberCommandQueueForHotPath(queue);
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

	ID3D12GraphicsCommandList *list = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&list))))
		return;
	void **vtable = DX12HookManager::GetVTable(list);
	if (!DX12CommandListHookRegistry::ShouldHookCommandListVTable(vtable)) {
		list->Release();
		return;
	}

#define DX12_CL_HOOK(slotName, originalName, callbackClass, callbackName) \
	{DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::slotName, \
		reinterpret_cast<void**>(&DX12CommandListHookState::originalName), \
		reinterpret_cast<void*>(callbackClass::callbackName), "ID3D12GraphicsCommandList::" #slotName}

	DX12VTableHook hooks[] = {
		DX12_CL_HOOK(Close, OrigCloseCommandList, DX12CommandListHookCallbacks, HookedCloseCommandList),
		DX12_CL_HOOK(Reset, OrigResetCommandList, DX12CommandListHookCallbacks, HookedResetCommandList),
		DX12_CL_HOOK(ClearState, OrigClearState, DX12CommandListHookCallbacks, HookedClearState),
		DX12_CL_HOOK(DrawInstanced, OrigDrawInstanced, DX12CommandListHookCallbacks, HookedDrawInstanced),
		DX12_CL_HOOK(DrawIndexedInstanced, OrigDrawIndexedInstanced, DX12CommandListHookCallbacks, HookedDrawIndexedInstanced),
		DX12_CL_HOOK(Dispatch, OrigDispatch, DX12CommandListHookCallbacks, HookedDispatch),
		DX12_CL_HOOK(CopyBufferRegion, OrigCopyBufferRegion, DX12CommandListHookCallbacks, HookedCopyBufferRegion),
		DX12_CL_HOOK(CopyTextureRegion, OrigCopyTextureRegion, DX12CommandListHookCallbacks, HookedCopyTextureRegion),
		DX12_CL_HOOK(CopyResource, OrigCopyResource, DX12CommandListHookCallbacks, HookedCopyResource),
		DX12_CL_HOOK(CopyTiles, OrigCopyTiles, DX12CommandListHookCallbacks, HookedCopyTiles),
		DX12_CL_HOOK(ResolveSubresource, OrigResolveSubresource, DX12CommandListHookCallbacks, HookedResolveSubresource),
		DX12_CL_HOOK(IaSetPrimitiveTopology, OrigIASetPrimitiveTopology, DX12CommandListHookCallbacks, HookedIASetPrimitiveTopology),
		DX12_CL_HOOK(RsSetViewports, OrigRSSetViewports, DX12CommandListHookCallbacks, HookedRSSetViewports),
		DX12_CL_HOOK(RsSetScissorRects, OrigRSSetScissorRects, DX12CommandListHookCallbacks, HookedRSSetScissorRects),
		DX12_CL_HOOK(OmSetBlendFactor, OrigOMSetBlendFactor, DX12CommandListHookCallbacks, HookedOMSetBlendFactor),
		DX12_CL_HOOK(OmSetStencilRef, OrigOMSetStencilRef, DX12CommandListHookCallbacks, HookedOMSetStencilRef),
		DX12_CL_HOOK(SetPipelineState, OrigSetPipelineState, DX12CommandListHookCallbacks, HookedSetPipelineState),
		DX12_CL_HOOK(ResourceBarrier, OrigResourceBarrier, DX12CommandListHookCallbacks, HookedResourceBarrier),
		DX12_CL_HOOK(ExecuteBundle, OrigExecuteBundle, DX12CommandListHookCallbacks, HookedExecuteBundle),
		DX12_CL_HOOK(SetDescriptorHeaps, OrigSetDescriptorHeaps, DX12CommandListComputeBindingHookCallbacks, HookedSetDescriptorHeaps),
		DX12_CL_HOOK(SetComputeRootSignature, OrigSetComputeRootSignature, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRootSignature),
		DX12_CL_HOOK(SetGraphicsRootSignature, OrigSetGraphicsRootSignature, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRootSignature),
		DX12_CL_HOOK(SetComputeRootDescriptorTable, OrigSetComputeRootDescriptorTable, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRootDescriptorTable),
		DX12_CL_HOOK(SetGraphicsRootDescriptorTable, OrigSetGraphicsRootDescriptorTable, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRootDescriptorTable),
		DX12_CL_HOOK(SetComputeRoot32BitConstant, OrigSetComputeRoot32BitConstant, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRoot32BitConstant),
		DX12_CL_HOOK(SetGraphicsRoot32BitConstant, OrigSetGraphicsRoot32BitConstant, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRoot32BitConstant),
		DX12_CL_HOOK(SetComputeRoot32BitConstants, OrigSetComputeRoot32BitConstants, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRoot32BitConstants),
		DX12_CL_HOOK(SetGraphicsRoot32BitConstants, OrigSetGraphicsRoot32BitConstants, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRoot32BitConstants),
		DX12_CL_HOOK(SetComputeRootConstantBufferView, OrigSetComputeRootConstantBufferView, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRootConstantBufferView),
		DX12_CL_HOOK(SetGraphicsRootConstantBufferView, OrigSetGraphicsRootConstantBufferView, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRootConstantBufferView),
		DX12_CL_HOOK(SetComputeRootShaderResourceView, OrigSetComputeRootShaderResourceView, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRootShaderResourceView),
		DX12_CL_HOOK(SetGraphicsRootShaderResourceView, OrigSetGraphicsRootShaderResourceView, DX12CommandListComputeBindingHookCallbacks, HookedSetGraphicsRootShaderResourceView),
		DX12_CL_HOOK(SetComputeRootUnorderedAccessView, OrigSetComputeRootUnorderedAccessView, DX12CommandListComputeBindingHookCallbacks, HookedSetComputeRootUnorderedAccessView),
		DX12_CL_HOOK(SetGraphicsRootUnorderedAccessView, OrigSetGraphicsRootUnorderedAccessView, DX12CommandListHookCallbacks, HookedSetGraphicsRootUnorderedAccessView),
		DX12_CL_HOOK(IaSetIndexBuffer, OrigIASetIndexBuffer, DX12CommandListHookCallbacks, HookedIASetIndexBuffer),
		DX12_CL_HOOK(IaSetVertexBuffers, OrigIASetVertexBuffers, DX12CommandListHookCallbacks, HookedIASetVertexBuffers),
		DX12_CL_HOOK(SoSetTargets, OrigSOSetTargets, DX12CommandListHookCallbacks, HookedSOSetTargets),
		DX12_CL_HOOK(OmSetRenderTargets, OrigOMSetRenderTargets, DX12CommandListHookCallbacks, HookedOMSetRenderTargets),
		DX12_CL_HOOK(ClearDepthStencilView, OrigClearDepthStencilView, DX12CommandListHookCallbacks, HookedClearDepthStencilView),
		DX12_CL_HOOK(ClearRenderTargetView, OrigClearRenderTargetView, DX12CommandListHookCallbacks, HookedClearRenderTargetView),
		DX12_CL_HOOK(ClearUnorderedAccessViewUint, OrigClearUnorderedAccessViewUint, DX12CommandListHookCallbacks, HookedClearUnorderedAccessViewUint),
		DX12_CL_HOOK(ClearUnorderedAccessViewFloat, OrigClearUnorderedAccessViewFloat, DX12CommandListHookCallbacks, HookedClearUnorderedAccessViewFloat),
		DX12_CL_HOOK(DiscardResource, OrigDiscardResource, DX12CommandListHookCallbacks, HookedDiscardResource),
		DX12_CL_HOOK(BeginQuery, OrigBeginQuery, DX12CommandListHookCallbacks, HookedBeginQuery),
		DX12_CL_HOOK(EndQuery, OrigEndQuery, DX12CommandListHookCallbacks, HookedEndQuery),
		DX12_CL_HOOK(ResolveQueryData, OrigResolveQueryData, DX12CommandListHookCallbacks, HookedResolveQueryData),
		DX12_CL_HOOK(SetPredication, OrigSetPredication, DX12CommandListHookCallbacks, HookedSetPredication),
		DX12_CL_HOOK(SetMarker, OrigCommandListSetMarker, DX12CommandListHookCallbacks, HookedCommandListSetMarker),
		DX12_CL_HOOK(BeginEvent, OrigCommandListBeginEvent, DX12CommandListHookCallbacks, HookedCommandListBeginEvent),
		DX12_CL_HOOK(EndEvent, OrigCommandListEndEvent, DX12CommandListHookCallbacks, HookedCommandListEndEvent),
		DX12_CL_HOOK(ExecuteIndirect, OrigExecuteIndirect, DX12CommandListHookCallbacks, HookedExecuteIndirect),
	};
	DX12HookManager::InstallVTableHooks(list, hooks, ARRAYSIZE(hooks));
#undef DX12_CL_HOOK
	list->Release();
}

void DX12CommandListHooks::HookCommandListCreation(IUnknown *device)
{
	if (!device)
		return;
	ID3D12Device *baseDevice = nullptr;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&baseDevice))))
		return;
	DX12VTableHook baseHooks[] = {
		{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandQueue,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandQueue),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedCreateCommandQueue), "ID3D12Device::CreateCommandQueue"},
		{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandAllocator,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandAllocator),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedCreateCommandAllocator), "ID3D12Device::CreateCommandAllocator"},
		{DX12CommandListHookSlotTypes::ID3D12Device::CreateCommandList,
			reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList),
			reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedCreateCommandList), "ID3D12Device::CreateCommandList"},
	};
	DX12HookManager::InstallVTableHooks(baseDevice, baseHooks, ARRAYSIZE(baseHooks));
	baseDevice->Release();

	ID3D12Device4 *device4 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4)))) {
		DX12VTableHook device4Hooks[] = {
			{DX12CommandListHookSlotTypes::ID3D12Device4::CreateCommandList1,
				reinterpret_cast<void**>(&DX12CommandListHookState::OrigCreateCommandList1),
				reinterpret_cast<void*>(DX12CommandListHookCallbacks::HookedCreateCommandList1), "ID3D12Device4::CreateCommandList1"},
		};
		DX12HookManager::InstallVTableHooks(device4, device4Hooks, ARRAYSIZE(device4Hooks));
		device4->Release();
	}
}
