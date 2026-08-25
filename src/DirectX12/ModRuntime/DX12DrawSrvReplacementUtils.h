#pragma once

// DX12DrawSrvReplacementUtils.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.


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


class DX12DrawSrvRootPlanTemplate
{
public:
	bool valid = false;
	bool sampler = false;
	bool dynamicCount = false;
	UINT fixedCount = 0;
};


class DX12DrawSrvSignaturePlanTemplate
{
public:
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 rootSignatureHash = 0;
	DX12DrawSrvRootPlanTemplate roots[MaxRootParameters] = {};
};


class DX12DrawSrvReplacementUtils
{
public:
	static void SyncDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		ID3D12DescriptorHeap *cbvSrvUavHeap,
		ID3D12DescriptorHeap *samplerHeap)
	{
		if (!commandList)
			return;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (cbvSrvUavHeap)
			heaps[heapCount++] = cbvSrvUavHeap;
		if (samplerHeap)
			heaps[heapCount++] = samplerHeap;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, cbvSrvUavHeap, samplerHeap);
		} else {
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, cbvSrvUavHeap, samplerHeap);
		}
		DX12BindingTracker::SetDescriptorHeaps(
			commandList, heapCount, heapCount ? heaps : nullptr);
	}

	static void SyncRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE table)
	{
		if (!commandList)
			return;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, !compute, rootParameterIndex, table);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, !compute, rootParameterIndex, table);
		}
		if (compute) {
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, rootParameterIndex, table);
		} else {
			DX12BindingTracker::SetGraphicsRootDescriptorTable(
				commandList, rootParameterIndex, table);
		}
	}

	static DX12DrawSrvSignaturePlanTemplate BuildSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature)
	{
		DX12DrawSrvSignaturePlanTemplate plan;
		plan.rootSignature = rootSignature.rootSignature;
		plan.rootSignatureHash = rootSignature.hash;
		for (const DX12RootParameterSummary &parameter :
				rootSignature.parameters) {
			if (parameter.rootParameterIndex >= MaxRootParameters)
				continue;
			DX12DrawSrvRootPlanTemplate &rootPlan =
				plan.roots[parameter.rootParameterIndex];
			if (parameter.parameterType !=
					D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
					parameter.ranges.empty()) {
				rootPlan = DX12DrawSrvRootPlanTemplate();
				continue;
			}
			bool hasSampler = false;
			bool hasResource = false;
			bool dynamicCount = false;
			UINT64 fixedCount = 0;
			for (const DX12RootDescriptorRangeSummary &range :
					parameter.ranges) {
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
					hasSampler = true;
				else
					hasResource = true;
				if (range.numDescriptors == UINT_MAX) {
					dynamicCount = true;
					continue;
				}
				const UINT64 count =
					static_cast<UINT64>(range.effectiveOffset) + range.numDescriptors;
				fixedCount = (std::max)(fixedCount, count);
			}
			if (hasSampler == hasResource)
				continue;
			rootPlan.valid = dynamicCount || fixedCount != 0;
			rootPlan.sampler = hasSampler;
			rootPlan.dynamicCount = dynamicCount;
			rootPlan.fixedCount = fixedCount > UINT_MAX ?
				UINT_MAX : static_cast<UINT>(fixedCount);
		}
		return plan;
	}

	static bool GetSignaturePlanTemplate(
		const DX12RootSignatureSummary &rootSignature,
		DX12DrawSrvSignaturePlanTemplate *plan)
	{
		if (!plan || !rootSignature.rootSignature)
			return false;

		static SRWLOCK s_planCacheLock = SRWLOCK_INIT;
		static std::unordered_map<ID3D12RootSignature*,
			DX12DrawSrvSignaturePlanTemplate> s_planCache;

		AcquireSRWLockShared(&s_planCacheLock);
		auto found = s_planCache.find(rootSignature.rootSignature);
		if (found != s_planCache.end() &&
		    found->second.rootSignatureHash == rootSignature.hash) {
			*plan = found->second;
			ReleaseSRWLockShared(&s_planCacheLock);
			return true;
		}
		ReleaseSRWLockShared(&s_planCacheLock);

		DX12DrawSrvSignaturePlanTemplate built =
			DX12DrawSrvReplacementUtils::BuildSignaturePlanTemplate(
				rootSignature);

		AcquireSRWLockExclusive(&s_planCacheLock);
		DX12DrawSrvSignaturePlanTemplate &stored =
			s_planCache[rootSignature.rootSignature];
		stored = built;
		*plan = stored;
		ReleaseSRWLockExclusive(&s_planCacheLock);
		return true;
	}

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
		return !binding.rootDescriptor &&
			binding.tableCpuStart != 0 &&
			binding.tableGpuStart.ptr != 0 &&
			binding.descriptorIncrement != 0 &&
			binding.tableHeap != nullptr;
	}

	static bool CollectSrvBindingsFromRuntimeLayout(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		std::vector<DX12CurrentShaderResourceBinding> *bindings)
	{
		if (!bindings)
			return false;
		bindings->clear();
		if (!commandList)
			return false;

		// thread_local: SnapshotBindingState fully overwrites the object on
		// success, so zero-initializing the ~60KB NSDMI arrays on every call
		// is wasted work on this per-draw query path.
		static thread_local CommandListBindingState snapshot;
		if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
			return false;

		ID3D12RootSignature *rootSignature =
			compute ? snapshot.computeRootSignature : snapshot.graphicsRootSignature;
		if (!rootSignature)
			return false;

		DX12RootSignatureSummary rootSignatureSummary;
		if (!DX12GetRootSignatureSummary(rootSignature, &rootSignatureSummary) ||
		    !rootSignatureSummary.parsed) {
			return false;
		}

		UINT estimatedBindingCount = 0;
		for (const DX12RootParameterSummary &parameter :
				rootSignatureSummary.parameters) {
			if (parameter.parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
			    parameter.rootParameterIndex >= MaxRootParameters) {
				continue;
			}
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
					continue;
				if (range.numDescriptors == UINT_MAX)
					continue;
				estimatedBindingCount += range.numDescriptors;
			}
		}
		if (estimatedBindingCount)
			bindings->reserve(estimatedBindingCount);

		for (const DX12RootParameterSummary &parameter :
				rootSignatureSummary.parameters) {
			if (parameter.parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
			    parameter.rootParameterIndex >= MaxRootParameters) {
				continue;
			}

			const RootTableState &table =
				compute ?
					snapshot.computeTables[parameter.rootParameterIndex] :
					snapshot.graphicsTables[parameter.rootParameterIndex];
			if (!table.valid)
				continue;

			DX12DescriptorHeapSummary heap;
			UINT64 tableBaseIndex = 0;
			SIZE_T tableCpuStart = 0;
			if (!ResolvePatchTableLocation(
					snapshot,
					table,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
					&heap,
					&tableBaseIndex,
					&tableCpuStart)) {
				continue;
			}

			const UINT tableCopyCount = DescriptorCountForTable(
				parameter,
				heap,
				tableBaseIndex,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
					continue;
				const UINT64 first = tableBaseIndex + range.effectiveOffset;
				if (first >= heap.numDescriptors)
					continue;
				const UINT64 available = heap.numDescriptors - first;
				const UINT64 declared = range.numDescriptors == UINT_MAX ?
					available : range.numDescriptors;
				const UINT count = static_cast<UINT>((std::min)(declared, available));
				for (UINT offset = 0; offset < count; ++offset) {
					DX12CurrentShaderResourceBinding binding;
					binding.rootParameterIndex = parameter.rootParameterIndex;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = range.baseShaderRegister + offset;
					binding.registerSpace = range.registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = tableCopyCount;
					binding.tableCpuStart = tableCpuStart;
					binding.tableGpuStart = table.baseDescriptor;
					binding.tableHeap = heap.heap;
					bindings->push_back(std::move(binding));
				}
			}
		}

		return !bindings->empty();
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
		if (!DX12ModNeedsShaderDescriptorTracking() &&
		    DX12DrawSrvReplacementUtils::CollectSrvBindingsFromRuntimeLayout(
				commandList, compute, bindings)) {
			return;
		}
		DX12BindingTracker::GetCurrentShaderResourceBindings(
			commandList, compute, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bindings);
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

	static UINT AddSlotRequestsByRegister(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests,
		size_t *bindingCount = nullptr)
	{
		if (bindingCount)
			*bindingCount = 0;
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;

		std::vector<DX12CurrentShaderResourceBinding> bindings;
		if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
				commandList,
				compute,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				slot,
				0,
				&bindings)) {
			return 0;
		}
		if (bindingCount)
			*bindingCount = bindings.size();
		return DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
			device, commandList, bindings, compute, slot, resourceName,
			pixelStage, source, requests);
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

	static const DX12RootParameterSummary *FindRootParameterSummary(
		const DX12RootSignatureSummary &rootSignature,
		UINT rootParameterIndex)
	{
		for (const DX12RootParameterSummary &candidate :
				rootSignature.parameters) {
			if (candidate.rootParameterIndex == rootParameterIndex)
				return &candidate;
		}
		return nullptr;
	}

	static bool ResolvePatchTableLocation(
		const CommandListBindingState &snapshot,
		const RootTableState &state,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		DX12DescriptorHeapSummary *heap,
		UINT64 *tableBaseIndex,
		SIZE_T *tableCpuStart)
	{
		if (!heap || !tableBaseIndex || !tableCpuStart || !state.valid)
			return false;
		*heap = DX12DescriptorHeapSummary();
		*tableBaseIndex = 0;
		*tableCpuStart = 0;

		ID3D12DescriptorHeap *boundHeap =
			type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
				snapshot.samplerHeap : snapshot.cbvSrvUavHeap;
		if (state.runtimeHeapResolved &&
		    state.runtimeHeapType == static_cast<UINT>(type) &&
		    state.runtimeHeapIncrement != 0 &&
		    state.runtimeHeap != nullptr &&
		    state.runtimeHeap == boundHeap &&
		    state.runtimeTableBaseIndex < state.runtimeHeapNumDescriptors) {
			heap->heap = state.runtimeHeap;
			heap->type = state.runtimeHeapType;
			heap->numDescriptors = state.runtimeHeapNumDescriptors;
			heap->cpuStart = state.runtimeHeapCpuStart;
			heap->gpuStart = state.runtimeHeapGpuStart;
			heap->increment = state.runtimeHeapIncrement;
			heap->version = state.runtimeHeapVersion;
			*tableBaseIndex = state.runtimeTableBaseIndex;
			*tableCpuStart = state.runtimeTableCpuStart;
			return true;
		}

		if (!ResolveTableHeap(state, type, heap) ||
		    !boundHeap ||
		    heap->heap != boundHeap ||
		    !heap->cpuStart ||
		    state.baseDescriptor.ptr < heap->gpuStart) {
			return false;
		}
		const UINT64 tableDelta = state.baseDescriptor.ptr - heap->gpuStart;
		if (tableDelta % heap->increment != 0)
			return false;
		*tableBaseIndex = tableDelta / heap->increment;
		if (*tableBaseIndex >= heap->numDescriptors)
			return false;
		*tableCpuStart =
			heap->cpuStart +
			static_cast<SIZE_T>(*tableBaseIndex) * heap->increment;
		return true;
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
		DX12DrawSrvSignaturePlanTemplate cachedPlan;
		if (!DX12DrawSrvReplacementUtils::GetSignaturePlanTemplate(
				rootSignature, &cachedPlan))
			return false;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &state = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (!state.valid)
				continue;
			const DX12DrawSrvRootPlanTemplate &rootPlan =
				cachedPlan.roots[root];
			if (!rootPlan.valid) {
				return false;
			}
			const D3D12_DESCRIPTOR_HEAP_TYPE type = rootPlan.sampler ?
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER :
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			DX12DescriptorHeapSummary heap = {};
			UINT64 tableBaseIndex = 0;
			SIZE_T tableCpuStart = 0;
			if (!ResolvePatchTableLocation(
					snapshot, state, type,
					&heap, &tableBaseIndex, &tableCpuStart)) {
				return false;
			}
			UINT count = 0;
			if (rootPlan.dynamicCount) {
				const DX12RootParameterSummary *parameter =
					DX12DrawSrvReplacementUtils::FindRootParameterSummary(
						rootSignature, root);
				if (!parameter) {
					return false;
				}
				count = DescriptorCountForTable(
					*parameter, heap, tableBaseIndex, type);
			} else if (tableBaseIndex < heap.numDescriptors) {
				const UINT64 available = heap.numDescriptors - tableBaseIndex;
				const UINT64 clampedCount = (std::min)(
					static_cast<UINT64>(rootPlan.fixedCount),
					available);
				count = clampedCount > UINT_MAX ?
					UINT_MAX : static_cast<UINT>(clampedCount);
			}
			if (!count) {
				return false;
			}
			DX12DrawSrvPatchTable table;
			table.compute = compute;
			table.rootParameterIndex = root;
			table.sampler = rootPlan.sampler;
			table.cpuStart = tableCpuStart;
			table.gpuStart = state.baseDescriptor;
			table.heap = heap.heap;
			table.increment = heap.increment;
			table.count = count;
			if (!rootPlan.sampler) {
				if (*totalDescriptors > MaxMirroredDescriptors ||
						count > MaxMirroredDescriptors - *totalDescriptors) {
					return false;
				}
				table.tempOffset = *totalDescriptors;
				*totalDescriptors += count;
			}
			tables->push_back(table);
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
		const LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
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
		const LARGE_INTEGER planBegin = DX12OuterTimingProbeUtils::Now();
		// thread_local: see the note above for why this is safe and cheaper.
		static thread_local CommandListBindingState snapshot;
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
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"build_mirror_plan",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(planBegin));

		D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
		D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
		UINT tempIncrement = 0;
		ID3D12DescriptorHeap *tempHeap = nullptr;
		const LARGE_INTEGER allocateBegin = DX12OuterTimingProbeUtils::Now();
		if (!EnsurePreSkinDescriptorRingLocked(
				device, commandList, totalDescriptors,
				&tempCpuBase, &tempGpuBase, &tempIncrement, &tempHeap)) {
			DX12LogManager::Fields("DX12DrawSrvReplacement",
				"stage=table_patch_fail,reason=descriptor_ring,count=%u",
				totalDescriptors);
			return false;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"allocate_mirror_range",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(allocateBegin));
		// Persist the only authoritative restore snapshot before issuing any
		// command-list state mutation. Restore never asks Binding State for a
		// second, potentially stale snapshot.
		replacement->drawSrvApplyState.originalBindings = snapshot;
		replacement->drawSrvApplyState.patchHeap = tempHeap;

		DX12InternalReplayScope internalReplay;
		const LARGE_INTEGER copyBegin = DX12OuterTimingProbeUtils::Now();
		// Batch every table-mirror copy into one CopyDescriptors call. The
		// destinations are disjoint slots in the freshly allocated temp ring
		// range and the sources live in other heaps, so merging the ranges is
		// identical to issuing one CopyDescriptorsSimple per table.
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopyDestStarts;
		static thread_local std::vector<UINT> tlsCopyDestSizes;
		static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsCopySrcStarts;
		static thread_local std::vector<UINT> tlsCopySrcSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copyDestStarts = tlsCopyDestStarts;
		std::vector<UINT> &copyDestSizes = tlsCopyDestSizes;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &copySrcStarts = tlsCopySrcStarts;
		std::vector<UINT> &copySrcSizes = tlsCopySrcSizes;
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
		for (const DX12DrawSrvPatchTable &table : tables) {
			if (table.sampler)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE source = {};
			source.ptr = table.cpuStart;
			D3D12_CPU_DESCRIPTOR_HANDLE destination = tempCpuBase;
			destination.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
			copyDestStarts.push_back(destination);
			copyDestSizes.push_back(table.count);
			copySrcStarts.push_back(source);
			copySrcSizes.push_back(table.count);
		}
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		copyDestStarts.clear();
		copyDestSizes.clear();
		copySrcStarts.clear();
		copySrcSizes.clear();
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
				copyDestStarts.push_back(destination);
				copyDestSizes.push_back(1);
				copySrcStarts.push_back(request.replacementCpu);
				copySrcSizes.push_back(1);
				request.targetCpu = destination;
				break;
			}
		}
		// Flush after the mirror batch so patched slots still overwrite the
		// mirrored originals, exactly like the sequential calls did.
		if (!copyDestStarts.empty()) {
			device->CopyDescriptors(
				static_cast<UINT>(copyDestStarts.size()), copyDestStarts.data(),
				copyDestSizes.data(),
				static_cast<UINT>(copySrcStarts.size()), copySrcStarts.data(),
				copySrcSizes.data(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"copy_mirror_descriptors",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(copyBegin));
		replacement->drawSrvApplyState.applied = true;
		const LARGE_INTEGER bindBegin = DX12OuterTimingProbeUtils::Now();
		ID3D12DescriptorHeap *heaps[2] = { tempHeap, snapshot.samplerHeap };
		commandList->SetDescriptorHeaps(snapshot.samplerHeap ? 2 : 1, heaps);
		SyncDescriptorHeaps(commandList, tempHeap, snapshot.samplerHeap);
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
			SyncRootDescriptorTable(
				commandList, table.compute, table.rootParameterIndex, tableGpu);
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"bind_mirror_tables",
			0,
			tables.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(bindBegin));
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"apply_total",
			0,
			totalDescriptors,
			DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
		return true;
	}

	static bool ApplyConfig(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const Bunny::TextureOverrideConfig &config,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
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

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawPsSrvResources.begin();
		     it != config.drawPsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					true, "ps-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					true, "ps-t", &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=ps-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawVsSrvResources.begin();
		     it != config.drawVsSrvResources.end(); ++it) {
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false, it->first, it->second,
					false, "vs-t", &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false, it->first, it->second,
					false, "vs-t", &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=vs-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
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
					config.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static bool ApplyCompiledPlan(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12CompiledTextureOverridePlan &plan,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement,
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvs = nullptr)
	{
		if (!device || !commandList || !replacement)
			return false;
		if (plan.drawSrvResources.empty() &&
		    plan.drawReplaceThisResource.empty())
			return false;

		const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
			graphicsSrvs;

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (const DX12CompiledDrawSrvResourceBinding &binding :
				plan.drawSrvResources) {
			const char *source = binding.pixelStage ? "ps-t" : "vs-t";
			size_t bindingCount = 0;
			UINT added = bindingsView ?
				DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
					device, commandList, *bindingsView, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests) :
				DX12DrawSrvReplacementUtils::AddSlotRequestsByRegister(
					device, commandList, false,
					binding.slot, binding.resource,
					binding.pixelStage, source, &requests, &bindingCount);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=%s,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					source,
					binding.slot,
					binding.resource.c_str(),
					bindingsView ? bindingsView->size() : bindingCount);
			}
		}

		if (!plan.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				plan.drawReplaceThisResource, &requests);
			if (!added) {
				DX12LogManager::Fields("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,resource=%S,graphicsCount=%zu",
					plan.drawReplaceThisResource.c_str(),
					bindingsView ? bindingsView->size() : 0);
			}
		}

		return DX12DrawSrvReplacementUtils::QueueRequests(requests, replacement);
	}

	static void Restore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement)
	{
		const LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
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
		SyncDescriptorHeaps(
			commandList, snapshot.cbvSrvUavHeap, snapshot.samplerHeap);
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (snapshot.graphicsTables[root].valid) {
				commandList->SetGraphicsRootDescriptorTable(
					root, snapshot.graphicsTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, false, root,
					snapshot.graphicsTables[root].baseDescriptor);
			}
			if (snapshot.computeTables[root].valid) {
				commandList->SetComputeRootDescriptorTable(
					root, snapshot.computeTables[root].baseDescriptor);
				SyncRootDescriptorTable(
					commandList, true, root,
					snapshot.computeTables[root].baseDescriptor);
			}
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"DrawSrv",
			"restore_original_tables",
			0,
			static_cast<size_t>(snapshot.graphicsTableValidCount) +
				snapshot.computeTableValidCount,
			DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
		replacement->drawSrvPatches.clear();
		state = DX12ModIaReplacement::DrawSrvApplyState();
	}
};
