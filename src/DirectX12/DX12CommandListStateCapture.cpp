#include "DX12CommandListStateCapture.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12HotPathTimingUtils.hpp"

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
