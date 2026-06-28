#pragma once

#include "DX12ResourceTracker.h"

#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12Json.h"
#include "DX12ModRuntime.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"
#include "crc32c.h"

struct RootSignatureRecord
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

struct DescriptorRecord
{
	std::string kind;
	UINT kindId = 0;
	UINT64 valueId = 0;
	SIZE_T cpuHandle = 0;
	ID3D12Resource *resource = nullptr;
	bool ownsResource = false;
	ID3D12Resource *counterResource = nullptr;
	bool ownsCounterResource = false;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
	D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	D3D12_SAMPLER_DESC sampler = {};
	bool hasDesc = false;
};

struct DescriptorHeapRecord
{
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	std::unordered_map<UINT, DescriptorRecord> slots;
};

struct DescriptorSlotLocation
{
	size_t heapIndex = 0;
	UINT slot = 0;
};

struct ResourceRecord
{
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	bool hasHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
};

struct BufferResolveCacheEntry
{
	UINT64 begin = 0;
	UINT64 end = 0;
	DX12BufferResourceSummary summary = {};
};

struct PsoRootRecord
{
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

extern SRWLOCK gResourceLock;
extern SRWLOCK gDescriptorLock;
extern std::vector<RootSignatureRecord> gRootSignatures;
extern std::vector<DescriptorHeapRecord> gDescriptorHeaps;
extern std::vector<DescriptorRecord> gDescriptors;
extern std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
extern std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
extern std::unordered_map<SIZE_T, DescriptorSlotLocation> gDescriptorByCpuHandle;
extern std::vector<PsoRootRecord> gPsoRoots;
extern std::unordered_map<UINT64, size_t> gPsoRootByIndex;
extern std::vector<ResourceRecord> gResources;
extern std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
extern std::map<UINT64, size_t> gBufferResourceByGpuVa;
extern std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
extern std::atomic<UINT64> gBufferResolveGeneration;
extern bool gCleanupRegistered;
extern UINT64 gResourcesRecordedFromCreate;
extern UINT64 gDescriptorRecordsSeen;
extern UINT64 gDescriptorCopyRecordsSeen;
extern UINT64 gDescriptorHeapRecordsSeen;
extern UINT64 gRootSignatureRecordsSeen;
extern UINT64 gPsoRootRecordsSeen;
extern LONG gLastResourceStatsPresent;

uint32_t HashBytes(uint32_t seed, const void *data, size_t size);
UINT64 Fnv1a64(const void *data, size_t size);
constexpr uint32_t MakeTrackerFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
constexpr bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}
D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc);
D3D12_RESOURCE_STATES ResourceStateFromLayout(D3D12_BARRIER_LAYOUT layout);
ID3D12Resource *CanonicalResource(ID3D12Resource *resource);
void RecordResource(ID3D12Resource *resource, const D3D12_RESOURCE_DESC *desc, const D3D12_HEAP_PROPERTIES *heapProperties, D3D12_RESOURCE_STATES initialState);
void CleanupTrackedResources();
void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength);
void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource);
void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource);
void ReleaseDescriptorRecordResources(DescriptorRecord *record);
DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record);
bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record);
void SetDescriptorKind(DescriptorRecord *record, UINT kindId);
void FillSrvBufferView(DescriptorRecord *record);
void FillUavBufferView(DescriptorRecord *record);
void PrepareDescriptorValueId(DescriptorRecord *record);
bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId);
void LogResourceTrackerStatsLocked();
void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap);
void RecordDescriptor(DescriptorRecord &&record);
bool ShouldTrackFullDescriptorMetadata();
bool ShouldTrackComputeDescriptorMetadata();
bool ShouldTrackDescriptorCreationMetadata();
bool ShouldTrackResourceMetadata();
bool ShouldTrackResourceCreationMetadata();
void RecordDescriptorCopyRange(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_CPU_DESCRIPTOR_HANDLE destStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart, UINT count);
