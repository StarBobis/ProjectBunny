#pragma once

#include <d3d12.h>

bool DX12CommandListCaptureShouldTrackBindings();
bool DX12CommandListCaptureShouldRecordBindingEvents();
bool DX12CommandListCaptureShouldTrackHuntIa();
bool DX12CommandListCaptureShouldTrackPsoState();

bool DX12CommandListCaptureShouldTrackBindingsCached(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListCaptureShouldRecordBindingEventsCached(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListCaptureShouldTrackHuntIaCached(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListCaptureShouldTrackPsoStateCached(ID3D12GraphicsCommandList *commandList);

void DX12CommandListCapturePrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
ID3D12PipelineState *DX12CommandListCapturePipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12CommandListCaptureDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
void DX12CommandListCaptureDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap);
void DX12CommandListCaptureComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
void DX12CommandListCaptureGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
void DX12CommandListCaptureComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
void DX12CommandListCaptureGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
void DX12CommandListCaptureComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
void DX12CommandListCaptureGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
void DX12CommandListCaptureComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset);
void DX12CommandListCaptureGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset);
void DX12CommandListCaptureComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
void DX12CommandListCaptureGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
void DX12CommandListCaptureIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
void DX12CommandListCaptureVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views);
