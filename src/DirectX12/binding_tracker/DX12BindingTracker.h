#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <stdint.h>

#include <string>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"

struct DX12FrameResourceBinding
{
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	std::string bindSpace;
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT rangeType = UINT_MAX;
	UINT shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	ID3D12DescriptorHeap *heap = nullptr;
	UINT heapType = 0;
	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	UINT64 heapGpuStart = 0;
	UINT64 gpuVirtualAddress = 0;
	std::string usageClass;
	UINT64 usageBegin = 0;
	UINT64 usageBytes = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

struct DX12FrameIaBufferBinding
{
	std::string bufferId;
	std::string role;
	std::string skinningClass;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	UINT64 producerEventSerial = 0;
	UINT64 producerDrawId = 0;
	UINT64 producerDispatchId = 0;
	UINT64 producerPsoIndex = 0;
	DX12PsoShaderInfo producerShaderInfo;
	std::string producerBindSpace;
	UINT producerRootParameterIndex = 0;
	UINT producerShaderRegister = UINT_MAX;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewGpuVa = 0;
	UINT64 viewSize = 0;
	UINT64 viewOffset = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource;
};

struct DX12FrameOutputBinding
{
	std::string kind;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

struct DX12CurrentIaBuffer
{
	std::string role;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource;
};

struct DX12CurrentIaState
{
	std::vector<DX12CurrentIaBuffer> vertexBuffers;
	DX12CurrentIaBuffer indexBuffer;
	bool hasIndexBuffer = false;
};

struct DX12CurrentComputeUavBinding
{
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	UINT descriptorIncrement = 0;
	UINT tableCopyCount = 0;
	SIZE_T tableCpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE tableGpuStart = {};
	ID3D12DescriptorHeap *tableHeap = nullptr;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

using DX12CurrentShaderResourceBinding = DX12CurrentComputeUavBinding;

struct DX12CurrentRootConstants
{
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};

void DX12BindingRegisterCommandList(ID3D12GraphicsCommandList *commandList);
void DX12BindingResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
void DX12BindingSetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12BindingRecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind);
bool DX12BindingSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps);
bool DX12BindingSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap);
void DX12BindingSetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
void DX12BindingSetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
void DX12BindingSetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
void DX12BindingSetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
void DX12BindingSetGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
void DX12BindingSetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
void DX12BindingSetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value);
void DX12BindingSetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value);
void DX12BindingSetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values);
void DX12BindingSetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values);
void DX12BindingSetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
void DX12BindingSetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
void DX12BindingSetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views);
void DX12BindingRecordOutputTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
void DX12BindingRecordRenderTargetClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView);
void DX12BindingRecordDepthStencilClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView);
bool DX12BindingGetCurrentIaState(
	ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state);
bool DX12BindingGetCurrentComputeUavs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs);
bool DX12BindingGetCurrentComputeSrvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *srvs);
bool DX12BindingGetCurrentComputeCbvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *cbvs);
bool DX12BindingGetCurrentShaderResourceBindings(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	std::vector<DX12CurrentShaderResourceBinding> *bindings);
bool DX12BindingGetCurrentComputeRootConstants(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentRootConstants> *constants);

bool DX12BindingGetCurrentComputeAll(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants);
UINT64 DX12BindingGetComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
bool DX12BindingGetCurrentDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap);
void DX12BindingRecordDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance,
	UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
void DX12BindingRecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation);
void DX12BindingRecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ);
void DX12BindingBeginFrame();
void DX12GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos);
void DX12GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings);
void DX12GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers);
void DX12GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings);
void DX12BuildIaBufferFileName(
	const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount);
DX12FrameAnalysisDumpResult DX12DumpBindingTrace(const wchar_t *dir);
DX12FrameAnalysisDumpResult DX12DumpCurrentFrameResourceTable(const wchar_t *dir);
