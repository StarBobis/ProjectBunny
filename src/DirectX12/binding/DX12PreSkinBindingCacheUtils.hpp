#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <stdint.h>
#include <string.h>

#include <vector>

#include "DX12BindingTrackerQuery.hpp"

class DX12PreSkinBindingRootTableCacheKey
{
public:
	UINT rootParameterIndex = 0;
	UINT64 baseDescriptorPtr = 0;
	bool resolvedHeap = false;
	ID3D12DescriptorHeap *heap = nullptr;
	SIZE_T heapCpuStart = 0;
	UINT64 heapGpuStart = 0;
	UINT heapIncrement = 0;
	UINT heapNumDescriptors = 0;
	UINT64 heapVersion = 0;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
};

class DX12PreSkinBindingRootDescriptorCacheKey
{
public:
	UINT rootParameterIndex = 0;
	UINT type = 0;
	D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};

class DX12PreSkinBindingRootConstantsCacheKey
{
public:
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};

class DX12PreSkinBindingCacheKey
{
public:
	UINT64 fingerprint = 0;
	UINT64 bufferResolveEpoch = 0;
	ID3D12RootSignature *computeRootSignature = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12PreSkinBindingRootTableCacheKey> rootTables;
	std::vector<DX12PreSkinBindingRootDescriptorCacheKey> rootDescriptors;
	std::vector<DX12PreSkinBindingRootConstantsCacheKey> rootConstants;
};

class DX12PreSkinBindingCacheEntry
{
public:
	bool valid = false;
	UINT64 age = 0;
	bool foundAny = false;
	DX12PreSkinBindingCacheKey key;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> constants;
};

class DX12PreSkinBindingTlsCache
{
public:
	UINT64 clock = 0;
	std::vector<DX12PreSkinBindingCacheEntry> entries;
};

class DX12PreSkinBindingCacheUtils
{
public:
	// The target title presents roughly 100-110 distinct compute binding
	// patterns per frame. A 64-entry LRU is smaller than that cyclic working set
	// and therefore evicts every entry immediately before the next-frame reuse.
	static constexpr size_t CacheCapacity = 256;

	static void MixValue(DX12PreSkinBindingCacheKey *key, UINT64 value)
	{
		if (!key)
			return;
		key->fingerprint ^= value + 0x9e3779b97f4a7c15ull +
			(key->fingerprint << 6) + (key->fingerprint >> 2);
	}

	static void MixPointer(DX12PreSkinBindingCacheKey *key, const void *value)
	{
		DX12PreSkinBindingCacheUtils::MixValue(
			key,
			static_cast<UINT64>(reinterpret_cast<uintptr_t>(value)));
	}

	static void InitializeKey(
		const CommandListBindingState &snapshot,
		UINT64 cbvSrvUavHeapVersion,
		UINT64 bufferResolveEpoch,
		DX12PreSkinBindingCacheKey *key)
	{
		if (!key)
			return;

		*key = DX12PreSkinBindingCacheKey();
		key->fingerprint = 14695981039346656037ull;
		key->computeRootSignature = snapshot.computeRootSignature;
		key->cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		key->cbvSrvUavHeapVersion = cbvSrvUavHeapVersion;
		key->bufferResolveEpoch = bufferResolveEpoch;
		key->rootTables.reserve(snapshot.computeTableValidCount);
		key->rootDescriptors.reserve(MaxRootParameters);
		key->rootConstants.reserve(MaxRootParameters);

		DX12PreSkinBindingCacheUtils::MixPointer(key, key->computeRootSignature);
		DX12PreSkinBindingCacheUtils::MixPointer(key, key->cbvSrvUavHeap);
		DX12PreSkinBindingCacheUtils::MixValue(key, key->cbvSrvUavHeapVersion);
	}

	static void AddRootTable(
		DX12PreSkinBindingCacheKey *key,
		UINT rootParameterIndex,
		const RootTableState &table,
		bool resolvedHeap,
		const DX12DescriptorHeapSummary &heap)
	{
		if (!key)
			return;

		DX12PreSkinBindingRootTableCacheKey tableKey;
		tableKey.rootParameterIndex = rootParameterIndex;
		tableKey.baseDescriptorPtr = table.baseDescriptor.ptr;
		if (resolvedHeap) {
			tableKey.resolvedHeap = true;
			tableKey.heap = heap.heap;
			tableKey.heapCpuStart = heap.cpuStart;
			tableKey.heapGpuStart = heap.gpuStart;
			tableKey.heapIncrement = heap.increment;
			tableKey.heapNumDescriptors = heap.numDescriptors;
			tableKey.heapVersion = heap.version;
		}

		key->rootTables.push_back(tableKey);
		DX12PreSkinBindingCacheUtils::MixValue(key, rootParameterIndex);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.baseDescriptorPtr);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.resolvedHeap ? 1 : 0);
		DX12PreSkinBindingCacheUtils::MixPointer(key, tableKey.heap);
		DX12PreSkinBindingCacheUtils::MixValue(
			key, static_cast<UINT64>(tableKey.heapCpuStart));
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.heapGpuStart);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.heapIncrement);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.heapNumDescriptors);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.heapVersion);
	}

	static void AddRootDescriptor(
		DX12PreSkinBindingCacheKey *key,
		UINT rootParameterIndex,
		const RootDescriptorState &rootDescriptor)
	{
		if (!key)
			return;

		DX12PreSkinBindingRootDescriptorCacheKey descriptorKey;
		descriptorKey.rootParameterIndex = rootParameterIndex;
		descriptorKey.type = static_cast<UINT>(rootDescriptor.type);
		descriptorKey.address = rootDescriptor.address;
		key->rootDescriptors.push_back(descriptorKey);
		DX12PreSkinBindingCacheUtils::MixValue(key, rootParameterIndex);
		DX12PreSkinBindingCacheUtils::MixValue(key, descriptorKey.type);
		DX12PreSkinBindingCacheUtils::MixValue(key, descriptorKey.address);
	}

	static void AddRootConstants(
		DX12PreSkinBindingCacheKey *key,
		UINT rootParameterIndex,
		const RootConstantsState &rootConstants)
	{
		if (!key)
			return;

		DX12PreSkinBindingRootConstantsCacheKey constantsKey;
		constantsKey.rootParameterIndex = rootParameterIndex;
		constantsKey.maxSet = rootConstants.maxSet;
		memcpy(constantsKey.values, rootConstants.values, sizeof(constantsKey.values));
		memcpy(
			constantsKey.valueValid,
			rootConstants.valueValid,
			sizeof(constantsKey.valueValid));
		key->rootConstants.push_back(constantsKey);
		DX12PreSkinBindingCacheUtils::MixValue(key, rootParameterIndex);
		DX12PreSkinBindingCacheUtils::MixValue(key, constantsKey.maxSet);
		for (UINT index = 0; index < 64; ++index) {
			DX12PreSkinBindingCacheUtils::MixValue(key, constantsKey.values[index]);
			DX12PreSkinBindingCacheUtils::MixValue(
				key, constantsKey.valueValid[index] ? 1 : 0);
		}
	}

	template <class ResolverUtils>
	static void BuildKey(
		const CommandListBindingState &snapshot,
		UINT64 cbvSrvUavHeapVersion,
		UINT64 bufferResolveEpoch,
		const DX12RootSignatureSummary *rootSignature,
		DX12PreSkinBindingCacheKey *key)
	{
		if (!key)
			return;

		// A heap-wide version changes for tens of thousands of unrelated copies
		// every frame. With a parsed root signature, observe only the descriptor
		// slots that this compute binding can actually consume.
		DX12PreSkinBindingCacheUtils::InitializeKey(
			snapshot,
			rootSignature ? 0 : cbvSrvUavHeapVersion,
			bufferResolveEpoch,
			key);

		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = snapshot.computeTables[root];
			if (table.valid) {
				DX12DescriptorHeapSummary heap = {};
				const bool resolvedHeap = ResolverUtils::ResolveTableHeap(
					snapshot, table, &heap);
				DX12DescriptorHeapSummary identityHeap = heap;
				if (rootSignature)
					identityHeap.version = 0;
				DX12PreSkinBindingCacheUtils::AddRootTable(
					key, root, table, resolvedHeap, identityHeap);

				if (resolvedHeap && rootSignature && heap.increment) {
					const DX12RootParameterSummary *parameter =
						DX12BindingTrackerQuery::FindRootParameter(*rootSignature, root);
					if (parameter && parameter->parameterType ==
							D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
						DX12PreSkinBindingRootTableCacheKey &tableKey =
							key->rootTables.back();
						const UINT64 tableBaseIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
							if (range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_UAV &&
								range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
								range.rangeType != D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
								continue;
							const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
								heap, tableBaseIndex, range);
							for (UINT offset = 0; offset < count; ++offset) {
								tableKey.observedSlots.push_back(static_cast<UINT>(
									tableBaseIndex + range.effectiveOffset + offset));
							}
						}
						tableKey.observedSlotVersions.resize(
							tableKey.observedSlots.size());
						tableKey.observedSlotValueIds.resize(
							tableKey.observedSlots.size());
						if (!ResolverUtils::ReadDescriptorSlotIdentities(
								heap.heap,
								tableKey.observedSlots.data(),
								tableKey.observedSlots.size(),
								tableKey.observedSlotVersions.data(),
								tableKey.observedSlotValueIds.data())) {
							tableKey.observedSlots.clear();
							tableKey.observedSlotVersions.clear();
							tableKey.observedSlotValueIds.clear();
							tableKey.heapVersion = heap.version;
							DX12PreSkinBindingCacheUtils::MixValue(key, heap.version);
						} else {
							for (size_t slotIndex = 0;
									slotIndex < tableKey.observedSlots.size(); ++slotIndex) {
								// A stable Descriptor Value ID survives repeated copies of the
								// same value. Fall back to the mutation version only when the
								// slot has no authoritative current value.
								if (tableKey.observedSlotValueIds[slotIndex])
									tableKey.observedSlotVersions[slotIndex] = 0;
								DX12PreSkinBindingCacheUtils::MixValue(
									key, tableKey.observedSlots[slotIndex]);
								DX12PreSkinBindingCacheUtils::MixValue(
									key, tableKey.observedSlotVersions[slotIndex]);
								DX12PreSkinBindingCacheUtils::MixValue(
									key, tableKey.observedSlotValueIds[slotIndex]);
							}
						}
					}
				}
			}

			const RootDescriptorState &rootDescriptor =
				snapshot.computeRootDescriptors[root];
			if (rootDescriptor.valid) {
				DX12PreSkinBindingCacheUtils::AddRootDescriptor(
					key, root, rootDescriptor);
			}

			const RootConstantsState &rootConstants =
				snapshot.computeRootConstants[root];
			if (rootConstants.valid) {
				DX12PreSkinBindingCacheUtils::AddRootConstants(
					key, root, rootConstants);
			}
		}
	}

	static bool RootTablesEqual(
		const DX12PreSkinBindingRootTableCacheKey &left,
		const DX12PreSkinBindingRootTableCacheKey &right)
	{
		return left.rootParameterIndex == right.rootParameterIndex &&
			left.baseDescriptorPtr == right.baseDescriptorPtr &&
			left.resolvedHeap == right.resolvedHeap &&
			left.heap == right.heap &&
			left.heapCpuStart == right.heapCpuStart &&
				left.heapGpuStart == right.heapGpuStart &&
				left.heapIncrement == right.heapIncrement &&
				left.heapNumDescriptors == right.heapNumDescriptors &&
				left.heapVersion == right.heapVersion &&
			left.observedSlots == right.observedSlots &&
				left.observedSlotVersions == right.observedSlotVersions &&
				left.observedSlotValueIds == right.observedSlotValueIds;
	}

	static bool RootDescriptorsEqual(
		const DX12PreSkinBindingRootDescriptorCacheKey &left,
		const DX12PreSkinBindingRootDescriptorCacheKey &right)
	{
		return left.rootParameterIndex == right.rootParameterIndex &&
			left.type == right.type &&
			left.address == right.address;
	}

	static bool RootConstantsEqual(
		const DX12PreSkinBindingRootConstantsCacheKey &left,
		const DX12PreSkinBindingRootConstantsCacheKey &right)
	{
		if (left.rootParameterIndex != right.rootParameterIndex ||
			left.maxSet != right.maxSet)
			return false;
		return memcmp(left.values, right.values, sizeof(left.values)) == 0 &&
			memcmp(left.valueValid, right.valueValid, sizeof(left.valueValid)) == 0;
	}

	static bool KeysEqual(
		const DX12PreSkinBindingCacheKey &left,
		const DX12PreSkinBindingCacheKey &right)
	{
		if (left.fingerprint != right.fingerprint ||
			left.computeRootSignature != right.computeRootSignature ||
			left.cbvSrvUavHeap != right.cbvSrvUavHeap ||
			left.cbvSrvUavHeapVersion != right.cbvSrvUavHeapVersion ||
			left.rootTables.size() != right.rootTables.size() ||
			left.rootDescriptors.size() != right.rootDescriptors.size() ||
			left.rootConstants.size() != right.rootConstants.size())
			return false;

		for (size_t index = 0; index < left.rootTables.size(); ++index) {
			if (!DX12PreSkinBindingCacheUtils::RootTablesEqual(
					left.rootTables[index], right.rootTables[index]))
				return false;
		}
		for (size_t index = 0; index < left.rootDescriptors.size(); ++index) {
			if (!DX12PreSkinBindingCacheUtils::RootDescriptorsEqual(
					left.rootDescriptors[index], right.rootDescriptors[index]))
				return false;
		}
		for (size_t index = 0; index < left.rootConstants.size(); ++index) {
			if (!DX12PreSkinBindingCacheUtils::RootConstantsEqual(
					left.rootConstants[index], right.rootConstants[index]))
				return false;
		}

		return true;
	}

	using BufferRangeValidator = bool (*)(UINT64, UINT64, UINT64);

	static bool RootDescriptorRangesStillValid(
		const DX12PreSkinBindingCacheKey &cached,
		const DX12PreSkinBindingCacheKey &current,
		BufferRangeValidator validator)
	{
		if (cached.bufferResolveEpoch == current.bufferResolveEpoch ||
				cached.rootDescriptors.empty())
			return true;
		if (!validator)
			return false;
		for (const DX12PreSkinBindingRootDescriptorCacheKey &descriptor :
				cached.rootDescriptors) {
			if (!descriptor.address || !validator(
					cached.bufferResolveEpoch, descriptor.address, 1))
				return false;
		}
		return true;
	}

	static bool TryLoad(
		DX12PreSkinBindingTlsCache *cache,
		const DX12PreSkinBindingCacheKey &key,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants,
		bool *foundAny,
		BufferRangeValidator bufferRangeValidator = nullptr)
	{
		if (!cache)
			return false;
		for (size_t index = 0; index < cache->entries.size(); ++index) {
			DX12PreSkinBindingCacheEntry &entry = cache->entries[index];
			if (!entry.valid ||
					!DX12PreSkinBindingCacheUtils::KeysEqual(entry.key, key) ||
					!DX12PreSkinBindingCacheUtils::RootDescriptorRangesStillValid(
						entry.key, key, bufferRangeValidator))
				continue;
			entry.key.bufferResolveEpoch = key.bufferResolveEpoch;
			entry.age = ++cache->clock;
			if (uavs) *uavs = entry.uavs;
			if (srvs) *srvs = entry.srvs;
			if (cbvs) *cbvs = entry.cbvs;
			if (constants) *constants = entry.constants;
			if (foundAny) *foundAny = entry.foundAny;
			return true;
		}
		return false;
	}

	static void Store(
		DX12PreSkinBindingTlsCache *cache,
		const DX12PreSkinBindingCacheKey &key,
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &constants,
		bool foundAny)
	{
		if (!cache)
			return;

		DX12PreSkinBindingCacheEntry *target = nullptr;
		if (cache->entries.size() < DX12PreSkinBindingCacheUtils::CacheCapacity) {
			DX12PreSkinBindingCacheEntry entry;
			cache->entries.push_back(entry);
			target = &cache->entries[cache->entries.size() - 1];
		} else {
			size_t targetIndex = 0;
			UINT64 oldestAge = cache->entries[0].age;
			for (size_t index = 1; index < cache->entries.size(); ++index) {
				if (cache->entries[index].age < oldestAge) {
					oldestAge = cache->entries[index].age;
					targetIndex = index;
				}
			}
			target = &cache->entries[targetIndex];
		}

		target->valid = true;
		target->age = ++cache->clock;
		target->foundAny = foundAny;
		target->key = key;
		target->uavs = uavs;
		target->srvs = srvs;
		target->cbvs = cbvs;
		target->constants = constants;
	}
};
