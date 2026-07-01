#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <stdint.h>

#include <string>
#include <vector>

#include "types/DX12PsoTypes.hpp"
#include "types/DX12ResourceTypes.hpp"

class DX12FrameResourceBinding
{
public:
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

class DX12FrameIaBufferBinding
{
public:
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
	UINT64 drawGpuVa = 0;
	UINT64 drawSize = 0;
	UINT64 drawOffset = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	UINT drawStartIndex = 0;
	UINT drawIndexCount = 0;
	INT drawBaseVertex = 0;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource;
};

class DX12FrameOutputBinding
{
public:
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

class DX12CurrentIaBuffer
{
public:
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

class DX12CurrentIaState
{
public:
	std::vector<DX12CurrentIaBuffer> vertexBuffers;
	DX12CurrentIaBuffer indexBuffer;
	bool hasIndexBuffer = false;
};

class DX12CurrentComputeUavBinding
{
public:
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

class DX12CurrentRootConstants
{
public:
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};
