#include "DX12BindingTrackerPrivate.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12Json.h"
#include "DX12State.h"

static bool InsertBindingSeen(
	std::unordered_set<std::string> *seen, const BindingEvent &event,
	const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex,
	ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor)
{
	if (!seen)
		return false;

	char key[320];
	sprintf_s(key, "%llu|%s|%u|%u|%p|%llu|%u",
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		heap,
		static_cast<unsigned long long>(descriptorIndex),
		rootDescriptor ? 1 : 0);
	return seen->insert(key).second;
}

static void WriteDescriptorResourceRow(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	UINT rootParameterIndex, UINT rangeIndex, UINT rangeType,
	UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor);

struct DescriptorSlotKey
{
	ID3D12DescriptorHeap *heap = nullptr;
	UINT64 descriptorIndex = 0;

	bool operator==(const DescriptorSlotKey &other) const
	{
		return heap == other.heap && descriptorIndex == other.descriptorIndex;
	}
};

struct DescriptorSlotKeyHash
{
	size_t operator()(const DescriptorSlotKey &key) const
	{
		uintptr_t heap = reinterpret_cast<uintptr_t>(key.heap);
		uint64_t mixed = static_cast<uint64_t>(heap >> 4) ^ (key.descriptorIndex * 0x9e3779b97f4a7c15ull);
		mixed ^= mixed >> 33;
		mixed *= 0xff51afd7ed558ccdull;
		mixed ^= mixed >> 33;
		return static_cast<size_t>(mixed);
	}
};

struct DescriptorLookup
{
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> byCpuHandle;
	std::unordered_map<DescriptorSlotKey, const DX12DescriptorSummary*, DescriptorSlotKeyHash> bySlot;
};

static bool DescriptorBelongsToHeap(
	const DX12DescriptorSummary &descriptor, const DX12DescriptorHeapSummary &heap,
	UINT64 *descriptorIndex)
{
	if (!descriptor.cpuHandle || !heap.cpuStart || !heap.increment || !descriptorIndex)
		return false;
	if (descriptor.cpuHandle < heap.cpuStart)
		return false;
	const SIZE_T offset = descriptor.cpuHandle - heap.cpuStart;
	if (offset % heap.increment != 0)
		return false;
	const UINT64 index = offset / heap.increment;
	if (index >= heap.numDescriptors)
		return false;
	*descriptorIndex = index;
	return true;
}

static void BuildDescriptorLookup(
	const std::vector<DX12DescriptorSummary> &descriptors,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	DescriptorLookup *lookup)
{
	if (!lookup)
		return;
	lookup->byCpuHandle.clear();
	lookup->byCpuHandle.reserve(descriptors.size());
	lookup->bySlot.clear();
	lookup->bySlot.reserve(descriptors.size());

	for (const DX12DescriptorSummary &descriptor : descriptors) {
		lookup->byCpuHandle[descriptor.cpuHandle] = &descriptor;
		for (const DX12DescriptorHeapSummary &heap : heaps) {
			UINT64 descriptorIndex = 0;
			if (!DescriptorBelongsToHeap(descriptor, heap, &descriptorIndex))
				continue;
			DescriptorSlotKey key;
			key.heap = heap.heap;
			key.descriptorIndex = descriptorIndex;
			lookup->bySlot[key] = &descriptor;
			break;
		}
	}
}

static const DX12DescriptorSummary *FindDescriptorForSlot(
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle)
{
	auto byCpu = lookup.byCpuHandle.find(cpuHandle);
	if (byCpu != lookup.byCpuHandle.end())
		return byCpu->second;
	if (!heap)
		return nullptr;
	DescriptorSlotKey key;
	key.heap = heap->heap;
	key.descriptorIndex = descriptorIndex;
	auto bySlot = lookup.bySlot.find(key);
	return bySlot != lookup.bySlot.end() ? bySlot->second : nullptr;
}

static UINT UnknownTableRangeType(UINT heapType)
{
	return heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : UINT_MAX;
}

static bool MakeTableBinding(
	DX12FrameResourceBinding *binding, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor)
{
	if (!binding || !heap)
		return false;

	binding->eventSerial = event.serial;
	binding->drawId = event.drawId;
	binding->dispatchId = event.dispatchId;
	binding->psoIndex = event.shaderInfo.psoIndex;
	binding->shaderInfo = event.shaderInfo;
	binding->bindSpace = bindSpace ? bindSpace : "";
	binding->rootParameterIndex = table.rootParameterIndex;
	binding->rangeIndex = rangeIndex;
	binding->rangeType = rangeType;
	binding->shaderVisibility = shaderVisibility;
	binding->shaderRegister = shaderRegister;
	binding->registerSpace = registerSpace;
	binding->descriptorOffset = descriptorOffset;
	binding->heap = heap->heap;
	binding->heapType = heap->type;
	binding->descriptorIndex = descriptorIndex;
	binding->gpuHandle = gpuHandle;
	binding->cpuHandle = cpuHandle;
	binding->heapGpuStart = heap->gpuStart;
	if (descriptor) {
		binding->descriptor = *descriptor;
		binding->hasDescriptor = true;
	}
	return true;
}

static bool WriteDescriptorTableEntry(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!file || !heap)
		return false;
	if (!InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
			rangeIndex, heap->heap, descriptorIndex, false))
		return false;
	if (!descriptor && untrackedSkipped)
		(*untrackedSkipped)++;
	WriteDescriptorResourceRow(file, event, bindSpace, table.rootParameterIndex,
		rangeIndex, rangeType, shaderRegister, registerSpace, descriptorOffset,
		heap, descriptorIndex, gpuHandle, cpuHandle, descriptor);
	return true;
}

static bool CollectDescriptorTableEntry(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT rangeIndex, UINT rangeType,
	UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!bindings || !heap)
		return false;
	if (!InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
			rangeIndex, heap->heap, descriptorIndex, false))
		return false;
	if (!descriptor && untrackedSkipped)
		(*untrackedSkipped)++;

	DX12FrameResourceBinding binding;
	if (!MakeTableBinding(&binding, event, bindSpace, table, rangeIndex, rangeType,
			shaderVisibility, shaderRegister, registerSpace, descriptorOffset,
			heap, descriptorIndex, gpuHandle, cpuHandle, descriptor))
		return false;
	bindings->push_back(std::move(binding));
	return true;
}

static bool VisitTableStartOnly(
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle,
	const DX12DescriptorHeapSummary **heap)
{
	if (!descriptorIndex || !gpuHandle || !cpuHandle || !heap)
		return false;
	*heap = DX12BindingFindHeapForGpuHandle(heaps, table, heapType);
	if (!*heap || (*heap)->increment == 0)
		return false;
	*descriptorIndex = (table.baseDescriptor.ptr - (*heap)->gpuStart) / (*heap)->increment;
	if (*descriptorIndex >= (*heap)->numDescriptors)
		return false;
	*gpuHandle = (*heap)->gpuStart + *descriptorIndex * (*heap)->increment;
	*cpuHandle = (*heap)->cpuStart + static_cast<SIZE_T>(*descriptorIndex) * (*heap)->increment;
	return true;
}

static void WriteDescriptorResourceRow(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	UINT rootParameterIndex, UINT rangeIndex, UINT rangeType,
	UINT shaderRegister, UINT registerSpace, UINT descriptorOffset,
	const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex,
	UINT64 gpuHandle, SIZE_T cpuHandle,
	const DX12DescriptorSummary *descriptor)
{
	if (!file || !heap)
		return;

	fprintf(file, "%llu,%llu,%s,%u,%u,%u,%u,%u,%u,%p,%s,%llu,0x%llx,0x%llx,0x%llx,",
		static_cast<unsigned long long>(event.serial),
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		rangeType,
		shaderRegister,
		registerSpace,
		descriptorOffset,
		heap->heap,
		DX12BindingHeapTypeName(heap->type),
		static_cast<unsigned long long>(descriptorIndex),
		static_cast<unsigned long long>(gpuHandle),
		static_cast<unsigned long long>(cpuHandle),
		static_cast<unsigned long long>(heap->gpuStart));

	if (!descriptor) {
		fprintf(file, "untracked,0,0,0,UNKNOWN,0,0,0,0,0,0,0,0\n");
		return;
	}

	fprintf(file, "%s,%p,%p,%u,",
		descriptor->kind.c_str(),
		descriptor->resource,
		descriptor->counterResource,
		descriptor->hasDesc ? 1 : 0);

	if (descriptor->hasResourceDesc) {
		const D3D12_RESOURCE_DESC &desc = descriptor->resourceDesc;
		fprintf(file, "%s,%llu,%u,%u,%u,%u,0x%llx,0x%llx,%u\n",
			DX12BindingResourceDimensionName(desc.Dimension),
			static_cast<unsigned long long>(desc.Width),
			desc.Height,
			desc.DepthOrArraySize,
			desc.MipLevels,
			static_cast<UINT>(desc.Format),
			static_cast<unsigned long long>(desc.Flags),
			static_cast<unsigned long long>(descriptor->gpuVirtualAddress),
			descriptor->viewDimension);
	} else {
		fprintf(file, "NONE,0,0,0,0,%u,0x0,0x%llx,%u\n",
			descriptor->viewFormat,
			static_cast<unsigned long long>(descriptor->gpuVirtualAddress),
			descriptor->viewDimension);
	}
}

static void WriteRootDescriptorResourceRow(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	const DX12RootParameterSummary *parameter,
	const DX12BufferResourceSummary *resource)
{
	if (!file)
		return;

	const UINT shaderRegister = parameter ? parameter->shaderRegister : UINT_MAX;
	const UINT registerSpace = parameter ? parameter->registerSpace : 0;
	fprintf(file, "%llu,%llu,%s,%u,%u,%u,%u,%u,%u,%p,%s,%llu,0x%llx,0x%llx,0x%llx,",
		static_cast<unsigned long long>(event.serial),
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootDescriptor.rootParameterIndex,
		UINT_MAX,
		static_cast<UINT>(rootDescriptor.type),
		shaderRegister,
		registerSpace,
		0,
		static_cast<void*>(nullptr),
		"ROOT_DESCRIPTOR",
		0ull,
		static_cast<unsigned long long>(rootDescriptor.address),
		0ull,
		0ull);

	if (!resource || !resource->resource) {
		fprintf(file, "root_%u,0,0,0,NONE,0,0,0,0,0,0,0x%llx,0\n",
			static_cast<UINT>(rootDescriptor.type),
			static_cast<unsigned long long>(rootDescriptor.address));
		return;
	}

	const D3D12_RESOURCE_DESC &desc = resource->resourceDesc;
	fprintf(file, "root_%u,%p,0,0,%s,%llu,%u,%u,%u,%u,0x%llx,0x%llx,0\n",
		static_cast<UINT>(rootDescriptor.type),
		resource->resource,
		DX12BindingResourceDimensionName(desc.Dimension),
		static_cast<unsigned long long>(desc.Width),
		desc.Height,
		desc.DepthOrArraySize,
		desc.MipLevels,
		static_cast<UINT>(desc.Format),
		static_cast<unsigned long long>(desc.Flags),
		static_cast<unsigned long long>(resource->gpuVirtualAddress));
}


static bool IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace)
{
	if (!bindSpace)
		return true;
	const bool samplerSpace = strstr(bindSpace, "sampler") != nullptr;
	return samplerSpace == (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
}

static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}


static void WriteFrameResourceBinding(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptors,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!file || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingGetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingFindRootParameter(rootSignature, table.rootParameterIndex);

	bool wroteRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			const DX12DescriptorHeapSummary *heap = DX12BindingFindHeapForTableRange(heaps, table, &range, heapType);
			if (!heap)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const UINT count = DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap->numDescriptors)
					break;
				const UINT64 gpuHandle = heap->gpuStart + descriptorIndex * heap->increment;
				const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
				const DX12DescriptorSummary *descriptor =
					FindDescriptorForSlot(descriptors, heap, descriptorIndex, cpuHandle);
				if (WriteDescriptorTableEntry(file, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.baseShaderRegister + offset,
						range.registerSpace, range.effectiveOffset + offset,
						heap, descriptorIndex, gpuHandle, cpuHandle, descriptor,
						seen, untrackedSkipped))
					wroteRange = true;
			}
		}
	}

	if (wroteRange)
		return;

	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	const DX12DescriptorHeapSummary *heap = nullptr;
	if (VisitTableStartOnly(table, heapType, heaps, &descriptorIndex,
			&gpuHandle, &cpuHandle, &heap)) {
		const DX12DescriptorSummary *descriptor =
			FindDescriptorForSlot(descriptors, heap, descriptorIndex, cpuHandle);
		// Without a root signature D3D12 only gives the table base handle, not a range length.
		// Record that one provable descriptor instead of guessing through the heap.
		WriteDescriptorTableEntry(file, event, bindSpace, table, UINT_MAX,
			UnknownTableRangeType(heapType), UINT_MAX, 0, 0, heap, descriptorIndex,
			gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
	}
}

static void WriteFrameRootDescriptor(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	std::unordered_set<std::string> *seen)
{
	if (!file || !rootDescriptor.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingGetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingFindRootParameter(rootSignature, rootDescriptor.rootParameterIndex);

	if (!InsertBindingSeen(seen, event, bindSpace, rootDescriptor.rootParameterIndex,
			UINT_MAX, nullptr, rootDescriptor.address, true))
		return;

	DX12BufferResourceSummary resource;
	const bool resolved = DX12ResolveBufferResourceByGpuVa(rootDescriptor.address, 1, &resource);
	WriteRootDescriptorResourceRow(file, event, bindSpace, rootDescriptor,
		parameter, resolved ? &resource : nullptr);
}

static void CollectFrameResourceBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptors,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!bindings || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingGetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingFindRootParameter(rootSignature, table.rootParameterIndex);

	bool collectedRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			const DX12DescriptorHeapSummary *heap = DX12BindingFindHeapForTableRange(heaps, table, &range, heapType);
			if (!heap)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const UINT count = DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap->numDescriptors)
					break;
				const UINT64 gpuHandle = heap->gpuStart + descriptorIndex * heap->increment;
				const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
				const DX12DescriptorSummary *descriptor =
					FindDescriptorForSlot(descriptors, heap, descriptorIndex, cpuHandle);
				if (CollectDescriptorTableEntry(bindings, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.baseShaderRegister + offset,
						range.shaderVisibility, range.registerSpace, range.effectiveOffset + offset,
						heap, descriptorIndex, gpuHandle, cpuHandle, descriptor,
						seen, untrackedSkipped))
					collectedRange = true;
			}
		}
	}

	if (collectedRange)
		return;

	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	const DX12DescriptorHeapSummary *heap = nullptr;
	if (VisitTableStartOnly(table, heapType, heaps, &descriptorIndex,
			&gpuHandle, &cpuHandle, &heap)) {
		const DX12DescriptorSummary *descriptor =
			FindDescriptorForSlot(descriptors, heap, descriptorIndex, cpuHandle);
		// Same design as file export: keep the table base binding when root metadata is absent,
		// but do not fabricate descriptor ranges that D3D12 did not expose.
		CollectDescriptorTableEntry(bindings, event, bindSpace, table, UINT_MAX,
			UnknownTableRangeType(heapType), D3D12_SHADER_VISIBILITY_ALL, UINT_MAX, 0, 0,
			heap, descriptorIndex, gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
	}
}

static void CollectFrameRootDescriptorBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	std::unordered_set<std::string> *seen)
{
	if (!bindings || !rootDescriptor.valid || !seen)
		return;
	if (!InsertBindingSeen(seen, event, bindSpace, rootDescriptor.rootParameterIndex,
			UINT_MAX, nullptr, rootDescriptor.address, true))
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingGetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingFindRootParameter(rootSignature, rootDescriptor.rootParameterIndex);

	DX12FrameResourceBinding binding;
	binding.eventSerial = event.serial;
	binding.drawId = event.drawId;
	binding.dispatchId = event.dispatchId;
	binding.psoIndex = event.shaderInfo.psoIndex;
	binding.shaderInfo = event.shaderInfo;
	binding.bindSpace = bindSpace ? bindSpace : "";
	binding.rootParameterIndex = rootDescriptor.rootParameterIndex;
	binding.rangeIndex = UINT_MAX;
	binding.rangeType = static_cast<UINT>(rootDescriptor.type);
	binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
	binding.shaderRegister = parameter ? parameter->shaderRegister : UINT_MAX;
	binding.registerSpace = parameter ? parameter->registerSpace : 0;
	binding.rootDescriptor = true;
	binding.gpuVirtualAddress = rootDescriptor.address;
	bindings->push_back(std::move(binding));
}

static void CountEventTables(
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	size_t *graphicsTables, size_t *computeTables,
	size_t *graphicsTablesWithRoots, size_t *computeTablesWithRoots,
	size_t *rootRangeTables, size_t *tableStartOnly,
	size_t *unboundedRanges, size_t *unboundedDescriptors,
	size_t *graphicsTablesWithoutRoots, size_t *computeTablesWithoutRoots,
	size_t *descriptorTablesWithoutRanges)
{
	if (!graphicsTables || !computeTables || !graphicsTablesWithRoots ||
			!computeTablesWithRoots || !rootRangeTables || !tableStartOnly ||
			!unboundedRanges || !unboundedDescriptors ||
			!graphicsTablesWithoutRoots || !computeTablesWithoutRoots ||
			!descriptorTablesWithoutRanges)
		return;

	DX12RootSignatureSummary graphicsRoot;
	DX12RootSignatureSummary computeRoot;
	const bool hasGraphicsRoot = DX12BindingGetRootSignatureForEvent(event, false, &graphicsRoot);
	const bool hasComputeRoot = DX12BindingGetRootSignatureForEvent(event, true, &computeRoot);
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (event.graphicsTables[i].valid) {
			(*graphicsTables)++;
			if (hasGraphicsRoot) {
				(*graphicsTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingFindRootParameter(graphicsRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingFindHeapForGpuHandle(heaps, event.graphicsTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.graphicsTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*graphicsTablesWithoutRoots)++;
			}
		}
		if (event.computeTables[i].valid) {
			(*computeTables)++;
			if (hasComputeRoot) {
				(*computeTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingFindRootParameter(computeRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingFindHeapForGpuHandle(heaps, event.computeTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.computeTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
						}
					}
				} else {
					(*tableStartOnly)++;
					(*descriptorTablesWithoutRanges)++;
				}
			} else {
				(*tableStartOnly)++;
				(*computeTablesWithoutRoots)++;
			}
		}
	}
}

static void CollectEventResourceBindings(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptorLookup,
	std::unordered_set<std::string> *seen,
	size_t *untrackedSkipped)
{
	if (!bindings || !seen)
		return;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		CollectFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped);
		CollectFrameResourceBinding(bindings, event, "graphics_sampler",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped);
		CollectFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped);
		CollectFrameResourceBinding(bindings, event, "compute_sampler",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped);
		CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
			event.graphicsRootDescriptors[i], seen);
		CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
			event.computeRootDescriptors[i], seen);
	}
}

static const DX12DescriptorSummary *FindLiveDescriptorForSlot(
	DX12DescriptorSummary *storage, SIZE_T cpuHandle)
{
	if (!storage)
		return nullptr;
	return DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, storage) ? storage : nullptr;
}

static void CollectLiveFrameResourceBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped)
{
	if (!bindings || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingGetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingFindRootParameter(rootSignature, table.rootParameterIndex);

	bool collectedRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			DX12DescriptorHeapSummary heap = {};
			if (!DX12FindDescriptorHeapByGpuHandle(
					table.baseDescriptor,
					static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(
						DX12BindingHeapTypeForDescriptorRange(range.rangeType)),
					&heap) || heap.increment == 0)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
			const UINT count = DX12BindingDescriptorCountForRange(heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap.numDescriptors)
					break;
				const UINT64 gpuHandle = heap.gpuStart + descriptorIndex * heap.increment;
				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
				DX12DescriptorSummary descriptorStorage;
				const DX12DescriptorSummary *descriptor =
					FindLiveDescriptorForSlot(&descriptorStorage, cpuHandle);
				if (CollectDescriptorTableEntry(bindings, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.shaderVisibility,
						range.baseShaderRegister + offset, range.registerSpace,
						range.effectiveOffset + offset, &heap, descriptorIndex,
						gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped))
					collectedRange = true;
			}
		}
	}

	if (collectedRange)
		return;

	DX12DescriptorHeapSummary heap = {};
	if (!DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(heapType), &heap) ||
			heap.increment == 0)
		return;
	const UINT64 descriptorIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
	if (descriptorIndex >= heap.numDescriptors)
		return;
	const UINT64 gpuHandle = heap.gpuStart + descriptorIndex * heap.increment;
	const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
	DX12DescriptorSummary descriptorStorage;
	const DX12DescriptorSummary *descriptor =
		FindLiveDescriptorForSlot(&descriptorStorage, cpuHandle);
	// Record only the table base when root metadata is unavailable; D3D12 does not expose
	// a descriptor-table length from the GPU handle alone.
	CollectDescriptorTableEntry(bindings, event, bindSpace, table, UINT_MAX,
		UnknownTableRangeType(heapType), D3D12_SHADER_VISIBILITY_ALL, UINT_MAX, 0, 0,
		&heap, descriptorIndex, gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
}

static void CollectLiveEventResourceBindings(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event,
	std::unordered_set<std::string> *seen,
	size_t *untrackedSkipped)
{
	if (!bindings || !seen)
		return;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		CollectLiveFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			seen, untrackedSkipped);
		CollectLiveFrameResourceBinding(bindings, event, "graphics_sampler",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			seen, untrackedSkipped);
		CollectLiveFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			seen, untrackedSkipped);
		CollectLiveFrameResourceBinding(bindings, event, "compute_sampler",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			seen, untrackedSkipped);
		CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
			event.graphicsRootDescriptors[i], seen);
		CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
			event.computeRootDescriptors[i], seen);
	}
}

void DX12BindingCaptureEventResourceBindings(
	const BindingEvent &event, std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	bindings->clear();
	std::unordered_set<std::string> seen;
	size_t untrackedSkipped = 0;
	CollectLiveEventResourceBindings(bindings, event, &seen, &untrackedSkipped);
}

void DX12GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	std::vector<BindingEvent> events;
	AcquireSRWLockShared(&gEventsLock);
	events = gEvents;
	ReleaseSRWLockShared(&gEventsLock);
	DX12FrameAnalysisLogJsonFunc("BindingResourcesSnapshot",
		"\"events\":%zu", events.size());

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	DX12FrameAnalysisLogJsonFunc("BindingResourcesMetadataReady",
		"\"descriptors\":%zu,\"heaps\":%zu",
		descriptors.size(), heaps.size());
	DescriptorLookup descriptorLookup;
	BuildDescriptorLookup(descriptors, heaps, &descriptorLookup);
	DX12FrameAnalysisLogJsonFunc("BindingResourcesLookupReady",
		"\"cpu_entries\":%zu,\"slot_entries\":%zu",
		descriptorLookup.byCpuHandle.size(), descriptorLookup.bySlot.size());

	bindings->clear();
	std::unordered_set<std::string> seen;
	size_t drawEvents = 0;
	size_t dispatchEvents = 0;
	size_t graphicsTables = 0;
	size_t computeTables = 0;
	size_t graphicsTablesWithRoots = 0;
	size_t computeTablesWithRoots = 0;
	size_t rootRangeTables = 0;
	size_t tableStartOnly = 0;
	size_t unboundedRanges = 0;
	size_t unboundedDescriptors = 0;
	size_t graphicsTablesWithoutRoots = 0;
	size_t computeTablesWithoutRoots = 0;
	size_t descriptorTablesWithoutRanges = 0;
	size_t samplerBindings = 0;
	size_t cbvBindings = 0;
	size_t srvBindings = 0;
	size_t uavBindings = 0;
	size_t untrackedBindings = 0;
	size_t untrackedSkipped = 0;
	size_t textureBindings = 0;
	size_t snapshotBindings = 0;
	size_t liveResolvedEvents = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingIsDrawEvent(event) && !DX12BindingIsDispatchEvent(event))
			continue;
		if (DX12BindingIsDrawEvent(event))
			drawEvents++;
		if (DX12BindingIsDispatchEvent(event))
			dispatchEvents++;
		CountEventTables(event, heaps, &graphicsTables, &computeTables,
			&graphicsTablesWithRoots, &computeTablesWithRoots,
			&rootRangeTables, &tableStartOnly, &unboundedRanges,
			&unboundedDescriptors, &graphicsTablesWithoutRoots,
			&computeTablesWithoutRoots, &descriptorTablesWithoutRanges);
		if (!event.resourceBindings.empty()) {
			snapshotBindings += event.resourceBindings.size();
			for (const DX12FrameResourceBinding &binding : event.resourceBindings) {
				if (binding.hasDescriptor)
					liveResolvedEvents++;
				bindings->push_back(binding);
			}
		} else {
			CollectEventResourceBindings(
				bindings, event, heaps, descriptorLookup, &seen, &untrackedSkipped);
		}
	}
	for (const DX12FrameResourceBinding &binding : *bindings) {
		if (!binding.hasDescriptor) {
			untrackedBindings++;
			continue;
		}
		if (binding.descriptor.kind == "CBV")
			cbvBindings++;
		else if (binding.descriptor.kind == "SRV")
			srvBindings++;
		else if (binding.descriptor.kind == "UAV")
			uavBindings++;
		else if (binding.descriptor.kind == "Sampler")
			samplerBindings++;
		if (binding.descriptor.hasResourceDesc &&
			IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			textureBindings++;
	}
	DX12FrameAnalysisLogJsonFunc("BindingResourcesCollected",
		"\"bindings\":%zu,\"unique\":%zu,\"snapshot_bindings\":%zu,\"snapshot_resolved\":%zu,\"draw_events\":%zu,\"dispatch_events\":%zu,\"graphics_tables\":%zu,\"compute_tables\":%zu,\"graphics_tables_with_roots\":%zu,\"compute_tables_with_roots\":%zu,\"root_range_tables\":%zu,\"table_start_only\":%zu,\"graphics_tables_without_roots\":%zu,\"compute_tables_without_roots\":%zu,\"descriptor_tables_without_ranges\":%zu,\"unbounded_ranges\":%zu,\"unbounded_descriptors\":%zu,\"cbv\":%zu,\"srv\":%zu,\"uav\":%zu,\"sampler\":%zu,\"texture\":%zu,\"untracked\":%zu,\"untracked_skipped\":%zu",
		bindings->size(), seen.size(), snapshotBindings, liveResolvedEvents,
		drawEvents, dispatchEvents,
		graphicsTables, computeTables,
		graphicsTablesWithRoots, computeTablesWithRoots,
		rootRangeTables, tableStartOnly,
		graphicsTablesWithoutRoots, computeTablesWithoutRoots,
		descriptorTablesWithoutRanges,
		unboundedRanges, unboundedDescriptors,
		cbvBindings, srvBindings, uavBindings, samplerBindings,
		textureBindings, untrackedBindings, untrackedSkipped);
}

void DX12GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings)
{
	if (!bindings)
		return;

	std::vector<OutputBindingEvent> events;
	AcquireSRWLockShared(&gEventsLock);
	events = gOutputEvents;
	const UINT64 dropped = gDroppedOutputEvents;
	ReleaseSRWLockShared(&gEventsLock);

	std::unordered_set<std::string> seen;
	bindings->clear();
	bindings->reserve(events.size());
	size_t rtv = 0;
	size_t dsv = 0;
	size_t untracked = 0;
	for (const OutputBindingEvent &event : events) {
		if (!event.cpuHandle)
			continue;
		char key[160];
		sprintf_s(key, "%s|%u|%zx|%llu|%llu|%llu",
			event.kind.c_str(),
			event.slot,
			event.cpuHandle,
			static_cast<unsigned long long>(event.drawId),
			static_cast<unsigned long long>(event.dispatchId),
			static_cast<unsigned long long>(
				(event.drawId || event.dispatchId) ? 0 : event.serial));
		if (!seen.insert(key).second)
			continue;

		DX12FrameOutputBinding binding;
		binding.kind = event.kind;
		binding.eventSerial = event.serial;
		binding.drawId = event.drawId;
		binding.dispatchId = event.dispatchId;
		binding.psoIndex = event.shaderInfo.psoIndex;
		binding.shaderInfo = event.shaderInfo;
		binding.slot = event.slot;
		binding.cpuHandle = event.cpuHandle;
		binding.hasDescriptor = DX12FindDescriptorSummaryByCpuHandle(
			event.cpuHandle, &binding.descriptor);
		if (binding.kind == "RTV")
			rtv++;
		else if (binding.kind == "DSV")
			dsv++;
		if (!binding.hasDescriptor)
			untracked++;
		bindings->push_back(std::move(binding));
	}

	DX12FrameAnalysisLogJsonFunc("OutputResourcesCollected",
		"\"bindings\":%zu,\"rtv\":%zu,\"dsv\":%zu,\"untracked\":%zu,\"dropped\":%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
	DX12LogJsonFunc("DX12OutputResourcesCollected",
		"\"bindings\":%zu,\"rtv\":%zu,\"dsv\":%zu,\"untracked\":%zu,\"dropped\":%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
}

static DX12FrameAnalysisDumpResult WriteFrameResourceFile(
	const wchar_t *dir, const std::vector<BindingEvent> &events)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	DX12FrameAnalysisLogJsonFunc("FrameResourceFileMetadataReady",
		"\"events\":%zu,\"descriptors\":%zu,\"heaps\":%zu",
		events.size(),
		descriptors.size(), heaps.size());
	DescriptorLookup descriptorLookup;
	BuildDescriptorLookup(descriptors, heaps, &descriptorLookup);
	DX12FrameAnalysisLogJsonFunc("FrameResourceFileLookupReady",
		"\"cpu_entries\":%zu,\"slot_entries\":%zu",
		descriptorLookup.byCpuHandle.size(), descriptorLookup.bySlot.size());

	wchar_t path[MAX_PATH];
	wchar_t summaryDir[MAX_PATH];
	DX12BindingGetSummaryDirectory(dir, summaryDir, ARRAYSIZE(summaryDir));
	swprintf_s(path, L"%s\\CurrentFrameResourcesDX12.txt", summaryDir);
	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file) {
		result.reason = "open_failed";
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonAppendStringField(fields, sizeof(fields), "reason", result.reason);
		DX12FrameAnalysisLogJsonFunc("CurrentFrameResourcesFailed", "%s", fields + 1);
		return result;
	}
	{
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12FrameAnalysisLogJsonFunc("FrameResourceFileOpened", "%s", fields + 1);
	}

	fprintf(file, "DX12 Current Frame Resources\n");
	fprintf(file, "============================\n");
	fprintf(file, "events=%zu descriptor_heaps=%zu descriptors=%zu\n\n",
		events.size(), heaps.size(), descriptors.size());
	fprintf(file,
		"event,pso,bind_space,root_param,range_index,range_type,shader_register,space,descriptor_offset,"
		"heap,heap_type,descriptor_index,gpu_handle,cpu_handle,heap_gpu_start,descriptor_kind,resource,"
		"counter_resource,has_view_desc,resource_dimension,width,height,depth_or_array,mips,format,flags,gpu_va,view_dimension\n");

	std::unordered_set<std::string> seen;
	size_t eventRows = 0;
	size_t untrackedSkipped = 0;
	for (const BindingEvent &event : events) {
		for (UINT i = 0; i < MaxRootParameters; ++i) {
			WriteFrameResourceBinding(file, event, "graphics_cbv_srv_uav",
				event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				heaps, descriptorLookup, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "graphics_sampler",
				event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorLookup, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "compute_cbv_srv_uav",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				heaps, descriptorLookup, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "compute_sampler",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorLookup, &seen, &untrackedSkipped);
			WriteFrameRootDescriptor(file, event, "graphics_root",
				event.graphicsRootDescriptors[i], &seen);
			WriteFrameRootDescriptor(file, event, "compute_root",
				event.computeRootDescriptors[i], &seen);
		}
		eventRows++;
	}
	DX12FrameAnalysisLogJsonFunc("FrameResourceFileProcessed",
		"\"events\":%zu,\"bindings\":%zu,\"untracked_skipped\":%zu",
		eventRows, seen.size(), untrackedSkipped);

	fclose(file);
	{
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonAppendRawField(fields, sizeof(fields), "bindings", std::to_string(seen.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "untracked_skipped", std::to_string(untrackedSkipped).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "heaps", std::to_string(heaps.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "descriptors", std::to_string(descriptors.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("CurrentFrameResourcesWritten", "%s", fields + 1);
	}
	result.ok = true;
	result.written = seen.size();
	result.skipped = untrackedSkipped;
	return result;
}

DX12FrameAnalysisDumpResult DX12DumpCurrentFrameResourceTable(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<BindingEvent> events;
	AcquireSRWLockShared(&gEventsLock);
	events = gEvents;
	ReleaseSRWLockShared(&gEventsLock);
	return WriteFrameResourceFile(dir, events);
}
