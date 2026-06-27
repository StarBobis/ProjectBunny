#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "DX12BindingTracker.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"


inline void DX12BindingGetSummaryDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	if (!dir || !path || pathCount == 0)
		return;
	swprintf_s(path, pathCount, L"%s\\summary", dir);
	CreateDirectoryW(path, nullptr);
}
constexpr UINT MaxRootParameters = 64;
constexpr UINT MaxVertexBufferSlots = 32;
constexpr UINT MaxTrackedEvents = 20000;
constexpr UINT MaxDescriptorsPerRangeDump = 256;

inline UINT DX12BindingDescriptorCountForRange(
	const DX12DescriptorHeapSummary &heap,
	UINT64 tableBaseIndex,
	const DX12RootDescriptorRangeSummary &range)
{
	const UINT64 firstDescriptor = tableBaseIndex + range.effectiveOffset;
	if (firstDescriptor >= heap.numDescriptors)
		return 0;

	const UINT64 heapAvailable = heap.numDescriptors - firstDescriptor;
	const UINT64 declaredCount =
		range.numDescriptors == UINT_MAX ? heapAvailable : range.numDescriptors;
	const UINT64 boundedCount = min(declaredCount, static_cast<UINT64>(MaxDescriptorsPerRangeDump));
	return boundedCount > UINT_MAX ? UINT_MAX : static_cast<UINT>(boundedCount);
}

struct RootTableState
{
	bool valid = false;
	UINT rootParameterIndex = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor = {};
};

struct RootDescriptorState
{
	bool valid = false;
	UINT rootParameterIndex = 0;
	D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_CBV;
	D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};

struct RootConstantsState
{
	bool valid = false;
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};

struct CommandListBindingState
{
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	UINT64 computeBindingSerial = 0;
	RootTableState graphicsTables[MaxRootParameters] = {};
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[MaxVertexBufferSlots] = {};
	bool vertexBufferValid[MaxVertexBufferSlots] = {};
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferValid = false;
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	UINT64 drawSerial = 0;
	UINT64 dispatchSerial = 0;
};

struct BindingEvent
{
	UINT64 serial = 0;
	std::string kind;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	DX12PsoShaderInfo shaderInfo = {};
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	RootTableState graphicsTables[MaxRootParameters] = {};
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[MaxVertexBufferSlots] = {};
	bool vertexBufferValid[MaxVertexBufferSlots] = {};
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferValid = false;
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	UINT vertexCountPerInstance = 0;
	UINT indexCountPerInstance = 0;
	UINT instanceCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;
	UINT startInstanceLocation = 0;
	UINT threadGroupCountX = 0;
	UINT threadGroupCountY = 0;
	UINT threadGroupCountZ = 0;
};

constexpr size_t kBindingShardCount = 64;

struct BindingShard
{
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> states;
};

extern BindingShard gBindingShards[kBindingShardCount];
extern SRWLOCK gEventsLock;
extern std::vector<BindingEvent> gEvents;
extern UINT64 gEventSerial;
extern volatile LONG64 gGlobalDrawSerial;
extern volatile LONG64 gGlobalDispatchSerial;
extern UINT64 gDroppedEvents;

// Per-thread read cache for binding tracker: avoids SRWLOCK on repeated identical calls.
// Key insight: D3D12 command lists are single-threaded, so TLS cache is safe.
struct BindingTlsTableCache {
	ID3D12GraphicsCommandList *cl = nullptr;
	UINT64 gpuHandle[MaxRootParameters] = {};
};
struct BindingTlsDescriptorCache {
	ID3D12GraphicsCommandList *cl = nullptr;
	UINT64 address[MaxRootParameters] = {};
};

inline BindingShard &BindingShardFor(ID3D12GraphicsCommandList *commandList)
{
	uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
	key ^= key >> 7;
	key *= 0x9e3779b97f4a7c15ull;
	return gBindingShards[(key >> 17) & (kBindingShardCount - 1)];
}

inline const char *DX12BindingHeapTypeName(UINT type)
{
	switch (type) {
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		return "CBV_SRV_UAV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
		return "SAMPLER";
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
		return "RTV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
		return "DSV";
	default:
		return "UNKNOWN";
	}
}

inline const char *DX12BindingResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
{
	switch (dimension) {
	case D3D12_RESOURCE_DIMENSION_BUFFER:
		return "BUFFER";
	case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
		return "TEXTURE1D";
	case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
		return "TEXTURE2D";
	case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
		return "TEXTURE3D";
	default:
		return "UNKNOWN";
	}
}

inline const DX12RootParameterSummary *DX12BindingFindRootParameter(
	const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex)
{
	for (const DX12RootParameterSummary &parameter : rootSignature.parameters) {
		if (parameter.rootParameterIndex == rootParameterIndex)
			return &parameter;
	}
	return nullptr;
}

inline bool DX12BindingGetRootSignatureForEvent(
	const BindingEvent &event, bool compute, DX12RootSignatureSummary *summary)
{
	if (!summary)
		return false;
	ID3D12RootSignature *rootSignature = compute ? event.computeRootSignature : event.graphicsRootSignature;
	if (!rootSignature && event.shaderInfo.psoIndex)
		DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &rootSignature);
	return rootSignature && DX12GetRootSignatureSummary(rootSignature, summary);
}

inline UINT DX12BindingHeapTypeForDescriptorRange(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
}

inline bool DX12BindingTableMatchesHeap(const RootTableState &table, const DX12DescriptorHeapSummary &heap)
{
	if (!table.valid || heap.increment == 0 || heap.gpuStart == 0)
		return false;
	const UINT64 begin = heap.gpuStart;
	const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
	return table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end;
}

inline const DX12DescriptorHeapSummary *DX12BindingFindHeapForGpuHandle(
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const RootTableState &table,
	UINT requiredType)
{
	for (const DX12DescriptorHeapSummary &heap : heaps) {
		if (heap.type != requiredType || heap.increment == 0 || heap.gpuStart == 0)
			continue;
		const UINT64 begin = heap.gpuStart;
		const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
		if (table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end)
			return &heap;
	}
	return nullptr;
}

inline const DX12DescriptorHeapSummary *DX12BindingFindHeapForTableRange(
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const RootTableState &table,
	const DX12RootDescriptorRangeSummary *range,
	UINT heapType)
{
	const UINT requiredType = range ? DX12BindingHeapTypeForDescriptorRange(range->rangeType) : heapType;
	for (const DX12DescriptorHeapSummary &heap : heaps) {
		if (heap.type != requiredType || !DX12BindingTableMatchesHeap(table, heap))
			continue;
		return &heap;
	}
	return nullptr;
}

inline const DX12DescriptorSummary *DX12BindingFindDescriptorByCpuHandle(
	const std::unordered_map<SIZE_T, const DX12DescriptorSummary*> &descriptorsByCpuHandle,
	SIZE_T cpuHandle)
{
	auto it = descriptorsByCpuHandle.find(cpuHandle);
	return it != descriptorsByCpuHandle.end() ? it->second : nullptr;
}

inline void DX12BindingBuildDescriptorLookup(
	const std::vector<DX12DescriptorSummary> &descriptors,
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> *descriptorsByCpuHandle)
{
	if (!descriptorsByCpuHandle)
		return;

	descriptorsByCpuHandle->clear();
	descriptorsByCpuHandle->reserve(descriptors.size());
	for (const DX12DescriptorSummary &descriptor : descriptors)
		(*descriptorsByCpuHandle)[descriptor.cpuHandle] = &descriptor;
}

inline bool DX12BindingIsDrawEvent(const BindingEvent &event)
{
	return event.kind == "draw" || event.kind == "draw_indexed";
}

inline bool DX12BindingIsDispatchEvent(const BindingEvent &event)
{
	return event.kind == "dispatch";
}
