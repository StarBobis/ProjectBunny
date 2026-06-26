#include "DX12CommandListStateCapture.h"

#include "DX12BindingTracker.h"
#include "DX12CommandListRuntime.h"
#include "DX12FrameAnalysis.h"
#include "DX12ModRuntime.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"

// --- global (uncached) predicates ---
// Used during initialisation and at frame boundaries where no single
// command-list pointer is the right scope.

bool DX12CommandListCaptureShouldTrackBindings()
{
	// Hunt and mod matching use lightweight RuntimeState / Hunt IA hashes;
	// only capture/dump paths need the full BindingTracker.
	return DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame();
}

bool DX12CommandListCaptureShouldRecordBindingEvents()
{
	return DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame();
}

bool DX12CommandListCaptureShouldTrackHuntIa()
{
	return DX12HuntIsEnabled() || DX12ModHasActiveTextureOverrides();
}

bool DX12CommandListCaptureShouldTrackPsoState()
{
	return DX12HuntIsEnabled() || DX12ModHasActiveShaderOverrides() ||
		DX12ModHasActiveTextureOverrides();
}

// --- per-command-list cached predicates ---
// Hot-path call sites pass the command-list pointer so the answer comes from
// the per-list cache instead of re-evaluating the global flags every time.

bool DX12CommandListCaptureShouldTrackBindingsCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListCaptureShouldTrackBindings();
	return DX12CommandListRuntimeGetTrackBindings(commandList);
}

bool DX12CommandListCaptureShouldRecordBindingEventsCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListCaptureShouldRecordBindingEvents();
	return DX12CommandListRuntimeGetRecordBindingEvents(commandList);
}

bool DX12CommandListCaptureShouldTrackHuntIaCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListCaptureShouldTrackHuntIa();
	return DX12CommandListRuntimeGetTrackHuntIa(commandList);
}

bool DX12CommandListCaptureShouldTrackPsoStateCached(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return DX12CommandListCaptureShouldTrackPsoState();
	return DX12CommandListRuntimeGetTrackPsoState(commandList);
}

// --- capture helpers (use cached predicates on the hot path) ---

void DX12CommandListCapturePrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (DX12CommandListCaptureShouldTrackHuntIaCached(commandList))
		DX12CommandListRuntimeRememberPrimitiveTopology(commandList, topology);
	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		DX12BindingSetPrimitiveTopology(commandList, topology);
}

ID3D12PipelineState *DX12CommandListCapturePipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	ID3D12PipelineState *activePipelineState = pipelineState;
	if (DX12ModHasActiveShaderOverrides()) {
		ID3D12PipelineState *replacement =
			DX12ModGetReplacementPipelineState(pipelineState);
		if (replacement)
			activePipelineState = replacement;
	}

	// SetPipelineState changes the PSO recorded into the command list. Keep
	// replacement resolution and capture in one place so callers use one PSO.
	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList)) {
		DX12BindingSetPipelineState(commandList, activePipelineState);
		if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
			DX12BindingRecordStateEvent(commandList, "set_pso");
	}
	if (DX12CommandListCaptureShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntimeRememberPipelineState(commandList, activePipelineState);
	if (DX12HuntIsEnabled())
		DX12HuntSetPipelineState(commandList, activePipelineState);
	return activePipelineState;
}

void DX12CommandListCaptureDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;

	const bool changed = DX12BindingSetDescriptorHeaps(commandList, count, heaps);
	if (changed && DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_heaps");
}

void DX12CommandListCaptureComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetComputeRootSignature(commandList, rootSignature);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_compute_root_signature");
}

void DX12CommandListCaptureGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetGraphicsRootSignature(commandList, rootSignature);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_graphics_root_signature");
}

void DX12CommandListCaptureComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetComputeRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_compute_table");
}

void DX12CommandListCaptureGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetGraphicsRootDescriptorTable(commandList, rootParameterIndex, baseDescriptor);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_graphics_table");
}

void DX12CommandListCaptureComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetComputeRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_compute_root_constant");
}

void DX12CommandListCaptureGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetGraphicsRoot32BitConstant(commandList, rootParameterIndex, destOffset, srcData);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_graphics_root_constant");
}

void DX12CommandListCaptureComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_compute_root_constants");
}

void DX12CommandListCaptureGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, num32BitValuesToSet, srcData);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "set_graphics_root_constants");
}

void DX12CommandListCaptureComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetComputeRootDescriptor(commandList, rootParameterIndex, type, address);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_compute_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_compute_root_srv" : "set_compute_root_uav");
}

void DX12CommandListCaptureGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		return;
	DX12BindingSetGraphicsRootDescriptor(commandList, rootParameterIndex, type, address);
	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, type == D3D12_ROOT_PARAMETER_TYPE_CBV ?
			"set_graphics_root_cbv" : type == D3D12_ROOT_PARAMETER_TYPE_SRV ?
			"set_graphics_root_srv" : "set_graphics_root_uav");
}

void DX12CommandListCaptureIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		DX12BindingSetIndexBuffer(commandList, view);
	if (DX12CommandListCaptureShouldTrackHuntIaCached(commandList))
		DX12HuntSetIndexBuffer(commandList, view);
	if (DX12ModHasActiveTextureOverrides())
		DX12CommandListRuntimeRememberIndexBuffer(commandList, view);
}

void DX12CommandListCaptureVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList))
		DX12BindingSetVertexBuffers(commandList, startSlot, count, views);
	if (DX12CommandListCaptureShouldTrackHuntIaCached(commandList))
		DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
	if (DX12ModHasActiveTextureOverrides())
		DX12CommandListRuntimeRememberVertexBuffers(commandList, startSlot, count, views);
}
