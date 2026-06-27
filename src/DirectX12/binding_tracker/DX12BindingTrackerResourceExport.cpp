#include "DX12BindingTrackerPrivate.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12Json.h"

static bool InsertBindingSeen(
	std::unordered_set<std::string> *seen, const BindingEvent &event,
	const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex,
	ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor)
{
	if (!seen)
		return false;

	char key[320];
	sprintf_s(key, "%llu|%s|%u|%u|%p|%llu|%u|%llu",
		static_cast<unsigned long long>(event.shaderInfo.psoIndex),
		bindSpace ? bindSpace : "",
		rootParameterIndex,
		rangeIndex,
		heap,
		static_cast<unsigned long long>(descriptorIndex),
		rootDescriptor ? 1 : 0,
		static_cast<unsigned long long>(event.serial));
	return seen->insert(key).second;
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


static void WriteFrameResourceBinding(
	FILE *file, const BindingEvent &event, const char *bindSpace,
	const RootTableState &table, UINT heapType,
	const std::vector<DX12DescriptorHeapSummary> &heaps,
	const std::unordered_map<SIZE_T, const DX12DescriptorSummary*> &descriptorsByCpuHandle,
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
				const DX12DescriptorSummary *descriptor = DX12BindingFindDescriptorByCpuHandle(descriptorsByCpuHandle, cpuHandle);
				if (!descriptor) {
					if (untrackedSkipped)
						(*untrackedSkipped)++;
					continue;
				}
				if (!InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
						range.rangeIndex, heap->heap, descriptorIndex, false))
					continue;
				WriteDescriptorResourceRow(file, event, bindSpace, table.rootParameterIndex,
					range.rangeIndex, range.rangeType, range.baseShaderRegister + offset,
					range.registerSpace, range.effectiveOffset + offset, heap, descriptorIndex,
					gpuHandle, cpuHandle, descriptor);
				wroteRange = true;
			}
		}
	}

	if (wroteRange)
		return;
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
	const std::unordered_map<SIZE_T, const DX12DescriptorSummary*> &descriptorsByCpuHandle,
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
				const DX12DescriptorSummary *descriptor = DX12BindingFindDescriptorByCpuHandle(descriptorsByCpuHandle, cpuHandle);
				if (!descriptor) {
					if (untrackedSkipped)
						(*untrackedSkipped)++;
					continue;
				}
				if (!InsertBindingSeen(seen, event, bindSpace, table.rootParameterIndex,
						range.rangeIndex, heap->heap, descriptorIndex, false))
					continue;

				DX12FrameResourceBinding binding;
				binding.eventSerial = event.serial;
				binding.drawId = event.drawId;
				binding.dispatchId = event.dispatchId;
				binding.psoIndex = event.shaderInfo.psoIndex;
				binding.shaderInfo = event.shaderInfo;
				binding.bindSpace = bindSpace ? bindSpace : "";
				binding.rootParameterIndex = table.rootParameterIndex;
				binding.rangeIndex = range.rangeIndex;
				binding.rangeType = range.rangeType;
				binding.shaderRegister = range.baseShaderRegister + offset;
				binding.registerSpace = range.registerSpace;
				binding.descriptorOffset = range.effectiveOffset + offset;
				binding.heap = heap->heap;
				binding.heapType = heap->type;
				binding.descriptorIndex = descriptorIndex;
				binding.gpuHandle = gpuHandle;
				binding.cpuHandle = cpuHandle;
				binding.heapGpuStart = heap->gpuStart;
				binding.descriptor = *descriptor;
				binding.hasDescriptor = true;
				bindings->push_back(std::move(binding));
				collectedRange = true;
			}
		}
	}

	if (collectedRange)
		return;
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
	binding.shaderRegister = parameter ? parameter->shaderRegister : UINT_MAX;
	binding.registerSpace = parameter ? parameter->registerSpace : 0;
	binding.rootDescriptor = true;
	binding.gpuVirtualAddress = rootDescriptor.address;
	bindings->push_back(std::move(binding));
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
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> descriptorsByCpuHandle;
	DX12BindingBuildDescriptorLookup(descriptors, &descriptorsByCpuHandle);
	DX12FrameAnalysisLogJsonFunc("BindingResourcesLookupReady",
		"\"entries\":%zu",
		descriptorsByCpuHandle.size());

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
	for (const BindingEvent &event : events) {
		if (!DX12BindingIsDrawEvent(event) && !DX12BindingIsDispatchEvent(event))
			continue;
		if (DX12BindingIsDrawEvent(event))
			drawEvents++;
		if (DX12BindingIsDispatchEvent(event))
			dispatchEvents++;
		DX12RootSignatureSummary graphicsRoot;
		DX12RootSignatureSummary computeRoot;
		const bool hasGraphicsRoot = DX12BindingGetRootSignatureForEvent(event, false, &graphicsRoot);
		const bool hasComputeRoot = DX12BindingGetRootSignatureForEvent(event, true, &computeRoot);
		for (UINT i = 0; i < MaxRootParameters; ++i) {
			if (event.graphicsTables[i].valid) {
				graphicsTables++;
				if (hasGraphicsRoot) {
					graphicsTablesWithRoots++;
					const DX12RootParameterSummary *parameter =
						DX12BindingFindRootParameter(graphicsRoot, i);
					if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
						rootRangeTables++;
						const DX12DescriptorHeapSummary *heap =
							DX12BindingFindHeapForGpuHandle(heaps, event.graphicsTables[i],
								D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						if (heap && heap->increment) {
							const UINT64 tableBaseIndex =
								(event.graphicsTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
							for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
								if (range.numDescriptors != UINT_MAX)
									continue;
								unboundedRanges++;
								unboundedDescriptors +=
									DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
							}
						}
					} else {
						tableStartOnly++;
						descriptorTablesWithoutRanges++;
					}
				} else {
					tableStartOnly++;
					graphicsTablesWithoutRoots++;
				}
			}
			if (event.computeTables[i].valid) {
				computeTables++;
				if (hasComputeRoot) {
					computeTablesWithRoots++;
					const DX12RootParameterSummary *parameter =
						DX12BindingFindRootParameter(computeRoot, i);
					if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
						rootRangeTables++;
						const DX12DescriptorHeapSummary *heap =
							DX12BindingFindHeapForGpuHandle(heaps, event.computeTables[i],
								D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						if (heap && heap->increment) {
							const UINT64 tableBaseIndex =
								(event.computeTables[i].baseDescriptor.ptr - heap->gpuStart) / heap->increment;
							for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
								if (range.numDescriptors != UINT_MAX)
									continue;
								unboundedRanges++;
								unboundedDescriptors +=
									DX12BindingDescriptorCountForRange(*heap, tableBaseIndex, range);
							}
						}
					} else {
						tableStartOnly++;
						descriptorTablesWithoutRanges++;
					}
				} else {
					tableStartOnly++;
					computeTablesWithoutRoots++;
				}
			}
			CollectFrameResourceBinding(bindings, event, "graphics_cbv_srv_uav",
				event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			CollectFrameResourceBinding(bindings, event, "graphics_sampler",
				event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			CollectFrameResourceBinding(bindings, event, "compute_cbv_srv_uav",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			CollectFrameResourceBinding(bindings, event, "compute_sampler",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			CollectFrameRootDescriptorBinding(bindings, event, "graphics_root",
				event.graphicsRootDescriptors[i], &seen);
			CollectFrameRootDescriptorBinding(bindings, event, "compute_root",
				event.computeRootDescriptors[i], &seen);
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
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			textureBindings++;
	}
	DX12FrameAnalysisLogJsonFunc("BindingResourcesCollected",
		"\"bindings\":%zu,\"unique\":%zu,\"draw_events\":%zu,\"dispatch_events\":%zu,\"graphics_tables\":%zu,\"compute_tables\":%zu,\"graphics_tables_with_roots\":%zu,\"compute_tables_with_roots\":%zu,\"root_range_tables\":%zu,\"table_start_only\":%zu,\"graphics_tables_without_roots\":%zu,\"compute_tables_without_roots\":%zu,\"descriptor_tables_without_ranges\":%zu,\"unbounded_ranges\":%zu,\"unbounded_descriptors\":%zu,\"cbv\":%zu,\"srv\":%zu,\"uav\":%zu,\"sampler\":%zu,\"texture\":%zu,\"untracked\":%zu,\"untracked_skipped\":%zu",
		bindings->size(), seen.size(),
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
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> descriptorsByCpuHandle;
	DX12BindingBuildDescriptorLookup(descriptors, &descriptorsByCpuHandle);
	DX12FrameAnalysisLogJsonFunc("FrameResourceFileLookupReady",
		"\"entries\":%zu",
		descriptorsByCpuHandle.size());

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
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "graphics_sampler",
				event.graphicsTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "compute_cbv_srv_uav",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
			WriteFrameResourceBinding(file, event, "compute_sampler",
				event.computeTables[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				heaps, descriptorsByCpuHandle, &seen, &untrackedSkipped);
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
