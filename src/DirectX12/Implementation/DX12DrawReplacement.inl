class DX12DrawSrvPatchRequest
{
public:
	DX12CurrentShaderResourceBinding binding;
	DX12LoadedResource *resource = nullptr;
	bool compute = false;
	const char *source = "";
	D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
	DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
};

class DX12DrawSrvPatchTable
{
public:
	bool compute = false;
	bool sampler = false;
	UINT rootParameterIndex = 0;
	SIZE_T cpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	ID3D12DescriptorHeap *heap = nullptr;
	UINT increment = 0;
	UINT count = 0;
	UINT tempOffset = 0;
};

class DX12DrawSrvReplacementUtils
{
public:
	static bool ShaderVisibilityMatchesPixel(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_PIXEL;
	}

	static bool ShaderVisibilityMatchesVertex(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_VERTEX;
	}

	static bool BindingCanBePatched(const DX12CurrentShaderResourceBinding &binding)
	{
		return binding.hasDescriptor &&
			!binding.rootDescriptor &&
			binding.tableCpuStart != 0 &&
			binding.tableGpuStart.ptr != 0 &&
			binding.descriptorIncrement != 0 &&
			binding.tableHeap != nullptr &&
			binding.descriptor.kind == "SRV";
	}

	static void CollectSrvBindings(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		std::vector<DX12CurrentShaderResourceBinding> *bindings)
	{
		if (!bindings)
			return;
		bindings->clear();
		if (!commandList)
			return;
		DX12BindingTracker::GetCurrentShaderResourceBindings(
			commandList, compute, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bindings);
#if defined(_DEBUG)
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=collect_srvs,bindSpace=%s,count=%zu,present=%ld",
			compute ? "compute" : "graphics",
			bindings->size(),
			DX12RuntimeState::GetPresentCount());
#endif
	}

	static uint64_t BindingKey(const DX12CurrentShaderResourceBinding &binding, bool compute)
	{
		uint64_t key = compute ? 0x9ae16a3b2f90404full : 0xc3a5c85c97cb3127ull;
		key ^= static_cast<uint64_t>(binding.rootParameterIndex) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.descriptorOffset) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.tableGpuStart.ptr) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		return key;
	}

	static void AddOrReplaceRequest(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding,
		DX12LoadedResource *resource,
		bool compute,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!requests || !resource || !resource->drawSrvCpu.ptr)
			return;
		if (!DX12DrawSrvReplacementUtils::BindingCanBePatched(binding))
			return;
		// Use the DDS default SRV directly. This matches DX11 behavior:
		// DirectX::CreateDDSTextureFromFileEx creates the replacement SRV
		// with DEFAULT component mapping, and PSSetShaderResources uses it
		// as-is without any "compatibility" conversion.
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = resource->drawSrvCpu;
		DXGI_FORMAT replacementViewFormat = resource->drawSrvFormat;
		const uint64_t key = DX12DrawSrvReplacementUtils::BindingKey(binding, compute);
		for (DX12DrawSrvPatchRequest &request : *requests) {
			if (DX12DrawSrvReplacementUtils::BindingKey(request.binding, request.compute) == key) {
				request.resource = resource;
				request.source = source ? source : "";
				request.replacementCpu = replacementCpu;
				request.replacementViewFormat = replacementViewFormat;
				return;
			}
		}
		DX12DrawSrvPatchRequest request;
		request.binding = binding;
		request.resource = resource;
		request.compute = compute;
		request.source = source ? source : "";
		request.replacementCpu = replacementCpu;
		request.replacementViewFormat = replacementViewFormat;
		requests->push_back(request);
	}

	static UINT AddSlotRequestsFromBindings(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentShaderResourceBinding> &bindings,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		UINT added = 0;
		DX12LoadedResource *resource = nullptr;
		for (const DX12CurrentShaderResourceBinding &binding : bindings) {
			if (binding.shaderRegister != slot || binding.registerSpace != 0)
				continue;
			const bool visibilityMatches = pixelStage ?
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesPixel(binding.shaderVisibility, compute) :
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesVertex(binding.shaderVisibility, compute);
			if (!visibilityMatches)
				continue;
			if (!resource)
				resource = DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
					device, commandList, resourceName);
			if (!resource)
				return added;
			DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
				device, binding, resource, compute, source, requests);
			++added;
		}
		return added;
	}

	static UINT AddThisRequestFromMatch(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12TextureOverrideMatch *match,
		const std::wstring &resourceName,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		if (!match || !match->hasThisSrvTarget) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,source=this,reason=no_this_target,resource=%S",
				resourceName.c_str());
			return 0;
		}
		DX12LoadedResource *resource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (!resource)
			return 0;
		DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
			device, match->thisSrvBinding, resource, match->compute, "this", requests);
		return 1;
	}

	static bool QueueRequests(
		const std::vector<DX12DrawSrvPatchRequest> &requests,
		DX12ModIaReplacement *replacement)
	{
		if (!replacement || requests.empty())
			return false;
		for (const DX12DrawSrvPatchRequest &request : requests) {
			if (!request.resource)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = request.replacementCpu.ptr ?
				request.replacementCpu : request.resource->drawSrvCpu;
			if (!replacementCpu.ptr)
				continue;
			const uint64_t key = BindingKey(request.binding, request.compute);
			DX12ModIaReplacement::DrawSrvTablePatch *queued = nullptr;
			for (DX12ModIaReplacement::DrawSrvTablePatch &existing :
					replacement->drawSrvPatches) {
				if (BindingKey(existing.binding, existing.compute) == key) {
					queued = &existing;
					break;
				}
			}
			if (!queued) {
				replacement->drawSrvPatches.push_back(
					DX12ModIaReplacement::DrawSrvTablePatch());
				queued = &replacement->drawSrvPatches.back();
			}
			queued->binding = request.binding;
			queued->replacementCpu = replacementCpu;
			queued->replacementViewFormat = request.replacementViewFormat;
			queued->resourceName = request.resource->name;
			queued->source = request.source ? request.source : "";
			queued->compute = request.compute;
			queued->rootParameterIndex = request.binding.rootParameterIndex;
			queued->originalTable = request.binding.tableGpuStart;
		}
		return !replacement->drawSrvPatches.empty();
	}

	static bool ResolveTableHeap(
		const RootTableState &table,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		if (table.runtimeHeapResolved &&
				table.runtimeHeapType == static_cast<UINT>(type) &&
				table.runtimeHeapIncrement && table.runtimeHeapGpuStart) {
			heap->heap = table.runtimeHeap;
			heap->type = table.runtimeHeapType;
			heap->numDescriptors = table.runtimeHeapNumDescriptors;
			heap->cpuStart = table.runtimeHeapCpuStart;
			heap->gpuStart = table.runtimeHeapGpuStart;
			heap->increment = table.runtimeHeapIncrement;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, type, heap) && heap->increment != 0;
	}

	static UINT DescriptorCountForTable(
		const DX12RootParameterSummary &parameter,
		const DX12DescriptorHeapSummary &heap,
		UINT64 tableBaseIndex,
		D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		UINT64 required = 0;
		for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
			const bool sampler =
				range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			if (sampler != (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
				continue;
			const UINT64 first = tableBaseIndex + range.effectiveOffset;
			if (first >= heap.numDescriptors)
				continue;
			const UINT64 available = heap.numDescriptors - first;
			const UINT64 declared = range.numDescriptors == UINT_MAX ?
				available : range.numDescriptors;
			const UINT64 count = (std::min)(declared, available);
			required = (std::max)(required,
				static_cast<UINT64>(range.effectiveOffset) + count);
		}
		return required > UINT_MAX ? UINT_MAX : static_cast<UINT>(required);
	}

	static bool RequestMatchesSrvLayout(
		const DX12ModIaReplacement::DrawSrvTablePatch &request,
		const DX12RootSignatureSummary &rootSignature)
	{
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex != request.binding.rootParameterIndex ||
					parameter.parameterType !=
						D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				continue;
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
						request.binding.descriptorOffset < range.effectiveOffset)
					continue;
				const UINT64 relative = static_cast<UINT64>(
					request.binding.descriptorOffset - range.effectiveOffset);
				if (range.numDescriptors != UINT_MAX &&
						relative >= range.numDescriptors)
					continue;
				const UINT64 shaderRegister =
					static_cast<UINT64>(range.baseShaderRegister) + relative;
				if (shaderRegister > UINT_MAX ||
						static_cast<UINT>(shaderRegister) !=
							request.binding.shaderRegister ||
						range.registerSpace != request.binding.registerSpace ||
						(request.binding.rangeIndex != UINT_MAX &&
							range.rangeIndex != request.binding.rangeIndex))
					continue;
				return true;
			}
			return false;
		}
		return false;
	}

	static bool AppendPatchTables(
		const CommandListBindingState &snapshot,
		const DX12RootSignatureSummary &rootSignature,
		bool compute,
		std::vector<DX12DrawSrvPatchTable> *tables,
		UINT *totalDescriptors)
	{
		if (!tables || !totalDescriptors)
			return false;
		static constexpr UINT MaxMirroredDescriptors = 65536;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &state = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (!state.valid)
				continue;
			const DX12RootParameterSummary *parameter = nullptr;
			for (const DX12RootParameterSummary &candidate :
					rootSignature.parameters) {
				if (candidate.rootParameterIndex == root) {
					parameter = &candidate;
					break;
				}
			}
			if (!parameter || parameter->parameterType !=
					D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
					parameter->ranges.empty()) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=root_parameter,pipeline=%s,root=%u",
					compute ? "compute" : "graphics", root);
#endif
				return false;
			}
			bool hasSampler = false;
			bool hasResource = false;
			for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_range,pipeline=%s,root=%u,range=%u,type=%u,"
					"numDescriptors=%u,effectiveOffset=%u,baseRegister=%u,space=%u",
					compute ? "compute" : "graphics", root, range.rangeIndex,
					range.rangeType, range.numDescriptors, range.effectiveOffset,
					range.baseShaderRegister, range.registerSpace);
#endif
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
					hasSampler = true;
				else
					hasResource = true;
			}
			if (hasSampler == hasResource) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=mixed_or_empty,pipeline=%s,root=%u,"
					"sampler=%s,resource=%s",
					compute ? "compute" : "graphics", root,
					hasSampler ? "true" : "false",
					hasResource ? "true" : "false");
#endif
				return false;
			}
			const D3D12_DESCRIPTOR_HEAP_TYPE type = hasSampler ?
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DX12DescriptorHeapSummary heap = {};
			ID3D12DescriptorHeap *boundHeap = hasSampler ?
				snapshot.samplerHeap : snapshot.cbvSrvUavHeap;
			if (!ResolveTableHeap(state, type, &heap) ||
					!boundHeap || heap.heap != boundHeap || !heap.cpuStart ||
					state.baseDescriptor.ptr < heap.gpuStart) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=heap,pipeline=%s,root=%u,"
					"resolvedHeap=%p,boundHeap=%p,cpuStart=0x%zx,gpuStart=0x%llx,table=0x%llx",
					compute ? "compute" : "graphics", root, heap.heap, boundHeap,
					heap.cpuStart, static_cast<unsigned long long>(heap.gpuStart),
					static_cast<unsigned long long>(state.baseDescriptor.ptr));
#endif
				return false;
			}
			const UINT64 tableDelta = state.baseDescriptor.ptr - heap.gpuStart;
			if (tableDelta % heap.increment != 0) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=alignment,pipeline=%s,root=%u,"
					"delta=%llu,increment=%u",
					compute ? "compute" : "graphics", root,
					static_cast<unsigned long long>(tableDelta), heap.increment);
#endif
				return false;
			}
			const UINT64 tableBaseIndex = tableDelta / heap.increment;
			if (tableBaseIndex >= heap.numDescriptors) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=base_index,pipeline=%s,root=%u,"
					"tableBaseIndex=%llu,heapDescriptors=%u",
					compute ? "compute" : "graphics", root,
					static_cast<unsigned long long>(tableBaseIndex), heap.numDescriptors);
#endif
				return false;
			}
			const UINT count = DescriptorCountForTable(
				*parameter, heap, tableBaseIndex, type);
			if (!count) {
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_plan_fail,reason=empty_count,pipeline=%s,root=%u,"
					"tableBaseIndex=%llu,heapDescriptors=%u",
					compute ? "compute" : "graphics", root,
					static_cast<unsigned long long>(tableBaseIndex), heap.numDescriptors);
#endif
				return false;
			}
			DX12DrawSrvPatchTable table;
			table.compute = compute;
			table.rootParameterIndex = root;
			table.sampler = hasSampler;
			table.cpuStart = heap.cpuStart +
				static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
			table.gpuStart = state.baseDescriptor;
			table.heap = heap.heap;
			table.increment = heap.increment;
			table.count = count;
			if (!hasSampler) {
				if (*totalDescriptors > MaxMirroredDescriptors ||
						count > MaxMirroredDescriptors - *totalDescriptors) {
#if defined(_DEBUG)
					DX12LogManager::Fields("DX12DrawSrvReplacement",
						"stage=table_plan_fail,reason=mirror_limit,pipeline=%s,root=%u,"
						"tableBaseIndex=%llu,heapDescriptors=%u,count=%u,total=%u,limit=%u",
						compute ? "compute" : "graphics", root,
						static_cast<unsigned long long>(tableBaseIndex), heap.numDescriptors,
						count, *totalDescriptors, MaxMirroredDescriptors);
#endif
					return false;
				}
				table.tempOffset = *totalDescriptors;
				*totalDescriptors += count;
			}
			tables->push_back(table);
#if defined(_DEBUG)
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_plan,pipeline=%s,root=%u,sampler=%s,"
				"tableBaseIndex=%llu,heapDescriptors=%u,count=%u,tempOffset=%u,total=%u",
				compute ? "compute" : "graphics", root,
				hasSampler ? "true" : "false",
				static_cast<unsigned long long>(tableBaseIndex), heap.numDescriptors,
				count, table.tempOffset, *totalDescriptors);
#endif
		}
		return true;
	}

	static bool BuildPatchTables(
		const CommandListBindingState &snapshot,
		const DX12RootSignatureSummary &rootSignature,
		std::vector<DX12DrawSrvPatchTable> *tables,
		UINT *totalDescriptors)
	{
		if (!tables || !totalDescriptors)
			return false;
		tables->clear();
		*totalDescriptors = 0;
		if (!AppendPatchTables(
				snapshot, rootSignature, false, tables, totalDescriptors))
			return false;
		return !tables->empty() && *totalDescriptors != 0;
	}

	// Descriptor contents are read by the GPU at execution time, not captured by
	// Draw*. Mirror every active graphics table into a command-list-lived heap,
	// patch that mirror, and temporarily bind it for the draw. This prevents the
	// game's descriptor ring from overwriting the replacement before submission.
	static bool ApplyPending(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement,
		bool executeOriginalDraw)
	{
		if (!device || !commandList || !replacement ||
				replacement->drawSrvPatches.empty())
			return false;
		if (replacement->drawSrvApplyState.applied) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=already_applied");
			return false;
		}
		if (commandList->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=bundle_requires_parent_replay");
			return false;
		}
		CommandListBindingState snapshot;
		if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=binding_snapshot");
			return false;
		}
		bool needsGraphicsTables = executeOriginalDraw || !replacement->draws.empty();
		bool needsComputeTables = !replacement->dispatches.empty();
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			if (request.compute)
				needsComputeTables = true;
			else
				needsGraphicsTables = true;
		}
		std::vector<DX12DrawSrvPatchTable> tables;
		UINT totalDescriptors = 0;
		DX12RootSignatureSummary graphicsRootSignature;
		DX12RootSignatureSummary computeRootSignature;
		bool hasGraphicsRootSignature = false;
		bool hasComputeRootSignature = false;
		if (needsGraphicsTables && snapshot.graphicsTableValidCount) {
			if (!snapshot.graphicsRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.graphicsRootSignature, &graphicsRootSignature) ||
					!AppendPatchTables(snapshot, graphicsRootSignature, false,
						&tables, &totalDescriptors)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=graphics_root_signature");
				return false;
			}
			hasGraphicsRootSignature = true;
		}
		if (needsComputeTables && snapshot.computeTableValidCount) {
			if (!snapshot.computeRootSignature ||
					!DX12GetRootSignatureSummary(
						snapshot.computeRootSignature, &computeRootSignature) ||
					!AppendPatchTables(snapshot, computeRootSignature, true,
						&tables, &totalDescriptors)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=compute_root_signature");
				return false;
			}
			hasComputeRootSignature = true;
		}
		if (tables.empty() || !totalDescriptors) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=table_plan");
			return false;
		}
		for (const DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			const DX12RootSignatureSummary *requestRootSignature =
				request.compute ?
					(hasComputeRootSignature ? &computeRootSignature : nullptr) :
					(hasGraphicsRootSignature ? &graphicsRootSignature : nullptr);
			if (!requestRootSignature ||
					!RequestMatchesSrvLayout(request, *requestRootSignature)) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_layout,bindSpace=%s,"
					"root=%u,range=%u,slot=%u,space=%u,offset=%u",
					request.compute ? "compute" : "graphics",
					request.binding.rootParameterIndex,
					request.binding.rangeIndex,
					request.binding.shaderRegister,
					request.binding.registerSpace,
					request.binding.descriptorOffset);
				return false;
			}
			bool found = false;
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				if (request.binding.descriptorOffset >= table.count)
					return false;
				found = true;
				break;
			}
			if (!found || !request.replacementCpu.ptr) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=table_patch_fail,reason=request_table,root=%u,offset=%u",
					request.binding.rootParameterIndex,
					request.binding.descriptorOffset);
				return false;
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
		D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
		UINT tempIncrement = 0;
		ID3D12DescriptorHeap *tempHeap = nullptr;
		if (!EnsurePreSkinDescriptorRingLocked(
				device, commandList, totalDescriptors,
				&tempCpuBase, &tempGpuBase, &tempIncrement, &tempHeap)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=descriptor_ring,count=%u",
				totalDescriptors);
			return false;
		}
		// Persist the only authoritative restore snapshot before issuing any
		// command-list state mutation. Restore never asks Binding State for a
		// second, potentially stale snapshot.
		replacement->drawSrvApplyState.originalBindings = snapshot;
		replacement->drawSrvApplyState.patchHeap = tempHeap;

		DX12InternalReplayScope internalReplay;
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE source = {};
			source.ptr = table.cpuStart;
			D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
			destination.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
			device->CopyDescriptorsSimple(
				table.count, destination, source,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		for (DX12ModIaReplacement::DrawSrvTablePatch &request :
				replacement->drawSrvPatches) {
			for (const DX12DrawSrvPatchTable &table : tables) {
				if (table.sampler ||
						table.compute != request.compute ||
						table.rootParameterIndex != request.binding.rootParameterIndex ||
						table.gpuStart.ptr != request.binding.tableGpuStart.ptr)
					continue;
				D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
				destination.ptr += static_cast<SIZE_T>(
					table.tempOffset + request.binding.descriptorOffset) * tempIncrement;
				device->CopyDescriptorsSimple(
					1, destination, request.replacementCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				request.targetCpu = destination;
#if defined(_DEBUG)
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patched,mode=mirrored_table,source=%s,"
					"slot=%u,root=%u,offset=%u,resource=%S,cpu=0x%zx",
					request.source.c_str(), request.binding.shaderRegister,
					request.binding.rootParameterIndex,
					request.binding.descriptorOffset,
					request.resourceName.c_str(), destination.ptr);
#endif
				break;
			}
		}
		replacement->drawSrvApplyState.applied = true;
		ID3D12DescriptorHeap *heaps[2] = { tempHeap, snapshot.samplerHeap };
		commandList->SetDescriptorHeaps(snapshot.samplerHeap ? 2 : 1, heaps);
		for (const DX12DrawSrvPatchTable &table : tables) {
			D3D12_GPU_DESCRIPTOR_HANDLE tableGpu = table.gpuStart;
			if (!table.sampler) {
				tableGpu = tempGpuBase;
				tableGpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
			}
			if (table.compute)
				commandList->SetComputeRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
			else
				commandList->SetGraphicsRootDescriptorTable(
					table.rootParameterIndex, tableGpu);
		}
#if defined(_DEBUG)
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=descriptors_patched,mode=mirrored_tables,requests=%zu,"
			"tables=%zu,descriptors=%u,present=%ld",
			replacement->drawSrvPatches.size(), tables.size(), totalDescriptors,
			DX12RuntimeState::GetPresentCount());
#endif
		return true;
	}

	static bool ApplyConfig(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const Bunny::TextureOverrideConfig &config,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement)
	{
		if (!device || !commandList || !replacement)
			return false;
		// A bundle cannot switch to a private descriptor heap unless its parent
		// direct list binds that exact heap. Defer support to ExecuteBundle replay;
		// do not record uploads or an incompatible SetDescriptorHeaps in a bundle.
		if (commandList->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,reason=bundle_requires_parent_replay");
			return false;
		}
		if (config.drawPsSrvResources.empty() &&
		    config.drawVsSrvResources.empty() &&
		    config.drawReplaceThisResource.empty())
			return false;

		std::vector<DX12CurrentShaderResourceBinding> graphicsSrvs;
		DX12DrawSrvReplacementUtils::CollectSrvBindings(commandList, false, &graphicsSrvs);

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawPsSrvResources.begin();
		     it != config.drawPsSrvResources.end(); ++it) {
			UINT added = DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
				device, commandList, graphicsSrvs, false, it->first, it->second,
				true, "ps-t", &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=ps-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(), graphicsSrvs.size());
			}
		}

		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawVsSrvResources.begin();
		     it != config.drawVsSrvResources.end(); ++it) {
			UINT added = DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
				device, commandList, graphicsSrvs, false, it->first, it->second,
				false, "vs-t", &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=vs-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(), graphicsSrvs.size());
			}
		}

		if (!config.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				config.drawReplaceThisResource, &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,hash=%08x,resource=%S,graphicsCount=%zu",
					static_cast<uint32_t>(config.hash),
					config.drawReplaceThisResource.c_str(), graphicsSrvs.size());
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static void Restore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement)
	{
		if (!replacement)
			return;
		DX12ModIaReplacement::DrawSrvApplyState &state =
			replacement->drawSrvApplyState;
		if (!commandList || !state.applied || !state.patchHeap) {
			replacement->drawSrvPatches.clear();
			state = DX12ModIaReplacement::DrawSrvApplyState();
			return;
		}
		const CommandListBindingState &snapshot = state.originalBindings;
		DX12InternalReplayScope internalReplay;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (snapshot.cbvSrvUavHeap)
			heaps[heapCount++] = snapshot.cbvSrvUavHeap;
		if (snapshot.samplerHeap)
			heaps[heapCount++] = snapshot.samplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (snapshot.graphicsTables[root].valid)
				commandList->SetGraphicsRootDescriptorTable(
					root, snapshot.graphicsTables[root].baseDescriptor);
			if (snapshot.computeTables[root].valid)
				commandList->SetComputeRootDescriptorTable(
					root, snapshot.computeTables[root].baseDescriptor);
		}
#if defined(_DEBUG)
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=descriptor_tables_restored,requests=%zu,snapshot=saved,present=%ld",
			replacement->drawSrvPatches.size(),
			DX12RuntimeState::GetPresentCount());
#endif
		replacement->drawSrvPatches.clear();
		state = DX12ModIaReplacement::DrawSrvApplyState();
	}
};
