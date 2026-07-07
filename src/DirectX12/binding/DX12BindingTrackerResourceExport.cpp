#include "DX12BindingTracker.hpp"
#include "DX12BindingTrackerQuery.hpp"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12CaptureLedger.hpp"
#include "DX12FrameAnalysis.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12RuntimeState.hpp"

static bool PopulatePreferredResourceEventSource(DX12FrameEventSource *source, const char *reason)
{
	if (!source)
		return false;

	if (DX12BindingTracker::PopulateActiveEventSource(
			source,
			reason ? reason : "PopulatePreferredResourceEventSource"))
		return true;

	*source = DX12FrameEventSource();
	return false;
}

bool DX12BindingResourceExport::InsertBindingSeen(
	std::unordered_set<std::string> *seen, const BindingEvent &event,
	const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex, UINT rangeType,
	ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor)
{
	if (!seen)
		return false;

	char key[320];
	sprintf_s(key, "%llu|%llu|%llu|%llu|%s|%u|%u|%u|%p|%llu|%u",
		static_cast<unsigned long long>(event.serial),
		static_cast<unsigned long long>(event.drawId),
		static_cast<unsigned long long>(event.dispatchId),
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		rangeType,
		heap,
		static_cast<unsigned long long>(descriptorIndex),
		rootDescriptor ? 1 : 0);
	return seen->insert(key).second;
}

bool DX12BindingResourceExport::DescriptorMatchesRangeType(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	if (!descriptor)
		return true;
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return descriptor->kind == "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return descriptor->kind == "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return descriptor->kind == "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return descriptor->kind == "Sampler";
	return true;
}

const DX12DescriptorSummary *DX12BindingResourceExport::DescriptorForExpectedRange(
	const DX12DescriptorSummary *descriptor, UINT rangeType)
{
	return DX12BindingResourceExport::DescriptorMatchesRangeType(descriptor, rangeType) ? descriptor : nullptr;
}

const char *DX12BindingResourceExport::DescriptorKindForRangeType(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}

const char *DX12BindingResourceExport::ExpectedBindingKind(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (binding.rootDescriptor) {
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return "CBV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return "SRV";
		if (binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return "UAV";
		return "";
	}
	if (binding.hasDescriptor)
		return binding.descriptor.kind.c_str();
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return "CBV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return "SRV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return "UAV";
	if (binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return "Sampler";
	return "";
}

bool DX12BindingResourceExport::IsBufferResourceSummary(const DX12ResourceSummary &resource)
{
	return resource.resource &&
		resource.hasResourceDesc &&
		resource.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		resource.size != 0;
}

void DX12BindingResourceExport::CollectHuntingUavStateResourceUsages(
	std::vector<DX12FrameResourceBinding> *bindings,
	const std::vector<BindingEvent> &events)
{
	if (!bindings || !DX12RuntimeState::IsHuntingMode())
		return;

	std::vector<DX12ResourceSummary> resources;
	DX12GetTrackedResourceSnapshot(&resources);
	if (resources.empty())
		return;

	std::vector<const BindingEvent*> dispatches;
	dispatches.reserve(events.size());
	for (const BindingEvent &event : events) {
		if (DX12BindingTrackerQuery::IsDispatchEvent(event))
			dispatches.push_back(&event);
	}
	if (dispatches.empty())
		return;

	std::unordered_set<std::string> seen;
	size_t candidates = 0;
	size_t emitted = 0;
	for (const DX12ResourceSummary &resource : resources) {
		if (!DX12BindingResourceExport::IsBufferResourceSummary(resource) ||
				!resource.hasCurrentState ||
				!(resource.currentState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
			continue;
		candidates++;

		const BindingEvent *producerEvent = dispatches.back();
		char key[96];
		sprintf_s(key, "%p|%llu|%llu",
			resource.resource,
			static_cast<unsigned long long>(resource.gpuVirtualAddress),
			static_cast<unsigned long long>(resource.size));
		if (!seen.insert(key).second)
			continue;

		DX12FrameResourceBinding binding;
		binding.eventSerial = producerEvent->serial;
		binding.dispatchId = producerEvent->dispatchId;
		binding.psoIndex = producerEvent->shaderInfo.psoIndex;
		binding.shaderInfo = producerEvent->shaderInfo;
		binding.bindSpace = "hunting_uav_state";
		binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		binding.shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		binding.shaderRegister = UINT_MAX;
		binding.usageClass = "UAV";
		binding.usageBegin = resource.gpuVirtualAddress;
		binding.usageBytes = resource.size;
		binding.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.kind = DescriptorKind::UAV;
		binding.descriptor.resource = resource.resource;
		binding.descriptor.resourceDesc = resource.resourceDesc;
		binding.descriptor.hasResourceDesc = true;
		binding.descriptor.resourceHeapType = resource.resourceHeapType;
		binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.resourceOffset = 0;
		binding.descriptor.viewSize = resource.size;
		binding.descriptor.currentState = resource.currentState;
		binding.descriptor.hasCurrentState = resource.hasCurrentState;
		binding.hasDescriptor = true;
		bindings->push_back(std::move(binding));
		emitted++;
	}

	DX12FrameAnalysis::LogJsonFunc("HuntingUavStateResourceUsages",
		"resources=%zu,dispatches=%zu,candidates=%zu,emitted=%zu",
		resources.size(), dispatches.size(), candidates, emitted);
}

void DX12BindingResourceExport::CollectActivePreSkinProducerBindings(
	std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	size_t candidates = 0;
	size_t emitted = 0;
	std::unordered_set<std::string> seen;
	const size_t initialCount = bindings->size();
	for (size_t i = 0; i < initialCount; ++i) {
		const DX12FrameResourceBinding &source = (*bindings)[i];
		if (source.bindSpace != "hunting_uav_state" ||
		    source.descriptor.kind != DescriptorKind::UAV ||
		    !source.hasDescriptor ||
		    !source.descriptor.resource ||
		    !source.descriptor.hasResourceDesc ||
		    source.descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
			continue;
		candidates++;

		DX12ModPreSkinProducerSummary producer = {};
		if (!DX12ModFindActivePreSkinProducerForOutput(
				source.descriptor.resource, source.descriptor.viewSize, 0, &producer))
			continue;
		if (!producer.hasDescriptor ||
		    producer.descriptor.kind != DescriptorKind::UAV ||
		    producer.shaderRegister == UINT_MAX)
			continue;

		char key[128];
		sprintf_s(key, "%llu|%p|%u|%u|%u|%u",
			static_cast<unsigned long long>(source.eventSerial),
			source.descriptor.resource,
			producer.rootParameterIndex,
			producer.rangeIndex,
			producer.shaderRegister,
			producer.registerSpace);
		if (!seen.insert(key).second)
			continue;

		DX12FrameResourceBinding binding = source;
		binding.bindSpace = "compute_cbv_srv_uav";
		binding.rootParameterIndex = producer.rootParameterIndex;
		binding.rangeIndex = producer.rangeIndex;
		binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		binding.shaderRegister = producer.shaderRegister;
		binding.registerSpace = producer.registerSpace;
		binding.descriptorOffset = producer.descriptorOffset;
		binding.rootDescriptor = producer.rootDescriptor;
		binding.gpuVirtualAddress = producer.gpuVirtualAddress;
		binding.usageClass.clear();
		binding.usageBegin = 0;
		binding.usageBytes = 0;
		binding.descriptor.structureByteStride =
			producer.outputStride ? producer.outputStride :
			(source.descriptor.structureByteStride ?
			 source.descriptor.structureByteStride :
			 producer.descriptor.structureByteStride);
		if (!binding.descriptor.structureByteStride && producer.descriptor.numElements)
			binding.descriptor.structureByteStride =
				static_cast<UINT>(source.descriptor.viewSize / producer.descriptor.numElements);
		binding.descriptor.numElements =
			binding.descriptor.structureByteStride ?
			static_cast<UINT>(source.descriptor.viewSize / binding.descriptor.structureByteStride) :
			source.descriptor.numElements;
		binding.descriptor.bufferViewOffset = 0;
		binding.descriptor.bufferViewBytes = source.descriptor.viewSize;
		binding.descriptor.viewDimension = D3D12_UAV_DIMENSION_BUFFER;
		binding.descriptor.viewSize = source.descriptor.viewSize;
		binding.descriptor.resourceOffset = source.descriptor.resourceOffset;
		binding.descriptor.gpuVirtualAddress = source.descriptor.gpuVirtualAddress;
		binding.hasDescriptor = true;
		bindings->push_back(std::move(binding));
		emitted++;
	}

	DX12FrameAnalysis::LogJsonFunc("ActivePreSkinProducerBindings",
		"candidates=%zu,emitted=%zu",
		candidates, emitted);
}

bool DX12BindingResourceExport::DescriptorBelongsToHeap(
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

void DX12BindingResourceExport::BuildDescriptorLookup(
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
			if (!DX12BindingResourceExport::DescriptorBelongsToHeap(descriptor, heap, &descriptorIndex))
				continue;
			DescriptorSlotKey key;
			key.heap = heap.heap;
			key.descriptorIndex = descriptorIndex;
			lookup->bySlot[key] = &descriptor;
			break;
		}
	}
}

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForSlot(
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

const DX12DescriptorSummary *DX12BindingResourceExport::FindDescriptorForEventSlot(
	DX12DescriptorSummary *historyStorage,
	const BindingEvent &event,
	const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap,
	UINT64 descriptorIndex, SIZE_T cpuHandle, UINT rangeType,
	size_t *historyLookups, size_t *historyHits)
{
	if (historyStorage && event.descriptorEpoch) {
		if (historyLookups)
			(*historyLookups)++;
		const char *kind = DX12BindingResourceExport::DescriptorKindForRangeType(rangeType);
		if (FindDescriptorSummaryByCpuHandleAtEpochAndKind(
				cpuHandle, event.descriptorEpoch, kind, historyStorage) ||
				FindDescriptorSummaryByCpuHandleAtEpoch(
					cpuHandle, event.descriptorEpoch, historyStorage)) {
			if (historyHits)
				(*historyHits)++;
			return historyStorage;
		}
	}
	return DX12BindingResourceExport::FindDescriptorForSlot(lookup, heap, descriptorIndex, cpuHandle);
}

UINT DX12BindingResourceExport::UnknownTableRangeType(UINT heapType)
{
	return heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : UINT_MAX;
}

bool DX12BindingResourceExport::MakeTableBinding(
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

bool DX12BindingResourceExport::CollectDescriptorTableEntry(
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
	const DX12DescriptorSummary *expectedDescriptor =
		DX12BindingResourceExport::DescriptorForExpectedRange(descriptor, rangeType);
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
			rangeIndex, rangeType, heap->heap, descriptorIndex, false))
		return false;
	if (!expectedDescriptor && untrackedSkipped)
		(*untrackedSkipped)++;

	DX12FrameResourceBinding binding;
	if (!DX12BindingResourceExport::MakeTableBinding(&binding, event, bindSpace, table, rangeIndex, rangeType,
			shaderVisibility, shaderRegister, registerSpace, descriptorOffset,
			heap, descriptorIndex, gpuHandle, cpuHandle, expectedDescriptor))
		return false;
	bindings->push_back(std::move(binding));
	return true;
}

bool DX12BindingResourceExport::VisitTableStartOnly(
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle,
	const DX12DescriptorHeapSummary **heap)
{
	if (!descriptorIndex || !gpuHandle || !cpuHandle || !heap)
		return false;
	*heap = DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, table, heapType);
	if (!*heap || (*heap)->increment == 0)
		return false;
	*descriptorIndex = (table.baseDescriptor.ptr - (*heap)->gpuStart) / (*heap)->increment;
	if (*descriptorIndex >= (*heap)->numDescriptors)
		return false;
	*gpuHandle = (*heap)->gpuStart + *descriptorIndex * (*heap)->increment;
	*cpuHandle = (*heap)->cpuStart + static_cast<SIZE_T>(*descriptorIndex) * (*heap)->increment;
	return true;
}

bool DX12BindingResourceExport::IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace)
{
	if (!bindSpace)
		return true;
	const bool samplerSpace = strstr(bindSpace, "sampler") != nullptr;
	return samplerSpace == (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
}

bool DX12BindingResourceExport::IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}


void DX12BindingResourceExport::CollectFrameResourceBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptors,
	std::unordered_set<std::string> *seen, size_t *untrackedSkipped,
	size_t *historyLookups, size_t *historyHits)
{
	if (!bindings || !table.valid || !seen)
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, table.rootParameterIndex);

	bool collectedRange = false;
	if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			if (!DX12BindingResourceExport::IsCompatibleRangeForSpace(range, bindSpace))
				continue;
			const DX12DescriptorHeapSummary *heap = DX12BindingTrackerQuery::FindHeapForTableRange(heaps, table, &range, heapType);
			if (!heap)
				continue;
			const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap->gpuStart) / heap->increment;
			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 descriptorIndex = tableBaseIndex + range.effectiveOffset + offset;
				if (descriptorIndex >= heap->numDescriptors)
					break;
				const UINT64 gpuHandle = heap->gpuStart + descriptorIndex * heap->increment;
				const SIZE_T cpuHandle = heap->cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap->increment;
				DX12DescriptorSummary historyStorage;
				const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
					&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
					range.rangeType, historyLookups, historyHits);
				if (DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table,
						range.rangeIndex, range.rangeType, range.shaderVisibility,
						range.baseShaderRegister + offset, range.registerSpace,
						range.effectiveOffset + offset,
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
	if (DX12BindingResourceExport::VisitTableStartOnly(table, heapType, heaps, &descriptorIndex,
			&gpuHandle, &cpuHandle, &heap)) {
		DX12DescriptorSummary historyStorage;
		const DX12DescriptorSummary *descriptor = DX12BindingResourceExport::FindDescriptorForEventSlot(
			&historyStorage, event, descriptors, heap, descriptorIndex, cpuHandle,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), historyLookups, historyHits);
		// Same design as file export: keep the table base binding when root metadata is absent,
		// but do not fabricate descriptor ranges that D3D12 did not expose.
		DX12BindingResourceExport::CollectDescriptorTableEntry(bindings, event, bindSpace, table, UINT_MAX,
			DX12BindingResourceExport::UnknownTableRangeType(heapType), D3D12_SHADER_VISIBILITY_ALL, UINT_MAX, 0, 0,
			heap, descriptorIndex, gpuHandle, cpuHandle, descriptor, seen, untrackedSkipped);
	}
}

void DX12BindingResourceExport::CollectFrameRootDescriptorBinding(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event, const char *bindSpace,
	const RootDescriptorState &rootDescriptor,
	std::unordered_set<std::string> *seen)
{
	if (!bindings || !rootDescriptor.valid || !seen)
		return;
	if (!DX12BindingResourceExport::InsertBindingSeen(seen, event, bindSpace, rootDescriptor.rootParameterIndex,
			UINT_MAX, static_cast<UINT>(rootDescriptor.type),
			nullptr, rootDescriptor.address, true))
		return;

	DX12RootSignatureSummary rootSignature;
	const bool compute = bindSpace && !strncmp(bindSpace, "compute", 7);
	const DX12RootParameterSummary *parameter = nullptr;
	if (DX12BindingTrackerQuery::GetRootSignatureForEvent(event, compute, &rootSignature))
		parameter = DX12BindingTrackerQuery::FindRootParameter(rootSignature, rootDescriptor.rootParameterIndex);

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
	DX12BufferResourceSummary resource;
	if (DX12ResolveBufferResourceByGpuVa(rootDescriptor.address, 1, &resource) &&
			resource.resource && resource.hasResourceDesc) {
		// Root descriptors carry only a GPU VA in D3D12, so project the resolved
		// buffer into descriptor form here. This keeps compute UAV producers on
		// the same event-resource path as descriptor tables, matching RenderDoc's
		// resource-usage granularity without scanning resources in Mod mode.
		binding.descriptor.kind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		binding.descriptor.resource = resource.resource;
		binding.descriptor.resourceDesc = resource.resourceDesc;
		binding.descriptor.hasResourceDesc = true;
		binding.descriptor.resourceHeapType = resource.resourceHeapType;
		binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = resource.resourceDesc.Width > resource.resourceOffset ?
			resource.resourceDesc.Width - resource.resourceOffset : 0;
		binding.descriptor.viewSize = availableBytes;
		binding.descriptor.currentState = resource.currentState;
		binding.descriptor.hasCurrentState = resource.hasCurrentState;
		binding.hasDescriptor = true;
	}
	bindings->push_back(std::move(binding));
}

void DX12BindingResourceExport::CountEventTables(
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
	const bool hasGraphicsRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, false, &graphicsRoot);
	const bool hasComputeRoot = DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &computeRoot);
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		if (event.graphicsTables[i].valid) {
			(*graphicsTables)++;
			if (hasGraphicsRoot) {
				(*graphicsTablesWithRoots)++;
				const DX12RootParameterSummary *parameter =
					DX12BindingTrackerQuery::FindRootParameter(graphicsRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.graphicsTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.graphicsTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
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
					DX12BindingTrackerQuery::FindRootParameter(computeRoot, i);
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					(*rootRangeTables)++;
					const DX12DescriptorHeapSummary *heap =
						DX12BindingTrackerQuery::FindHeapForGpuHandle(heaps, event.computeTables[i],
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					if (heap && heap->increment) {
						const UINT64 tableBaseIndex =
							(event.computeTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.numDescriptors != UINT_MAX)
								continue;
							(*unboundedRanges)++;
							(*unboundedDescriptors) +=
								DX12BindingTrackerQuery::DescriptorCountForRange(*heap, tableBaseIndex, range);
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

void DX12BindingResourceExport::CollectEventResourceBindings(
	std::vector<DX12FrameResourceBinding> *bindings,
	const BindingEvent &event,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const DescriptorLookup &descriptorLookup,
	std::unordered_set<std::string> *seen,
	size_t *untrackedSkipped,
	size_t *historyLookups,
	 size_t *historyHits)
{
	if (!bindings || !seen)
		return;
	for (UINT i = 0; i < MaxRootParameters; ++i) {
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "graphics_sampler",
			event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameResourceBinding(bindings, event, "compute_sampler",
			event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			heaps, descriptorLookup, seen, untrackedSkipped, historyLookups, historyHits);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
			event.graphicsRootDescriptors[i], seen);
		DX12BindingResourceExport::CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
			event.computeRootDescriptors[i], seen);
	}
}

void DX12BindingResourceExport::GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameResourceBindings");
	const std::vector<BindingEvent> &events = source.GetEvents();
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesSnapshot",
		"events=%zu,capture_session_id=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId));

	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12DescriptorHeapSummary> heaps;
	DX12GetResourceMetadataSnapshot(nullptr, &descriptors, nullptr, &heaps);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesMetadataReady",
		"descriptors=%zu,heaps=%zu",
		descriptors.size(), heaps.size());
	DescriptorLookup descriptorLookup;
	DX12BindingResourceExport::BuildDescriptorLookup(descriptors, heaps, &descriptorLookup);
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesLookupReady",
		"cpu_entries=%zu,slot_entries=%zu",
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
	size_t untrackedCbvBindings = 0;
	size_t untrackedSrvBindings = 0;
	size_t untrackedUavBindings = 0;
	size_t untrackedSkipped = 0;
	size_t textureBindings = 0;
	size_t rebuiltEventBindings = 0;
	size_t rebuiltResolvedBindings = 0;
	size_t descriptorHistoryLookups = 0;
	size_t descriptorHistoryHits = 0;
	size_t zeroDescriptorEpochEvents = 0;
	for (const BindingEvent &event : events) {
		if (!DX12BindingTrackerQuery::IsDrawEvent(event) && !DX12BindingTrackerQuery::IsDispatchEvent(event))
			continue;
		if (event.descriptorEpoch == 0)
			zeroDescriptorEpochEvents++;
		if (DX12BindingTrackerQuery::IsDrawEvent(event))
			drawEvents++;
		if (DX12BindingTrackerQuery::IsDispatchEvent(event))
			dispatchEvents++;
		DX12BindingResourceExport::CountEventTables(event, heaps, &graphicsTables, &computeTables,
			&graphicsTablesWithRoots, &computeTablesWithRoots,
			&rootRangeTables, &tableStartOnly, &unboundedRanges,
			&unboundedDescriptors, &graphicsTablesWithoutRoots,
			&computeTablesWithoutRoots, &descriptorTablesWithoutRanges);
		const size_t before = bindings->size();
		// Rebuild from the final descriptor metadata snapshot. Event recording only
		// stores root table/root descriptor state; resolving resources here matches
		// RenderDoc's event-granular model and avoids hot-path descriptor scans.
		DX12BindingResourceExport::CollectEventResourceBindings(
			bindings, event, heaps, descriptorLookup, &seen, &untrackedSkipped,
			&descriptorHistoryLookups, &descriptorHistoryHits);
		rebuiltEventBindings += bindings->size() - before;
		for (size_t i = before; i < bindings->size(); ++i) {
			if ((*bindings)[i].hasDescriptor)
				rebuiltResolvedBindings++;
		}
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesDiscoveryFallbackSkipped",
		"\"reason\":\"single_frame_event_scope\",\"hunting_uav_state\":0,\"active_pre_skin\":0");
	for (const DX12FrameResourceBinding &binding : *bindings) {
		const char *expectedKind = DX12BindingResourceExport::ExpectedBindingKind(binding);
		if (!binding.hasDescriptor) {
			untrackedBindings++;
			if (!strcmp(expectedKind, "CBV"))
				untrackedCbvBindings++;
			else if (!strcmp(expectedKind, "SRV"))
				untrackedSrvBindings++;
			else if (!strcmp(expectedKind, "UAV"))
				untrackedUavBindings++;
			continue;
		}
		if (!strcmp(expectedKind, "CBV"))
			cbvBindings++;
		else if (!strcmp(expectedKind, "SRV"))
			srvBindings++;
		else if (!strcmp(expectedKind, "UAV"))
			uavBindings++;
		else if (!strcmp(expectedKind, "Sampler"))
			samplerBindings++;
		if (binding.descriptor.hasResourceDesc &&
			DX12BindingResourceExport::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			textureBindings++;
	}
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesCollected",
		"bindings=%zu,unique=%zu,rebuilt_bindings=%zu,rebuilt_resolved=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,zero_descriptor_epoch_events=%zu,draw_events=%zu,dispatch_events=%zu,graphics_tables=%zu,compute_tables=%zu,graphics_tables_with_roots=%zu,compute_tables_with_roots=%zu,root_range_tables=%zu,table_start_only=%zu,graphics_tables_without_roots=%zu,compute_tables_without_roots=%zu,descriptor_tables_without_ranges=%zu,unbounded_ranges=%zu,unbounded_descriptors=%zu,cbv=%zu,srv=%zu,uav=%zu,sampler=%zu,texture=%zu,untracked=%zu,untracked_cbv=%zu,untracked_srv=%zu,untracked_uav=%zu,untracked_skipped=%zu",
		bindings->size(), seen.size(), rebuiltEventBindings, rebuiltResolvedBindings,
		descriptorHistoryLookups, descriptorHistoryHits, zeroDescriptorEpochEvents,
		drawEvents, dispatchEvents,
		graphicsTables, computeTables,
		graphicsTablesWithRoots, computeTablesWithRoots,
		rootRangeTables, tableStartOnly,
		graphicsTablesWithoutRoots, computeTablesWithoutRoots,
		descriptorTablesWithoutRanges,
		unboundedRanges, unboundedDescriptors,
		cbvBindings, srvBindings, uavBindings, samplerBindings,
		textureBindings, untrackedBindings,
		untrackedCbvBindings, untrackedSrvBindings, untrackedUavBindings,
		untrackedSkipped);
	const char *historyStatus = "ok";
	if (zeroDescriptorEpochEvents)
		historyStatus = "zero_epoch";
	else if (descriptorHistoryLookups && !descriptorHistoryHits)
		historyStatus = "miss";
	else if (untrackedUavBindings && !descriptorHistoryLookups)
		historyStatus = "no_history_lookup";
	else if (untrackedUavBindings)
		historyStatus = "untracked_uav";
	DX12FrameAnalysis::LogJsonFunc("BindingResourcesHistoryHealth",
		"status=%s,events=%zu,zero_descriptor_epoch_events=%zu,descriptor_history_lookups=%zu,descriptor_history_hits=%zu,untracked_uav=%zu",
		historyStatus,
		drawEvents + dispatchEvents,
		zeroDescriptorEpochEvents,
		descriptorHistoryLookups,
		descriptorHistoryHits,
		untrackedUavBindings);
}

void DX12BindingResourceExport::GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings)
{
	if (!bindings)
		return;

	DX12FrameEventSource source;
	PopulatePreferredResourceEventSource(&source, "GetCurrentFrameOutputBindings");
	const std::vector<OutputBindingEvent> &events = source.GetOutputEvents();
	const UINT64 dropped = source.droppedOutputEvents;
	DX12FrameAnalysis::LogJsonFunc("OutputResourcesLedgerSnapshot",
		"events=%zu,capture_session_id=%llu,dropped=%llu",
		events.size(),
		static_cast<unsigned long long>(source.captureSessionId),
		static_cast<unsigned long long>(dropped));

	std::unordered_set<std::string> seen;
	bindings->clear();
	bindings->reserve(events.size());
	size_t rtv = 0;
	size_t dsv = 0;
	size_t untracked = 0;
	std::vector<OutputBindingEvent> uniqueEvents;
	std::vector<SIZE_T> cpuHandles;
	uniqueEvents.reserve(events.size());
	cpuHandles.reserve(events.size());
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
		uniqueEvents.push_back(event);
		cpuHandles.push_back(event.cpuHandle);
	}

	std::vector<DX12DescriptorSummary> descriptorSummaries(uniqueEvents.size());
	std::vector<uint8_t> descriptorFound(uniqueEvents.size());
	DX12FindDescriptorSummariesByCpuHandle(
		cpuHandles.data(), cpuHandles.size(),
		descriptorSummaries.data(), descriptorFound.data(), true);

	for (size_t i = 0; i < uniqueEvents.size(); ++i) {
		const OutputBindingEvent &event = uniqueEvents[i];
		DX12FrameOutputBinding binding;
		binding.kind = event.kind;
		binding.eventSerial = event.serial;
		binding.drawId = event.drawId;
		binding.dispatchId = event.dispatchId;
		binding.psoIndex = event.shaderInfo.psoIndex;
		binding.shaderInfo = event.shaderInfo;
		binding.slot = event.slot;
		binding.cpuHandle = event.cpuHandle;
		binding.hasDescriptor = descriptorFound[i] != 0;
		if (binding.hasDescriptor)
			binding.descriptor = descriptorSummaries[i];
		if (binding.kind == "RTV")
			rtv++;
		else if (binding.kind == "DSV")
			dsv++;
		if (!binding.hasDescriptor)
			untracked++;
		bindings->push_back(std::move(binding));
	}

	DX12FrameAnalysis::LogJsonFunc("OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
	DX12LogManager::JsonFunc("DX12OutputResourcesCollected",
		"bindings=%zu,rtv=%zu,dsv=%zu,untracked=%zu,dropped=%llu",
		bindings->size(), rtv, dsv, untracked,
		static_cast<unsigned long long>(dropped));
}










