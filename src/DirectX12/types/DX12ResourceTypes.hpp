#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

class DX12RootDescriptorRangeSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT rangeIndex = 0;
	UINT rangeType = 0;
	UINT numDescriptors = 0;
	UINT baseShaderRegister = 0;
	UINT registerSpace = 0;
	UINT offsetInDescriptorsFromTableStart = 0;
	UINT flags = 0;
	UINT effectiveOffset = 0;
	UINT shaderVisibility = 0;
};

class DX12RootParameterSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT parameterType = 0;
	UINT shaderVisibility = 0;
	UINT shaderRegister = 0;
	UINT registerSpace = 0;
	UINT rootDescriptorFlags = 0;
	UINT num32BitValues = 0;
	std::vector<DX12RootDescriptorRangeSummary> ranges;
};

class DX12RootSignatureSummary
{
public:
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 hash = 0;
	SIZE_T size = 0;
	UINT nodeMask = 0;
	UINT version = 0;
	UINT flags = 0;
	UINT staticSamplerCount = 0;
	bool parsed = false;
	std::vector<DX12RootParameterSummary> parameters;
};

class DescriptorKind
{
public:
	static constexpr std::string_view CBV = "CBV";
	static constexpr std::string_view SRV = "SRV";
	static constexpr std::string_view UAV = "UAV";
	static constexpr std::string_view RTV = "RTV";
	static constexpr std::string_view DSV = "DSV";
	static constexpr std::string_view Sampler = "Sampler";
	static constexpr UINT CBV_ID = 1;
	static constexpr UINT SRV_ID = 2;
	static constexpr UINT UAV_ID = 3;
	static constexpr UINT RTV_ID = 4;
	static constexpr UINT DSV_ID = 5;
	static constexpr UINT Sampler_ID = 6;
	static constexpr std::string_view CONSTANT_BUFFER_VIEW = "CONSTANT_BUFFER_VIEW";
	static constexpr std::string_view SHADER_RESOURCE_VIEW = "SHADER_RESOURCE_VIEW";
	static constexpr std::string_view UNORDERED_ACCESS_VIEW = "UNORDERED_ACCESS_VIEW";
	static constexpr std::string_view RENDER_TARGET_VIEW = "RENDER_TARGET_VIEW";
	static constexpr std::string_view DEPTH_STENCIL_VIEW = "DEPTH_STENCIL_VIEW";
};

class IaBufferRole
{
public:
	static constexpr std::string_view IB = "IB";
	static constexpr std::string_view VB = "VB";
};

class DX12DescriptorSummary
{
public:
	std::string kind;
	SIZE_T cpuHandle = 0;
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *counterResource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	bool hasSrvDesc = false;
	UINT viewFormat = 0;
	UINT viewDimension = 0;
	UINT cbvSize = 0;
	UINT64 firstElement = 0;
	UINT numElements = 0;
	UINT structureByteStride = 0;
	UINT64 bufferViewOffset = 0;
	UINT64 bufferViewBytes = 0;
};

class DX12DescriptorHeapSummary
{
public:
	ID3D12DescriptorHeap *heap = nullptr;
	UINT type = 0;
	UINT numDescriptors = 0;
	UINT flags = 0;
	UINT nodeMask = 0;
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	UINT64 version = 0;
};

class DX12PsoRootSummary
{
public:
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

class DX12BufferResourceSummary
{
public:
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
};

class DX12ResourceSummary
{
public:
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool retained = false;
};
