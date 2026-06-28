#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "DX12FrameAnalysis.h"

struct DX12RootDescriptorRangeSummary
{
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

struct DX12RootParameterSummary
{
	UINT rootParameterIndex = 0;
	UINT parameterType = 0;
	UINT shaderVisibility = 0;
	UINT shaderRegister = 0;
	UINT registerSpace = 0;
	UINT rootDescriptorFlags = 0;
	UINT num32BitValues = 0;
	std::vector<DX12RootDescriptorRangeSummary> ranges;
};

struct DX12RootSignatureSummary
{
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

// Named constants for descriptor kind strings used throughout DX12 code.
// Usage: descriptor.kind == DescriptorKind::SRV  instead of  descriptor.kind == "SRV"
struct DescriptorKind {
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
	// Long-form names set by CreateShaderResourceView etc.
	static constexpr std::string_view CONSTANT_BUFFER_VIEW = "CONSTANT_BUFFER_VIEW";
	static constexpr std::string_view SHADER_RESOURCE_VIEW = "SHADER_RESOURCE_VIEW";
	static constexpr std::string_view UNORDERED_ACCESS_VIEW = "UNORDERED_ACCESS_VIEW";
	static constexpr std::string_view RENDER_TARGET_VIEW = "RENDER_TARGET_VIEW";
	static constexpr std::string_view DEPTH_STENCIL_VIEW = "DEPTH_STENCIL_VIEW";
};

struct IaBufferRole {
	static constexpr std::string_view IB = "IB";
	static constexpr std::string_view VB = "VB";
};

struct DX12DescriptorSummary
{
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
	bool hasDesc = false;
	UINT viewFormat = 0;
	UINT viewDimension = 0;
	UINT cbvSize = 0;
	UINT64 firstElement = 0;
	UINT numElements = 0;
	UINT structureByteStride = 0;
	UINT64 bufferViewOffset = 0;
	UINT64 bufferViewBytes = 0;
};

struct DX12DescriptorHeapSummary
{
	ID3D12DescriptorHeap *heap = nullptr;
	UINT type = 0;
	UINT numDescriptors = 0;
	UINT flags = 0;
	UINT nodeMask = 0;
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
};

struct DX12PsoRootSummary
{
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

struct DX12BufferResourceSummary
{
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
};

void DX12HookResourceMetadata(ID3D12Device *device);
void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary);
bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps);
bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
void DX12BeginFrameAnalysisMetadataCache();
void DX12EndFrameAnalysisMetadataCache();
DX12FrameAnalysisDumpResult DX12DumpResourceMetadata(const wchar_t *dir);
