#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <vector>

struct DX12ActiveIaState
{
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

struct DX12IaBufferHash
{
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};

struct DX12IaHashState
{
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
};

struct DX12CommandListTrackingCache
{
	UINT64 generation = 0;
	bool trackBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};

struct DX12CommandListRuntimeState
{
	ID3D12PipelineState *pipelineState = nullptr;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
};

UINT64 DX12CommandListRuntimeTrackingGeneration();
void DX12CommandListRuntimeBumpTrackingGeneration();

void DX12CommandListRuntimeRegister(ID3D12GraphicsCommandList *commandList);
void DX12CommandListRuntimeReset(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
void DX12CommandListRuntimeRememberPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12CommandListRuntimeResetIa(ID3D12GraphicsCommandList *commandList);
void DX12CommandListRuntimeRememberIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
void DX12CommandListRuntimeRememberVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views);
void DX12CommandListRuntimeRememberPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
void DX12CommandListRuntimeBumpComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListRuntimeBuildIaHashState(
	const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount,
	UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
ID3D12PipelineState *DX12CommandListRuntimeGetPipelineState(
	ID3D12GraphicsCommandList *commandList);
DX12CommandListRuntimeState DX12CommandListRuntimeGetState(
	ID3D12GraphicsCommandList *commandList);
const DX12CommandListRuntimeState *DX12CommandListRuntimeGetStatePtr(
	ID3D12GraphicsCommandList *commandList);

void DX12CommandListRuntimeSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap);
bool DX12CommandListRuntimeGetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap);

bool DX12CommandListRuntimeGetTrackBindings(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListRuntimeGetRecordBindingEvents(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListRuntimeGetTrackHuntIa(ID3D12GraphicsCommandList *commandList);
bool DX12CommandListRuntimeGetTrackPsoState(ID3D12GraphicsCommandList *commandList);
