#pragma once

#include <d3d12.h>

#include "DX12CommandListTrackingState.hpp"

class DX12CommandListStateCapture
{
public:
	static bool ShouldTrackBindings();
	static bool ShouldTrackIaBindings();
	static bool ShouldRecordBindingEvents();
	static bool ShouldTrackHuntIa();
	static bool ShouldTrackPsoState();
	static DX12CommandListTrackingState MakeTrackingState();
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);

	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
};
