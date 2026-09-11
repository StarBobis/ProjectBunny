// DX12BindingTracker: sharded binding state, runtime snapshots and query caches.
// Split from DX12BindingTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <chrono>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


std::unordered_set<DX12CaptureOutputKey, DX12CaptureOutputKeyHash>
	DX12CaptureLedger::OutputKeys;


BindingShard DX12BindingTrackerState::Shards[kBindingShardCount];
volatile LONG64 DX12BindingTrackerState::GlobalDrawSerial = 0;
volatile LONG64 DX12BindingTrackerState::GlobalDispatchSerial = 0;

static thread_local BindingTlsTableCache tlsGraphicsTableCache;
static thread_local BindingTlsTableCache tlsComputeTableCache;
static thread_local BindingTlsDescriptorCache tlsGraphicsDescCache;
static thread_local BindingTlsDescriptorCache tlsComputeDescCache;
static thread_local BindingTlsDescriptorQueryCache
	tlsDescriptorQueryCaches[BindingTlsDescriptorQueryCache::CacheSlotCount];
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

static bool RuntimeSlotIdentityMatches(
	UINT64 currentVersion,
	UINT64 currentValueId,
	UINT64 cachedVersion,
	UINT64 cachedValueId)
{
	// A stable descriptor Value ID survives repeated copies of the same
	// value, while the mutation version churns whenever the game rewrites
	// its descriptor arena (e.g. ZZZ re-copies ring descriptors every
	// frame). When the cache captured a non-zero value identity, the
	// semantic content is proven identical by the value id alone, so the
	// ring-slot version is allowed to rotate. Fall back to comparing both
	// version and value id when the cached slot has no authoritative value.
	if (cachedValueId != 0)
		return currentValueId == cachedValueId;
	return currentVersion == cachedVersion &&
		currentValueId == cachedValueId;
}

static bool RuntimeResolveHeapRecord(
	const RootTableState &table,
	UINT slot,
	DescriptorHeapRecord::StateLocks **stateLocks)
{
	if (!stateLocks ||
			!table.runtimeHeap || !table.runtimeHeapCpuStart ||
			table.runtimeHeapIncrement == 0 ||
			slot >= table.runtimeHeapNumDescriptors)
		return false;
	const SIZE_T cpuHandle = table.runtimeHeapCpuStart +
		static_cast<SIZE_T>(slot) * table.runtimeHeapIncrement;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved = {};
	if (!DX12DescriptorHeapShadowUtils::HeapCacheLookup(
			cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&resolved) ||
			resolved.slot != slot ||
			!resolved.heap || !resolved.heap->stateLocks)
		return false;
	*stateLocks = resolved.heap->stateLocks.get();
	return true;
}

static bool RuntimeReadSlotIdentitiesFast(
	const DX12DescriptorHeapSummary &heapSummary,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds)
{
	if (!heapSummary.heap || !heapSummary.cpuStart ||
			heapSummary.increment == 0 || count == 0 ||
			!slots || !versions || !valueIds)
		return false;
	// Resolve the heap record once through the lock-free CPU-handle cache,
	// then read every slot directly through the per-slot seqlock. No global
	// descriptor lock is taken on this path.
	const SIZE_T firstCpu = heapSummary.cpuStart +
		static_cast<SIZE_T>(slots[0]) * heapSummary.increment;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved = {};
	if (!DX12DescriptorHeapShadowUtils::HeapCacheLookup(
			firstCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			&resolved) ||
			!resolved.heap || !resolved.heap->stateLocks)
		return false;
	for (size_t index = 0; index < count; ++index) {
		const UINT slot = slots[index];
		if (slot >= heapSummary.numDescriptors)
			return false;
		DescriptorHeapRecord::StateLocks::SlotIdentity identity = {};
		if (!resolved.heap->stateLocks->ObserveSlotIdentity(
				slot, &identity))
			return false;
		versions[index] = identity.version;
		valueIds[index] = identity.valueId;
	}
	return true;
}

static bool RuntimeDescriptorRootsMatch(
	const DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return false;
	const DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const bool checkAll = !runtimeState->computeRootUsedMaskValid;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (!checkAll &&
				!(runtimeState->computeRootUsedMask & (1ull << root)))
			continue;
		const RootDescriptorState &descriptor =
			runtimeState->computeRootDescriptors[root];
		const bool valid = descriptor.valid;
		if (valid != cache.bindingsRootValid[root])
			return false;
		if (valid && descriptor.address != cache.bindingsRootIdentities[root])
			return false;
	}
	return true;
}

static bool RuntimeConstantsMatch(
	const DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return false;
	const DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const bool checkAll = !runtimeState->computeRootUsedMaskValid;
	// Every cached constant must still match the live root constant state.
	for (const DX12CurrentRootConstants &cached : cache.rootConstants) {
		const UINT root = cached.rootParameterIndex;
		if (root >= MaxRootParameters)
			return false;
		const RootConstantsState &current =
			runtimeState->computeRootConstants[root];
		if (!current.valid ||
				current.maxSet != cached.maxSet ||
				memcmp(current.values, cached.values,
					sizeof(current.values)) != 0 ||
				memcmp(current.valueValid, cached.valueValid,
					sizeof(current.valueValid)) != 0)
			return false;
	}
	// A used root that gained constants absent from the cache invalidates it.
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		if (!checkAll &&
				!(runtimeState->computeRootUsedMask & (1ull << root)))
			continue;
		if (!runtimeState->computeRootConstants[root].valid)
			continue;
		bool found = false;
		for (const DX12CurrentRootConstants &cached : cache.rootConstants) {
			if (cached.rootParameterIndex == root) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

bool DX12BindingTracker::ValidateRuntimePreSkinBindingState(
	const DX12SimulateContext *runtimeState)
{
	return RuntimeDescriptorRootsMatch(runtimeState) &&
		RuntimeConstantsMatch(runtimeState);
}

static void RebaseRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState)
{
	if (!runtimeState)
		return;
	DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const auto rebase = [runtimeState](
		std::vector<DX12CurrentComputeUavBinding> &bindings) {
		for (DX12CurrentComputeUavBinding &binding : bindings) {
			if (!binding.hasDescriptor || binding.rootDescriptor ||
					binding.rootParameterIndex >= MaxRootParameters)
				continue;
			const RootTableState &table =
				runtimeState->computeRootTables[binding.rootParameterIndex];
			if (!table.valid || !table.runtimeHeapResolved ||
					table.runtimeHeapIncrement == 0)
				continue;
			binding.tableHeap = table.runtimeHeap;
			binding.tableCpuStart = table.runtimeTableCpuStart;
			binding.tableGpuStart.ptr = table.runtimeTableGpuStart;
			binding.descriptorIncrement = table.runtimeHeapIncrement;
			binding.descriptor.cpuHandle = table.runtimeTableCpuStart +
				static_cast<SIZE_T>(binding.descriptorOffset) *
					table.runtimeHeapIncrement;
		}
	};
	rebase(cache.uavs);
	rebase(cache.srvs);
	rebase(cache.cbvs);
}

static bool TryGetRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	(void)computeBindingSerial;
	if (!runtimeState || !runtimeState->resolvedCompute.bindingsValid ||
			runtimeState->resolvedCompute.bindingsHeap != runtimeState->cbvSrvUavHeap ||
			runtimeState->resolvedCompute.bindingsRootSignature !=
				runtimeState->computeRootSignature)
		return false;

	// DX12-style semantic validation: root descriptors must match exactly
	// (a bare GPU VA has no value identity), root constants must match by
	// content, and descriptor-table content is proven by per-slot value
	// identities read at the CURRENT ring locations. The previous dirty-mask
	// / binding-serial checks invalidated on every re-bind even when the game
	// bound identical descriptors at a rotated ring position, forcing a full
	// re-collect per dispatch (DX11-style deferred state inspection).
	if (!RuntimeDescriptorRootsMatch(runtimeState) ||
			!RuntimeConstantsMatch(runtimeState))
		return false;

	DX12SimulateContextComputeTemplate &cache =
		runtimeState->resolvedCompute;
	const size_t observedCount = cache.bindingsObservedSlots.size();
	if (observedCount != cache.observedSlots.size() ||
			observedCount != cache.observedSlotVersions.size() ||
			observedCount != cache.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!cache.bindingsHeap)
			return false;
		// A rotated table invalidates the "no descriptor writes since last
		// check" shortcut: the new slots were never validated, so their
		// content must be read even when the global serial is unchanged.
		bool tablesUnchanged = true;
		for (size_t index = 0; index < observedCount; ++index) {
			const UINT root = cache.bindingsObservedSlots[index].root;
			if (root >= MaxRootParameters)
				return false;
			const RootTableState &table = runtimeState->computeRootTables[root];
			const UINT64 cachedBase = cache.bindingsRootTableBases[root];
			const UINT64 currentBase = table.valid ?
				table.baseDescriptor.ptr : 0;
			if (currentBase != cachedBase) {
				tablesUnchanged = false;
				break;
			}
		}
		const UINT64 descriptorQuerySerialBefore =
			CurrentDescriptorQuerySerial();
		if (cache.bindingsDescriptorQuerySerial !=
				descriptorQuerySerialBefore ||
				!tablesUnchanged) {
			std::array<DescriptorHeapRecord::StateLocks*,
				MaxRootParameters> stateLocksByRoot = {};
			for (size_t index = 0; index < observedCount; ++index) {
				const DX12SimulateContextComputeTemplate::RuntimeObservedSlot
					&observed = cache.bindingsObservedSlots[index];
				if (observed.root >= MaxRootParameters)
					return false;
				const RootTableState &table =
					runtimeState->computeRootTables[observed.root];
				if (!table.valid || !table.runtimeHeapResolved ||
						table.runtimeHeapIncrement == 0)
					return false;
				const UINT64 currentSlot64 =
					table.runtimeTableBaseIndex + observed.offset;
				if (currentSlot64 >= table.runtimeHeapNumDescriptors)
					return false;
				const UINT currentSlot = static_cast<UINT>(currentSlot64);
				DescriptorHeapRecord::StateLocks *stateLocks =
					stateLocksByRoot[observed.root];
				if (!stateLocks &&
						!RuntimeResolveHeapRecord(
							table, currentSlot, &stateLocks))
					return false;
				stateLocksByRoot[observed.root] = stateLocks;
				UINT64 currentVersion = 0;
				UINT64 currentValueId = 0;
				DescriptorHeapRecord::StateLocks::SlotIdentity identity = {};
				if (!stateLocks->ObserveSlotIdentity(
						currentSlot, &identity))
					return false;
				currentVersion = identity.version;
				currentValueId = identity.valueId;
				if (!RuntimeSlotIdentityMatches(
						currentVersion,
						currentValueId,
						observed.version,
						observed.valueId)) {
					return false;
				}
			}
			const UINT64 descriptorQuerySerialAfter =
				CurrentDescriptorQuerySerial();
			if (descriptorQuerySerialAfter != descriptorQuerySerialBefore)
				return false;
			cache.bindingsDescriptorQuerySerial =
				descriptorQuerySerialAfter;
			for (size_t index = 0; index < observedCount; ++index) {
				const UINT root = cache.bindingsObservedSlots[index].root;
				cache.bindingsRootTableBases[root] =
					runtimeState->computeRootTables[root].baseDescriptor.ptr;
			}
			}
	}

	// The ring may have rotated since the collection. The cached bindings are
	// semantically identical (value IDs matched), so rebase their CPU/GPU
	// table locations to the current tables before handing them out.
	RebaseRuntimePreSkinBindings(runtimeState);

	if (uavs)
		*uavs = &cache.uavs;
	if (srvs)
		*srvs = &cache.srvs;
	if (cbvs)
		*cbvs = &cache.cbvs;
	if (constants)
		*constants = &cache.rootConstants;
	if (dependencyFingerprint)
		*dependencyFingerprint = cache.bindingsDependencyFingerprint;
	if (foundAny)
		*foundAny = cache.bindingsFoundAny;
	return true;
}

static bool TryLoadRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	const std::vector<DX12CurrentComputeUavBinding> *cachedUavs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedSrvs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedCbvs = nullptr;
	const std::vector<DX12CurrentRootConstants> *cachedConstants = nullptr;
	if (!TryGetRuntimePreSkinBindings(
			runtimeState,
			computeBindingSerial,
			&cachedUavs,
			&cachedSrvs,
			&cachedCbvs,
			&cachedConstants,
			dependencyFingerprint,
			foundAny))
		return false;
	if (uavs && cachedUavs)
		*uavs = *cachedUavs;
	if (srvs && cachedSrvs)
		*srvs = *cachedSrvs;
	if (cbvs && cachedCbvs)
		*cbvs = *cachedCbvs;
	if (constants && cachedConstants)
		*constants = *cachedConstants;
	return true;
}

static void StoreRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	UINT64 descriptorQuerySerial,
	UINT64 dependencyFingerprint,
	bool usesRootValues,
	bool foundAny,
	const std::vector<UINT> &observedRootParameters,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		&observedSlotsRelative,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &constants)
{
	if (!runtimeState)
		return;
	runtimeState->resolvedCompute.bindingsValid = true;
	runtimeState->resolvedCompute.bindingsFoundAny = foundAny;
	runtimeState->resolvedCompute.bindingsUseRootValues = usesRootValues;
	runtimeState->resolvedCompute.bindingsDescriptorVersion =
		runtimeState->computeDescriptorBindingVersion;
	runtimeState->resolvedCompute.bindingsDescriptorQuerySerial =
		descriptorQuerySerial;
	runtimeState->resolvedCompute.bindingsDependencyFingerprint =
		dependencyFingerprint;
	runtimeState->resolvedCompute.bindingsHeap = runtimeState->cbvSrvUavHeap;
	runtimeState->resolvedCompute.bindingsRootSignature =
		runtimeState->computeRootSignature;
	// Snapshot the exact descriptor-root identity (bare GPU VA roots have no
	// content-addressed value identity, so they must match exactly on hit).
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootDescriptorState &descriptor =
			runtimeState->computeRootDescriptors[root];
		runtimeState->resolvedCompute.bindingsRootValid[root] =
			descriptor.valid;
		runtimeState->resolvedCompute.bindingsRootIdentities[root] =
			descriptor.valid ? descriptor.address : 0;
		runtimeState->resolvedCompute.bindingsRootTableBases[root] =
			runtimeState->computeRootTables[root].valid ?
				runtimeState->computeRootTables[root].baseDescriptor.ptr : 0;
	}
	runtimeState->resolvedCompute.bindingsRootRevisions.resize(
		observedRootParameters.size());
	for (size_t index = 0; index < observedRootParameters.size(); ++index) {
		const UINT root = observedRootParameters[index];
		runtimeState->resolvedCompute.bindingsRootRevisions[index] =
			root < runtimeState->computeRootBindingRevisions.size() ?
				runtimeState->computeRootBindingRevisions[root] : 0;
	}
	runtimeState->resolvedCompute.observedSlots = observedSlots;
	runtimeState->resolvedCompute.observedSlotVersions = observedSlotVersions;
	runtimeState->resolvedCompute.observedSlotValueIds = observedSlotValueIds;
	if (observedSlotsRelative.size() == observedSlots.size()) {
		runtimeState->resolvedCompute.bindingsObservedSlots =
			observedSlotsRelative;
	} else {
		runtimeState->resolvedCompute.bindingsObservedSlots.clear();
	}
	runtimeState->resolvedCompute.uavs = uavs;
	runtimeState->resolvedCompute.srvs = srvs;
	runtimeState->resolvedCompute.cbvs = cbvs;
	runtimeState->resolvedCompute.rootConstants = constants;

	// DXVK-style ResetDirtyTracking: the cache now reflects every binding
	// this root signature uses, so those dirty bits are consumed. Bits for
	// root parameters outside the used mask (churn on tables the shader does
	// not reference) are left set; they never invalidate the cache and any
	// future collection pass clears them naturally. When no parsed root
	// signature is available the mask is not authoritative, so leave it
	// untouched (the serial fallback governs).
	if (runtimeState->computeRootUsedMaskValid) {
		runtimeState->computeRootDirtyMask &=
			~runtimeState->computeRootUsedMask;
	}
}

bool DX12BindingTracker::GetSimulateContextPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	if (uavs)
		*uavs = nullptr;
	if (srvs)
		*srvs = nullptr;
	if (cbvs)
		*cbvs = nullptr;
	if (constants)
		*constants = nullptr;
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (foundAny)
		*foundAny = false;
	if (!commandList)
		return false;

	DX12SimulateContext *context =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (!context || !context->computeBindingSerial)
		return false;

	bool cachedFoundAny = false;
	const bool loaded = TryGetRuntimePreSkinBindings(
		context,
		context->computeBindingSerial,
		uavs,
		srvs,
		cbvs,
		constants,
		dependencyFingerprint,
		&cachedFoundAny);
	if (foundAny)
		*foundAny = cachedFoundAny;
	return loaded;
}

static bool TryLoadRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	std::vector<DX12CurrentComputeUavBinding> *bindings,
	bool *foundAny)
{
	if (!runtimeState || !bindings)
		return false;
	(void)bindingQuerySerial;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return false;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	if (!cache.valid ||
			cache.compute != compute ||
			cache.rangeType != static_cast<UINT>(rangeType) ||
			cache.bindingsHeap != runtimeState->cbvSrvUavHeap)
		return false;
	// Validate the exact root-binding identity instead of revisions/serial:
	// games like ZZZ re-bind root tables on every draw even when binding the
	// same descriptors, so revision/serial-based validation misses on every
	// draw and forces a full re-collect (snapshot copy + descriptor resolve +
	// locks) per draw. Same root signature + same root table/descriptor
	// identity + same observed slot versions proves the collected bindings
	// are identical.
	const ID3D12RootSignature *currentRootSignature = compute ?
		runtimeState->computeRootSignature :
		runtimeState->graphicsRootSignature;
	if (cache.rootSignature != currentRootSignature)
		return false;
	const RootTableState *currentTables = compute ?
		runtimeState->computeRootTables :
		runtimeState->graphicsRootTables;
	const RootDescriptorState *currentRootDescriptors = compute ?
		runtimeState->computeRootDescriptors :
		runtimeState->graphicsRootDescriptors;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const bool currentValid =
			currentTables[root].valid ||
			currentRootDescriptors[root].valid;
		if (currentValid != cache.rootBindingValid[root])
			return false;
		if (!currentValid)
			continue;
		const UINT64 currentIdentity = currentTables[root].valid ?
			currentTables[root].baseDescriptor.ptr :
			currentRootDescriptors[root].address;
		if (currentIdentity != cache.rootBindingIdentities[root])
			return false;
	}

	const size_t observedCount = cache.observedSlots.size();
	if (observedCount != cache.observedSlotVersions.size() ||
			observedCount != cache.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!cache.bindingsHeap)
			return false;
		const UINT64 descriptorQuerySerialBefore =
			CurrentDescriptorQuerySerial();
		if (cache.descriptorQuerySerial != descriptorQuerySerialBefore) {
			constexpr size_t StackIdentityCount = 64;
			std::array<UINT64, StackIdentityCount> stackSlotVersions;
			std::array<UINT64, StackIdentityCount> stackSlotValueIds;
			std::vector<UINT64> heapSlotVersions;
			std::vector<UINT64> heapSlotValueIds;
			UINT64 *currentSlotVersions = stackSlotVersions.data();
			UINT64 *currentSlotValueIds = stackSlotValueIds.data();
			if (observedCount > StackIdentityCount) {
				heapSlotVersions.resize(observedCount);
				heapSlotValueIds.resize(observedCount);
				currentSlotVersions = heapSlotVersions.data();
				currentSlotValueIds = heapSlotValueIds.data();
			}
			if (!DX12GetDescriptorSlotIdentities(
					cache.bindingsHeap,
					cache.observedSlots.data(),
					observedCount,
					currentSlotVersions,
					currentSlotValueIds))
				return false;
			for (size_t index = 0; index < observedCount; ++index) {
				if (!RuntimeSlotIdentityMatches(
						currentSlotVersions[index],
						currentSlotValueIds[index],
						cache.observedSlotVersions[index],
						cache.observedSlotValueIds[index])) {
					return false;
				}
			}
			const UINT64 descriptorQuerySerialAfter =
				CurrentDescriptorQuerySerial();
			if (descriptorQuerySerialAfter != descriptorQuerySerialBefore)
				return false;
			cache.descriptorQuerySerial = descriptorQuerySerialAfter;
		}
	}

	*bindings = cache.bindings;
	if (foundAny)
		*foundAny = cache.foundAny;
	return true;
}

static void StoreRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	bool foundAny,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12CurrentComputeUavBinding> &bindings)
{
	if (!runtimeState)
		return;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	cache.valid = true;
	cache.foundAny = foundAny;
	cache.compute = compute;
	cache.rangeType = static_cast<UINT>(rangeType);
	cache.bindingQuerySerial = bindingQuerySerial;
	cache.descriptorQuerySerial = CurrentDescriptorQuerySerial();
	cache.bindingsHeap = runtimeState->cbvSrvUavHeap;
	// Snapshot the exact root-binding identity for validation (see
	// TryLoadRuntimeDescriptorBindings): revisions/serial bump on every
	// re-bind, which defeats the cache in binding-churn games.
	cache.rootSignature = compute ?
		runtimeState->computeRootSignature :
		runtimeState->graphicsRootSignature;
	{
		const RootTableState *tables = compute ?
			runtimeState->computeRootTables :
			runtimeState->graphicsRootTables;
		const RootDescriptorState *rootDescriptors = compute ?
			runtimeState->computeRootDescriptors :
			runtimeState->graphicsRootDescriptors;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const bool tableValid = tables[root].valid;
			const bool descriptorValid = rootDescriptors[root].valid;
			cache.rootBindingValid[root] = tableValid || descriptorValid;
			cache.rootBindingIdentities[root] = tableValid ?
				tables[root].baseDescriptor.ptr :
				(descriptorValid ? rootDescriptors[root].address : 0);
		}
	}
	cache.rootParameters.clear();
	cache.rootRevisions.clear();
	cache.usesRootRevisions = !bindings.empty();
	if (cache.usesRootRevisions) {
		const std::array<UINT64, MaxRootParameters> &revisions = compute ?
			runtimeState->computeRootBindingRevisions :
			runtimeState->graphicsRootBindingRevisions;
		std::array<bool, MaxRootParameters> rootSeen = {};
		for (const DX12CurrentComputeUavBinding &binding : bindings) {
			if (binding.rootParameterIndex >= revisions.size()) {
				cache.usesRootRevisions = false;
				cache.rootParameters.clear();
				cache.rootRevisions.clear();
				break;
			}
			if (rootSeen[binding.rootParameterIndex])
				continue;
			rootSeen[binding.rootParameterIndex] = true;
			cache.rootParameters.push_back(binding.rootParameterIndex);
			cache.rootRevisions.push_back(
				revisions[binding.rootParameterIndex]);
		}
	}
	cache.observedSlots = observedSlots;
	cache.observedSlotVersions = observedSlotVersions;
	cache.observedSlotValueIds = observedSlotValueIds;
	cache.bindings = bindings;
}

static UINT64 GetRuntimeDescriptorQuerySerial(
	const DX12SimulateContext *runtimeState,
	bool compute)
{
	if (!runtimeState)
		return 0;
	const UINT64 serial = compute ?
		runtimeState->computeDescriptorQuerySerial :
		runtimeState->graphicsDescriptorQuerySerial;
	return serial ? serial : 1;
}

static bool BuildRuntimeBindingSnapshot(
	const DX12SimulateContext *runtimeState,
	CommandListBindingState *snapshot)
{
	if (!runtimeState || !snapshot)
		return false;

	// Reconstruct in place: assigning a default-constructed temporary would
	// zero the ~60KB of NSDMI arrays once for the temporary and again copy
	// them over *snapshot on this per-query hot path.
	std::destroy_at(snapshot);
	new (snapshot) CommandListBindingState();
	snapshot->pipelineState = runtimeState->pipelineState;
	snapshot->graphicsRootSignature = runtimeState->graphicsRootSignature;
	snapshot->computeRootSignature = runtimeState->computeRootSignature;
	snapshot->cbvSrvUavHeap = runtimeState->cbvSrvUavHeap;
	snapshot->samplerHeap = runtimeState->samplerHeap;
	snapshot->runtimeCbvSrvUavHeapResolved =
		runtimeState->runtimeCbvSrvUavHeapResolved;
	snapshot->runtimeCbvSrvUavHeap = runtimeState->runtimeCbvSrvUavHeap;
	snapshot->runtimeSamplerHeapResolved =
		runtimeState->runtimeSamplerHeapResolved;
	snapshot->runtimeSamplerHeap = runtimeState->runtimeSamplerHeap;
	snapshot->computeBindingSerial = runtimeState->computeBindingSerial;
	snapshot->bindingQuerySerial = runtimeState->bindingQuerySerial;
	snapshot->graphicsTableValidCount = runtimeState->graphicsRootTableValidCount;
	snapshot->computeTableValidCount = runtimeState->computeRootTableValidCount;
	memcpy(snapshot->graphicsTables,
		runtimeState->graphicsRootTables,
		sizeof(snapshot->graphicsTables));
	memcpy(snapshot->computeTables,
		runtimeState->computeRootTables,
		sizeof(snapshot->computeTables));
	memcpy(snapshot->graphicsRootDescriptors,
		runtimeState->graphicsRootDescriptors,
		sizeof(snapshot->graphicsRootDescriptors));
	memcpy(snapshot->computeRootDescriptors,
		runtimeState->computeRootDescriptors,
		sizeof(snapshot->computeRootDescriptors));
	memcpy(snapshot->graphicsRootConstants,
		runtimeState->graphicsRootConstants,
		sizeof(snapshot->graphicsRootConstants));
	memcpy(snapshot->computeRootConstants,
		runtimeState->computeRootConstants,
		sizeof(snapshot->computeRootConstants));
	snapshot->primitiveTopology = runtimeState->ia.primitiveTopology;
	if (runtimeState->ia.hasIndexBuffer) {
		snapshot->indexBuffer = runtimeState->ia.indexBuffer;
		snapshot->indexBufferValid = true;
		snapshot->indexBufferResource = runtimeState->ia.indexBufferResource;
		snapshot->indexBufferResolved = runtimeState->ia.indexBufferResolved;
	}
	for (UINT slot = 0; slot < ARRAYSIZE(runtimeState->ia.vertexBuffers); ++slot) {
		if (!runtimeState->ia.hasVertexBuffer[slot])
			continue;
		snapshot->vertexBuffers[slot] = runtimeState->ia.vertexBuffers[slot];
		snapshot->vertexBufferValid[slot] = true;
		snapshot->vertexBufferResources[slot] =
			runtimeState->ia.vertexBufferResources[slot];
		snapshot->vertexBufferResolved[slot] =
			runtimeState->ia.vertexBufferResolved[slot];
	}
	return true;
}

// Compute-only snapshot consumed by the PreSkin collector. Keeping this a
// dedicated type makes the contract explicit: this path only observes the
// compute root signature / tables / descriptors / constants and the bound
// CBV/SRV/UAV heap. The full CommandListBindingState (graphics tables, IA
// views, render targets, ...) is intentionally never copied here.
struct DX12PreSkinComputeSnapshot
{
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	UINT64 computeBindingSerial = 0;
	UINT64 bindingQuerySerial = 1;
	UINT computeTableValidCount = 0;
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
};

static bool BuildRuntimeComputeBindingSnapshot(
	const DX12SimulateContext *runtimeState,
	DX12PreSkinComputeSnapshot *snapshot)
{
	if (!runtimeState || !snapshot)
		return false;
	snapshot->pipelineState = runtimeState->pipelineState;
	snapshot->computeRootSignature = runtimeState->computeRootSignature;
	snapshot->cbvSrvUavHeap = runtimeState->cbvSrvUavHeap;
	snapshot->runtimeCbvSrvUavHeapResolved =
		runtimeState->runtimeCbvSrvUavHeapResolved;
	snapshot->runtimeCbvSrvUavHeap = runtimeState->runtimeCbvSrvUavHeap;
	snapshot->computeBindingSerial = runtimeState->computeBindingSerial;
	snapshot->bindingQuerySerial = runtimeState->bindingQuerySerial;
	snapshot->computeTableValidCount = runtimeState->computeRootTableValidCount;
	memcpy(snapshot->computeTables,
		runtimeState->computeRootTables,
		sizeof(snapshot->computeTables));
	memcpy(snapshot->computeRootDescriptors,
		runtimeState->computeRootDescriptors,
		sizeof(snapshot->computeRootDescriptors));
	memcpy(snapshot->computeRootConstants,
		runtimeState->computeRootConstants,
		sizeof(snapshot->computeRootConstants));
	return true;
}

static void FillPreSkinComputeSnapshotFromFull(
	const CommandListBindingState &src,
	DX12PreSkinComputeSnapshot *dst)
{
	if (!dst)
		return;
	dst->pipelineState = src.pipelineState;
	dst->computeRootSignature = src.computeRootSignature;
	dst->cbvSrvUavHeap = src.cbvSrvUavHeap;
	dst->runtimeCbvSrvUavHeapResolved = src.runtimeCbvSrvUavHeapResolved;
	dst->runtimeCbvSrvUavHeap = src.runtimeCbvSrvUavHeap;
	dst->computeBindingSerial = src.computeBindingSerial;
	dst->bindingQuerySerial = src.bindingQuerySerial;
	dst->computeTableValidCount = src.computeTableValidCount;
	memcpy(dst->computeTables,
		src.computeTables,
		sizeof(dst->computeTables));
	memcpy(dst->computeRootDescriptors,
		src.computeRootDescriptors,
		sizeof(dst->computeRootDescriptors));
	memcpy(dst->computeRootConstants,
		src.computeRootConstants,
		sizeof(dst->computeRootConstants));
}

class DX12BindingTrackerQueryCacheUtils
{
public:
	static bool ReadDescriptorSlotIdentities(
		ID3D12DescriptorHeap *heap,
		const UINT *slots,
		size_t count,
		UINT64 *versions,
		UINT64 *valueIds,
		const DX12DescriptorHeapSummary *summary = nullptr)
	{
		if (summary && RuntimeReadSlotIdentitiesFast(
				*summary, slots, count, versions, valueIds))
			return true;
		return DX12GetDescriptorSlotIdentities(
			heap, slots, count, versions, valueIds);
	}

	static void FillRuntimeTableFromHeapSummary(
		RootTableState *table,
		const DX12DescriptorHeapSummary &heap)
	{
		if (!table || !table->valid ||
				heap.increment == 0 || heap.gpuStart == 0 ||
				table->baseDescriptor.ptr < heap.gpuStart)
			return;

		const UINT64 tableBaseIndex =
			(table->baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			return;

		table->runtimeHeapResolved = true;
		table->runtimeHeap = heap.heap;
		table->runtimeHeapType = heap.type;
		table->runtimeHeapCpuStart = heap.cpuStart;
		table->runtimeHeapGpuStart = heap.gpuStart;
		table->runtimeHeapIncrement = heap.increment;
		table->runtimeHeapNumDescriptors = heap.numDescriptors;
		table->runtimeHeapVersion = heap.version;
		table->runtimeTableBaseIndex = tableBaseIndex;
		table->runtimeTableCpuStart =
			heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		table->runtimeTableGpuStart =
			heap.gpuStart + tableBaseIndex * heap.increment;
	}

	static void InvalidateRuntimeTable(
		RootTableState *table)
	{
		if (!table)
			return;

		table->runtimeHeapResolved = false;
		table->runtimeHeap = nullptr;
		table->runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		table->runtimeHeapCpuStart = 0;
		table->runtimeHeapGpuStart = 0;
		table->runtimeHeapIncrement = 0;
		table->runtimeHeapNumDescriptors = 0;
		table->runtimeHeapVersion = 0;
		table->runtimeTableBaseIndex = 0;
		table->runtimeTableCpuStart = 0;
		table->runtimeTableGpuStart = 0;
	}

	template <class Snapshot>
	static void RefreshRuntimeTable(
		const Snapshot &snapshot,
		RootTableState *table)
	{
		if (!table || !table->valid)
			return;

		DX12BindingTrackerQueryCacheUtils::InvalidateRuntimeTable(table);

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
				snapshot, *table, &heap))
			return;
		DX12BindingTrackerQueryCacheUtils::FillRuntimeTableFromHeapSummary(
			table, heap);
	}

	static bool MakeHeapSummaryFromRuntimeTable(
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid || !table.runtimeHeapResolved ||
				table.runtimeHeapIncrement == 0 ||
				table.runtimeTableBaseIndex >= table.runtimeHeapNumDescriptors)
			return false;

		heap->heap = table.runtimeHeap;
		heap->type = table.runtimeHeapType;
		heap->numDescriptors = table.runtimeHeapNumDescriptors;
		heap->cpuStart = table.runtimeHeapCpuStart;
		heap->gpuStart = table.runtimeHeapGpuStart;
		heap->increment = table.runtimeHeapIncrement;
		heap->version = table.runtimeHeapVersion;
		return true;
	}

	template <class Snapshot>
	static bool TryGetBoundCbvSrvUavHeap(
		const Snapshot &snapshot,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !snapshot.cbvSrvUavHeap)
			return false;
		if (snapshot.runtimeCbvSrvUavHeapResolved &&
				snapshot.runtimeCbvSrvUavHeap.heap == snapshot.cbvSrvUavHeap &&
				snapshot.runtimeCbvSrvUavHeap.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				snapshot.runtimeCbvSrvUavHeap.increment != 0) {
			*heap = snapshot.runtimeCbvSrvUavHeap;
			return true;
		}
		if (!DX12FindDescriptorHeapByPtr(snapshot.cbvSrvUavHeap, heap))
			return false;
		return heap->type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			heap->increment != 0;
	}

	static void StoreRuntimeHeapSummaries(
		CommandListBindingState *state,
		const DX12DescriptorHeapSummary *cbvSrvUavHeap,
		bool cbvSrvUavResolved,
		const DX12DescriptorHeapSummary *samplerHeap,
		bool samplerResolved)
	{
		if (!state)
			return;
		state->runtimeCbvSrvUavHeapResolved = cbvSrvUavResolved;
		state->runtimeCbvSrvUavHeap =
			(cbvSrvUavResolved && cbvSrvUavHeap) ?
			*cbvSrvUavHeap : DX12DescriptorHeapSummary();
		state->runtimeSamplerHeapResolved = samplerResolved;
		state->runtimeSamplerHeap =
			(samplerResolved && samplerHeap) ?
			*samplerHeap : DX12DescriptorHeapSummary();
	}

	static void RefreshAllRuntimeTables(CommandListBindingState *state)
	{
		if (!state)
			return;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state->graphicsTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->graphicsTables[root]);
			if (state->computeTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->computeTables[root]);
		}
	}

	template <class Snapshot>
	static bool ResolveTableHeap(
		const Snapshot &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
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

	template <class Snapshot>
	static bool ResolveRuntimeTableHeap(
		const Snapshot &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
		return DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
			snapshot, table, heap);
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
		// Callers always pass a freshly default-constructed event, so there is
		// no need to reset it here; doing so would zero ~60KB of arrays twice.
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
		std::vector<DX12CurrentComputeUavBinding> *outBindings,
		ID3D12DescriptorHeap **observedSlotHeap = nullptr,
		std::vector<UINT> *observedSlots = nullptr)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		if (observedSlotHeap)
			*observedSlotHeap = nullptr;
		if (observedSlots)
			observedSlots->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
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
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						cpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 descriptorIndex =
								tableBaseIndex + range.effectiveOffset + offset;
							if (descriptorIndex >= heap.numDescriptors)
								break;
							if (observedSlots)
								observedSlots->push_back(static_cast<UINT>(descriptorIndex));
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
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						if (observedSlots && descriptorIndex < heap.numDescriptors)
							observedSlots->push_back(static_cast<UINT>(descriptorIndex));
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

	static bool CollectDescriptorBindingsByRegister(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
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
		if (!hasRootSignature) {
			std::vector<DX12CurrentComputeUavBinding> allBindings;
			if (!CollectDescriptorBindings(
					snapshot, compute, rangeType, descriptorKind,
					rootDescriptorType, &allBindings))
				return false;
			for (const DX12CurrentComputeUavBinding &binding : allBindings) {
				if (binding.shaderRegister != shaderRegister ||
				    binding.registerSpace != registerSpace)
					continue;
				outBindings->push_back(binding);
			}
			return !outBindings->empty();
		}

		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			const DX12RootParameterSummary *parameter =
				DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
			if (table.valid && parameter &&
			    parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType != rangeType ||
					    range.registerSpace != registerSpace ||
					    shaderRegister < range.baseShaderRegister)
						continue;
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBaseIndex =
						(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
						heap, tableBaseIndex, range);
					const UINT offset = shaderRegister - range.baseShaderRegister;
					if (offset >= count)
						continue;
					const UINT64 descriptorIndex =
						tableBaseIndex + range.effectiveOffset + offset;
					if (descriptorIndex >= heap.numDescriptors)
						continue;
					const SIZE_T cpuHandle =
						heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
					DX12DescriptorSummary descriptor = {};
					const bool foundDescriptor = completeDescriptorResourceInfo ?
						DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
						DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
					if (!foundDescriptor || descriptor.kind != descriptorKind)
						continue;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = shaderRegister;
					binding.registerSpace = registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = count;
					binding.tableCpuStart =
						heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
					binding.tableGpuStart.ptr =
						heap.gpuStart + tableBaseIndex * heap.increment;
					binding.tableHeap = heap.heap;
					binding.descriptor = descriptor;
					binding.hasDescriptor = true;
					outBindings->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (!rootDescriptor.valid || rootDescriptor.type != rootDescriptorType || !parameter)
				continue;
			if (parameter->shaderRegister != shaderRegister ||
			    parameter->registerSpace != registerSpace)
				continue;
			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = root;
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
			binding.rootDescriptor = true;
			binding.gpuVirtualAddress = rootDescriptor.address;
			outBindings->push_back(std::move(binding));
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

		// Root-signature-driven iteration: use declared parameter count as upper bound.
		const UINT maxRoot = hasRootSignature
			? static_cast<UINT>(rootSignature.parameters.size())
			: MaxRootParameters;

		static thread_local std::vector<SIZE_T> tlsCpuHandles;
		static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
		static thread_local std::vector<uint8_t> tlsFound;

		for (UINT root = 0; root < maxRoot; ++root) {
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
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && srvs) {
							out = srvs;
							kind = "SRV";
							completeDescriptorResourceInfo = true;
						}
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && cbvs) {
							out = cbvs;
							kind = "CBV";
							completeDescriptorResourceInfo = true;
						}
						else { continue; }

						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBase, range);
						tlsCpuHandles.clear();
						tlsCpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 di = tableBase + range.effectiveOffset + offset;
							if (di >= heap.numDescriptors) break;
							const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
							tlsCpuHandles.push_back(cpuHandle);
						}
						tlsDescriptors.resize(tlsCpuHandles.size());
						tlsFound.resize(tlsCpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							tlsCpuHandles.data(), tlsCpuHandles.size(),
							tlsDescriptors.data(), tlsFound.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < tlsCpuHandles.size(); ++offset) {
							if (!tlsFound[offset] || tlsDescriptors[offset].kind != kind)
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
							binding.descriptor = tlsDescriptors[offset];
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
					if (desc.kind == DescriptorKind::UAV && uavs) out = uavs;
					else if (desc.kind == DescriptorKind::SRV && srvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = srvs;
						completeDescriptorResourceInfo = true;
					}
					else if (desc.kind == DescriptorKind::CBV && cbvs) {
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
// CollectPreSkinBindings - lightweight replacement for GetCurrentComputeAll
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
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint)
{
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (!commandList)
		return false;

	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
		DX12CommandListRuntime::GetTrackingState(runtimeState) :
		DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	const UINT64 computeBindingSerial =
		runtimeState ?
		runtimeState->computeBindingSerial :
		DX12BindingTracker::GetComputeBindingSerial(commandList);
	bool cachedFoundAny = false;
	DX12_PROF_COUNT("Rm.PreSkinBindingCacheCalls");
	if (runtimeState && computeBindingSerial &&
			TryLoadRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				uavs,
				srvs,
				cbvs,
				constants,
				dependencyFingerprint,
				&cachedFoundAny)) {
		DX12_PROF_COUNT("Rm.PreSkinBindingCacheHits");
		return cachedFoundAny;
	}

	DX12PreSkinComputeSnapshot snapshot;
	DX12_PROF_STAGE_BEGIN(BindSnapshot);
	bool snapshotReady = false;
	if (preferRuntimeSnapshot) {
		snapshotReady = BuildRuntimeComputeBindingSnapshot(
			runtimeState, &snapshot);
	} else {
		// Non-runtime tracked lists snapshot the full state, then only the
		// compute fields this collector observes are carried over.
		// thread_local: SnapshotBindingState fully overwrites the object on
		// success, so a fresh zero-init of the ~60KB NSDMI arrays per call is
		// wasted work on this per-dispatch path.
		static thread_local CommandListBindingState fullSnapshot;
		if (DX12BindingTracker::SnapshotBindingState(
				commandList, &fullSnapshot)) {
			FillPreSkinComputeSnapshotFromFull(fullSnapshot, &snapshot);
			snapshotReady = true;
		}
	}
	if (!snapshotReady) {
		DX12_PROF_STAGE_END(BindSnapshot);
		return false;
	}
	DX12_PROF_STAGE_END(BindSnapshot);

	// Cache root signature summary per ID3D12RootSignature* pointer.
	// Parse the blob once, reuse across all dispatches with the same signature.
	// Thread-local: the summary only changes on a root-signature/PSO transition,
	// so per-thread caching removes the global SRW lock + shared unordered_map
	// lookup from the per-dispatch PreSkin hot path. Reloads invalidate via the
	// reload generation, matching the surrounding resident-state pattern.
	static thread_local std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary> s_rootSigCache;
	static thread_local UINT64 s_rootSigCacheReloadGen = 0;
	UINT64 reloadGen = DX12ModGetReloadGeneration();
	if (s_rootSigCacheReloadGen != reloadGen) {
		s_rootSigCache.clear();
		s_rootSigCacheReloadGen = reloadGen;
	}

	ID3D12RootSignature *computeRootSig = snapshot.computeRootSignature;
	if (!computeRootSig && snapshot.pipelineState) {
		DX12PsoShaderInfo shaderInfo = {};
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &shaderInfo) &&
			shaderInfo.psoIndex &&
			DX12GetPsoRootSignature(shaderInfo.psoIndex, &psoRootSignature) &&
			psoRootSignature) {
			computeRootSig = psoRootSignature;
		}
	}
	DX12RootSignatureSummary rootSignatureSummary;
	const DX12RootSignatureSummary *cachedSummary = nullptr;
	if (computeRootSig) {
		std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary>::const_iterator sigIt =
			s_rootSigCache.find(computeRootSig);
		if (sigIt != s_rootSigCache.end()) {
			// Point straight at the cached entry instead of deep-copying it.
			// DX12RootSignatureSummary owns a vector of parameter summaries that
			// each own a vector of ranges, so the copy cost a dozen allocations
			// per dispatch for a structure that is only read through the
			// pointer below. Nothing inserts into s_rootSigCache after this
			// point on the hit path, so the reference cannot be invalidated.
			cachedSummary = &sigIt->second;
		} else {
			DX12RootSignatureSummary parsed;
			if (DX12GetRootSignatureSummary(computeRootSig, &parsed)) {
				s_rootSigCache[computeRootSig] = parsed;
				rootSignatureSummary = parsed;
				cachedSummary = &rootSignatureSummary;
			}
		}
	}
	const bool hasParsedRootSignatureSummary =
		cachedSummary && cachedSummary->parsed;
	// The runtime state caches the heap content version at SetDescriptorHeaps
	// time, so the runtime path does not need the global descriptor lock to
	// re-read it here.
	const UINT64 descriptorHeapVersionBefore =
		preferRuntimeSnapshot ?
			runtimeState->cbvSrvUavHeapVersion :
			DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);
	const UINT64 bufferResolveEpoch =
		gBufferResolveGeneration.load(std::memory_order_relaxed);
	DX12PreSkinBindingCacheKey bindingCacheKey;
	DX12PreSkinBindingCacheUtils::BuildKey<DX12BindingTrackerQueryCacheUtils>(
		snapshot,
		descriptorHeapVersionBefore,
		bufferResolveEpoch,
		hasParsedRootSignatureSummary ? cachedSummary : nullptr,
		&bindingCacheKey);
	static thread_local UINT64 tlsPreSkinBindingCacheReloadGeneration = 0;
	static thread_local DX12PreSkinBindingTlsCache tlsPreSkinBindingCache;
	if (tlsPreSkinBindingCacheReloadGeneration != reloadGen) {
		tlsPreSkinBindingCache = DX12PreSkinBindingTlsCache();
		tlsPreSkinBindingCacheReloadGeneration = reloadGen;
	}
	bool contentCachedFoundAny = false;
	if (DX12PreSkinBindingCacheUtils::TryLoad(
			&tlsPreSkinBindingCache,
			bindingCacheKey,
			uavs,
			srvs,
			cbvs,
			constants,
			&contentCachedFoundAny)) {
		if (dependencyFingerprint)
			*dependencyFingerprint = bindingCacheKey.fingerprint;
		DX12_PROF_COUNT("Rm.PreSkinBindingCacheHits");
		return contentCachedFoundAny;
	}
	DX12_PROF_COUNT("Rm.PreSkinBindingCacheMisses");

	bool allRuntimeDescriptorsStable = true;
	bool canStoreRuntimeBindings = runtimeState && computeBindingSerial != 0;
	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	bool usesRootValues = false;

	// Thread-local scratch buffers grow to the actual descriptor workload and
	// retain capacity across calls. Correctness is bounded by D3D12 metadata,
	// not by a local fixed-size array.
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalUavs;
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalSrvs;
	static thread_local std::vector<DX12CurrentComputeUavBinding> tlsLocalCbvs;
	static thread_local std::vector<DX12CurrentRootConstants> tlsLocalConstants;
	static thread_local std::vector<UINT> tlsObservedRootParameters;
	static thread_local std::vector<UINT> tlsObservedSlots;
	static thread_local std::vector<UINT> tlsObservedRoots;
	static thread_local std::vector<UINT> tlsObservedOffsets;
	static thread_local std::vector<UINT64> tlsObservedSlotVersions;
	static thread_local std::vector<UINT64> tlsObservedSlotValueIds;
	static thread_local std::vector<
		DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		tlsObservedSlotsRelative;
	static thread_local std::vector<SIZE_T> tlsCpuHandles;
	static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
	static thread_local std::vector<uint8_t> tlsFound;
	static thread_local std::vector<uint8_t> tlsPending;
	static thread_local std::vector<UINT> tlsDescriptorOffsets;
	static thread_local std::vector<UINT64> tlsDescriptorIndices;
	static thread_local std::vector<UINT> tlsDescriptorSlots;

	std::vector<DX12CurrentComputeUavBinding> &localUavs = tlsLocalUavs;
	std::vector<DX12CurrentComputeUavBinding> &localSrvs = tlsLocalSrvs;
	std::vector<DX12CurrentComputeUavBinding> &localCbvs = tlsLocalCbvs;
	std::vector<DX12CurrentRootConstants> &localConstants = tlsLocalConstants;
	std::vector<UINT> &observedRootParameters = tlsObservedRootParameters;
	std::vector<UINT> &observedSlots = tlsObservedSlots;
	std::vector<UINT> &observedRoots = tlsObservedRoots;
	std::vector<UINT> &observedOffsets = tlsObservedOffsets;
	std::vector<UINT64> &observedSlotVersions = tlsObservedSlotVersions;
	std::vector<UINT64> &observedSlotValueIds = tlsObservedSlotValueIds;
	std::vector<DX12SimulateContextComputeTemplate::RuntimeObservedSlot>
		&observedSlotsRelative = tlsObservedSlotsRelative;
	localUavs.clear();
	localSrvs.clear();
	localCbvs.clear();
	localConstants.clear();
	observedRootParameters.clear();
	observedSlots.clear();
	observedRoots.clear();
	observedOffsets.clear();
	observedSlotVersions.clear();
	observedSlotValueIds.clear();
	observedSlotsRelative.clear();

	DX12_PROF_STAGE_BEGIN(BindCollect);

	// Root-signature-driven iteration: use declared parameter count as upper bound.
	// If metadata is temporarily unavailable, keep the cheap root descriptor /
	// constant paths alive, but never turn that miss into an O(heap_size) scan.
	const UINT maxRoot = hasParsedRootSignatureSummary
		? static_cast<UINT>(cachedSummary->parameters.size())
		: MaxRootParameters;
	for (UINT root = 0; root < maxRoot; ++root) {
		bool affectsPreSkinBindings = !hasParsedRootSignatureSummary;
		if (hasParsedRootSignatureSummary) {
			const DX12RootParameterSummary *parameter =
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root);
			if (parameter) {
				affectsPreSkinBindings =
					parameter->parameterType !=
						D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
							range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
							range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
						affectsPreSkinBindings = true;
						break;
					}
				}
			}
		}
		if (affectsPreSkinBindings)
			observedRootParameters.push_back(root);
	}

	// Collect root constants directly from snapshot
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootConstantsState &rc = snapshot.computeRootConstants[root];
		if (!rc.valid)
			continue;
		DX12_PROF_COUNT("Rm.PreSkinBindingConstants");
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = rc.maxSet;
		memcpy(current.values, rc.values, sizeof(current.values));
		memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
		localConstants.push_back(current);
		usesRootValues = true;
	}

	// Collect descriptor table bindings.
	// With root signature: iterate exactly the declared ranges (correct offset/count).
	// P5: Bulk descriptor lookup: collect ALL CPU handles from ALL ranges first,
	// then issue a SINGLE DX12FindDescriptorSummariesByCpuHandleNoWait call,
	// then distribute results back to per-range output vectors.
	// Without root signature: scan to the descriptor heap boundary, classify by kind.

	// Per-descriptor metadata for bulk lookup distribution.
	// Slot i tracks: output vector, kind string, root param index, range metadata, heap data.
	static thread_local std::vector<std::vector<DX12CurrentComputeUavBinding>*> tlsOutVec;
	static thread_local std::vector<const char*> tlsKind;
	static thread_local std::vector<UINT> tlsRootIndex;
	static thread_local std::vector<UINT> tlsRangeIndex;
	static thread_local std::vector<UINT> tlsRangeShaderVisibility;
	static thread_local std::vector<UINT> tlsRangeBaseShaderRegister;
	static thread_local std::vector<UINT> tlsRangeRegisterSpace;
	static thread_local std::vector<UINT> tlsRangeEffectiveOffset;
	static thread_local std::vector<UINT> tlsCount;
	static thread_local std::vector<UINT64> tlsTableBaseIndex;
	static thread_local std::vector<SIZE_T> tlsTableCpuStart;
	static thread_local std::vector<UINT64> tlsTableGpuStart;
	static thread_local std::vector<UINT> tlsTableIncrement;
	static thread_local std::vector<ID3D12DescriptorHeap*> tlsTableHeap;

	tlsCpuHandles.clear();
	tlsDescriptorOffsets.clear();
	tlsDescriptorIndices.clear();
	tlsDescriptorSlots.clear();
	tlsOutVec.clear();
	tlsKind.clear();
	tlsRootIndex.clear();
	tlsRangeIndex.clear();
	tlsRangeShaderVisibility.clear();
	tlsRangeBaseShaderRegister.clear();
	tlsRangeRegisterSpace.clear();
	tlsRangeEffectiveOffset.clear();
	tlsCount.clear();
	tlsTableBaseIndex.clear();
	tlsTableCpuStart.clear();
	tlsTableGpuStart.clear();
	tlsTableIncrement.clear();
	tlsTableHeap.clear();

	// Phase 1: Collect all CPU handles from root-signature ranges.
	size_t totalHandleCount = 0;
	ID3D12DescriptorHeap *slotLookupHeap = nullptr;
	bool canUseSlotLookup = true;
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootTableState &table = snapshot.computeTables[root];
		if (!table.valid)
			continue;
		DX12_PROF_COUNT("Rm.PreSkinBindingRootTables");

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveRuntimeTableHeap(snapshot, table, &heap)) {
			DX12_PROF_COUNT("Rm.PreSkinBindingRuntimeUnresolved");
			allRuntimeDescriptorsStable = false;
			continue;
		}
		if (table.runtimeHeapResolved)
			DX12_PROF_COUNT("Rm.PreSkinBindingRuntimeResolved");
		else
			DX12_PROF_COUNT("Rm.PreSkinBindingRuntimeUnresolved");

		const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			continue;

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		if (!parameter || parameter->parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			// Without parsed root-signature metadata, do not speculate about the
			// descriptor table extent. DX11-style fast rejection is preferable to
			// scanning an arbitrary portion of the heap on the dispatch hot path.
			continue;
		}

		// Precompute heap data shared by all ranges in this table.
		const SIZE_T tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		const UINT64 tableGpuStart = heap.gpuStart + tableBaseIndex * heap.increment;

		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			DX12_PROF_COUNT("Rm.PreSkinBindingRanges");

			std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
			const char *kind = nullptr;
			if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
				out = &localUavs; kind = "UAV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
				out = &localSrvs; kind = "SRV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
				out = &localCbvs; kind = "CBV";
			} else {
				continue;
			}

			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 di = tableBaseIndex + range.effectiveOffset + offset;
				if (di >= heap.numDescriptors)
					break;
				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
				DX12_PROF_COUNT("Rm.PreSkinBindingFullDescriptorLookups");
				if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
					canUseSlotLookup = false;
				} else if (!slotLookupHeap) {
					slotLookupHeap = heap.heap;
				} else if (slotLookupHeap != heap.heap) {
					canUseSlotLookup = false;
				}
				if (canStoreRuntimeBindings) {
					if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
						canStoreRuntimeBindings = false;
					} else if (!observedSlotHeap) {
						observedSlotHeap = heap.heap;
					} else if (observedSlotHeap != heap.heap) {
						canStoreRuntimeBindings = false;
					}
					if (canStoreRuntimeBindings)
						observedSlots.push_back(static_cast<UINT>(di));
					if (canStoreRuntimeBindings) {
						observedRoots.push_back(root);
						observedOffsets.push_back(static_cast<UINT>(
							di - tableBaseIndex));
					}
				}

				tlsCpuHandles.push_back(cpuHandle);
				tlsDescriptorSlots.push_back(static_cast<UINT>(di));
				tlsDescriptorOffsets.push_back(offset);
				tlsOutVec.push_back(out);
				tlsKind.push_back(kind);
				tlsRootIndex.push_back(root);
				tlsRangeIndex.push_back(range.rangeIndex);
				tlsRangeShaderVisibility.push_back(range.shaderVisibility);
				tlsRangeBaseShaderRegister.push_back(range.baseShaderRegister);
				tlsRangeRegisterSpace.push_back(range.registerSpace);
				tlsRangeEffectiveOffset.push_back(range.effectiveOffset);
				tlsCount.push_back(count);
				tlsTableBaseIndex.push_back(tableBaseIndex);
				tlsTableCpuStart.push_back(tableCpuStart);
				tlsTableGpuStart.push_back(tableGpuStart);
				tlsTableIncrement.push_back(heap.increment);
				tlsTableHeap.push_back(heap.heap);
				totalHandleCount++;
			}
		}
	}

	// Phase 2: Single bulk descriptor lookup for all root-signature ranges.
	if (totalHandleCount > 0) {
		tlsDescriptors.resize(totalHandleCount);
		tlsFound.resize(totalHandleCount);
		tlsPending.resize(totalHandleCount);
		if (canUseSlotLookup && slotLookupHeap) {
			memset(tlsPending.data(), 0, totalHandleCount * sizeof(uint8_t));
			DX12FindDescriptorSummariesByHeapSlot(
				slotLookupHeap,
				tlsDescriptorSlots.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		} else {
			DX12FindDescriptorSummariesByCpuHandleNoWait(
				tlsCpuHandles.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				tlsPending.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		}

		LONG64 descriptorHits = 0;
		LONG64 descriptorMisses = 0;
		LONG64 descriptorPending = 0;
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (tlsPending[i]) {
				descriptorPending++;
				allRuntimeDescriptorsStable = false;
			} else if (tlsFound[i]) {
				descriptorHits++;
			} else {
				descriptorMisses++;
				allRuntimeDescriptorsStable = false;
			}
		}
		DX12_PROF_ADD_ITEMS("Rm.PreSkinBindingDescriptorHits", descriptorHits);
		DX12_PROF_ADD_ITEMS("Rm.PreSkinBindingDescriptorMisses", descriptorMisses);
		DX12_PROF_ADD_ITEMS("Rm.PreSkinBindingDescriptorPending", descriptorPending);
		DX12_PROF_ADD_ITEMS("Rm.PreSkinBindingWaitAvoided",
			descriptorHits + descriptorMisses + descriptorPending);

		// Phase 3: Distribute results to per-range output vectors.
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (!tlsFound[i] || tlsDescriptors[i].kind != tlsKind[i])
				continue;

			std::vector<DX12CurrentComputeUavBinding> *out = tlsOutVec[i];

			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = tlsRootIndex[i];
			binding.rangeIndex = tlsRangeIndex[i];
			binding.shaderVisibility = tlsRangeShaderVisibility[i];
			binding.shaderRegister =
				tlsRangeBaseShaderRegister[i] + tlsDescriptorOffsets[i];
			binding.registerSpace = tlsRangeRegisterSpace[i];
			binding.descriptorOffset =
				tlsRangeEffectiveOffset[i] + tlsDescriptorOffsets[i];
			binding.descriptorIncrement = tlsTableIncrement[i];
			binding.tableCopyCount = tlsCount[i];
			binding.tableCpuStart = tlsTableCpuStart[i];
			binding.tableGpuStart.ptr = tlsTableGpuStart[i];
			binding.tableHeap = tlsTableHeap[i];
			binding.descriptor = tlsDescriptors[i];
			binding.hasDescriptor = true;
			out->push_back(std::move(binding));
		}
	}

	// Collect root descriptors (bare GPU VA, no heap needed)
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
		if (!rd.valid)
			continue;
		DX12_PROF_COUNT("Rm.PreSkinBindingRootDescriptors");

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		DX12CurrentComputeUavBinding binding;
		binding.rootParameterIndex = root;
		binding.rootDescriptor = true;
		binding.gpuVirtualAddress = rd.address;
		if (parameter) {
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
		}
		usesRootValues = true;

		if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV)
			localUavs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV)
			localSrvs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
			localCbvs.push_back(std::move(binding));
	}

	DX12_PROF_STAGE_END(BindCollect);

	const bool foundAny = !localUavs.empty() || !localSrvs.empty() ||
		!localCbvs.empty() || !localConstants.empty();
	const UINT64 descriptorHeapVersionAfter =
		preferRuntimeSnapshot ?
			runtimeState->cbvSrvUavHeapVersion :
			DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);
	const UINT64 descriptorQuerySerialAfter = CurrentDescriptorQuerySerial();
	// The global descriptor-query serial bumps on any descriptor write anywhere
	// in the process, including slots this dispatch never observes. Requiring it
	// to stay constant across the collect pass made the cache un-storeable in
	// descriptor-churn games (observed 0% hit rate): the serial changed mid-collect
	// even when every observed slot was stable. The load path already re-reads each
	// observed slot's version/value-id to prove correctness, so keep only the
	// heap-structure stability check here.
	const bool descriptorStateStable =
		descriptorHeapVersionAfter == descriptorHeapVersionBefore;
	if (allRuntimeDescriptorsStable && descriptorStateStable) {
		DX12PreSkinBindingCacheUtils::Store(
			&tlsPreSkinBindingCache,
			bindingCacheKey,
			localUavs,
			localSrvs,
			localCbvs,
			localConstants,
			foundAny);
	}
	if (canStoreRuntimeBindings && allRuntimeDescriptorsStable && descriptorStateStable) {
		observedSlotVersions.resize(observedSlots.size());
		observedSlotValueIds.resize(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			if (observedSlotHeap &&
					runtimeState->runtimeCbvSrvUavHeapResolved &&
					runtimeState->runtimeCbvSrvUavHeap.heap ==
						observedSlotHeap) {
				observedSlotsValid = RuntimeReadSlotIdentitiesFast(
					runtimeState->runtimeCbvSrvUavHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
			} else {
				observedSlotsValid =
					observedSlotHeap &&
					DX12GetDescriptorSlotIdentities(
						observedSlotHeap,
						observedSlots.data(),
						observedSlots.size(),
						observedSlotVersions.data(),
						observedSlotValueIds.data());
			}
		}
		if (observedSlotsValid) {
			const UINT64 runtimeDependencyFingerprint = bindingCacheKey.fingerprint;
			observedSlotsRelative.resize(observedSlots.size());
			for (size_t slotIndex = 0;
					slotIndex < observedSlots.size(); ++slotIndex) {
				DX12SimulateContextComputeTemplate::RuntimeObservedSlot &slot =
					observedSlotsRelative[slotIndex];
				slot.root = observedRoots[slotIndex];
				slot.offset = observedOffsets[slotIndex];
				slot.version = observedSlotVersions[slotIndex];
				slot.valueId = observedSlotValueIds[slotIndex];
			}
			StoreRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				descriptorQuerySerialAfter,
				runtimeDependencyFingerprint,
				usesRootValues,
				foundAny,
				observedRootParameters,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				observedSlotsRelative,
				localUavs,
				localSrvs,
				localCbvs,
				localConstants);
			if (dependencyFingerprint)
				*dependencyFingerprint = runtimeDependencyFingerprint;
			DX12_PROF_COUNT("Rm.PreSkinBindingCacheStores");
		}
	} else if (!allRuntimeDescriptorsStable || !descriptorStateStable) {
	}

	if (uavs) *uavs = std::move(localUavs);
	if (srvs) *srvs = std::move(localSrvs);
	if (cbvs) *cbvs = std::move(localCbvs);
	if (constants) *constants = std::move(localConstants);

	return foundAny;
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

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(
				const_cast<DX12SimulateContext*>(runtimeState)) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		if (!BuildRuntimeBindingSnapshot(runtimeState, snapshot))
			return false;
		if (bindingQuerySerial)
			*bindingQuerySerial =
				snapshot->bindingQuerySerial ? snapshot->bindingQuerySerial : 1;
		return true;
	}

	bool found = false;
	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		*snapshot = it->second;
		serial = it->second.bindingQuerySerial;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (runtimeState) {
		// With no shard state to merge, build directly into the output and
		// skip a full ~60KB copy of the scratch snapshot. The merge case uses
		// a thread_local scratch: BuildRuntimeBindingSnapshot reconstructs it
		// in place, so zero-initializing a fresh local per call is wasted.
		static thread_local CommandListBindingState runtimeSnapshot;
		CommandListBindingState *runtimeTarget =
			found ? &runtimeSnapshot : snapshot;
		if (BuildRuntimeBindingSnapshot(runtimeState, runtimeTarget)) {
			if (!found) {
				serial = snapshot->bindingQuerySerial;
				found = true;
			} else {
				snapshot->graphicsRootSignature =
					runtimeSnapshot.graphicsRootSignature;
				snapshot->computeRootSignature =
					runtimeSnapshot.computeRootSignature;
				snapshot->cbvSrvUavHeap = runtimeSnapshot.cbvSrvUavHeap;
				snapshot->samplerHeap = runtimeSnapshot.samplerHeap;
				snapshot->runtimeCbvSrvUavHeapResolved =
					runtimeSnapshot.runtimeCbvSrvUavHeapResolved;
				snapshot->runtimeCbvSrvUavHeap =
					runtimeSnapshot.runtimeCbvSrvUavHeap;
				snapshot->runtimeSamplerHeapResolved =
					runtimeSnapshot.runtimeSamplerHeapResolved;
				snapshot->runtimeSamplerHeap =
					runtimeSnapshot.runtimeSamplerHeap;
				snapshot->graphicsTableValidCount =
					runtimeSnapshot.graphicsTableValidCount;
				snapshot->computeTableValidCount =
					runtimeSnapshot.computeTableValidCount;
				memcpy(
					snapshot->graphicsTables,
					runtimeSnapshot.graphicsTables,
					sizeof(snapshot->graphicsTables));
				memcpy(
					snapshot->computeTables,
					runtimeSnapshot.computeTables,
					sizeof(snapshot->computeTables));
				memcpy(
					snapshot->graphicsRootDescriptors,
					runtimeSnapshot.graphicsRootDescriptors,
					sizeof(snapshot->graphicsRootDescriptors));
				memcpy(
					snapshot->computeRootDescriptors,
					runtimeSnapshot.computeRootDescriptors,
					sizeof(snapshot->computeRootDescriptors));
				memcpy(
					snapshot->graphicsRootConstants,
					runtimeSnapshot.graphicsRootConstants,
					sizeof(snapshot->graphicsRootConstants));
				memcpy(
					snapshot->computeRootConstants,
					runtimeSnapshot.computeRootConstants,
					sizeof(snapshot->computeRootConstants));
				serial ^= runtimeSnapshot.bindingQuerySerial;
			}
		}
	}
	if (bindingQuerySerial)
		*bindingQuerySerial = serial ? serial : 1;
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

void DX12BindingTracker::ResolveDeferredIaResourcesForSnapshot(
	CommandListBindingState *state)
{
	if (!state)
		return;
	// Snapshot copies are owned by the caller; resolving them is safe without
	// the shard lock because the resource database lookups take their own
	// locks and the copy is not shared.
	ResolveDeferredIaResources(state);
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
	// Callers always pass a freshly default-constructed event, so there is no
	// need to reset it here; doing so would zero ~60KB of arrays twice.
	event->kind = kind ? kind : "";
	event->captureSessionId = captureSessionId;
	const bool isDrawKind = kind && (!strcmp(kind, "draw") || !strcmp(kind, "draw_indexed"));
	const bool isDispatchKind = kind && !strcmp(kind, "dispatch");
	event->drawId = isDrawKind ? state.drawSerial : 0;
	event->dispatchId = isDispatchKind ? state.dispatchSerial : 0;
	event->descriptorEpoch = descriptorEpoch;
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	DX12_PROF_COUNT("Rm.DescriptorEpochWrites");
	event->commandList = commandList;
	event->pipelineState = state.pipelineState;
	event->graphicsRootSignature = state.graphicsRootSignature;
	event->computeRootSignature = state.computeRootSignature;
	event->cbvSrvUavHeap = state.cbvSrvUavHeap;
	event->samplerHeap = state.samplerHeap;
	if (isDrawKind) {
		// Draw events only need graphics state
		memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
		memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
		memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
		memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
		memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
		memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
		memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	} else if (isDispatchKind) {
		// Dispatch events only need compute state
		memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
		memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
		memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
	} else {
		// Other event kinds: copy everything
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
	}
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
	DX12BindingTracker::RecordTemplateEvent(event.commandList, event);
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
	DX12BindingTracker::RecordTemplateOutputEvent(commandList, event);
}

void DX12BindingTracker::StoreCurrentOutputTargetsLocked(
	ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId, UINT64 drawSerial)
{
	if (captureSessionId == 0)
		captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		if (state.renderTargetValid[i])
			DX12BindingTracker::StoreOutputEventLocked("RTV", commandList, state, i,
				state.renderTargets[i].ptr, drawSerial, captureSessionId);
	}
	if (state.depthStencilValid)
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, state, 0,
			state.depthStencil.ptr, drawSerial, captureSessionId);
}

void DX12BindingTracker::ReleaseRecordedEvent(CommandListRecordedEvent *event)
{
	if (!event)
		return;
	if (event->kind == CommandListRecordedEventKind::Binding)
		DX12BindingTracker::ReleaseEventIaResources(&event->binding);
}

void DX12BindingTracker::ReleaseRecordedEventsLocked(
	BindingShard *shard, ID3D12GraphicsCommandList *commandList)
{
	if (!shard || !commandList)
		return;
	auto it = shard->recordedEvents.find(commandList);
	if (it == shard->recordedEvents.end())
		return;
	for (CommandListRecordedEvent &event : it->second)
		DX12BindingTracker::ReleaseRecordedEvent(&event);
	shard->recordedEvents.erase(it);
}

void DX12BindingTracker::RecordTemplateEvent(
	ID3D12GraphicsCommandList *commandList, const BindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Binding;
	recorded.binding = event;
	recorded.binding.commandList = commandList;
	// Stamp the session at record time: a lagged submit marker can then move
	// only this session's events and can never pollute a newer capture.
	recorded.binding.captureSessionId = event.captureSessionId;
	recorded.binding.serial = 0;
	DX12BindingTracker::RetainEventIaResources(&recorded.binding);
	const std::string kind = recorded.binding.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::RecordTemplateOutputEvent(
	ID3D12GraphicsCommandList *commandList, const OutputBindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Output;
	recorded.output = event;
	recorded.output.commandList = commandList;
	// Stamp the session at record time; see RecordTemplateEvent.
	recorded.output.captureSessionId = event.captureSessionId;
	recorded.output.serial = 0;
	const std::string kind = recorded.output.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	DX12_PROF_COUNT("Rm.BindingEventsRecorded");
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateOutputEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::StoreEventInCaptureLedger(const BindingEvent &event)
{
	BindingEvent stored = event;
	DX12BindingTracker::RetainEventIaResources(&stored);
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::RecordEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored)
		return;
	DX12BindingTracker::ReleaseEventIaResources(&stored);
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerEventSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::StoreOutputEventInCaptureLedger(const OutputBindingEvent &event)
{
	OutputBindingEvent stored = event;
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::TryStoreOutputEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored ||
		ledgerResult == DX12CaptureLedgerRecordResult::Duplicate)
		return;
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerOutputSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::SubmitRecordedEvent(
	const CommandListRecordedEvent &recorded, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		return;
	if (recorded.kind == CommandListRecordedEventKind::Binding) {
		BindingEvent event = recorded.binding;
		event.captureSessionId = captureSessionId;
		event.serial = 0;
		DX12BindingTracker::StoreEventInCaptureLedger(event);
		return;
	}
	OutputBindingEvent event = recorded.output;
	event.captureSessionId = captureSessionId;
	event.serial = 0;
	DX12BindingTracker::StoreOutputEventInCaptureLedger(event);
}

void DX12BindingTracker::SubmitCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;
	// Submission is a queue marker: the worker moves this command list's
	// recorded events into the ledger after every event task enqueued before
	// this submit (FIFO), so the recording thread never waits on the worker.
	// D3D12 guarantees recording stopped (Close) before ExecuteCommandLists,
	// so no event task for this CL can follow the marker in the queue.
	DX12BindingCollectQueue::EnqueueSubmitCommandList(commandList, captureSessionId);
}

bool DX12BindingTracker::RegisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	const bool inserted = shard.states.try_emplace(commandList).second;
	if (inserted) {
		// Bump generation to invalidate any TLS-cached state pointers
		// that may be invalidated by unordered_map rehash.
		shard.generation++;
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return inserted;
}

bool DX12BindingTracker::UnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	bool removedState = false;
	bool removedEvents = false;
	size_t recordedEventCount = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto eventIt = shard.recordedEvents.find(commandList);
	if (eventIt != shard.recordedEvents.end()) {
		recordedEventCount = eventIt->second.size();
		removedEvents = true;
	}
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);

	auto stateIt = shard.states.find(commandList);
	if (stateIt != shard.states.end()) {
		DX12BindingTracker::ReleaseRetainedIaResources(&stateIt->second);
		shard.states.erase(stateIt);
		removedState = true;
	}
	if (removedState || removedEvents)
		shard.generation++;
	ReleaseSRWLockExclusive(&shard.lock);

	if (tlsGraphicsTableCache.cl == commandList)
		tlsGraphicsTableCache.cl = nullptr;
	if (tlsComputeTableCache.cl == commandList)
		tlsComputeTableCache.cl = nullptr;
	if (tlsGraphicsDescCache.cl == commandList)
		tlsGraphicsDescCache.cl = nullptr;
	if (tlsComputeDescCache.cl == commandList)
		tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches) {
		if (queryCache.commandList == commandList)
			queryCache.commandList = nullptr;
	}
	if (tlsComputeAllQueryCache.commandList == commandList)
		tlsComputeAllQueryCache.commandList = nullptr;
	if (tlsStateCache.commandList == commandList) {
		tlsStateCache.commandList = nullptr;
		tlsStateCache.state = nullptr;
	}

	// Drop the recorder-side delta stream and order the worker's
	// materialized-state cleanup behind any in-flight events for this CL.
	DX12BindingCollectQueue::EraseRecorderState(commandList);

	if (removedState || removedEvents) {
		DX12_LOG("DX12BindingTracker",
			"stage=unregister_command_list,commandList=%p,state=%u,recordedEvents=%zu",
			commandList,
			removedState ? 1u : 0u,
			recordedEventCount);
	}
	return removedState || removedEvents;
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
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);
	DX12BindingTracker::ReleaseRetainedIaResources(&state);
	// Targeted reset instead of placement-new: reconstructing the ~58KB state
	// zeroed the 41KB root-constant payload arrays on every command-list Reset
	// (several lists per frame - 3.8/frame measured in the current scene), all
	// under the exclusive shard lock. The payloads are
	// only read while their valid flags are set, so clearing validity and
	// identity fields is equivalent.
	state.ResetForRecording();
	state.pipelineState = initialState;
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RefreshPipelineShaderInfo(&state);
	ReleaseSRWLockExclusive(&shard.lock);

	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches)
		queryCache.commandList = nullptr;
	tlsComputeAllQueryCache.commandList = nullptr;
	tlsStateCache.commandList = nullptr;
	tlsStateCache.state = nullptr;

	// The re-recorded stream starts from a clean slate: the next event sends
	// a full snapshot so the worker's materialized state cannot carry state
	// from the previous recording pass.
	DX12BindingCollectQueue::ResetRecorderState(commandList);
}

bool DX12BindingTracker::SetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return false;

	DX12_PROF_COUNT("Rm.PsoCalls");
	// Lock-free fast path: the TLS-cached binding state already carries this
	// PSO. The shard lock + map re-validation below would otherwise run on
	// every redundant SetPipelineState (games set the same PSO thousands of
	// times per frame); the TLS cache is invalidated on shard generation
	// change, so a warm match is exact for this thread and command list.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->pipelineState == pipelineState) {
			DX12_PROF_COUNT("Rm.PsoNoChange");
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.pipelineState == pipelineState) {
		DX12_PROF_COUNT("Rm.PsoNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.pipelineState = pipelineState;
	DX12BindingTracker::InvalidatePipelineShaderInfo(&state);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12_PROF_COUNT("Rm.PsoChanged");
	ReleaseSRWLockExclusive(&shard.lock);

	// Shadow stream: record the mutation for the async worker's replay.
	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::PipelineState;
	entry.pipelineState = pipelineState;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
}

bool DX12BindingTracker::SetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return false;

	// Lock-free fast path, same TLS validation as GetOrResolveBindingState.
	// SetGraphicsRootSignature is called once per SetGraphicsRootSignature
	// API call; titles that re-set the same signature before every draw were
	// paying a full shard exclusive lock + map lookup per call even though the
	// state never changed.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->graphicsRootSignature == rootSignature) {
			DX12_PROF_COUNT("Rm.GraphicsRootSignatureCalls");
			DX12_PROF_COUNT("Rm.GraphicsRootSignatureNoChange");
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.graphicsRootSignature == rootSignature) {
		DX12_PROF_COUNT("Rm.GraphicsRootSignatureCalls");
		DX12_PROF_COUNT("Rm.GraphicsRootSignatureNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.graphicsRootSignature = rootSignature;
	// D3D12 root arguments become stale when the root signature changes. Keep
	// Binding State subject to the same rule so a table from the previous root
	// signature can never be matched or replayed against the new layout.
	// Shared with the queue's delta replay so the live state and the
	// materialized state can never diverge.
	DX12BindingCollectQueue::ResetGraphicsRootArguments(&state);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12_PROF_COUNT("Rm.GraphicsRootSignatureCalls");
	DX12_PROF_COUNT("Rm.GraphicsRootSignatureChanged");
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::GraphicsRootSignature;
	entry.rootSignature = rootSignature;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
}

bool DX12BindingTracker::SetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return false;

	// Lock-free fast path, mirrors SetGraphicsRootSignature.
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation &&
			tlsStateCache.state->computeRootSignature == rootSignature) {
			DX12_PROF_COUNT("Rm.ComputeRootSignatureCalls");
			DX12_PROF_COUNT("Rm.ComputeRootSignatureNoChange");
			return false;
		}
	}
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.computeRootSignature == rootSignature) {
		DX12_PROF_COUNT("Rm.ComputeRootSignatureCalls");
		DX12_PROF_COUNT("Rm.ComputeRootSignatureNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	state.computeRootSignature = rootSignature;
	// Shared with the queue's delta replay; see SetGraphicsRootSignature.
	DX12BindingCollectQueue::ResetComputeRootArguments(&state);
	state.computeTableValidCount = 0;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12_PROF_COUNT("Rm.ComputeRootSignatureCalls");
	DX12_PROF_COUNT("Rm.ComputeRootSignatureChanged");
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeTableCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::ComputeRootSignature;
	entry.rootSignature = rootSignature;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
}

void DX12BindingTracker::RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind)
{
	if (!commandList)
		return;

	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;

	// Async path: bundle the accumulated state changes into a compact delta
	// packet; no 60KB snapshot is taken on the recording thread.
	std::vector<DX12BindingDeltaEntry> packet;
	std::unique_ptr<CommandListBindingState> fullState;
	if (DX12BindingCollectQueue::BuildEventPacket(commandList, &packet, &fullState)) {
		if (DX12BindingCollectQueue::EnqueueStateEvent(
				commandList,
				&packet,
				&fullState,
				kind ? kind : "state",
				descriptorEpoch,
				captureSessionId))
			return;
		// Queue full / stopping: put the entries back so the async stream
		// stays consistent, then process synchronously from a live snapshot.
		// The full-state snapshot is dropped here (its references released);
		// the recorder state still requires a full snapshot, so the next
		// event rebuilds it.
		DX12BindingCollectQueue::RestoreEventPacket(commandList, &packet);
		if (fullState)
			DX12BindingTracker::ReleaseRetainedIaResources(fullState.get());
	}

	// thread_local: see the note in SnapshotBindingState's pre-skin caller.
	static thread_local CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
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

	DX12_PROF_COUNT("Rm.DescriptorHeapResolveCalls");
	DX12_PROF_ADD_ITEMS("Rm.DescriptorHeapResolveSlots", static_cast<LONG64>(count));
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
		DX12_PROF_COUNT("Rm.DescriptorHeapCalls");
		DX12_PROF_COUNT("Rm.DescriptorHeapNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	UINT invalidatedGraphicsTables = state.graphicsTableValidCount;
	UINT invalidatedComputeTables = state.computeTableValidCount;
	// Shared with the queue's delta replay so the live state and the
	// materialized state can never diverge.
	DX12BindingCollectQueue::ApplyDescriptorHeapsChange(
		&state, cbvSrvUavHeap, samplerHeap);
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	DX12_PROF_COUNT("Rm.DescriptorHeapCalls");
	DX12_PROF_COUNT("Rm.DescriptorHeapChanged");
	DX12_PROF_ADD_ITEMS("Rm.DescriptorHeapInvalidatedGraphicsTables",
		static_cast<LONG64>(invalidatedGraphicsTables));
	DX12_PROF_ADD_ITEMS("Rm.DescriptorHeapInvalidatedComputeTables",
		static_cast<LONG64>(invalidatedComputeTables));

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::DescriptorHeaps;
	entry.heaps.cbvSrvUavHeap = cbvSrvUavHeap;
	entry.heaps.samplerHeap = samplerHeap;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
}

bool DX12BindingTracker::SetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return false;
	DX12_PROF_COUNT("Rm.GraphicsRootTableCalls");
	if (tlsGraphicsTableCache.cl == commandList &&
		tlsGraphicsTableCache.valid[rootParameterIndex] &&
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12_PROF_COUNT("Rm.GraphicsRootTableTlsHits");
		return false;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &stateTable = state.graphicsTables[rootParameterIndex];
	if (stateTable.valid && stateTable.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsTableCache.cl != commandList) {
			memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
			memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
			tlsGraphicsTableCache.cl = commandList;
		}
		tlsGraphicsTableCache.valid[rootParameterIndex] = true;
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12_PROF_COUNT("Rm.GraphicsRootTableNoChange");
		return false;
	}
	if (!stateTable.valid)
		++state.graphicsTableValidCount;
	stateTable = RootTableState();
	stateTable.valid = true;
	stateTable.rootParameterIndex = rootParameterIndex;
	stateTable.baseDescriptor = baseDescriptor;
	const RootTableState recordedTable = stateTable;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsTableCache.cl != commandList) {
		memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
		memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
		tlsGraphicsTableCache.cl = commandList;
	}
	tlsGraphicsTableCache.valid[rootParameterIndex] = true;
	tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12_PROF_COUNT("Rm.GraphicsRootTableChanged");

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::GraphicsTable;
	entry.index = rootParameterIndex;
	entry.table = recordedTable;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
}

bool DX12BindingTracker::SetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return false;
	DX12_PROF_COUNT("Rm.ComputeRootTableCalls");
	if (tlsComputeTableCache.cl == commandList &&
		tlsComputeTableCache.valid[rootParameterIndex] &&
		tlsComputeTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12_PROF_COUNT("Rm.ComputeRootTableTlsHits");
		return false;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &table = state.computeTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeTableCache.cl != commandList) {
			memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
			memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
			tlsComputeTableCache.cl = commandList;
		}
		tlsComputeTableCache.valid[rootParameterIndex] = true;
		tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12_PROF_COUNT("Rm.ComputeRootTableNoChange");
		return false;
	}
	if (!table.valid)
		++state.computeTableValidCount;
	table = RootTableState();
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	const RootTableState recordedTable = table;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeTableCache.cl != commandList) {
		memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
		memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
		tlsComputeTableCache.cl = commandList;
	}
	tlsComputeTableCache.valid[rootParameterIndex] = true;
	tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12_PROF_COUNT("Rm.ComputeRootTableChanged");

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::ComputeTable;
	entry.index = rootParameterIndex;
	entry.table = recordedTable;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	return true;
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
	if (state.primitiveTopology == topology) {
		DX12_PROF_COUNT("Rm.PrimitiveTopologyCalls");
		DX12_PROF_COUNT("Rm.PrimitiveTopologyNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.primitiveTopology = topology;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12_PROF_COUNT("Rm.PrimitiveTopologyCalls");
	DX12_PROF_COUNT("Rm.PrimitiveTopologyChanged");
	ReleaseSRWLockExclusive(&shard.lock);

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::PrimitiveTopology;
	entry.topology = topology;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
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

	const bool newValid = view != nullptr;
	const bool same =
		state.indexBufferValid == newValid &&
		(!newValid ||
			(state.indexBuffer.BufferLocation == view->BufferLocation &&
				state.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state.indexBuffer.Format == view->Format));
	if (same) {
		DX12_PROF_COUNT("Rm.IndexBufferCalls");
		DX12_PROF_COUNT("Rm.IndexBufferNoChange");
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}

	const bool hadResolved = state.indexBufferResolved;
	DX12BindingTracker::ReleaseResolvedIaResource(
		&state.indexBufferResource, &state.indexBufferResolved);
	if (hadResolved)
		DX12_PROF_COUNT("Rm.IndexBufferResolvedReleased");
	if (view) {
		state.indexBuffer = *view;
		state.indexBufferValid = true;
	} else {
		state.indexBuffer = {};
		state.indexBufferValid = false;
	}
	// Resource resolution DEFERRED to draw time; see SetVertexBuffers comment.
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12_PROF_COUNT("Rm.IndexBufferCalls");
	DX12_PROF_COUNT("Rm.IndexBufferChanged");
	ReleaseSRWLockExclusive(&shard.lock);

	DX12BindingCollectQueue::RecordIndexBufferChange(
		commandList, view, newValid);
}

void DX12BindingTracker::SetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || startSlot >= MaxVertexBufferSlots)
		return;

	const UINT trackedCount = min(count, MaxVertexBufferSlots - startSlot);
	DX12_PROF_COUNT("Rm.VertexBufferCalls");
	DX12_PROF_ADD_ITEMS("Rm.VertexBufferSlots", static_cast<LONG64>(trackedCount));

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

	bool anyChanged = false;
	for (UINT i = 0; i < trackedCount; ++i) {
		const UINT slot = startSlot + i;
		D3D12_VERTEX_BUFFER_VIEW newView = {};
		bool newValid = false;
		if (views) {
			newView = views[i];
			newValid = newView.BufferLocation != 0 && newView.SizeInBytes != 0;
		}
		const bool same =
			state.vertexBufferValid[slot] == newValid &&
			state.vertexBuffers[slot].BufferLocation == newView.BufferLocation &&
			state.vertexBuffers[slot].SizeInBytes == newView.SizeInBytes &&
			state.vertexBuffers[slot].StrideInBytes == newView.StrideInBytes;
		if (same) {
			DX12_PROF_COUNT("Rm.VertexBufferNoChangeSlots");
			continue;
		}

		const bool hadResolved = state.vertexBufferResolved[slot];
		DX12BindingTracker::ReleaseResolvedIaResource(
			&state.vertexBufferResources[slot],
			&state.vertexBufferResolved[slot]);
		if (hadResolved)
			DX12_PROF_COUNT("Rm.VertexBufferResolvedReleased");
		state.vertexBuffers[slot] = newView;
		state.vertexBufferValid[slot] = newValid;
		DX12_PROF_COUNT("Rm.VertexBufferChangedSlots");
		anyChanged = true;
		// Resource resolution DEFERRED to draw time (RecordDrawInstanced /
		// RecordDrawIndexedInstanced). This is a ~90% cost reduction per call:
		//   Old: ReleaseResolvedIaResource + ResolveAndRetainIaResource
		//        -> DX12ReleaseRetainedResource + DX12ResolveBufferResourceByGpuVa + DX12RetainResource
		//   New: trivial struct copy (D3D12_VERTEX_BUFFER_VIEW = 3x UINT64)
		DX12BindingCollectQueue::RecordVertexBufferChange(
			commandList, slot, newView, newValid);
	}
	if (anyChanged)
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
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
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	if (preferRuntimeSnapshot) {
		const UINT64 runtimeBindingQuerySerial =
			GetRuntimeDescriptorQuerySerial(runtimeState, compute);
		bool cachedFoundAny = false;
		if (TryLoadRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				runtimeBindingQuerySerial,
				uavs,
				&cachedFoundAny)) {
			return cachedFoundAny;
		}
	}

	// thread_local: see the note in SnapshotBindingState's pre-skin caller.
	static thread_local CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	const bool snapshotReady = preferRuntimeSnapshot ?
		BuildRuntimeBindingSnapshot(runtimeState, &snapshot) :
		DX12BindingTracker::SnapshotBindingState(
			commandList, &snapshot, &bindingQuerySerial);
	if (!snapshotReady)
		return false;
	if (preferRuntimeSnapshot)
		bindingQuerySerial = GetRuntimeDescriptorQuerySerial(runtimeState, compute);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	const UINT cacheSlot = BindingTlsDescriptorQueryCache::CacheSlotIndex(
		compute, rangeType);
	if (cacheSlot == UINT_MAX)
		return false;
	BindingTlsDescriptorQueryCache &queryCache = tlsDescriptorQueryCaches[cacheSlot];
	if (queryCache.commandList == commandList &&
		queryCache.compute == compute &&
		queryCache.rangeType == static_cast<UINT>(rangeType) &&
		queryCache.bindingQuerySerial == bindingQuerySerial &&
		queryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		queryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		*uavs = queryCache.bindings;
		const UINT64 heapVersionAfterHit = DX12GetDescriptorHeapVersion(
			snapshot.cbvSrvUavHeap);
		if (heapVersionAfterHit != cbvSrvUavHeapVersion) {
			uavs->clear();
		} else {
			return !uavs->empty();
		}
	}

	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	std::vector<UINT> observedSlots;
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindings(
		snapshot,
		compute,
		rangeType,
		descriptorKind,
		rootDescriptorType,
		uavs,
		&observedSlotHeap,
		&observedSlots);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		queryCache.commandList = commandList;
		queryCache.compute = compute;
		queryCache.rangeType = static_cast<UINT>(rangeType);
		queryCache.bindingQuerySerial = bindingQuerySerial;
		queryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		queryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		queryCache.bindings = *uavs;
	} else {
		queryCache.commandList = nullptr;
	}
	if (preferRuntimeSnapshot &&
			runtimeState &&
			bindingQuerySerial != 0 &&
			cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		std::vector<UINT64> observedSlotVersions(observedSlots.size());
		std::vector<UINT64> observedSlotValueIds(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			observedSlotsValid =
				observedSlotHeap &&
				DX12GetDescriptorSlotIdentities(
					observedSlotHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
		}
		if (observedSlotsValid) {
			StoreRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				bindingQuerySerial,
				foundAny,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				*uavs);
		}
	}
	return foundAny;
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

	// thread_local: see the note in SnapshotBindingState's pre-skin caller.
	static thread_local CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	DX12_PROF_STAGE_BEGIN(BindSnapshot);
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial)) {
		DX12_PROF_STAGE_END(BindSnapshot);
		return false;
	}
	DX12_PROF_STAGE_END(BindSnapshot);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	// Use computeTables content hash as the cache key: unchanged bindings keep the hash stable across dispatches.
	// Heap pointers can change on SetDescriptorHeaps, so they are not suitable as the key.
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
		tlsComputeAllQueryCache.computeTableHash == tableHash &&
		tlsComputeAllQueryCache.bindingQuerySerial == bindingQuerySerial &&
		tlsComputeAllQueryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		if (uavs) *uavs = tlsComputeAllQueryCache.uavs;
		if (srvs) *srvs = tlsComputeAllQueryCache.srvs;
		if (cbvs) *cbvs = tlsComputeAllQueryCache.cbvs;
		if (constants) *constants = tlsComputeAllQueryCache.constants;
		if (DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap) ==
				cbvSrvUavHeapVersion) {
			return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
				(cbvs && !cbvs->empty()) || (constants && !constants->empty());
		}
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();
	}

	std::vector<DX12CurrentComputeUavBinding> cachedUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedCbvs;
	std::vector<DX12CurrentRootConstants> cachedConstants;
	DX12_PROF_STAGE_BEGIN(BindCollect);
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectComputeAll(
		snapshot, &cachedUavs, &cachedSrvs, &cachedCbvs, &cachedConstants);
	DX12_PROF_STAGE_END(BindCollect);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		tlsComputeAllQueryCache.commandList = commandList;
		tlsComputeAllQueryCache.computeTableHash = tableHash;
		tlsComputeAllQueryCache.bindingQuerySerial = bindingQuerySerial;
		tlsComputeAllQueryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		tlsComputeAllQueryCache.uavs = cachedUavs;
		tlsComputeAllQueryCache.srvs = cachedSrvs;
		tlsComputeAllQueryCache.cbvs = cachedCbvs;
		tlsComputeAllQueryCache.constants = cachedConstants;
	} else {
		tlsComputeAllQueryCache.commandList = nullptr;
	}
	if (uavs) *uavs = cachedUavs;
	if (srvs) *srvs = cachedSrvs;
	if (cbvs) *cbvs = cachedCbvs;
	if (constants) *constants = cachedConstants;
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

bool DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT shaderRegister,
	UINT registerSpace,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	if (!bindings)
		return false;
	bindings->clear();

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
		return false;
	}

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		// thread_local: BuildRuntimeBindingSnapshot resets the object before
		// filling it, so a per-call zero-init is wasted work.
		static thread_local CommandListBindingState snapshot;
		if (!BuildRuntimeBindingSnapshot(runtimeState, &snapshot))
			return false;
		return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
			snapshot,
			compute,
			rangeType,
			shaderRegister,
			registerSpace,
			descriptorKind,
			rootDescriptorType,
			bindings);
	}

	// thread_local: see the note in SnapshotBindingState's pre-skin caller.
	static thread_local CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, nullptr))
		return false;

	return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
		snapshot,
		compute,
		rangeType,
		shaderRegister,
		registerSpace,
		descriptorKind,
		rootDescriptorType,
		bindings);
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

	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;

	// Async path: the recording thread only bundles the accumulated state
	// changes into a compact packet (no shard lock, no 60KB snapshot); the
	// worker replays it and builds the event.
	std::vector<DX12BindingDeltaEntry> packet;
	std::unique_ptr<CommandListBindingState> fullState;
	if (DX12BindingCollectQueue::BuildEventPacket(commandList, &packet, &fullState)) {
		if (DX12BindingCollectQueue::EnqueueDrawEvent(
				commandList,
				&packet,
				&fullState,
				"draw",
				descriptorEpoch,
				captureSessionId,
				drawSerial,
				vertexCountPerInstance,
				0,
				instanceCount,
				startVertexLocation,
				0,
				0,
				startInstanceLocation))
			return;
		// Queue full / stopping: restore the log and fall back to the
		// synchronous snapshot path (the pre-queue behavior).
		DX12BindingCollectQueue::RestoreEventPacket(commandList, &packet);
		if (fullState)
			DX12BindingTracker::ReleaseRetainedIaResources(fullState.get());
	}

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
	// eagerly in SetVertexBuffers/SetIndexBuffer; deferred to here for ~90%
	// per-call cost reduction).
	ResolveDeferredIaResources(&state);
	// Copy-construct directly: default-constructing first would zero ~60KB
	// (NSDMI arrays) that the copy then overwrites on every draw call.
	const CommandListBindingState snapshot(state);
	ReleaseSRWLockExclusive(&shard.lock);

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
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(
		commandList, snapshot, captureSessionId, drawSerial);
}

void DX12BindingTracker::RecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation)
{
	if (!commandList)
		return;

	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;

	std::vector<DX12BindingDeltaEntry> packet;
	std::unique_ptr<CommandListBindingState> fullState;
	if (DX12BindingCollectQueue::BuildEventPacket(commandList, &packet, &fullState)) {
		if (DX12BindingCollectQueue::EnqueueDrawEvent(
				commandList,
				&packet,
				&fullState,
				"draw_indexed",
				descriptorEpoch,
				captureSessionId,
				drawSerial,
				0,
				indexCountPerInstance,
				instanceCount,
				0,
				startIndexLocation,
				baseVertexLocation,
				startInstanceLocation))
			return;
		DX12BindingCollectQueue::RestoreEventPacket(commandList, &packet);
		if (fullState)
			DX12BindingTracker::ReleaseRetainedIaResources(fullState.get());
	}

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
	// Resolve deferred VB/IB resources before snapshot; see RecordDrawInstanced.
	ResolveDeferredIaResources(&state);
	// Copy-construct directly; see RecordDrawInstanced for why.
	const CommandListBindingState snapshot(state);
	ReleaseSRWLockExclusive(&shard.lock);

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
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(
		commandList, snapshot, captureSessionId, drawSerial);
}

void DX12BindingTracker::RecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (!commandList)
		return;

	const UINT64 dispatchSerial =
		static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDispatchSerial));
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;

	std::vector<DX12BindingDeltaEntry> packet;
	std::unique_ptr<CommandListBindingState> fullState;
	if (DX12BindingCollectQueue::BuildEventPacket(commandList, &packet, &fullState)) {
		if (DX12BindingCollectQueue::EnqueueDispatchEvent(
				commandList,
				&packet,
				&fullState,
				descriptorEpoch,
				captureSessionId,
				dispatchSerial,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ))
			return;
		DX12BindingCollectQueue::RestoreEventPacket(commandList, &packet);
		if (fullState)
			DX12BindingTracker::ReleaseRetainedIaResources(fullState.get());
	}

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
	// Copy-construct directly; see RecordDrawInstanced for why.
	const CommandListBindingState snapshot(state);
	ReleaseSRWLockExclusive(&shard.lock);

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
	DX12_PROF_COUNT("Rm.GraphicsRootDescriptorCalls");
	if (tlsGraphicsDescCache.cl == commandList &&
		tlsGraphicsDescCache.valid[rootParameterIndex] &&
		tlsGraphicsDescCache.address[rootParameterIndex] == address &&
		tlsGraphicsDescCache.type[rootParameterIndex] == type)
	{
		DX12_PROF_COUNT("Rm.GraphicsRootDescriptorTlsHits");
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.graphicsRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsDescCache.cl != commandList) {
			memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
			memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
			memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
			tlsGraphicsDescCache.cl = commandList;
		}
		tlsGraphicsDescCache.valid[rootParameterIndex] = true;
		tlsGraphicsDescCache.address[rootParameterIndex] = address;
		tlsGraphicsDescCache.type[rootParameterIndex] = type;
		DX12_PROF_COUNT("Rm.GraphicsRootDescriptorNoChange");
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	const RootDescriptorState recordedDescriptor = descriptor;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsDescCache.cl != commandList) {
		memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
		memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
		memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
		tlsGraphicsDescCache.cl = commandList;
	}
	tlsGraphicsDescCache.valid[rootParameterIndex] = true;
	tlsGraphicsDescCache.address[rootParameterIndex] = address;
	tlsGraphicsDescCache.type[rootParameterIndex] = type;
	DX12_PROF_COUNT("Rm.GraphicsRootDescriptorChanged");

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::GraphicsRootDescriptor;
	entry.index = rootParameterIndex;
	entry.descriptor = recordedDescriptor;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
}

void DX12BindingTracker::SetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12_PROF_COUNT("Rm.ComputeRootDescriptorCalls");
	if (tlsComputeDescCache.cl == commandList &&
		tlsComputeDescCache.valid[rootParameterIndex] &&
		tlsComputeDescCache.address[rootParameterIndex] == address &&
		tlsComputeDescCache.type[rootParameterIndex] == type)
	{
		DX12_PROF_COUNT("Rm.ComputeRootDescriptorTlsHits");
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.computeRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeDescCache.cl != commandList) {
			memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
			memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
			memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
			tlsComputeDescCache.cl = commandList;
		}
		tlsComputeDescCache.valid[rootParameterIndex] = true;
		tlsComputeDescCache.address[rootParameterIndex] = address;
		tlsComputeDescCache.type[rootParameterIndex] = type;
		DX12_PROF_COUNT("Rm.ComputeRootDescriptorNoChange");
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	const RootDescriptorState recordedDescriptor = descriptor;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeDescCache.cl != commandList) {
		memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
		memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
		memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
		tlsComputeDescCache.cl = commandList;
	}
	tlsComputeDescCache.valid[rootParameterIndex] = true;
	tlsComputeDescCache.address[rootParameterIndex] = address;
	tlsComputeDescCache.type[rootParameterIndex] = type;
	DX12_PROF_COUNT("Rm.ComputeRootDescriptorChanged");

	DX12BindingDeltaEntry entry;
	entry.kind = DX12BindingDeltaKind::ComputeRootDescriptor;
	entry.index = rootParameterIndex;
	entry.descriptor = recordedDescriptor;
	DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
}

bool DX12BindingTracker::SetRootConstantsLocked(
	RootConstantsState *constants, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!constants || !values || destOffset >= 64 || count == 0)
		return false;
	const UINT *src = static_cast<const UINT*>(values);
	const UINT copyCount = min(count, 64 - destOffset);
	bool changed = !constants->valid ||
		constants->rootParameterIndex != rootParameterIndex;
	constants->valid = true;
	constants->rootParameterIndex = rootParameterIndex;
	for (UINT i = 0; i < copyCount; ++i) {
		const UINT slot = destOffset + i;
		changed = changed || !constants->valueValid[slot] ||
			constants->values[slot] != src[i];
		constants->values[slot] = src[i];
		constants->valueValid[slot] = true;
		if (slot + 1 > constants->maxSet)
			constants->maxSet = slot + 1;
	}
	return changed;
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

	DX12_PROF_COUNT("Rm.GraphicsRootConstantsCalls");
	DX12_PROF_ADD_ITEMS("Rm.GraphicsRootConstantsValues", static_cast<LONG64>(count));
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.graphicsRootConstants[rootParameterIndex];
	const bool changed = DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values);
	if (changed)
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);

	if (changed) {
		DX12BindingDeltaEntry entry;
		entry.kind = DX12BindingDeltaKind::GraphicsRootConstants;
		entry.index = rootParameterIndex;
		entry.offset = destOffset;
		entry.count = min(count, 64 - destOffset);
		memcpy(
			entry.constants.values,
			values,
			entry.count * sizeof(UINT));
		DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	}
}

void DX12BindingTracker::SetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	DX12_PROF_COUNT("Rm.ComputeRootConstantsCalls");
	DX12_PROF_ADD_ITEMS("Rm.ComputeRootConstantsValues", static_cast<LONG64>(count));
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.computeRootConstants[rootParameterIndex];
	const bool changed = DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values);
	if (changed) {
		++state.computeBindingSerial;
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	}
	ReleaseSRWLockExclusive(&shard.lock);

	if (changed) {
		DX12BindingDeltaEntry entry;
		entry.kind = DX12BindingDeltaKind::ComputeRootConstants;
		entry.index = rootParameterIndex;
		entry.offset = destOffset;
		entry.count = min(count, 64 - destOffset);
		memcpy(
			entry.constants.values,
			values,
			entry.count * sizeof(UINT));
		DX12BindingCollectQueue::AppendDeltaEntry(commandList, entry);
	}
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

