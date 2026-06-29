#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>
#include <vector>

#include "DX12BindingTrackerState.hpp"
#include "DX12ResourceTracker.hpp"

class DX12BindingTrackerUtils
{
public:
	static UINT DescriptorCountForRange(
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
	static void GetSummaryDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
	{
		if (!dir || !path || pathCount == 0)
			return;
		swprintf_s(path, pathCount, L"%s\\summary", dir);
		CreateDirectoryW(path, nullptr);
	}

	static const char *HeapTypeName(UINT type)
	{
		switch (type)
		{
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

	static const char *ResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
	{
		switch (dimension)
		{
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

	static const DX12RootParameterSummary *FindRootParameter(
		const DX12RootSignatureSummary &rootSignature, UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &parameter : rootSignature.parameters)
		{
			if (parameter.rootParameterIndex == rootParameterIndex)
				return &parameter;
		}
		return nullptr;
	}

	static bool GetRootSignatureForEvent(
		const BindingEvent &event, bool compute, DX12RootSignatureSummary *summary)
	{
		if (!summary)
			return false;
		ID3D12RootSignature *rootSignature = compute ? event.computeRootSignature : event.graphicsRootSignature;
		if (!rootSignature && event.shaderInfo.psoIndex)
			DX12GetPsoRootSignature(event.shaderInfo.psoIndex, &rootSignature);
		return rootSignature && DX12GetRootSignatureSummary(rootSignature, summary);
	}

	static UINT HeapTypeForDescriptorRange(UINT rangeType)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	}

	static bool TableMatchesHeap(const RootTableState &table, const DX12DescriptorHeapSummary &heap)
	{
		if (!table.valid || heap.increment == 0 || heap.gpuStart == 0)
			return false;
		const UINT64 begin = heap.gpuStart;
		const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
		return table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end;
	}

	static const DX12DescriptorHeapSummary *FindHeapForGpuHandle(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		UINT requiredType)
	{
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || heap.increment == 0 || heap.gpuStart == 0)
				continue;
			const UINT64 begin = heap.gpuStart;
			const UINT64 end = begin + static_cast<UINT64>(heap.numDescriptors) * heap.increment;
			if (table.baseDescriptor.ptr >= begin && table.baseDescriptor.ptr < end)
				return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorHeapSummary *FindHeapForTableRange(
		const std::vector<DX12DescriptorHeapSummary> &heaps,
		const RootTableState &table,
		const DX12RootDescriptorRangeSummary *range,
		UINT heapType)
	{
		const UINT requiredType = range ? HeapTypeForDescriptorRange(range->rangeType) : heapType;
		for (const DX12DescriptorHeapSummary &heap : heaps)
		{
			if (heap.type != requiredType || !TableMatchesHeap(table, heap))
				continue;
			return &heap;
		}
		return nullptr;
	}

	static const DX12DescriptorSummary *FindDescriptorByCpuHandle(
		const std::unordered_map<SIZE_T, const DX12DescriptorSummary *> &descriptorsByCpuHandle,
		SIZE_T cpuHandle)
	{
		auto it = descriptorsByCpuHandle.find(cpuHandle);
		return it != descriptorsByCpuHandle.end() ? it->second : nullptr;
	}

	static void BuildDescriptorLookup(
		const std::vector<DX12DescriptorSummary> &descriptors,
		std::unordered_map<SIZE_T, const DX12DescriptorSummary *> *descriptorsByCpuHandle)
	{
		if (!descriptorsByCpuHandle)
			return;

		descriptorsByCpuHandle->clear();
		descriptorsByCpuHandle->reserve(descriptors.size());
		for (const DX12DescriptorSummary &descriptor : descriptors)
			(*descriptorsByCpuHandle)[descriptor.cpuHandle] = &descriptor;
	}

	static bool IsDrawEvent(const BindingEvent &event)
	{
		return event.kind == "draw" || event.kind == "draw_indexed";
	}

	static bool IsDispatchEvent(const BindingEvent &event)
	{
		return event.kind == "dispatch";
	}
};
