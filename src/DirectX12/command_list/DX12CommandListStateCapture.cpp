#include "DX12CommandListStateCapture.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12ModRuntime.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"


bool DX12CommandListStateCapture::ShouldTrackBindings()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
		DX12ModNeedsPreSkinningUavProbe() ||
		DX12ModNeedsShaderDescriptorTracking();
}

bool DX12CommandListStateCapture::ShouldRecordBindingEvents()
{
	return DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame();
}

bool DX12CommandListStateCapture::ShouldTrackHuntIa()
{
	return DX12HuntIsEnabled();
}

bool DX12CommandListStateCapture::ShouldTrackPsoState()
{
	return DX12HuntIsEnabled() || DX12ModHasActiveShaderOverrides() ||
		DX12ModNeedsPreSkinningUavProbe();
}


bool DX12CommandListStateCapture::ShouldTrackBindingsCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackBindings();
	return DX12CommandListRuntime::GetTrackBindings(commandList);
}

bool DX12CommandListStateCapture::ShouldRecordBindingEventsCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldRecordBindingEvents();
	return DX12CommandListRuntime::GetRecordBindingEvents(commandList);
}

bool DX12CommandListStateCapture::ShouldTrackHuntIaCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackHuntIa();
	return DX12CommandListRuntime::GetTrackHuntIa(commandList);
}

bool DX12CommandListStateCapture::ShouldTrackPsoStateCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListStateCapture::ShouldTrackPsoState();
	return DX12CommandListRuntime::GetTrackPsoState(commandList);
}


void DX12CommandListStateCapture::PrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (DX12CommandListStateCapture::ShouldTrackHuntIaCached(commandList))
		DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
	if (DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		DX12BindingTracker::SetPrimitiveTopology(commandList, topology);
}

ID3D12PipelineState *DX12CommandListStateCapture::PipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	ID3D12PipelineState *activePipelineState = pipelineState;
	if (DX12ModHasActiveShaderOverrides()) {
		ID3D12PipelineState *replacement =
			DX12ModGetReplacementPipelineState(pipelineState);
		if (replacement)
			activePipelineState = replacement;
	}

	const bool trackBindings = DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList);
	if (trackBindings) {
		DX12BindingTracker::SetPipelineState(commandList, activePipelineState);
		if (trackBindings && DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
			DX12BindingTracker::RecordStateEvent(commandList, "set_pso");
	}
	if (DX12CommandListStateCapture::ShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntime::RememberPipelineState(commandList, activePipelineState);
	if (DX12HuntIsEnabled())
		DX12HuntSetPipelineState(commandList, activePipelineState);
	return activePipelineState;
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, count, heaps);
	if (changed && DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::DescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;

	const bool changed = DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
	if (changed && DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListStateCapture::ComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetComputeRootSignature(commandList, rootSignature);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_signature");
}

void DX12CommandListStateCapture::GraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetGraphicsRootSignature(commandList, rootSignature);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_signature");
}

void DX12CommandListStateCapture::ComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetComputeRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_table");
}

void DX12CommandListStateCapture::GraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetGraphicsRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_table");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetComputeRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constant");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constant");
}

void DX12CommandListStateCapture::ComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_compute_root_constants");
}

void DX12CommandListStateCapture::GraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "set_graphics_root_constants");
}

void DX12CommandListStateCapture::ComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetComputeRootDescriptor(commandList, rootParameterIndex, type, address);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_compute_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_compute_root_srv" : "set_compute_root_uav");
}

void DX12CommandListStateCapture::GraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		return;
	DX12BindingTracker::SetGraphicsRootDescriptor(commandList, rootParameterIndex, type, address);
	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_graphics_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_graphics_root_srv" : "set_graphics_root_uav");
}

void DX12CommandListStateCapture::IndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		DX12BindingTracker::SetIndexBuffer(commandList, view);
	if (DX12CommandListStateCapture::ShouldTrackHuntIaCached(commandList))
		DX12HuntSetIndexBuffer(commandList, view);
	if (DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
}

void DX12CommandListStateCapture::VertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList))
		DX12BindingTracker::SetVertexBuffers(commandList, startSlot, count, views);
	if (DX12CommandListStateCapture::ShouldTrackHuntIaCached(commandList))
		DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
	if (DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
}


