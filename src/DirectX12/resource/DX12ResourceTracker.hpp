#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "DX12FrameAnalysisTypes.hpp"
#include "types/DX12ResourceTypes.hpp"

void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
const char *DX12DiagnoseBufferResolveByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource);
uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize);
bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize);
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size, const uint8_t **data, size_t *dataSize);
UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource);
void DX12InvalidateBufferContent(ID3D12Resource *resource);
bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash);
void DX12InvalidateTextureHash(ID3D12Resource *resource);
bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox);
UINT64 DX12GetIaHashContentGeneration();
UINT64 DX12GetIaHashResolveGeneration();
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
void DX12GetTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12GetRetainedTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12ReleaseTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
bool DX12RetainResource(ID3D12Resource *resource);
void DX12ReleaseRetainedResource(ID3D12Resource *resource);
bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
void DX12FindDescriptorSummariesByCpuHandle(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	bool completeResourceInfo = true);
void DX12FindDescriptorSummariesByCpuHandleNoWait(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	uint8_t *pending,
	bool completeResourceInfo = true);
UINT64 CurrentDescriptorEpoch();
UINT64 CurrentDescriptorQuerySerial();
UINT64 DX12GetDescriptorHeapVersion(ID3D12DescriptorHeap *heap);
bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary);
bool FindDescriptorSummaryByCpuHandleAtEpochAndKind(
	SIZE_T cpuHandle, UINT64 epoch, const char *kind, DX12DescriptorSummary *summary);
void DX12PrimeDescriptorHistoryForHunting();
bool DX12FindDescriptorHeapByPtr(
	ID3D12DescriptorHeap *heap, DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
void DX12BeginFrameAnalysisMetadataCache();
void DX12EndFrameAnalysisMetadataCache();

