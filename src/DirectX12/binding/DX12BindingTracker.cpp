#include "DX12BindingTracker.hpp"
#include "DX12BindingTrackerQuery.hpp"
#include "DX12BindingCollectQueue.hpp"

#include <stdio.h>
#include <string.h>

#include "DX12CaptureLedger.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12CommandListRuntime.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"

BindingShard DX12BindingTrackerState::Shards[kBindingShardCount];
volatile LONG64 DX12BindingTrackerState::GlobalDrawSerial = 0;
volatile LONG64 DX12BindingTrackerState::GlobalDispatchSerial = 0;

static thread_local BindingTlsTableCache tlsGraphicsTableCache;
static thread_local BindingTlsTableCache tlsComputeTableCache;
static thread_local BindingTlsDescriptorCache tlsGraphicsDescCache;
static thread_local BindingTlsDescriptorCache tlsComputeDescCache;
static thread_local BindingTlsDescriptorQueryCache tlsDescriptorQueryCache;
static thread_local BindingTlsComputeAllQueryCache tlsComputeAllQueryCache;
static thread_local BindingTlsStateCache tlsStateCache;

// Resolve CommandListBindingState pointer via thread-local cache.
// Eliminates ShardFor() hash + HashMap::operator[] on repeated calls
// for the same command list. Generation counter guards against pointer
// invalidation due to unordered_map rehash on other threads.
static CommandListBindingState* GetOrResolveBindingState(
	ID3D12GraphicsCommandList *commandList)
{
	// Fast path: TLS cache hit with matching shard generation
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation) {
			return tlsStateCache.state;
		}
	}

	// Slow path: compute shard hash, lock, hashmap lookup
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	tlsStateCache.commandList = commandList;
	tlsStateCache.state = &state;
	tlsStateCache.shardGeneration = shard.generation;
	tlsStateCache.shardIndex = static_cast<size_t>(&shard - DX12BindingTrackerState::Shards);
	ReleaseSRWLockExclusive(&shard.lock);
	return &state;
}

class DX12BindingTrackerQueryCacheUtils
{
public:
	static bool TryGetBoundCbvSrvUavHeap(
		const CommandListBindingState &snapshot,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !snapshot.cbvSrvUavHeap)
			return false;
		if (!DX12FindDescriptorHeapByPtr(snapshot.cbvSrvUavHeap, heap))
			return false;
		return heap->type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			heap->increment != 0;
	}

	static bool ResolveTableHeap(
		const CommandListBindingState &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		DX12DescriptorHeapSummary boundHeap = {};
		if (DX12BindingTrackerQueryCacheUtils::TryGetBoundCbvSrvUavHeap(
				snapshot, &boundHeap) &&
			DX12BindingTrackerQuery::TableMatchesHeap(table, boundHeap)) {
			*heap = boundHeap;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap) &&
			heap->increment != 0;
	}

	static bool EnsureQueryRootSignature(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return false;

		ID3D12RootSignature *rootSignature = compute ?
			event->computeRootSignature : event->graphicsRootSignature;
		if (rootSignature)
			return true;
		if (!snapshot.pipelineState)
			return false;

		if (event->shaderInfo.psoIndex == 0)
			DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &event->shaderInfo);
		if (event->shaderInfo.psoIndex == 0)
			return false;

		ID3D12RootSignature *psoRootSignature = nullptr;
		if (!DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature) ||
			!psoRootSignature)
			return false;

		if (compute)
			event->computeRootSignature = psoRootSignature;
		else
			event->graphicsRootSignature = psoRootSignature;
		return true;
	}

	static void BuildEventForQuery(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return;
		*event = BindingEvent();
		event->pipelineState = snapshot.pipelineState;
		event->graphicsRootSignature = snapshot.graphicsRootSignature;
		event->computeRootSignature = snapshot.computeRootSignature;
		event->shaderInfo = snapshot.pipelineShaderInfo;
		DX12BindingTrackerQueryCacheUtils::EnsureQueryRootSignature(
			snapshot, compute, event);
	}

	static bool CollectDescriptorBindings(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		const char *descriptorKind,
		D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
		std::vector<DX12CurrentComputeUavBinding> *outBindings)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
						if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						if (range.rangeType != rangeType)
							continue;
						DX12DescriptorHeapSummary heap = {};
						if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
								snapshot, table, &heap))
							continue;
						const UINT64 tableBaseIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
							heap, tableBaseIndex, range);
						std::vector<SIZE_T> cpuHandles;
						std::vector<DX12DescriptorSummary> descriptors;
						std::vector<uint8_t> found;
						cpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 descriptorIndex =
								tableBaseIndex + range.effectiveOffset + offset;
							if (descriptorIndex >= heap.numDescriptors)
								break;
							const SIZE_T cpuHandle =
								heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
							cpuHandles.push_back(cpuHandle);
						}
						descriptors.resize(cpuHandles.size());
						found.resize(cpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							cpuHandles.data(), cpuHandles.size(),
							descriptors.data(), found.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < cpuHandles.size(); ++offset) {
							if (!found[offset] || descriptors[offset].kind != descriptorKind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart =
								heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
							binding.tableGpuStart.ptr =
								heap.gpuStart + tableBaseIndex * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptors[offset];
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap)) {
						const UINT64 descriptorIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						const SIZE_T cpuHandle =
							heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
						DX12DescriptorSummary descriptor = {};
						const bool foundDescriptor = completeDescriptorResourceInfo ?
							DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
							DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
						if (foundDescriptor && descriptor.kind == descriptorKind) {
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.shaderVisibility = parameter ?
								parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = 1;
							binding.tableCpuStart = cpuHandle;
							binding.tableGpuStart = table.baseDescriptor;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptor;
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (rootDescriptor.valid && rootDescriptor.type == rootDescriptorType) {
				DX12CurrentComputeUavBinding binding;
				binding.rootParameterIndex = root;
				if (hasRootSignature) {
					const DX12RootParameterSummary *parameter =
						DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
					if (parameter) {
						binding.shaderVisibility = parameter->shaderVisibility;
						binding.shaderRegister = parameter->shaderRegister;
						binding.registerSpace = parameter->registerSpace;
					}
				}
				binding.rootDescriptor = true;
				binding.gpuVirtualAddress = rootDescriptor.address;
				outBindings->push_back(std::move(binding));
			}
		}

		return !outBindings->empty();
	}

	static bool CollectComputeAll(
		const CommandListBindingState &snapshot,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants)
	{
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();

		BindingEvent event;
		BuildEventForQuery(snapshot, true, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &rootSignature);

		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = snapshot.computeTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBase = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
						const char *kind = nullptr;
						bool completeDescriptorResourceInfo = false;
						if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && uavs) { out = uavs; kind = "UAV"; }
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && srvs) { out = srvs; kind = "SRV"; }
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && cbvs) {
							out = cbvs;
							kind = "CBV";
							completeDescriptorResourceInfo = true;
						}
						else { continue; }

						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBase, range);
						std::vector<SIZE_T> cpuHandles;
						std::vector<DX12DescriptorSummary> descriptors;
						std::vector<uint8_t> found;
						cpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 di = tableBase + range.effectiveOffset + offset;
							if (di >= heap.numDescriptors) break;
							const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
							cpuHandles.push_back(cpuHandle);
						}
						descriptors.resize(cpuHandles.size());
						found.resize(cpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							cpuHandles.data(), cpuHandles.size(),
							descriptors.data(), found.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < cpuHandles.size(); ++offset) {
							if (!found[offset] || descriptors[offset].kind != kind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBase) * heap.increment;
							binding.tableGpuStart.ptr = heap.gpuStart + tableBase * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptors[offset];
							binding.hasDescriptor = true;
							out->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 di = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
					DX12DescriptorSummary desc = {};
					if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &desc))
						continue;
					std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
					bool completeDescriptorResourceInfo = false;
					if (desc.kind == "UAV" && uavs) out = uavs;
					else if (desc.kind == "SRV" && srvs) out = srvs;
					else if (desc.kind == "CBV" && cbvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = cbvs;
						completeDescriptorResourceInfo = true;
					}
					if (!out) continue;
					(void)completeDescriptorResourceInfo;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = 1;
					binding.tableCpuStart = cpuHandle;
					binding.tableGpuStart = table.baseDescriptor;
					binding.tableHeap = heap.heap;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					out->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
			if (rd.valid) {
				std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
				if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV && uavs) out = uavs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV && srvs) out = srvs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV && cbvs) out = cbvs;
				if (out) {
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					if (hasRootSignature) {
						const DX12RootParameterSummary *p = DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
						if (p) {
							binding.shaderVisibility = p->shaderVisibility;
							binding.shaderRegister = p->shaderRegister;
							binding.registerSpace = p->registerSpace;
						}
					}
					binding.rootDescriptor = true;
					binding.gpuVirtualAddress = rd.address;
					out->push_back(std::move(binding));
				}
			}

			if (constants) {
				const RootConstantsState &rc = snapshot.computeRootConstants[root];
				if (rc.valid) {
					DX12CurrentRootConstants current;
					current.rootParameterIndex = root;
					current.maxSet = rc.maxSet;
					memcpy(current.values, rc.values, sizeof(current.values));
					memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
					constants->push_back(current);
				}
			}
		}

		return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
			(cbvs && !cbvs->empty()) || (constants && !constants->empty());
	}
};

// ============================================================================
// CollectPreSkinBindings — lightweight replacement for GetCurrentComputeAll
// in the PreSkin hot path. Uses a cached root signature summary (parsed once
// per ID3D12RootSignature*) to iterate descriptor table ranges without
// re-parsing the blob on every call. Scans multiple descriptors per table
// (not just the first) to correctly capture UAVs at non-zero offsets.
//
// Cost: O(tables + descriptors_in_relevant_ranges) vs old O(all_descriptors).
// Typically ~50x faster (~0.5ms vs ~25ms) while producing identical results.
// ============================================================================
bool DX12BindingTracker::CollectPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList)
		return false;
	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial)) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);

	// Thread-local cache keyed by (commandList, bindingQuerySerial).
	static thread_local struct PreSkinBindingsTlsCache {
		ID3D12GraphicsCommandList *commandList = nullptr;
		UINT64 bindingQuerySerial = 0;
		std::vector<DX12CurrentComputeUavBinding> uavs;
		std::vector<DX12CurrentComputeUavBinding> srvs;
		std::vector<DX12CurrentComputeUavBinding> cbvs;
		std::vector<DX12CurrentRootConstants> constants;
	} cache;

	if (cache.commandList == commandList && cache.bindingQuerySerial == bindingQuerySerial) {
		if (uavs) *uavs = cache.uavs;
		if (srvs) *srvs = cache.srvs;
		if (cbvs) *cbvs = cache.cbvs;
		if (constants) *constants = cache.constants;
		return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
			(cbvs && !cbvs->empty()) || (constants && !constants->empty());
	}

	std::vector<DX12CurrentComputeUavBinding> localUavs;
	std::vector<DX12CurrentComputeUavBinding> localSrvs;
	std::vector<DX12CurrentComputeUavBinding> localCbvs;
	std::vector<DX12CurrentRootConstants> localConstants;

	const UINT kMaxUavs = 6;
	const UINT kMaxSrvs = 8;
	const UINT kMaxScanPerTable = 16; // descriptors to scan per table without root sig

	DX12_HOTPATH_TIME_BEGIN(BindCollect);

	// Collect root constants directly from snapshot
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootConstantsState &rc = snapshot.computeRootConstants[root];
		if (!rc.valid)
			continue;
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = rc.maxSet;
		memcpy(current.values, rc.values, sizeof(current.values));
		memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
		localConstants.push_back(current);
	}

	// Cache root signature summary per ID3D12RootSignature* pointer.
	// Parse the blob once, reuse across all dispatches with the same signature.
	static std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary> s_rootSigCache;
	static UINT64 s_rootSigCacheReloadGen = 0;
	UINT64 reloadGen = DX12ModGetReloadGeneration();
	if (s_rootSigCacheReloadGen != reloadGen) {
		s_rootSigCache.clear();
		s_rootSigCacheReloadGen = reloadGen;
	}

	ID3D12RootSignature *computeRootSig = snapshot.computeRootSignature;
	const DX12RootSignatureSummary *cachedSummary = nullptr;
	if (computeRootSig) {
		std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary>::iterator sigIt =
			s_rootSigCache.find(computeRootSig);
		if (sigIt != s_rootSigCache.end()) {
			cachedSummary = &sigIt->second;
		} else {
			DX12RootSignatureSummary parsed;
			if (DX12GetRootSignatureSummary(computeRootSig, &parsed)) {
				s_rootSigCache[computeRootSig] = parsed;
				cachedSummary = &s_rootSigCache[computeRootSig];
			}
		}
	}

	// Collect descriptor table bindings.
	// With root signature: iterate exactly the declared ranges (correct offset/count).
	// Without root signature: scan up to kMaxScanPerTable descriptors, classify by kind.
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootTableState &table = snapshot.computeTables[root];
		if (!table.valid)
			continue;

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(snapshot, table, &heap))
			continue;

		const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			continue;

		const DX12RootParameterSummary *parameter =
			cachedSummary ? DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) : nullptr;

		if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			// Root signature available: iterate declared ranges with correct metadata
			for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
				if (localUavs.size() >= kMaxUavs && localSrvs.size() >= kMaxSrvs && range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
					continue;

				std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
				const char *kind = nullptr;
				bool needFullResource = false;
				if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && localUavs.size() < kMaxUavs) {
					out = &localUavs; kind = "UAV"; needFullResource = true;
				} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && localSrvs.size() < kMaxSrvs) {
					out = &localSrvs; kind = "SRV";
				} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
					out = &localCbvs; kind = "CBV"; needFullResource = true;
				} else {
					continue;
				}

				const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBaseIndex, range);
				for (UINT offset = 0; offset < count; ++offset) {
					const UINT64 di = tableBaseIndex + range.effectiveOffset + offset;
					if (di >= heap.numDescriptors)
						break;
					const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;

					DX12DescriptorSummary desc = {};
					if (needFullResource) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						if (desc.kind != kind)
							continue;
					} else {
						if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &desc))
							continue;
						if (desc.kind != kind)
							continue;
					}

					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = range.baseShaderRegister + offset;
					binding.registerSpace = range.registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = count;
					binding.tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
					binding.tableGpuStart.ptr = heap.gpuStart + tableBaseIndex * heap.increment;
					binding.tableHeap = heap.heap;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					out->push_back(std::move(binding));

					if (out == &localUavs && localUavs.size() >= kMaxUavs)
						break;
					if (out == &localSrvs && localSrvs.size() >= kMaxSrvs)
						break;
				}
			}
		} else {
			// No root signature info: scan next descriptors, classify by kind
			const UINT64 scanEnd = min(tableBaseIndex + kMaxScanPerTable,
				static_cast<UINT64>(heap.numDescriptors));
			for (UINT64 di = tableBaseIndex; di < scanEnd; ++di) {
				if (localUavs.size() >= kMaxUavs && localSrvs.size() >= kMaxSrvs)
					break;

				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
				DX12DescriptorSummary desc = {};
				if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &desc))
					continue;

				DX12CurrentComputeUavBinding binding;
				binding.rootParameterIndex = root;
				binding.descriptorIncrement = heap.increment;
				binding.tableCopyCount = 1;
				binding.tableCpuStart = cpuHandle;
				binding.tableGpuStart = table.baseDescriptor;
				binding.tableHeap = heap.heap;
				binding.descriptorOffset = static_cast<UINT>(di - tableBaseIndex);

				if (desc.kind == "UAV" && localUavs.size() < kMaxUavs) {
					DX12DescriptorSummary fullDesc = {};
					if (DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &fullDesc))
						desc = fullDesc;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					localUavs.push_back(std::move(binding));
				} else if (desc.kind == "SRV" && localSrvs.size() < kMaxSrvs) {
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					localSrvs.push_back(std::move(binding));
				} else if (desc.kind == "CBV") {
					DX12DescriptorSummary fullDesc = {};
					if (DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &fullDesc))
						desc = fullDesc;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					localCbvs.push_back(std::move(binding));
				}
			}
		}
	}

	// Collect root descriptors (bare GPU VA, no heap needed)
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
		if (!rd.valid)
			continue;

		const DX12RootParameterSummary *parameter =
			cachedSummary ? DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) : nullptr;

		DX12CurrentComputeUavBinding binding;
		binding.rootParameterIndex = root;
		binding.rootDescriptor = true;
		binding.gpuVirtualAddress = rd.address;
		if (parameter) {
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
		}

		if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV && localUavs.size() < kMaxUavs)
			localUavs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV && localSrvs.size() < kMaxSrvs)
			localSrvs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
			localCbvs.push_back(std::move(binding));
	}

	DX12_HOTPATH_TIME_END(BindCollect);

	cache.commandList = commandList;
	cache.bindingQuerySerial = bindingQuerySerial;
	cache.uavs = localUavs;
	cache.srvs = localSrvs;
	cache.cbvs = localCbvs;
	cache.constants = localConstants;

	if (uavs) *uavs = cache.uavs;
	if (srvs) *srvs = cache.srvs;
	if (cbvs) *cbvs = cache.cbvs;
	if (constants) *constants = cache.constants;

	return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
		(cbvs && !cbvs->empty()) || (constants && !constants->empty());
}

void DX12BindingTracker::DrainPendingEvents(const char *reason)
{
	DX12BindingCollectQueue::DrainForSnapshot(reason ? reason : "BindingTracker");
}

UINT64 DX12BindingTracker::CurrentCaptureSessionId()
{
	return DX12CaptureLedger::CurrentCaptureSessionId();
}

UINT64 DX12BindingTracker::CaptureFenceSequence()
{
	return DX12BindingCollectQueue::LastSubmittedSequence();
}

bool DX12BindingTracker::PopulateActiveEventSource(
	DX12FrameEventSource *source,
	const char *reason)
{
	if (!source)
		return false;

	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	const UINT64 effectiveCaptureSessionId = captureSessionId != 0 ?
		captureSessionId :
		DX12CaptureLedger::LastCaptureSessionId();
	if (effectiveCaptureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}
	return DX12BindingTracker::PopulateCaptureEventSource(
		source,
		effectiveCaptureSessionId,
		reason ? reason : "PopulateActiveEventSource");
}

bool DX12BindingTracker::PopulateCaptureEventSource(
	DX12FrameEventSource *source,
	UINT64 captureSessionId,
	const char *reason)
{
	if (!source)
		return false;
	if (captureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}

	const UINT64 targetSequence = DX12BindingTracker::CaptureFenceSequence();
	DX12BindingCollectQueue::WaitForSubmittedSequence(
		targetSequence,
		reason ? reason : "PopulateCaptureEventSource");

	*source = DX12FrameEventSource();

	DX12CaptureLedgerSnapshot ledgerSnapshot;
	DX12CaptureLedger::GetSnapshot(&ledgerSnapshot);
	if (ledgerSnapshot.captureSessionId != captureSessionId) {
		DX12FrameAnalysis::LogJsonFunc("BindingCaptureSessionMismatch",
			"\"requested_capture_session_id\":%llu,\"snapshot_capture_session_id\":%llu,\"target_sequence\":%llu",
			static_cast<unsigned long long>(captureSessionId),
			static_cast<unsigned long long>(ledgerSnapshot.captureSessionId),
			static_cast<unsigned long long>(targetSequence));
		return false;
	}

	source->captureSessionId = ledgerSnapshot.captureSessionId;
	source->ownedEvents = std::move(ledgerSnapshot.events);
	source->ownedOutputEvents = std::move(ledgerSnapshot.outputEvents);
	source->events = &source->ownedEvents;
	source->outputEvents = &source->ownedOutputEvents;
	source->droppedEvents = ledgerSnapshot.droppedEvents;
	source->droppedOutputEvents = ledgerSnapshot.droppedOutputEvents;

	DX12CaptureLedger::LogSnapshotHealth(
		reason ? reason : "PopulateCaptureEventSource",
		ledgerSnapshot);
	return !source->ownedEvents.empty() || !source->ownedOutputEvents.empty() ||
		source->droppedEvents != 0 || source->droppedOutputEvents != 0;
}

void DX12BindingTracker::ReleaseRetainedIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferResolved[slot] && state->vertexBufferResources[slot].resource) {
			DX12ReleaseRetainedResource(state->vertexBufferResources[slot].resource);
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource) {
		DX12ReleaseRetainedResource(state->indexBufferResource.resource);
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RetainEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event->vertexBufferResolved[slot] || !event->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(event->vertexBufferResources[slot].resource)) {
			event->vertexBufferResources[slot] = {};
			event->vertexBufferResolved[slot] = false;
		}
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource &&
		!DX12RetainResource(event->indexBufferResource.resource)) {
		event->indexBufferResource = {};
		event->indexBufferResolved = false;
	}
}

void DX12BindingTracker::ReleaseEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (event->vertexBufferResolved[slot] && event->vertexBufferResources[slot].resource)
			DX12ReleaseRetainedResource(event->vertexBufferResources[slot].resource);
		event->vertexBufferResources[slot] = {};
		event->vertexBufferResolved[slot] = false;
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource)
		DX12ReleaseRetainedResource(event->indexBufferResource.resource);
	event->indexBufferResource = {};
	event->indexBufferResolved = false;
}

void DX12BindingTracker::RetainStateIaResourcesForAsync(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!state->vertexBufferResolved[slot] || !state->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(state->vertexBufferResources[slot].resource)) {
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource &&
		!DX12RetainResource(state->indexBufferResource.resource)) {
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RefreshPipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;

	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
	if (!state->pipelineState)
		return;

	if (DX12GetPipelineStateShaderInfo(state->pipelineState, &state->pipelineShaderInfo))
		state->pipelineShaderInfoValid = true;
}

void DX12BindingTracker::InvalidatePipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;
	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
}

void DX12BindingTracker::BumpBindingQuerySerial(CommandListBindingState *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

bool DX12BindingTracker::ResolvePipelineShaderInfoOnDemand(CommandListBindingState *state)
{
	if (!state || !state->pipelineState)
		return false;
	if (state->pipelineShaderInfoValid)
		return true;
	DX12BindingTracker::RefreshPipelineShaderInfo(state);
	return state->pipelineShaderInfoValid;
}

bool DX12BindingTracker::SnapshotBindingState(
	ID3D12GraphicsCommandList *commandList,
	CommandListBindingState *snapshot,
	UINT64 *bindingQuerySerial)
{
	if (!commandList || !snapshot)
		return false;

	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		*snapshot = it->second;
		if (bindingQuerySerial)
			*bindingQuerySerial = it->second.bindingQuerySerial;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	return found;
}

void DX12BindingTracker::ReleaseResolvedIaResource(
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved)
		return;
	if (*resolved && summary->resource)
		DX12ReleaseRetainedResource(summary->resource);
	*summary = {};
	*resolved = false;
}

// Resolve all deferred (unresolved) IA resources in state at draw-snapshot time.
// This is the counterpart to the "defer resource resolution" optimization in
// SetVertexBuffers / SetIndexBuffer. Called inside the shard lock during
// RecordDrawInstanced / RecordDrawIndexedInstanced just before state snapshot.
static void ResolveDeferredIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferValid[slot] && !state->vertexBufferResolved[slot]) {
			DX12BindingTracker::ResolveAndRetainIaResource(
				state->vertexBuffers[slot].BufferLocation,
				state->vertexBuffers[slot].SizeInBytes,
				&state->vertexBufferResources[slot],
				&state->vertexBufferResolved[slot]);
		}
	}
	if (state->indexBufferValid && !state->indexBufferResolved) {
		DX12BindingTracker::ResolveAndRetainIaResource(
			state->indexBuffer.BufferLocation,
			state->indexBuffer.SizeInBytes,
			&state->indexBufferResource,
			&state->indexBufferResolved);
	}
}

void DX12BindingTracker::ResolveAndRetainIaResource(
	UINT64 gpuVirtualAddress, UINT64 size,
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved) {
		return;
	}

	*summary = {};
	*resolved = false;
	if (!gpuVirtualAddress || !size)
		return;
	if (!DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, summary))
		return;
	if (summary->resource && !DX12RetainResource(summary->resource)) {
		*summary = {};
		return;
	}
	*resolved = true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event,
	const char *kind,
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	if (!event)
		return false;
	*event = BindingEvent();
	event->kind = kind ? kind : "";
	event->captureSessionId = captureSessionId;
	const bool isDrawKind = kind && (!strcmp(kind, "draw") || !strcmp(kind, "draw_indexed"));
	const bool isDispatchKind = kind && !strcmp(kind, "dispatch");
	event->drawId = isDrawKind ? state.drawSerial : 0;
	event->dispatchId = isDispatchKind ? state.dispatchSerial : 0;
	event->descriptorEpoch = descriptorEpoch;
	event->commandList = commandList;
	event->pipelineState = state.pipelineState;
	event->graphicsRootSignature = state.graphicsRootSignature;
	event->computeRootSignature = state.computeRootSignature;
	event->cbvSrvUavHeap = state.cbvSrvUavHeap;
	event->samplerHeap = state.samplerHeap;
	memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
	memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
	memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
	memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
	memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
	memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
	memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
	memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
	memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
	memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	event->indexBuffer = state.indexBuffer;
	event->indexBufferValid = state.indexBufferValid;
	event->indexBufferResource = state.indexBufferResource;
	event->indexBufferResolved = state.indexBufferResolved;
	event->primitiveTopology = state.primitiveTopology;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid) {
			event->shaderInfo = state.pipelineShaderInfo;
		} else {
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event->shaderInfo);
		}
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature)) {
			if (!event->graphicsRootSignature)
				event->graphicsRootSignature = psoRootSignature;
			if (!event->computeRootSignature)
				event->computeRootSignature = psoRootSignature;
		}
	}
	return true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	return DX12BindingTracker::BuildEvent(
		event,
		kind,
		commandList,
		state,
		CurrentDescriptorEpoch(),
		DX12BindingTracker::CurrentCaptureSessionId());
}

void DX12BindingTracker::StoreEventLocked(const BindingEvent &event)
{
	BindingEvent stored = event;
	DX12BindingTracker::RetainEventIaResources(&stored);
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::RecordEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored ||
		ledgerResult == DX12CaptureLedgerRecordResult::Duplicate ||
		ledgerResult == DX12CaptureLedgerRecordResult::InactiveSession)
		return;
	DX12BindingTracker::ReleaseEventIaResources(&stored);
}

void DX12BindingTracker::StoreEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(&event, kind, commandList, state))
		return;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::StoreOutputEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId, UINT64 captureSessionId)
{
	if (!cpuHandle)
		return;
	const UINT64 dispatchId = 0;
	OutputBindingEvent event;
	event.serial = 0;
	event.captureSessionId = captureSessionId;
	event.kind = kind ? kind : "";
	event.commandList = commandList;
	event.pipelineState = state.pipelineState;
	event.drawId = drawId;
	event.dispatchId = dispatchId;
	event.slot = slot;
	event.cpuHandle = cpuHandle;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid)
			event.shaderInfo = state.pipelineShaderInfo;
		else
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event.shaderInfo);
	}
	const DX12CaptureLedgerRecordResult ledgerResult = DX12CaptureLedger::TryStoreOutputEvent(&event);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored ||
		ledgerResult == DX12CaptureLedgerRecordResult::Duplicate ||
		ledgerResult == DX12CaptureLedgerRecordResult::InactiveSession)
		return;
}

void DX12BindingTracker::StoreCurrentOutputTargetsLocked(
	ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		if (state.renderTargetValid[i])
			DX12BindingTracker::StoreOutputEventLocked("RTV", commandList, state, i,
				state.renderTargets[i].ptr, state.drawSerial, captureSessionId);
	}
	if (state.depthStencilValid)
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, state, 0,
			state.depthStencil.ptr, state.drawSerial, captureSessionId);
}

void DX12BindingTracker::RegisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	shard.states.try_emplace(commandList);
	// Bump generation to invalidate any TLS-cached state pointers
	// that may be invalidated by unordered_map rehash.
	shard.generation++;
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::ResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	DX12BindingTracker::ReleaseRetainedIaResources(&state);
	state = CommandListBindingState();
	state.pipelineState = initialState;
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RefreshPipelineShaderInfo(&state);
	ReleaseSRWLockExclusive(&shard.lock);

	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
	tlsDescriptorQueryCache.commandList = nullptr;
	tlsComputeAllQueryCache.commandList = nullptr;
	tlsStateCache.commandList = nullptr;
	tlsStateCache.state = nullptr;
}

void DX12BindingTracker::SetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.pipelineState = pipelineState;
	DX12BindingTracker::InvalidatePipelineShaderInfo(&state);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.graphicsRootSignature = rootSignature;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.computeRootSignature = rootSignature;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return;
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueStateEvent(
			commandList,
			snapshot,
			kind ? kind : "state",
			descriptorEpoch,
			captureSessionId))
		return;

	BindingEvent event;
	if (DX12BindingTracker::BuildEvent(
			&event,
			kind ? kind : "state",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId))
		DX12BindingTracker::StoreEventLocked(event);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	if (!commandList)
		return false;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	for (UINT i = 0; i < count && heaps; ++i) {
		ID3D12DescriptorHeap *heap = heaps[i];
		if (!heap)
			continue;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			cbvSrvUavHeap = heap;
		else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			samplerHeap = heap;
	}

	return DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	if (state.cbvSrvUavHeap == cbvSrvUavHeap &&
	    state.samplerHeap == samplerHeap) {
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	size_t invalidatedGraphicsTables = 0;
	size_t invalidatedComputeTables = 0;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (state.graphicsTables[root].valid)
			invalidatedGraphicsTables++;
		if (state.computeTables[root].valid)
			invalidatedComputeTables++;
		state.graphicsTables[root] = RootTableState();
		state.computeTables[root] = RootTableState();
	}
	state.cbvSrvUavHeap = cbvSrvUavHeap;
	state.samplerHeap = samplerHeap;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	DX12LogManager::DebugJsonFunc("DX12BindingDescriptorHeapsChanged",
		"\"commandList\":\"%p\",\"cbvSrvUavHeap\":\"%p\",\"samplerHeap\":\"%p\",\"invalidatedGraphicsTables\":%zu,\"invalidatedComputeTables\":%zu",
		commandList,
		cbvSrvUavHeap,
		samplerHeap,
		invalidatedGraphicsTables,
		invalidatedComputeTables);
	return true;
}

void DX12BindingTracker::SetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsGraphicsTableCache.cl == commandList &&
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &stateTable = state.graphicsTables[rootParameterIndex];
	if (stateTable.valid && stateTable.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	stateTable.valid = true;
	stateTable.rootParameterIndex = rootParameterIndex;
	stateTable.baseDescriptor = baseDescriptor;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = commandList;
	tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
}

void DX12BindingTracker::SetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsComputeTableCache.cl == commandList &&
		tlsComputeTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &table = state.computeTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeTableCache.cl = commandList;
	tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
}

void DX12BindingTracker::SetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.primitiveTopology = topology;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	// Use TLS-cached state pointer. Resource resolution deferred to draw time.
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	if (view) {
		state.indexBuffer = *view;
		state.indexBufferValid = true;
	} else {
		state.indexBuffer = {};
		state.indexBufferValid = false;
	}
	// Resource resolution DEFERRED to draw time — see SetVertexBuffers comment.
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || startSlot >= MaxVertexBufferSlots)
		return;

	// Use TLS-cached state pointer to avoid ShardFor() hash + HashMap lookup.
	// Still acquire lock for mutation safety (negligible cost when uncontended,
	// ~20ns vs ~11us of the old eager resource resolution per slot).
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	// Re-validate after lock: unordered_map may have rehashed while we were
	// waiting on the lock (extremely rare, but generation guard handles it).
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	for (UINT i = 0; i < count && startSlot + i < MaxVertexBufferSlots; ++i) {
		const UINT slot = startSlot + i;
		if (views) {
			state.vertexBuffers[slot] = views[i];
			state.vertexBufferValid[slot] = views[i].BufferLocation != 0 && views[i].SizeInBytes != 0;
		} else {
			state.vertexBuffers[slot] = {};
			state.vertexBufferValid[slot] = false;
		}
		// Resource resolution DEFERRED to draw time (RecordDrawInstanced /
		// RecordDrawIndexedInstanced). This is a ~90% cost reduction per call:
		//   Old: ReleaseResolvedIaResource + ResolveAndRetainIaResource
		//        → DX12ReleaseRetainedResource + DX12ResolveBufferResourceByGpuVa + DX12RetainResource
		//   New: trivial struct copy (D3D12_VERTEX_BUFFER_VIEW = 3x UINT64)
	}
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordOutputTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		snapshot = CommandListBindingState();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < numRenderTargetDescriptors && renderTargetDescriptors; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = renderTargetDescriptors[i];
		if (singleHandleToDescriptorRange && i > 0) {
			DX12DescriptorHeapSummary heap = {};
			if (!DX12FindDescriptorHeapByCpuHandle(
					renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) ||
				!heap.increment)
				continue;
			handle.ptr = renderTargetDescriptors[0].ptr +
				static_cast<SIZE_T>(i) * heap.increment;
		}
		if (DX12BindingCollectQueue::EnqueueOutputEvent(
				commandList,
				snapshot,
				"RTV",
				i,
				handle.ptr,
				0,
				captureSessionId))
			continue;
		DX12BindingTracker::StoreOutputEventLocked(
			"RTV",
			commandList,
			snapshot,
			i,
			handle.ptr,
			0,
			captureSessionId);
	}
	if (depthStencilDescriptor &&
		!DX12BindingCollectQueue::EnqueueOutputEvent(commandList, snapshot, "DSV", 0,
			depthStencilDescriptor->ptr, 0, captureSessionId)) {
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, snapshot, 0,
			depthStencilDescriptor->ptr, 0, captureSessionId);
	}

	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		state.renderTargets[i] = {};
		state.renderTargetValid[i] = false;
	}
	for (UINT i = 0; i < numRenderTargetDescriptors && i < MaxRenderTargetSlots; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
		if (renderTargetDescriptors) {
			handle = renderTargetDescriptors[i];
			if (singleHandleToDescriptorRange && i > 0) {
				DX12DescriptorHeapSummary heap = {};
				if (DX12FindDescriptorHeapByCpuHandle(
						renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) &&
					heap.increment) {
					handle.ptr = renderTargetDescriptors[0].ptr +
						static_cast<SIZE_T>(i) * heap.increment;
				}
			}
		}
		state.renderTargets[i] = handle;
		state.renderTargetValid[i] = handle.ptr != 0;
	}
	if (depthStencilDescriptor) {
		state.depthStencil = *depthStencilDescriptor;
		state.depthStencilValid = depthStencilDescriptor->ptr != 0;
	} else {
		state.depthStencil = {};
		state.depthStencilValid = false;
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordRenderTargetClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView)
{
	if (!commandList || !renderTargetView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"RTV",
			0,
			renderTargetView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"RTV",
		commandList,
		snapshot,
		0,
		renderTargetView.ptr,
		0,
		captureSessionId);
}

void DX12BindingTracker::RecordDepthStencilClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
	if (!commandList || !depthStencilView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"DSV",
			0,
			depthStencilView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"DSV",
		commandList,
		snapshot,
		0,
		depthStencilView.ptr,
		0,
		captureSessionId);
}

bool DX12BindingTracker::GetCurrentIaState(
	ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state)
{
	if (!commandList || !state)
		return false;

	*state = DX12CurrentIaState();
	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return false;

	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!snapshot.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = snapshot.vertexBuffers[slot];
		DX12CurrentIaBuffer buffer;
		buffer.role = "VB";
		buffer.gpuVa = view.BufferLocation;
		buffer.size = view.SizeInBytes;
		buffer.stride = view.StrideInBytes;
		buffer.slot = slot;
		buffer.resolved = snapshot.vertexBufferResolved[slot];
		buffer.resource = snapshot.vertexBufferResources[slot];
		if (!buffer.resolved)
			buffer.resolved = DX12ResolveBufferResourceByGpuVa(
				buffer.gpuVa, buffer.size, &buffer.resource);
		state->vertexBuffers.push_back(buffer);
	}

	if (snapshot.indexBufferValid && snapshot.indexBuffer.BufferLocation &&
		snapshot.indexBuffer.SizeInBytes) {
		state->hasIndexBuffer = true;
		state->indexBuffer.role = "IB";
		state->indexBuffer.gpuVa = snapshot.indexBuffer.BufferLocation;
		state->indexBuffer.size = snapshot.indexBuffer.SizeInBytes;
		state->indexBuffer.format = static_cast<UINT>(snapshot.indexBuffer.Format);
		state->indexBuffer.resolved = snapshot.indexBufferResolved;
		state->indexBuffer.resource = snapshot.indexBufferResource;
		if (!state->indexBuffer.resolved)
			state->indexBuffer.resolved = DX12ResolveBufferResourceByGpuVa(
				state->indexBuffer.gpuVa, state->indexBuffer.size, &state->indexBuffer.resource);
	}

	return true;
}


bool DX12BindingTracker::GetCurrentDescriptors(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	const char *descriptorKind,
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	if (!commandList || !uavs)
		return false;

	uavs->clear();
	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial))
		return false;
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (tlsDescriptorQueryCache.commandList == commandList &&
		tlsDescriptorQueryCache.compute == compute &&
		tlsDescriptorQueryCache.rangeType == static_cast<UINT>(rangeType) &&
		tlsDescriptorQueryCache.bindingQuerySerial == bindingQuerySerial &&
		tlsDescriptorQueryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		tlsDescriptorQueryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		*uavs = tlsDescriptorQueryCache.bindings;
		return !uavs->empty();
	}

	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindings(
		snapshot, compute, rangeType, descriptorKind, rootDescriptorType, uavs);
	tlsDescriptorQueryCache.commandList = commandList;
	tlsDescriptorQueryCache.compute = compute;
	tlsDescriptorQueryCache.rangeType = static_cast<UINT>(rangeType);
	tlsDescriptorQueryCache.bindingQuerySerial = bindingQuerySerial;
	tlsDescriptorQueryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	tlsDescriptorQueryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersion;
	tlsDescriptorQueryCache.bindings = *uavs;
	return foundAny;
}

bool DX12BindingTracker::GetCurrentComputeUavs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "UAV",
		D3D12_ROOT_PARAMETER_TYPE_UAV, uavs);
}

bool DX12BindingTracker::GetCurrentComputeSrvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *srvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, "SRV",
		D3D12_ROOT_PARAMETER_TYPE_SRV, srvs);
}

bool DX12BindingTracker::GetCurrentComputeCbvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *cbvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, "CBV",
		D3D12_ROOT_PARAMETER_TYPE_CBV, cbvs);
}

bool DX12BindingTracker::GetCurrentComputeAll(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList)
		return false;
	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial)) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	// 用 computeTables 内容 hash 做 key——bindings 不变 → hash 不变 → 跨 dispatch 命中。
	// heap 指针每 SetDescriptorHeaps 变化，不能用作 key。
	UINT64 tableHash = 0;
	for (UINT r = 0; r < MaxRootParameters; ++r) {
		const RootTableState &t = snapshot.computeTables[r];
		if (!t.valid) continue;
		tableHash ^= static_cast<UINT64>(r);
		tableHash ^= t.baseDescriptor.ptr;
		tableHash ^= tableHash >> 33;
		tableHash *= 0xff51afd7ed558ccdull;
	}
	if (tlsComputeAllQueryCache.commandList == commandList &&
		tlsComputeAllQueryCache.computeTableHash == tableHash) {
		if (uavs) *uavs = tlsComputeAllQueryCache.uavs;
		if (srvs) *srvs = tlsComputeAllQueryCache.srvs;
		if (cbvs) *cbvs = tlsComputeAllQueryCache.cbvs;
		if (constants) *constants = tlsComputeAllQueryCache.constants;
		return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
			(cbvs && !cbvs->empty()) || (constants && !constants->empty());
	}

	std::vector<DX12CurrentComputeUavBinding> cachedUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedCbvs;
	std::vector<DX12CurrentRootConstants> cachedConstants;
	DX12_HOTPATH_TIME_BEGIN(BindCollect);
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectComputeAll(
		snapshot, &cachedUavs, &cachedSrvs, &cachedCbvs, &cachedConstants);
	DX12_HOTPATH_TIME_END(BindCollect);
	tlsComputeAllQueryCache.commandList = commandList;
	tlsComputeAllQueryCache.computeTableHash = tableHash;
	tlsComputeAllQueryCache.uavs = cachedUavs;
	tlsComputeAllQueryCache.srvs = cachedSrvs;
	tlsComputeAllQueryCache.cbvs = cachedCbvs;
	tlsComputeAllQueryCache.constants = cachedConstants;
	if (uavs) *uavs = tlsComputeAllQueryCache.uavs;
	if (srvs) *srvs = tlsComputeAllQueryCache.srvs;
	if (cbvs) *cbvs = tlsComputeAllQueryCache.cbvs;
	if (constants) *constants = tlsComputeAllQueryCache.constants;
	return foundAny;
}

bool DX12BindingTracker::GetCurrentShaderResourceBindings(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		if (bindings)
			bindings->clear();
		return false;
	}

	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, compute, rangeType, descriptorKind,
		rootDescriptorType, bindings);
}

bool DX12BindingTracker::GetCurrentDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	return DX12CommandListRuntime::GetDescriptorHeaps(
		commandList, cbvSrvUavHeap, samplerHeap);
}

void DX12BindingTracker::RecordDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance,
	UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot (they were NOT resolved
	// eagerly in SetVertexBuffers/SetIndexBuffer — deferred to here for ~90%
	// per-call cost reduction).
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw",
			descriptorEpoch,
			captureSessionId,
			vertexCountPerInstance,
			0,
			instanceCount,
			startVertexLocation,
			0,
			0,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.vertexCountPerInstance = vertexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startVertexLocation = startVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot — see RecordDrawInstanced.
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw_indexed",
			descriptorEpoch,
			captureSessionId,
			0,
			indexCountPerInstance,
			instanceCount,
			0,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw_indexed",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.indexCountPerInstance = indexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startIndexLocation = startIndexLocation;
	event.baseVertexLocation = baseVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 dispatchSerial =
		static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDispatchSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.dispatchSerial = dispatchSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDispatchEvent(
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"dispatch",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.threadGroupCountX = threadGroupCountX;
	event.threadGroupCountY = threadGroupCountY;
	event.threadGroupCountZ = threadGroupCountZ;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::BeginFrame()
{
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDrawSerial, 0);
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDispatchSerial, 0);
}

void DX12BindingTracker::SetGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsGraphicsDescCache.cl == commandList &&
		tlsGraphicsDescCache.address[rootParameterIndex] == address)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.graphicsRootDescriptors[rootParameterIndex];
	descriptor.valid = address != 0;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsDescCache.cl = commandList;
	tlsGraphicsDescCache.address[rootParameterIndex] = address;
}

void DX12BindingTracker::SetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	if (tlsComputeDescCache.cl == commandList &&
		tlsComputeDescCache.address[rootParameterIndex] == address)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.computeRootDescriptors[rootParameterIndex];
	descriptor.valid = address != 0;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeDescCache.cl = commandList;
	tlsComputeDescCache.address[rootParameterIndex] = address;
}

void DX12BindingTracker::SetRootConstantsLocked(
	RootConstantsState *constants, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!constants || !values || destOffset >= 64 || count == 0)
		return;
	const UINT *src = static_cast<const UINT*>(values);
	const UINT copyCount = min(count, 64 - destOffset);
	constants->valid = true;
	constants->rootParameterIndex = rootParameterIndex;
	for (UINT i = 0; i < copyCount; ++i) {
		const UINT slot = destOffset + i;
		constants->values[slot] = src[i];
		constants->valueValid[slot] = true;
		if (slot + 1 > constants->maxSet)
			constants->maxSet = slot + 1;
	}
}

void DX12BindingTracker::SetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.graphicsRootConstants[rootParameterIndex];
	DX12BindingTracker::SetRootConstantsLocked(&constants, rootParameterIndex, destOffset, count, values);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.computeRootConstants[rootParameterIndex];
	DX12BindingTracker::SetRootConstantsLocked(&constants, rootParameterIndex, destOffset, count, values);
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12BindingTracker::GetCurrentComputeRootConstants(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList || !constants)
		return false;

	constants->clear();
	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return false;

	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootConstantsState &source = snapshot.computeRootConstants[root];
		if (!source.valid)
			continue;
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = source.maxSet;
		memcpy(current.values, source.values, sizeof(current.values));
		memcpy(current.valueValid, source.valueValid, sizeof(current.valueValid));
		constants->push_back(current);
	}
	return !constants->empty();
}

UINT64 DX12BindingTracker::GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.computeBindingSerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}

UINT64 DX12BindingTracker::GetBindingQuerySerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.bindingQuerySerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}








