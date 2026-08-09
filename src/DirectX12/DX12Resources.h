// ============================================================
// DX12Resources.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Runtime.h"

// ===================== Core/DX12FeatureCore.hpp =====================

#include <d3d11.h>

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain);
void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12NativeOverlayShutdown();
bool DX12NativeOverlayIsOverlayWindow(HWND hwnd);

class DX12DescriptorHeapReleaseTracker : public IUnknown
{
public:
	explicit DX12DescriptorHeapReleaseTracker(ID3D12DescriptorHeap *heap) : mRef(1), mHeap(heap) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
	{
		if (!object)
			return E_POINTER;
		if (riid == IID_IUnknown) {
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(InterlockedIncrement(&mRef));
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG ref = InterlockedDecrement(&mRef);
		if (ref == 0) {
			if (mInstalled) {
				DX12_CREATE_TIMING_SCOPE(DescriptorHeapInvalidate, Record);
				AcquireSRWLockExclusive(&gDescriptorLock);
				InvalidateDescriptorHeapLocked(mHeap);
				ReleaseSRWLockExclusive(&gDescriptorLock);
			}
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12DescriptorHeap *mHeap;
	bool mInstalled = false;
};

class DX12ResourceReleaseTracker : public IUnknown
{
public:
	explicit DX12ResourceReleaseTracker(ID3D12Resource *resource) : mRef(1), mResource(resource) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
	{
		if (!object)
			return E_POINTER;
		if (riid == IID_IUnknown) {
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(InterlockedIncrement(&mRef));
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG ref = InterlockedDecrement(&mRef);
		if (ref == 0) {
			if (mInstalled) {
				DX12_CREATE_TIMING_SCOPE(ResourceInvalidate, Record);
				AcquireSRWLockExclusive(&gResourceLock);
				InvalidateResourceLocked(mResource);
				ReleaseSRWLockExclusive(&gResourceLock);
			}
			delete this;
		}
		return static_cast<ULONG>(ref);
	}

	volatile LONG mRef;
	ID3D12Resource *mResource;
	bool mInstalled = false;
};


class DX12ResourceTrackerPrivateDataGuids
{
public:
	inline static const GUID DescriptorHeapReleaseTracker =
	{0x28c66065, 0x55b7, 0x46df, {0x9e, 0x1e, 0x08, 0x52, 0x8d, 0x31, 0xcb, 0x1a}};
	inline static const GUID ResourceReleaseTracker =
	{0xe1a4bb6a, 0xf6fc, 0x4a80, {0x90, 0xa4, 0xf5, 0xb9, 0x13, 0x32, 0xf1, 0x5c}};
};



class DX12ObjectLifetimeTrackerUtils
{
public:
	static HRESULT InstallResourceReleaseTracker(ID3D12Resource *resource)
	{
		if (!resource)
			return E_POINTER;

		DX12ResourceReleaseTracker *tracker =
			new (std::nothrow) DX12ResourceReleaseTracker(resource);
		if (!tracker)
			return E_OUTOFMEMORY;

		const HRESULT hr = resource->SetPrivateDataInterface(
			DX12ResourceTrackerPrivateDataGuids::ResourceReleaseTracker,
			tracker);
		if (SUCCEEDED(hr))
			tracker->mInstalled = true;
		tracker->Release();

		DX12LogManager::Fields("DX12ObjectLifetimeTracker",
			"kind=resource,stage=install,resource=%p,hr=0x%lx",
			resource, hr);
		return hr;
	}

	static HRESULT InstallDescriptorHeapReleaseTracker(ID3D12DescriptorHeap *heap)
	{
		if (!heap)
			return E_POINTER;

		DX12DescriptorHeapReleaseTracker *tracker =
			new (std::nothrow) DX12DescriptorHeapReleaseTracker(heap);
		if (!tracker)
			return E_OUTOFMEMORY;

		const HRESULT hr = heap->SetPrivateDataInterface(
			DX12ResourceTrackerPrivateDataGuids::DescriptorHeapReleaseTracker,
			tracker);
		if (SUCCEEDED(hr))
			tracker->mInstalled = true;
		tracker->Release();

		DX12LogManager::Fields("DX12ObjectLifetimeTracker",
			"kind=descriptor_heap,stage=install,heap=%p,hr=0x%lx",
			heap, hr);
		return hr;
	}
};





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
	std::unordered_multimap<UINT64, size_t> fingerprintIndex;
	size_t growthBudget = 256;
};

class DX12FeatureCacheGrowthUtils
{
public:
	static size_t NextBudget(size_t currentBudget, size_t minRequired)
	{
		size_t next = currentBudget ? currentBudget : 256;
		while (next < minRequired) {
			const size_t doubled = next * 2;
			if (doubled <= next) {
				next = minRequired;
				break;
			}
			next = doubled;
		}
		return next;
	}

	template <class AssocContainer>
	static void EnsureBudget(
		size_t *budget,
		AssocContainer *entries,
		size_t minRequired)
	{
		if (!budget || !entries || minRequired <= *budget)
			return;
		const size_t next = NextBudget(*budget, minRequired);
		entries->reserve(next);
		*budget = next;
	}
};

class DX12PreSkinBindingCacheUtils
{
public:
	// Start at the measured working set, then grow when a title exceeds it
	// instead of thrashing through LRU eviction every frame.
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

	template <class Snapshot>
	static void InitializeKey(
		const Snapshot &snapshot,
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
		const DX12DescriptorHeapSummary &heap,
		bool normalizeRingLocation)
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
		if (!normalizeRingLocation)
			DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.baseDescriptorPtr);
		DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.resolvedHeap ? 1 : 0);
		DX12PreSkinBindingCacheUtils::MixPointer(key, tableKey.heap);
		if (!normalizeRingLocation) {
			DX12PreSkinBindingCacheUtils::MixValue(
				key, static_cast<UINT64>(tableKey.heapCpuStart));
			DX12PreSkinBindingCacheUtils::MixValue(key, tableKey.heapGpuStart);
		}
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

	template <class ResolverUtils, class Snapshot>
	static void BuildKey(
		const Snapshot &snapshot,
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
					key, root, table, resolvedHeap, identityHeap,
					rootSignature != nullptr);

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
								tableKey.observedSlotValueIds.data(),
								&heap)) {
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
								// Ring slot/location is transient. Value identity is the
								// semantic dependency and survives descriptor-ring rotation.
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
		const bool contentAddressed = !left.observedSlotValueIds.empty() &&
			!right.observedSlotValueIds.empty();
		return left.rootParameterIndex == right.rootParameterIndex &&
			(contentAddressed || left.baseDescriptorPtr == right.baseDescriptorPtr) &&
			left.resolvedHeap == right.resolvedHeap &&
			left.heap == right.heap &&
			(contentAddressed || left.heapCpuStart == right.heapCpuStart) &&
				(contentAddressed || left.heapGpuStart == right.heapGpuStart) &&
				left.heapIncrement == right.heapIncrement &&
				left.heapNumDescriptors == right.heapNumDescriptors &&
				left.heapVersion == right.heapVersion &&
			(contentAddressed || left.observedSlots == right.observedSlots) &&
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

	static void RebaseBindings(
		std::vector<DX12CurrentComputeUavBinding> *bindings,
		const DX12PreSkinBindingCacheKey &key)
	{
		if (!bindings)
			return;
		for (DX12CurrentComputeUavBinding &binding : *bindings) {
			if (binding.rootDescriptor)
				continue;
			for (const DX12PreSkinBindingRootTableCacheKey &table : key.rootTables) {
				if (table.rootParameterIndex != binding.rootParameterIndex ||
					!table.resolvedHeap || !table.heapIncrement)
					continue;
				const UINT64 baseIndex =
					(table.baseDescriptorPtr - table.heapGpuStart) / table.heapIncrement;
				binding.tableHeap = table.heap;
				binding.tableCpuStart = table.heapCpuStart +
					static_cast<SIZE_T>(baseIndex) * table.heapIncrement;
				binding.tableGpuStart.ptr = table.baseDescriptorPtr;
				binding.descriptorIncrement = table.heapIncrement;
				if (binding.hasDescriptor)
					binding.descriptor.cpuHandle = binding.tableCpuStart +
						static_cast<SIZE_T>(binding.descriptorOffset) * table.heapIncrement;
				break;
			}
		}
	}

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
		const auto candidates = cache->fingerprintIndex.equal_range(key.fingerprint);
		for (auto candidate = candidates.first;
				candidate != candidates.second; ++candidate) {
			const size_t index = candidate->second;
			if (index >= cache->entries.size())
				continue;
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
			DX12PreSkinBindingCacheUtils::RebaseBindings(uavs, key);
			DX12PreSkinBindingCacheUtils::RebaseBindings(srvs, key);
			DX12PreSkinBindingCacheUtils::RebaseBindings(cbvs, key);
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
		size_t targetIndex = 0;
		const auto existingMappings =
			cache->fingerprintIndex.equal_range(key.fingerprint);
		for (auto mapping = existingMappings.first;
				mapping != existingMappings.second; ++mapping) {
			const size_t index = mapping->second;
			if (index >= cache->entries.size())
				continue;
			DX12PreSkinBindingCacheEntry &entry = cache->entries[index];
			if (!entry.valid ||
					!DX12PreSkinBindingCacheUtils::KeysEqual(entry.key, key))
				continue;
			target = &entry;
			targetIndex = index;
			break;
		}
		const bool hasExistingEntry = target != nullptr;
		if (!target) {
			const size_t required = cache->entries.size() + 1;
			if (required > cache->growthBudget) {
				const size_t nextBudget =
					DX12FeatureCacheGrowthUtils::NextBudget(
						cache->growthBudget, required);
				cache->entries.reserve(nextBudget);
				cache->fingerprintIndex.reserve(nextBudget * 2);
				cache->growthBudget = nextBudget;
			}
			cache->entries.push_back(DX12PreSkinBindingCacheEntry());
			targetIndex = cache->entries.size() - 1;
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
		if (!hasExistingEntry)
			cache->fingerprintIndex.emplace(key.fingerprint, targetIndex);
	}
};


class DX12PreSkinOutputResizeCacheIdentity
{
public:
	std::uintptr_t originalResource = 0;
	std::uint64_t requiredByteWidth = 0;
	std::uint32_t stride = 0;
	std::uint32_t ringSlot = 0;

	bool operator==(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		return originalResource == rhs.originalResource &&
			requiredByteWidth == rhs.requiredByteWidth &&
			stride == rhs.stride &&
			ringSlot == rhs.ringSlot;
	}

	bool operator!=(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		return !(*this == rhs);
	}

	bool operator<(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		if (originalResource != rhs.originalResource)
			return originalResource < rhs.originalResource;
		if (requiredByteWidth != rhs.requiredByteWidth)
			return requiredByteWidth < rhs.requiredByteWidth;
		if (stride != rhs.stride)
			return stride < rhs.stride;
		return ringSlot < rhs.ringSlot;
	}
};




class DX12PsoDecisionCacheDispatchKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;
	bool activePreSkinProbe = false;

	bool operator==(const DX12PsoDecisionCacheDispatchKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			activeShaderOverrides == other.activeShaderOverrides &&
			activePreSkinProbe == other.activePreSkinProbe;
	}
};

class DX12PsoDecisionCacheDispatchKeyHash
{
public:
	size_t operator()(const DX12PsoDecisionCacheDispatchKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash ^= key.reloadGeneration + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<size_t>(key.activeShaderOverrides) * 0xff51afd7ed558ccdull;
		hash ^= static_cast<size_t>(key.activePreSkinProbe) * 0xc4ceb9fe1a85ec53ull;
		return hash;
	}
};

class DX12PsoDecisionCacheDrawKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;

	bool operator==(const DX12PsoDecisionCacheDrawKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			activeShaderOverrides == other.activeShaderOverrides;
	}
};

class DX12PsoDecisionCacheDrawKeyHash
{
public:
	size_t operator()(const DX12PsoDecisionCacheDrawKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash ^= key.reloadGeneration + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<size_t>(key.activeShaderOverrides) * 0xff51afd7ed558ccdull;
		return hash;
	}
};

class DX12PsoDecisionCacheDispatchEntry
{
public:
	DX12CompiledDispatchDecision decision;
};

class DX12PsoDecisionCacheDrawEntry
{
public:
	DX12CompiledDrawDecision decision;
};

class DX12PsoDecisionCacheUtils
{
public:
	// Decisions are immutable for a reload generation. Command-list recording is
	// already thread-affine, so a process-wide LRU only serialized otherwise
	// independent recording threads on every draw/dispatch cache hit. Keep the
	// resident PSO decisions in thread-local generation arenas instead: reads are
	// lock-free and a generation change discards the complete arena in O(1).
	inline static thread_local UINT64 ReloadGeneration = 0;
	static constexpr UINT64 CacheFormatVersionCurrent = 5;
	inline static thread_local UINT64 CacheFormatVersion = 0;
	static constexpr size_t InitialEntriesPerKind = 4096;
	inline static thread_local size_t DispatchGrowthBudget = InitialEntriesPerKind;
	inline static thread_local size_t DrawGrowthBudget = InitialEntriesPerKind;
	inline static thread_local std::unordered_map<
		DX12PsoDecisionCacheDispatchKey,
		DX12PsoDecisionCacheDispatchEntry,
		DX12PsoDecisionCacheDispatchKeyHash> DispatchEntries;
	inline static thread_local std::unordered_map<
		DX12PsoDecisionCacheDrawKey,
		DX12PsoDecisionCacheDrawEntry,
		DX12PsoDecisionCacheDrawKeyHash> DrawEntries;

	static void ClearIfReloadChanged(UINT64 reloadGeneration)
	{
		if (DX12PsoDecisionCacheUtils::ReloadGeneration != reloadGeneration ||
			DX12PsoDecisionCacheUtils::CacheFormatVersion !=
				CacheFormatVersionCurrent) {
			DX12PsoDecisionCacheUtils::ReloadGeneration = reloadGeneration;
			DX12PsoDecisionCacheUtils::CacheFormatVersion =
				CacheFormatVersionCurrent;
			DX12PsoDecisionCacheUtils::DispatchEntries.clear();
			DX12PsoDecisionCacheUtils::DrawEntries.clear();
		}
	}

	static bool TryLoadDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		DX12CompiledDispatchDecision *decision)
	{
		if (!decision)
			return false;

		DX12PsoDecisionCacheDispatchKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;
		key.activePreSkinProbe = activePreSkinProbe;

		std::unordered_map<
			DX12PsoDecisionCacheDispatchKey,
			DX12PsoDecisionCacheDispatchEntry,
			DX12PsoDecisionCacheDispatchKeyHash>::iterator it =
			DX12PsoDecisionCacheUtils::DispatchEntries.find(key);
		if (it != DX12PsoDecisionCacheUtils::DispatchEntries.end()) {
			*decision = it->second.decision;
			return true;
		}
		return false;
	}

	static void StoreDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		const DX12CompiledDispatchDecision &decision)
	{
		DX12PsoDecisionCacheDispatchKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;
		key.activePreSkinProbe = activePreSkinProbe;

		if (DX12PsoDecisionCacheUtils::DispatchEntries.find(key) ==
				DX12PsoDecisionCacheUtils::DispatchEntries.end()) {
			DX12FeatureCacheGrowthUtils::EnsureBudget(
				&DX12PsoDecisionCacheUtils::DispatchGrowthBudget,
				&DX12PsoDecisionCacheUtils::DispatchEntries,
				DX12PsoDecisionCacheUtils::DispatchEntries.size() + 1);
		}
		DX12PsoDecisionCacheDispatchEntry &entry =
			DX12PsoDecisionCacheUtils::DispatchEntries[key];
		entry.decision = decision;
	}

	static bool TryLoadDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		DX12CompiledDrawDecision *decision)
	{
		if (!decision)
			return false;

		DX12PsoDecisionCacheDrawKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;

		std::unordered_map<
			DX12PsoDecisionCacheDrawKey,
			DX12PsoDecisionCacheDrawEntry,
			DX12PsoDecisionCacheDrawKeyHash>::iterator it =
			DX12PsoDecisionCacheUtils::DrawEntries.find(key);
		if (it != DX12PsoDecisionCacheUtils::DrawEntries.end()) {
			*decision = it->second.decision;
			return true;
		}
		return false;
	}

	static void StoreDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		const DX12CompiledDrawDecision &decision)
	{
		DX12PsoDecisionCacheDrawKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.activeShaderOverrides = activeShaderOverrides;

		if (DX12PsoDecisionCacheUtils::DrawEntries.find(key) ==
				DX12PsoDecisionCacheUtils::DrawEntries.end()) {
			DX12FeatureCacheGrowthUtils::EnsureBudget(
				&DX12PsoDecisionCacheUtils::DrawGrowthBudget,
				&DX12PsoDecisionCacheUtils::DrawEntries,
				DX12PsoDecisionCacheUtils::DrawEntries.size() + 1);
		}
		DX12PsoDecisionCacheDrawEntry &entry =
			DX12PsoDecisionCacheUtils::DrawEntries[key];
		entry.decision = decision;
	}
};


class DX12PsoInputLayoutUtils
{
public:
	static UINT FormatSize(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 16;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 12;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return 8;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return 4;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			return 2;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
			return 1;
		default:
			return 0;
		}
	}

	static UINT FormatAlignment(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			return 4;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
			return 2;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return 1;
		default:
			return 0;
		}
	}

	static const char *FormatName(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_UNKNOWN: return "DXGI_FORMAT_UNKNOWN";
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return "DXGI_FORMAT_R32G32B32A32_FLOAT";
		case DXGI_FORMAT_R32G32B32A32_UINT: return "DXGI_FORMAT_R32G32B32A32_UINT";
		case DXGI_FORMAT_R32G32B32A32_SINT: return "DXGI_FORMAT_R32G32B32A32_SINT";
		case DXGI_FORMAT_R32G32B32_FLOAT: return "DXGI_FORMAT_R32G32B32_FLOAT";
		case DXGI_FORMAT_R32G32B32_UINT: return "DXGI_FORMAT_R32G32B32_UINT";
		case DXGI_FORMAT_R32G32B32_SINT: return "DXGI_FORMAT_R32G32B32_SINT";
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return "DXGI_FORMAT_R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R16G16B16A16_UNORM: return "DXGI_FORMAT_R16G16B16A16_UNORM";
		case DXGI_FORMAT_R16G16B16A16_UINT: return "DXGI_FORMAT_R16G16B16A16_UINT";
		case DXGI_FORMAT_R16G16B16A16_SNORM: return "DXGI_FORMAT_R16G16B16A16_SNORM";
		case DXGI_FORMAT_R16G16B16A16_SINT: return "DXGI_FORMAT_R16G16B16A16_SINT";
		case DXGI_FORMAT_R32G32_FLOAT: return "DXGI_FORMAT_R32G32_FLOAT";
		case DXGI_FORMAT_R32G32_UINT: return "DXGI_FORMAT_R32G32_UINT";
		case DXGI_FORMAT_R32G32_SINT: return "DXGI_FORMAT_R32G32_SINT";
		case DXGI_FORMAT_R10G10B10A2_UNORM: return "DXGI_FORMAT_R10G10B10A2_UNORM";
		case DXGI_FORMAT_R10G10B10A2_UINT: return "DXGI_FORMAT_R10G10B10A2_UINT";
		case DXGI_FORMAT_R11G11B10_FLOAT: return "DXGI_FORMAT_R11G11B10_FLOAT";
		case DXGI_FORMAT_R8G8B8A8_UNORM: return "DXGI_FORMAT_R8G8B8A8_UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
		case DXGI_FORMAT_R8G8B8A8_UINT: return "DXGI_FORMAT_R8G8B8A8_UINT";
		case DXGI_FORMAT_R8G8B8A8_SNORM: return "DXGI_FORMAT_R8G8B8A8_SNORM";
		case DXGI_FORMAT_R8G8B8A8_SINT: return "DXGI_FORMAT_R8G8B8A8_SINT";
		case DXGI_FORMAT_R16G16_FLOAT: return "DXGI_FORMAT_R16G16_FLOAT";
		case DXGI_FORMAT_R16G16_UNORM: return "DXGI_FORMAT_R16G16_UNORM";
		case DXGI_FORMAT_R16G16_UINT: return "DXGI_FORMAT_R16G16_UINT";
		case DXGI_FORMAT_R16G16_SNORM: return "DXGI_FORMAT_R16G16_SNORM";
		case DXGI_FORMAT_R16G16_SINT: return "DXGI_FORMAT_R16G16_SINT";
		case DXGI_FORMAT_R32_FLOAT: return "DXGI_FORMAT_R32_FLOAT";
		case DXGI_FORMAT_R32_UINT: return "DXGI_FORMAT_R32_UINT";
		case DXGI_FORMAT_R32_SINT: return "DXGI_FORMAT_R32_SINT";
		case DXGI_FORMAT_R8G8_UNORM: return "DXGI_FORMAT_R8G8_UNORM";
		case DXGI_FORMAT_R8G8_UINT: return "DXGI_FORMAT_R8G8_UINT";
		case DXGI_FORMAT_R8G8_SNORM: return "DXGI_FORMAT_R8G8_SNORM";
		case DXGI_FORMAT_R8G8_SINT: return "DXGI_FORMAT_R8G8_SINT";
		case DXGI_FORMAT_R16_FLOAT: return "DXGI_FORMAT_R16_FLOAT";
		case DXGI_FORMAT_R16_UNORM: return "DXGI_FORMAT_R16_UNORM";
		case DXGI_FORMAT_R16_UINT: return "DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R16_SNORM: return "DXGI_FORMAT_R16_SNORM";
		case DXGI_FORMAT_R16_SINT: return "DXGI_FORMAT_R16_SINT";
		case DXGI_FORMAT_R8_UNORM: return "DXGI_FORMAT_R8_UNORM";
		case DXGI_FORMAT_R8_UINT: return "DXGI_FORMAT_R8_UINT";
		case DXGI_FORMAT_R8_SNORM: return "DXGI_FORMAT_R8_SNORM";
		case DXGI_FORMAT_R8_SINT: return "DXGI_FORMAT_R8_SINT";
		case DXGI_FORMAT_A8_UNORM: return "DXGI_FORMAT_A8_UNORM";
		case DXGI_FORMAT_B8G8R8A8_UNORM: return "DXGI_FORMAT_B8G8R8A8_UNORM";
		case DXGI_FORMAT_B8G8R8X8_UNORM: return "DXGI_FORMAT_B8G8R8X8_UNORM";
		default: return "DXGI_FORMAT_OTHER";
		}
	}

	static float Float16(uint16_t f16)
	{
		uint32_t f32 = ((f16 & 0x8000) << 16) | ((f16 & 0x03ff) << 13);
		int biasedExponent = (f16 & 0x7c00) >> 10;
		if (biasedExponent == 0x1f) {
			f32 |= 0x7f800000;
		} else if (biasedExponent) {
			biasedExponent = biasedExponent - 15 + 127;
			f32 |= static_cast<uint32_t>(biasedExponent) << 23;
		}
		float result = 0.0f;
		memcpy(&result, &f32, sizeof(result));
		return result;
	}

	static float Unorm24(uint32_t value) { return static_cast<float>(value) / static_cast<float>(0xffffff); }
	static float Unorm16(uint16_t value) { return static_cast<float>(value) / static_cast<float>(0xffff); }
	static float Snorm16(int16_t value) { return static_cast<float>(value) / static_cast<float>(0x7fff); }
	static float Unorm8(uint8_t value) { return static_cast<float>(value) / static_cast<float>(0xff); }
	static float Snorm8(int8_t value) { return static_cast<float>(value) / static_cast<float>(0x7f); }

	static int WriteFormatValue(FILE *file, DXGI_FORMAT format, const unsigned char *data)
	{
		if (!file || !data)
			return 0;

		const float *f = reinterpret_cast<const float*>(data);
		const uint32_t *u32 = reinterpret_cast<const uint32_t*>(data);
		const int32_t *s32 = reinterpret_cast<const int32_t*>(data);
		const uint16_t *u16 = reinterpret_cast<const uint16_t*>(data);
		const int16_t *s16 = reinterpret_cast<const int16_t*>(data);
		const uint8_t *u8 = reinterpret_cast<const uint8_t*>(data);
		const int8_t *s8 = reinterpret_cast<const int8_t*>(data);

		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return fprintf(file, "%08x, %08x, %08x, %08x", u32[0], u32[1], u32[2], u32[3]);
		case DXGI_FORMAT_R32G32B32_TYPELESS:
			return fprintf(file, "%08x, %08x, %08x", u32[0], u32[1], u32[2]);
		case DXGI_FORMAT_R32G32_TYPELESS:
			return fprintf(file, "%08x, %08x", u32[0], u32[1]);
		case DXGI_FORMAT_R32_TYPELESS:
			return fprintf(file, "%08x", u32[0]);
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", f[0], f[1], f[2], f[3]);
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return fprintf(file, "%.9g, %.9g, %.9g", f[0], f[1], f[2]);
		case DXGI_FORMAT_R32G32_FLOAT:
			return fprintf(file, "%.9g, %.9g", f[0], f[1]);
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return fprintf(file, "%.9g", f[0]);
		case DXGI_FORMAT_R32G32B32A32_UINT:
			return fprintf(file, "%u, %u, %u, %u", u32[0], u32[1], u32[2], u32[3]);
		case DXGI_FORMAT_R32G32B32_UINT:
			return fprintf(file, "%u, %u, %u", u32[0], u32[1], u32[2]);
		case DXGI_FORMAT_R32G32_UINT:
			return fprintf(file, "%u, %u", u32[0], u32[1]);
		case DXGI_FORMAT_R32_UINT:
			return fprintf(file, "%u", u32[0]);
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return fprintf(file, "%d, %d, %d, %d", s32[0], s32[1], s32[2], s32[3]);
		case DXGI_FORMAT_R32G32B32_SINT:
			return fprintf(file, "%d, %d, %d", s32[0], s32[1], s32[2]);
		case DXGI_FORMAT_R32G32_SINT:
			return fprintf(file, "%d, %d", s32[0], s32[1]);
		case DXGI_FORMAT_R32_SINT:
			return fprintf(file, "%d", s32[0]);
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return fprintf(file, "%04x, %04x, %04x, %04x", u16[0], u16[1], u16[2], u16[3]);
		case DXGI_FORMAT_R16G16_TYPELESS:
			return fprintf(file, "%04x, %04x", u16[0], u16[1]);
		case DXGI_FORMAT_R16_TYPELESS:
			return fprintf(file, "%04x", u16[0]);
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", Float16(u16[0]), Float16(u16[1]), Float16(u16[2]), Float16(u16[3]));
		case DXGI_FORMAT_R16G16_FLOAT:
			return fprintf(file, "%.9g, %.9g", Float16(u16[0]), Float16(u16[1]));
		case DXGI_FORMAT_R16_FLOAT:
			return fprintf(file, "%.9g", Float16(u16[0]));
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", Unorm16(u16[0]), Unorm16(u16[1]), Unorm16(u16[2]), Unorm16(u16[3]));
		case DXGI_FORMAT_R16G16_UNORM:
			return fprintf(file, "%.9g, %.9g", Unorm16(u16[0]), Unorm16(u16[1]));
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return fprintf(file, "%.9g", Unorm16(u16[0]));
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", Snorm16(s16[0]), Snorm16(s16[1]), Snorm16(s16[2]), Snorm16(s16[3]));
		case DXGI_FORMAT_R16G16_SNORM:
			return fprintf(file, "%.9g, %.9g", Snorm16(s16[0]), Snorm16(s16[1]));
		case DXGI_FORMAT_R16_SNORM:
			return fprintf(file, "%.9g", Snorm16(s16[0]));
		case DXGI_FORMAT_R16G16B16A16_UINT:
			return fprintf(file, "%u, %u, %u, %u", u16[0], u16[1], u16[2], u16[3]);
		case DXGI_FORMAT_R16G16_UINT:
			return fprintf(file, "%u, %u", u16[0], u16[1]);
		case DXGI_FORMAT_R16_UINT:
			return fprintf(file, "%u", u16[0]);
		case DXGI_FORMAT_R16G16B16A16_SINT:
			return fprintf(file, "%d, %d, %d, %d", s16[0], s16[1], s16[2], s16[3]);
		case DXGI_FORMAT_R16G16_SINT:
			return fprintf(file, "%d, %d", s16[0], s16[1]);
		case DXGI_FORMAT_R16_SINT:
			return fprintf(file, "%d", s16[0]);
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return fprintf(file, "%02x, %02x, %02x, %02x", u8[0], u8[1], u8[2], u8[3]);
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return fprintf(file, "%02x, %02x, %02x", u8[0], u8[1], u8[2]);
		case DXGI_FORMAT_R8G8_TYPELESS:
			return fprintf(file, "%02x, %02x", u8[0], u8[1]);
		case DXGI_FORMAT_R8_TYPELESS:
			return fprintf(file, "%02x", u8[0]);
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", Unorm8(u8[0]), Unorm8(u8[1]), Unorm8(u8[2]), Unorm8(u8[3]));
		case DXGI_FORMAT_R8G8_UNORM:
			return fprintf(file, "%.9g, %.9g", Unorm8(u8[0]), Unorm8(u8[1]));
		case DXGI_FORMAT_R8_UNORM:
			return fprintf(file, "%.9g", Unorm8(u8[0]));
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			return fprintf(file, "%.9g, %.9g, %.9g, %.9g", Snorm8(s8[0]), Snorm8(s8[1]), Snorm8(s8[2]), Snorm8(s8[3]));
		case DXGI_FORMAT_R8G8_SNORM:
			return fprintf(file, "%.9g, %.9g", Snorm8(s8[0]), Snorm8(s8[1]));
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_A8_UNORM:
			return fprintf(file, "%.9g", Snorm8(s8[0]));
		case DXGI_FORMAT_R8G8B8A8_UINT:
			return fprintf(file, "%u, %u, %u, %u", u8[0], u8[1], u8[2], u8[3]);
		case DXGI_FORMAT_R8G8_UINT:
			return fprintf(file, "%u, %u", u8[0], u8[1]);
		case DXGI_FORMAT_R8_UINT:
			return fprintf(file, "%u", u8[0]);
		case DXGI_FORMAT_R8G8B8A8_SINT:
			return fprintf(file, "%d, %d, %d, %d", s8[0], s8[1], s8[2], s8[3]);
		case DXGI_FORMAT_R8G8_SINT:
			return fprintf(file, "%d, %d", s8[0], s8[1]);
		case DXGI_FORMAT_R8_SINT:
			return fprintf(file, "%d", s8[0]);
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return fprintf(file, "%.9g, %d", f[0], u8[4]);
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return fprintf(file, "%.9g, %d", Unorm24(u32[0] & 0xffffff), u8[3]);
		default:
			break;
		}

		const UINT size = FormatSize(format);
		for (UINT i = 0; i < size; ++i)
			fprintf(file, "%02x", data[i]);
		return static_cast<int>(size * 2);
	}
};



// ===================== Resources/DX12ResourceServices.hpp =====================


enum class DX12ResourceCollectTaskKind
{
	Resource,
	ResourceBarrier,
	RootSignature,
	PsoRootSignature
};

class DX12ResourceCollectTask
{
public:
	DX12ResourceCollectTaskKind kind = DX12ResourceCollectTaskKind::Resource;
	ID3D12Resource *resource = nullptr;
	bool retainedResource = false;
	D3D12_RESOURCE_DESC desc = {};
	bool hasDesc = false;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	bool hasHeapProperties = false;
	PlacedResourceInfo placedInfo = {};
	bool hasPlacedInfo = false;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	const char *source = nullptr;
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	ID3D12RootSignature *rootSignature = nullptr;
	std::vector<uint8_t> rootSignatureBlob;
	UINT rootSignatureNodeMask = 0;
	UINT64 psoIndex = 0;
	std::string psoKind;
	ID3D12PipelineState *pipelineState = nullptr;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12ResourceCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12ResourceCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> dropped = 0;
	inline static std::atomic<uint64_t> addRefFailed = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> resourceTasksCompleted = 0;
	inline static std::atomic<uint64_t> resourceTasksElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTasksMaxElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTaskStatsSerial = 0;
	inline static uint64_t activeTasks = 0;

	static void Start();
	static void Stop();
	static bool Enqueue(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static bool EnqueueResourceBarriers(
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		const char *source);
	static bool EnqueueRootSignature(
		ID3D12RootSignature *rootSignature,
		UINT nodeMask,
		const void *blob,
		SIZE_T blobLength,
		const char *source);
	static bool EnqueuePsoRootSignature(
		UINT64 psoIndex,
		const char *kind,
		ID3D12PipelineState *pipelineState,
		ID3D12RootSignature *rootSignature,
		const char *source);
	static void DrainForSnapshot(const char *reason);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static void WorkerLoop();
	static void DrainPending(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void WaitForIdle(const char *reason);
	static void ProcessTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static bool TryAddRefBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseTaskRootSignature(DX12ResourceCollectTask *task);
	static void ReleaseTaskPsoRootSignature(DX12ResourceCollectTask *task);
	static void UpdateMaxDepth(size_t depth);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
	static void RecordResourceTaskStats(DX12ResourceCollectTask *task, const char *source, int64_t elapsedUs);
};




// Resource dump tasks intentionally keep failure notes per item so a bad
// resource never aborts the whole frame dump batch.
enum class DumpTaskSource
{
	Descriptor,
	InputAssembler,
	Output,
	Resource,
};

class DumpTask
{
public:
	DumpTaskSource sourceKind = DumpTaskSource::Descriptor;
	DX12FrameResourceBinding binding;
	DX12FrameIaBufferBinding iaBuffer;
	DX12FrameOutputBinding outputBinding;
	ID3D12Resource *source = nullptr;
	ID3D12Resource *readback = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> textureLayouts;
	std::vector<UINT> textureRows;
	std::vector<UINT64> textureRowSizes;
	UINT64 sourceOffset = 0;
		UINT64 copyBytes = 0;
		UINT64 readbackOffset = 0;
		uint32_t contentHash = 0;
		D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
		bool needsBarrier = false;
		bool isTexture = false;
		bool hasContentHash = false;
		bool ready = false;
	bool copied = false;
	bool stateKnown = false;
	bool directMap = false;
	bool keepReadbackAlive = false;
	bool sourceRetained = false;
	std::wstring fileName;
	std::string dedupeKey;
	const char *skipNote = nullptr;
};



class DX12ResourceDumpSafetyUtils
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static UINT64 MaxSingleReadbackBytes()
	{
		return UINT64_MAX;
	}

	static UINT MaxTextureSubresourcesForDump()
	{
		return static_cast<UINT>(~0u);
	}

	static bool TextureSubresourceCountForDump(
		const D3D12_RESOURCE_DESC &desc, UINT *subresourceCount)
	{
		if (!subresourceCount)
			return false;
		*subresourceCount = 0;
		if (!IsTextureResourceDimension(desc.Dimension))
			return false;
		if (desc.Width == 0 || desc.Height == 0 || desc.DepthOrArraySize == 0)
			return false;
		if (desc.SampleDesc.Count == 0 || desc.SampleDesc.Count > 64)
			return false;
		const UINT mipLevels = desc.MipLevels ? desc.MipLevels : 1;
		const UINT64 arrayOrDepth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
			1 : desc.DepthOrArraySize;
		const UINT64 count = static_cast<UINT64>(mipLevels) * arrayOrDepth;
		if (count == 0 || count > static_cast<UINT64>(MaxTextureSubresourcesForDump()))
			return false;
		*subresourceCount = static_cast<UINT>(count);
		return true;
	}

	static bool ResourceDescLooksDumpable(const D3D12_RESOURCE_DESC &desc)
	{
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			return desc.Width > 0;
		if (IsTextureResourceDimension(desc.Dimension)) {
			UINT subresources = 0;
			return TextureSubresourceCountForDump(desc, &subresources);
		}
		return false;
	}

	static bool DescriptorResourceDescLooksDumpable(const DX12DescriptorSummary &descriptor)
	{
		return descriptor.resource &&
			descriptor.hasResourceDesc &&
			ResourceDescLooksDumpable(descriptor.resourceDesc);
	}

	static bool HeapTypeAllowsSafeDiscoveryCopy(UINT heapType, bool hasHeapType)
	{
		if (!hasHeapType)
			return false;
		return heapType == D3D12_HEAP_TYPE_UPLOAD ||
			heapType == D3D12_HEAP_TYPE_READBACK;
	}

	static bool ResourceStateAllowsLiveDiscoveryCopy(const DX12ResourceSummary &resource)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				resource.resourceHeapType, resource.hasResourceHeapType))
			return true;

		return resource.hasCurrentState;
	}

	static bool DescriptorStateAllowsLiveDiscoveryCopy(const DX12DescriptorSummary &descriptor)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				descriptor.resourceHeapType, descriptor.hasResourceHeapType))
			return true;

		return descriptor.hasCurrentState;
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12ResourceSummary &resource)
	{
		if (!resource.hasCurrentState)
			return "resource_state_unknown";
		if (!resource.stateTrackedByBarrier)
			return "resource_state_create_only";
		return "resource_discovery_copy_unsafe";
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12DescriptorSummary &descriptor)
	{
		if (!descriptor.hasCurrentState)
			return "descriptor_state_unknown";
		if (!descriptor.stateTrackedByBarrier)
			return "descriptor_state_create_only";
		return "descriptor_discovery_copy_unsafe";
	}

	static bool DiscoveryBudgetAllows(
		UINT queuedTasks, UINT64 queuedBytes, UINT64 nextBytes,
		UINT taskBudget, UINT64 byteBudget)
	{
		if (taskBudget > 0 && queuedTasks >= taskBudget)
			return false;
		if (byteBudget > 0 &&
			(queuedBytes > byteBudget || nextBytes > byteBudget - queuedBytes))
			return false;
		return true;
	}
};




class DX12ResourceDumpCore
{
public:
static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static const char *DumpTaskSourceName(DumpTaskSource sourceKind)
{
	switch (sourceKind) {
	case DumpTaskSource::Descriptor:
		return "descriptor";
	case DumpTaskSource::InputAssembler:
		return "ia";
	case DumpTaskSource::Output:
		return "output";
	case DumpTaskSource::Resource:
		return "resource";
	default:
		return "unknown";
	}
}

static uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysis::LogJsonFunc("Crc32cFailed",
			"\"exception\":\"0x%lx\",\"size\":%zu", GetExceptionCode(), size);
		return 0;
	}
}

static void GetDedupedDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	swprintf_s(path, pathCount, L"%s\\deduped", dir);
	DX12ResourceDumpCore::EnsureDirectory(path);
}
static UINT64 FrameReadbackBudgetBytes()
{
	return 0;
}

static UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static const char *RootDescriptorKindName(UINT rangeType)
{
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
		return DescriptorKind::UAV.data();
	return "";
}

static const char *DescriptorRangeKindName(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return DescriptorKind::UAV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return DescriptorKind::Sampler.data();
	return "";
}

static const char *BindingKindName(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (!binding.descriptor.kind.empty())
		return binding.descriptor.kind.c_str();
	if (binding.rootDescriptor)
		return DX12ResourceDumpCore::RootDescriptorKindName(binding.rangeType);
	return DescriptorRangeKindName(binding.rangeType);
}

static DX12FrameResourceBinding ResourceBindingFromOutputBinding(
	const DX12FrameOutputBinding &output)
{
	DX12FrameResourceBinding binding;
	binding.eventSerial = output.eventSerial;
	binding.drawId = output.drawId ? output.drawId : output.eventSerial;
	binding.dispatchId = output.dispatchId;
	binding.psoIndex = output.psoIndex;
	binding.shaderInfo = output.shaderInfo;
	binding.bindSpace = "output";
	binding.rootParameterIndex = output.slot;
	binding.rangeIndex = 0;
	binding.rangeType = output.kind == "DSV" ?
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	binding.shaderRegister = output.slot;
	binding.registerSpace = 0;
	binding.descriptorOffset = output.slot;
	binding.cpuHandle = output.cpuHandle;
	binding.descriptor = output.descriptor;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = output.kind;
	binding.hasDescriptor = output.hasDescriptor;
	return binding;
}


static DX12FrameResourceBinding ResourceBindingFromTrackedResource(
	const DX12ResourceSummary &resource, UINT64 index)
{
	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_resource";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptorIndex = index;
	binding.descriptor.kind = DescriptorKind::SRV;
	binding.descriptor.resource = resource.resource;
	binding.descriptor.resourceDesc = resource.resourceDesc;
	binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
	binding.descriptor.resourceHeapType = resource.resourceHeapType;
	binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
	binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
	binding.descriptor.viewSize = resource.size;
	binding.descriptor.currentState = resource.currentState;
	binding.descriptor.hasCurrentState = resource.hasCurrentState;
	binding.descriptor.stateTrackedByBarrier = resource.stateTrackedByBarrier;
	binding.hasDescriptor = resource.resource != nullptr;
	return binding;
}


};




class DX12ResourceDumpBufferTextUtils
{
public:
	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_UNDEFINED:
			return "undefined";
		case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
			return "pointlist";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return "linelist";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
			return "linestrip";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return "trianglelist";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			return "trianglestrip";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
			return "linelist_adj";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
			return "linestrip_adj";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
			return "trianglelist_adj";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
			return "trianglestrip_adj";
		case D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:
			return "1_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:
			return "2_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:
			return "3_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:
			return "4_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:
			return "5_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:
			return "6_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:
			return "7_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:
			return "8_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:
			return "9_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST:
			return "10_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST:
			return "11_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST:
			return "12_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST:
			return "13_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST:
			return "14_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST:
			return "15_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST:
			return "16_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST:
			return "17_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST:
			return "18_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST:
			return "19_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST:
			return "20_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST:
			return "21_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST:
			return "22_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST:
			return "23_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST:
			return "24_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST:
			return "25_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST:
			return "26_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST:
			return "27_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST:
			return "28_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST:
			return "29_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST:
			return "30_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST:
			return "31_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST:
			return "32_control_point_patchlist";
		default:
			return "invalid";
		}
	}

	static int TopologyIndexGrouping(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return 2;
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return 3;
		default:
			return 1;
		}
	}

	static const char *IndexFormatName(UINT format)
	{
		switch (format) {
		case DXGI_FORMAT_R16_UINT:
			return "DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R32_UINT:
			return "DXGI_FORMAT_R32_UINT";
		default:
			return "UNKNOWN";
		}
	}

	static bool BindingIsConstantBuffer(const DumpTask &task)
	{
		if (task.sourceKind != DumpTaskSource::Descriptor)
			return false;
		if (task.binding.rootDescriptor)
			return task.binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV;
		return task.binding.descriptor.kind == DescriptorKind::CBV ||
			task.binding.descriptor.kind == DescriptorKind::CONSTANT_BUFFER_VIEW ||
			task.binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	}

	static char GenericBufferType(const DumpTask &task)
	{
		return BindingIsConstantBuffer(task) ? 'c' : '?';
	}

	static int GenericBufferIndex(const DumpTask &task)
	{
		if (task.sourceKind != DumpTaskSource::Descriptor)
			return -1;
		if (task.binding.shaderRegister == UINT_MAX)
			return -1;
		return static_cast<int>(task.binding.shaderRegister);
	}

	static UINT GenericBufferStride(const DumpTask &task)
	{
		if (task.sourceKind == DumpTaskSource::InputAssembler)
			return task.iaBuffer.stride;
		return task.binding.descriptor.structureByteStride;
	}

	static bool WriteGenericBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const UINT64 offset = task.sourceOffset;
		const UINT stride = GenericBufferStride(task);
		if (offset)
			fprintf(file, "offset: %llu\n", static_cast<unsigned long long>(offset));
		if (stride)
			fprintf(file, "stride: %u\n", stride);

		const char components[] = "xyzw";
		const char type = GenericBufferType(task);
		const int index = GenericBufferIndex(task);
		const UINT64 registerBase = offset / 16;
		for (UINT64 row = 0; row + 16 <= task.copyBytes; row += 16) {
			const float *values = reinterpret_cast<const float*>(data + row);
			const UINT64 registerIndex = registerBase + row / 16;
			UINT componentStart = 0;
			if (row == 0)
				componentStart = static_cast<UINT>((offset % 16) / 4);
			for (UINT component = componentStart; component < 4; ++component) {
				if (index == -1) {
					fprintf(file, "buf[%llu].%c: %.9g\n",
						static_cast<unsigned long long>(registerIndex),
						components[component], values[component]);
				} else {
					fprintf(file, "%cb%i[%llu].%c: %.9g\n",
						type, index,
						static_cast<unsigned long long>(registerIndex),
						components[component], values[component]);
				}
			}
		}
		return true;
	}

	static bool WriteIndexBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
		if (buffer.drawStartIndex || buffer.drawIndexCount) {
			fprintf(file, "first index: %u\n", buffer.drawStartIndex);
			fprintf(file, "index count: %u\n", buffer.drawIndexCount);
		}

		const D3D12_PRIMITIVE_TOPOLOGY topology = buffer.topology;
		if (topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
			fprintf(file, "topology: %s\n", TopologyName(topology));
		const int grouping = TopologyIndexGrouping(topology);
		const UINT format = buffer.format;

		if (format == DXGI_FORMAT_R16_UINT) {
			fprintf(file, "format: DXGI_FORMAT_R16_UINT\n");
			const UINT elementCount = static_cast<UINT>(task.copyBytes / sizeof(uint16_t));
			UINT start = buffer.drawStartIndex;
			if (start > elementCount)
				start = elementCount;
			UINT end = elementCount;
			if (buffer.drawIndexCount)
				end = (std::min<UINT>)(end, start + buffer.drawIndexCount);
			const uint16_t *indices = reinterpret_cast<const uint16_t*>(data);
			for (UINT i = start; i < end; ++i) {
				if ((i - start) % grouping == 0)
					fprintf(file, "\n");
				else
					fprintf(file, " ");
				fprintf(file, "%u", indices[i]);
			}
			fprintf(file, "\n");
			return true;
		}

		if (format == DXGI_FORMAT_R32_UINT) {
			fprintf(file, "format: DXGI_FORMAT_R32_UINT\n");
			const UINT elementCount = static_cast<UINT>(task.copyBytes / sizeof(uint32_t));
			UINT start = buffer.drawStartIndex;
			if (start > elementCount)
				start = elementCount;
			UINT end = elementCount;
			if (buffer.drawIndexCount)
				end = (std::min<UINT>)(end, start + buffer.drawIndexCount);
			const uint32_t *indices = reinterpret_cast<const uint32_t*>(data);
			for (UINT i = start; i < end; ++i) {
				if ((i - start) % grouping == 0)
					fprintf(file, "\n");
				else
					fprintf(file, " ");
				fprintf(file, "%u", indices[i]);
			}
			fprintf(file, "\n");
			return true;
		}

		fprintf(file, "format %u is illegal\n", format);
		return true;
	}

	static const char *InputSlotClassName(D3D12_INPUT_CLASSIFICATION value)
	{
		switch (value) {
		case D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:
			return "per-vertex";
		case D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:
			return "per-instance";
		default:
			return nullptr;
		}
	}

	static void WriteInputLayout(
		FILE *file, const DX12PsoInputLayoutInfo &layout, UINT slot,
		bool *perVertex, bool *perInstance)
	{
		if (!file)
			return;
		if (perVertex)
			*perVertex = false;
		if (perInstance)
			*perInstance = false;

		for (UINT i = 0; i < layout.elements.size(); ++i) {
			const DX12PsoInputElementInfo &element = layout.elements[i];
			fprintf(file, "element[%u]:\n", i);
			fprintf(file, "  SemanticName: %s\n", element.semanticName.c_str());
			fprintf(file, "  SemanticIndex: %u\n", element.semanticIndex);
			fprintf(file, "  Format: %s\n", DX12PsoInputLayoutUtils::FormatName(element.format));
			fprintf(file, "  InputSlot: %u\n", element.inputSlot);
			if (element.appendAlignedElement)
				fprintf(file, "  AlignedByteOffset: append\n");
			else
				fprintf(file, "  AlignedByteOffset: %u\n", element.alignedByteOffset);
			const char *slotClass = InputSlotClassName(element.inputSlotClass);
			if (slotClass) {
				fprintf(file, "  InputSlotClass: %s\n", slotClass);
				if (element.inputSlot == slot &&
						element.inputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA &&
						perVertex)
					*perVertex = true;
				if (element.inputSlot == slot &&
						element.inputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
						perInstance)
					*perInstance = true;
			} else {
				fprintf(file, "  InputSlotClass: %u\n", static_cast<UINT>(element.inputSlotClass));
			}
			fprintf(file, "  InstanceDataStepRate: %u\n", element.instanceDataStepRate);
		}
	}

	static void WriteVertexElement(
		FILE *file, const unsigned char *vertexData, const DX12PsoInputElementInfo &element,
		UINT slot, UINT vertexIndex, UINT stride)
	{
		if (!file || !vertexData || element.inputSlot != slot)
			return;

		const UINT offset = element.resolvedByteOffset;
		fprintf(file, "vb%u[%u]+%03u %s", slot, vertexIndex, offset, element.semanticName.c_str());
		if (element.semanticIndex)
			fprintf(file, "%u", element.semanticIndex);
		fprintf(file, ": ");

		const UINT size = DX12PsoInputLayoutUtils::FormatSize(element.format);
		if (size && offset + size <= stride) {
			DX12PsoInputLayoutUtils::WriteFormatValue(file, element.format, vertexData + offset);
			fprintf(file, "\n");
		} else {
			fprintf(file, "\n");
			if (!size)
				fprintf(file, "# WARNING: Unknown format size, vertex buffer may be decoded incorrectly\n");
			if (offset + size > stride)
				fprintf(file, "# WARNING: Offset exceeded stride, vertex buffer may be decoded incorrectly\n");
		}
	}

	static void WriteKnownVertexData(
		FILE *file, const DumpTask &task, const unsigned char *data, UINT start, UINT end)
	{
		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		for (UINT vertex = start; vertex < end; ++vertex) {
			fprintf(file, "\n");
			const unsigned char *vertexData = data + static_cast<size_t>(buffer.stride) * vertex;
			for (UINT elementIndex = 0; elementIndex < buffer.inputLayout.elements.size(); ++elementIndex) {
				const DX12PsoInputElementInfo &element = buffer.inputLayout.elements[elementIndex];
				if (element.inputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA)
					continue;
				WriteVertexElement(file, vertexData, element, buffer.slot, vertex - start, buffer.stride);
			}
		}
	}

	static void WriteKnownInstanceData(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		if (!buffer.stride || !buffer.drawInstanceCount)
			return;

		const UINT elementCount = static_cast<UINT>(task.copyBytes / buffer.stride);
		const UINT start = buffer.drawStartInstance;
		UINT end = elementCount;
		if (start < end)
			end = (std::min<UINT>)(end, start + buffer.drawInstanceCount);
		else
			return;

		for (UINT instance = start; instance < end; ++instance) {
			fprintf(file, "\n");
			for (UINT elementIndex = 0; elementIndex < buffer.inputLayout.elements.size(); ++elementIndex) {
				const DX12PsoInputElementInfo &element = buffer.inputLayout.elements[elementIndex];
				if (element.inputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA)
					continue;
				UINT index = instance;
				if (element.instanceDataStepRate)
					index = (instance - start) / element.instanceDataStepRate + start;
				if (index >= elementCount)
					continue;
				const unsigned char *instanceData = data + static_cast<size_t>(buffer.stride) * index;
				WriteVertexElement(file, instanceData, element, buffer.slot, index - start, buffer.stride);
			}
		}
	}

	static void WriteUnknownVertexData(
		FILE *file, const DX12FrameIaBufferBinding &buffer, const unsigned char *data,
		UINT start, UINT end)
	{
		const float *floats = reinterpret_cast<const float*>(data);
		const uint32_t *words = reinterpret_cast<const uint32_t*>(data);
		const uint8_t *bytes = reinterpret_cast<const uint8_t*>(data);
		for (UINT vertex = start; vertex < end; ++vertex) {
			fprintf(file, "\n");
			UINT j = 0;
			for (; j < buffer.stride / 4; ++j) {
				const UINT wordIndex = vertex * buffer.stride / 4 + j;
				fprintf(file, "vb%u[%u]+%03u: 0x%08x %.9g\n",
					buffer.slot, vertex - start, j * 4,
					words[wordIndex], floats[wordIndex]);
			}
			for (j = j * 4; j < buffer.stride; ++j) {
				const UINT byteIndex = vertex * buffer.stride + j;
				fprintf(file, "vb%u[%u]+%03u: 0x%02x\n",
					buffer.slot, vertex - start, j, bytes[byteIndex]);
			}
		}
	}

	static bool WriteVertexBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		const UINT stride = buffer.stride;
		if (task.sourceOffset)
			fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
		fprintf(file, "stride: %u\n", stride);
		if (buffer.drawStartIndex || buffer.drawIndexCount) {
			fprintf(file, "first vertex: %u\n", buffer.drawStartIndex);
			fprintf(file, "vertex count: %u\n", buffer.drawIndexCount);
		}
		if (buffer.drawStartInstance || buffer.drawInstanceCount) {
			fprintf(file, "first instance: %u\n", buffer.drawStartInstance);
			fprintf(file, "instance count: %u\n", buffer.drawInstanceCount);
		}
		if (buffer.topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
			fprintf(file, "topology: %s\n", TopologyName(buffer.topology));

		bool perVertex = false;
		bool perInstance = false;
		if (!buffer.inputLayout.elements.empty())
			WriteInputLayout(file, buffer.inputLayout, buffer.slot, &perVertex, &perInstance);
		if (!stride)
			return true;

		const UINT vertexCount = static_cast<UINT>(task.copyBytes / stride);
		UINT start = buffer.drawStartIndex;
		if (start > vertexCount)
			start = vertexCount;
		UINT end = vertexCount;
		if (buffer.drawIndexCount)
			end = (std::min<UINT>)(end, start + buffer.drawIndexCount);

		if (!buffer.inputLayout.elements.empty()) {
			if (perVertex) {
				fprintf(file, "\nvertex-data:\n");
				WriteKnownVertexData(file, task, data, start, end);
			}
			if (perInstance) {
				fprintf(file, "\ninstance-data:\n");
				WriteKnownInstanceData(file, task, data);
			}
		} else {
			WriteUnknownVertexData(file, buffer, data, start, end);
		}
		return true;
	}

	static bool WriteBufferTextFile(FILE *file, const DumpTask &task, const void *mappedData)
	{
		if (!file || !mappedData || task.isTexture)
			return false;
		const unsigned char *data =
			static_cast<const unsigned char*>(mappedData) + task.readbackOffset;
		if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::IB)
			return WriteIndexBufferText(file, task, data);
		if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::VB)
			return WriteVertexBufferText(file, task, data);
		return WriteGenericBufferText(file, task, data);
	}
};



class DX12ResourceDumpDedupedNamingUtils
{
public:
	static const wchar_t *ExtensionForTask(const DumpTask &task)
	{
		return task.isTexture ? L"dds" : L"buf";
	}

	static DXGI_FORMAT TextureFormatForTask(const DumpTask &task)
	{
		UINT viewFormat = 0;
		if (task.sourceKind == DumpTaskSource::Output &&
				task.outputBinding.hasDescriptor &&
				task.outputBinding.descriptor.viewFormat) {
			viewFormat = task.outputBinding.descriptor.viewFormat;
		} else if (task.binding.hasDescriptor && task.binding.descriptor.viewFormat) {
			viewFormat = task.binding.descriptor.viewFormat;
		}
		return viewFormat ? static_cast<DXGI_FORMAT>(viewFormat) : task.desc.Format;
	}

	static const wchar_t *TextureFormatName(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_UNKNOWN: return L"DXGI_FORMAT_UNKNOWN";
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: return L"DXGI_FORMAT_R32G32B32A32_TYPELESS";
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return L"DXGI_FORMAT_R32G32B32A32_FLOAT";
		case DXGI_FORMAT_R32G32B32A32_UINT: return L"DXGI_FORMAT_R32G32B32A32_UINT";
		case DXGI_FORMAT_R32G32B32A32_SINT: return L"DXGI_FORMAT_R32G32B32A32_SINT";
		case DXGI_FORMAT_R32G32B32_TYPELESS: return L"DXGI_FORMAT_R32G32B32_TYPELESS";
		case DXGI_FORMAT_R32G32B32_FLOAT: return L"DXGI_FORMAT_R32G32B32_FLOAT";
		case DXGI_FORMAT_R32G32B32_UINT: return L"DXGI_FORMAT_R32G32B32_UINT";
		case DXGI_FORMAT_R32G32B32_SINT: return L"DXGI_FORMAT_R32G32B32_SINT";
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return L"DXGI_FORMAT_R16G16B16A16_TYPELESS";
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R16G16B16A16_UNORM: return L"DXGI_FORMAT_R16G16B16A16_UNORM";
		case DXGI_FORMAT_R16G16B16A16_UINT: return L"DXGI_FORMAT_R16G16B16A16_UINT";
		case DXGI_FORMAT_R16G16B16A16_SNORM: return L"DXGI_FORMAT_R16G16B16A16_SNORM";
		case DXGI_FORMAT_R16G16B16A16_SINT: return L"DXGI_FORMAT_R16G16B16A16_SINT";
		case DXGI_FORMAT_R32G32_TYPELESS: return L"DXGI_FORMAT_R32G32_TYPELESS";
		case DXGI_FORMAT_R32G32_FLOAT: return L"DXGI_FORMAT_R32G32_FLOAT";
		case DXGI_FORMAT_R32G32_UINT: return L"DXGI_FORMAT_R32G32_UINT";
		case DXGI_FORMAT_R32G32_SINT: return L"DXGI_FORMAT_R32G32_SINT";
		case DXGI_FORMAT_R32G8X24_TYPELESS: return L"DXGI_FORMAT_R32G8X24_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return L"DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return L"DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return L"DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: return L"DXGI_FORMAT_R10G10B10A2_TYPELESS";
		case DXGI_FORMAT_R10G10B10A2_UNORM: return L"DXGI_FORMAT_R10G10B10A2_UNORM";
		case DXGI_FORMAT_R10G10B10A2_UINT: return L"DXGI_FORMAT_R10G10B10A2_UINT";
		case DXGI_FORMAT_R11G11B10_FLOAT: return L"DXGI_FORMAT_R11G11B10_FLOAT";
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return L"DXGI_FORMAT_R8G8B8A8_TYPELESS";
		case DXGI_FORMAT_R8G8B8A8_UNORM: return L"DXGI_FORMAT_R8G8B8A8_UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return L"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
		case DXGI_FORMAT_R8G8B8A8_UINT: return L"DXGI_FORMAT_R8G8B8A8_UINT";
		case DXGI_FORMAT_R8G8B8A8_SNORM: return L"DXGI_FORMAT_R8G8B8A8_SNORM";
		case DXGI_FORMAT_R8G8B8A8_SINT: return L"DXGI_FORMAT_R8G8B8A8_SINT";
		case DXGI_FORMAT_R16G16_TYPELESS: return L"DXGI_FORMAT_R16G16_TYPELESS";
		case DXGI_FORMAT_R16G16_FLOAT: return L"DXGI_FORMAT_R16G16_FLOAT";
		case DXGI_FORMAT_R16G16_UNORM: return L"DXGI_FORMAT_R16G16_UNORM";
		case DXGI_FORMAT_R16G16_UINT: return L"DXGI_FORMAT_R16G16_UINT";
		case DXGI_FORMAT_R16G16_SNORM: return L"DXGI_FORMAT_R16G16_SNORM";
		case DXGI_FORMAT_R16G16_SINT: return L"DXGI_FORMAT_R16G16_SINT";
		case DXGI_FORMAT_R32_TYPELESS: return L"DXGI_FORMAT_R32_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT: return L"DXGI_FORMAT_D32_FLOAT";
		case DXGI_FORMAT_R32_FLOAT: return L"DXGI_FORMAT_R32_FLOAT";
		case DXGI_FORMAT_R32_UINT: return L"DXGI_FORMAT_R32_UINT";
		case DXGI_FORMAT_R32_SINT: return L"DXGI_FORMAT_R32_SINT";
		case DXGI_FORMAT_R24G8_TYPELESS: return L"DXGI_FORMAT_R24G8_TYPELESS";
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return L"DXGI_FORMAT_D24_UNORM_S8_UINT";
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return L"DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return L"DXGI_FORMAT_X24_TYPELESS_G8_UINT";
		case DXGI_FORMAT_R8G8_TYPELESS: return L"DXGI_FORMAT_R8G8_TYPELESS";
		case DXGI_FORMAT_R8G8_UNORM: return L"DXGI_FORMAT_R8G8_UNORM";
		case DXGI_FORMAT_R8G8_UINT: return L"DXGI_FORMAT_R8G8_UINT";
		case DXGI_FORMAT_R8G8_SNORM: return L"DXGI_FORMAT_R8G8_SNORM";
		case DXGI_FORMAT_R8G8_SINT: return L"DXGI_FORMAT_R8G8_SINT";
		case DXGI_FORMAT_R16_TYPELESS: return L"DXGI_FORMAT_R16_TYPELESS";
		case DXGI_FORMAT_R16_FLOAT: return L"DXGI_FORMAT_R16_FLOAT";
		case DXGI_FORMAT_D16_UNORM: return L"DXGI_FORMAT_D16_UNORM";
		case DXGI_FORMAT_R16_UNORM: return L"DXGI_FORMAT_R16_UNORM";
		case DXGI_FORMAT_R16_UINT: return L"DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R16_SNORM: return L"DXGI_FORMAT_R16_SNORM";
		case DXGI_FORMAT_R16_SINT: return L"DXGI_FORMAT_R16_SINT";
		case DXGI_FORMAT_R8_TYPELESS: return L"DXGI_FORMAT_R8_TYPELESS";
		case DXGI_FORMAT_R8_UNORM: return L"DXGI_FORMAT_R8_UNORM";
		case DXGI_FORMAT_R8_UINT: return L"DXGI_FORMAT_R8_UINT";
		case DXGI_FORMAT_R8_SNORM: return L"DXGI_FORMAT_R8_SNORM";
		case DXGI_FORMAT_R8_SINT: return L"DXGI_FORMAT_R8_SINT";
		case DXGI_FORMAT_A8_UNORM: return L"DXGI_FORMAT_A8_UNORM";
		case DXGI_FORMAT_R1_UNORM: return L"DXGI_FORMAT_R1_UNORM";
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return L"DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
		case DXGI_FORMAT_R8G8_B8G8_UNORM: return L"DXGI_FORMAT_R8G8_B8G8_UNORM";
		case DXGI_FORMAT_G8R8_G8B8_UNORM: return L"DXGI_FORMAT_G8R8_G8B8_UNORM";
		case DXGI_FORMAT_BC1_TYPELESS: return L"DXGI_FORMAT_BC1_TYPELESS";
		case DXGI_FORMAT_BC1_UNORM: return L"DXGI_FORMAT_BC1_UNORM";
		case DXGI_FORMAT_BC1_UNORM_SRGB: return L"DXGI_FORMAT_BC1_UNORM_SRGB";
		case DXGI_FORMAT_BC2_TYPELESS: return L"DXGI_FORMAT_BC2_TYPELESS";
		case DXGI_FORMAT_BC2_UNORM: return L"DXGI_FORMAT_BC2_UNORM";
		case DXGI_FORMAT_BC2_UNORM_SRGB: return L"DXGI_FORMAT_BC2_UNORM_SRGB";
		case DXGI_FORMAT_BC3_TYPELESS: return L"DXGI_FORMAT_BC3_TYPELESS";
		case DXGI_FORMAT_BC3_UNORM: return L"DXGI_FORMAT_BC3_UNORM";
		case DXGI_FORMAT_BC3_UNORM_SRGB: return L"DXGI_FORMAT_BC3_UNORM_SRGB";
		case DXGI_FORMAT_BC4_TYPELESS: return L"DXGI_FORMAT_BC4_TYPELESS";
		case DXGI_FORMAT_BC4_UNORM: return L"DXGI_FORMAT_BC4_UNORM";
		case DXGI_FORMAT_BC4_SNORM: return L"DXGI_FORMAT_BC4_SNORM";
		case DXGI_FORMAT_BC5_TYPELESS: return L"DXGI_FORMAT_BC5_TYPELESS";
		case DXGI_FORMAT_BC5_UNORM: return L"DXGI_FORMAT_BC5_UNORM";
		case DXGI_FORMAT_BC5_SNORM: return L"DXGI_FORMAT_BC5_SNORM";
		case DXGI_FORMAT_B5G6R5_UNORM: return L"DXGI_FORMAT_B5G6R5_UNORM";
		case DXGI_FORMAT_B5G5R5A1_UNORM: return L"DXGI_FORMAT_B5G5R5A1_UNORM";
		case DXGI_FORMAT_B8G8R8A8_UNORM: return L"DXGI_FORMAT_B8G8R8A8_UNORM";
		case DXGI_FORMAT_B8G8R8X8_UNORM: return L"DXGI_FORMAT_B8G8R8X8_UNORM";
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return L"DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return L"DXGI_FORMAT_B8G8R8A8_TYPELESS";
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
		case DXGI_FORMAT_B8G8R8X8_TYPELESS: return L"DXGI_FORMAT_B8G8R8X8_TYPELESS";
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
		case DXGI_FORMAT_BC6H_TYPELESS: return L"DXGI_FORMAT_BC6H_TYPELESS";
		case DXGI_FORMAT_BC6H_UF16: return L"DXGI_FORMAT_BC6H_UF16";
		case DXGI_FORMAT_BC6H_SF16: return L"DXGI_FORMAT_BC6H_SF16";
		case DXGI_FORMAT_BC7_TYPELESS: return L"DXGI_FORMAT_BC7_TYPELESS";
		case DXGI_FORMAT_BC7_UNORM: return L"DXGI_FORMAT_BC7_UNORM";
		case DXGI_FORMAT_BC7_UNORM_SRGB: return L"DXGI_FORMAT_BC7_UNORM_SRGB";
		case DXGI_FORMAT_AYUV: return L"DXGI_FORMAT_AYUV";
		case DXGI_FORMAT_Y410: return L"DXGI_FORMAT_Y410";
		case DXGI_FORMAT_Y416: return L"DXGI_FORMAT_Y416";
		case DXGI_FORMAT_NV12: return L"DXGI_FORMAT_NV12";
		case DXGI_FORMAT_P010: return L"DXGI_FORMAT_P010";
		case DXGI_FORMAT_P016: return L"DXGI_FORMAT_P016";
		case DXGI_FORMAT_420_OPAQUE: return L"DXGI_FORMAT_420_OPAQUE";
		case DXGI_FORMAT_YUY2: return L"DXGI_FORMAT_YUY2";
		case DXGI_FORMAT_Y210: return L"DXGI_FORMAT_Y210";
		case DXGI_FORMAT_Y216: return L"DXGI_FORMAT_Y216";
		case DXGI_FORMAT_NV11: return L"DXGI_FORMAT_NV11";
		case DXGI_FORMAT_AI44: return L"DXGI_FORMAT_AI44";
		case DXGI_FORMAT_IA44: return L"DXGI_FORMAT_IA44";
		case DXGI_FORMAT_P8: return L"DXGI_FORMAT_P8";
		case DXGI_FORMAT_A8P8: return L"DXGI_FORMAT_A8P8";
		case DXGI_FORMAT_B4G4R4A4_UNORM: return L"DXGI_FORMAT_B4G4R4A4_UNORM";
		default:
			return nullptr;
		}
	}

	static void BuildTextureFormatName(DXGI_FORMAT format, wchar_t *name, size_t nameCount)
	{
		if (!name || nameCount == 0)
			return;
		const wchar_t *formatName = TextureFormatName(format);
		if (formatName)
			wcsncpy_s(name, nameCount, formatName, _TRUNCATE);
		else
			swprintf_s(name, nameCount, L"DXGI_FORMAT_%u", static_cast<UINT>(format));
	}

	static void BuildDedupedFileName(
		const DumpTask &task, uint32_t huntHash, uint32_t fileHash,
		wchar_t *fileName, size_t fileNameCount)
	{
		if (!fileName || fileNameCount == 0)
			return;
		fileName[0] = L'\0';

		const wchar_t *extension = ExtensionForTask(task);
		if (task.isTexture) {
			wchar_t formatName[96];
			BuildTextureFormatName(TextureFormatForTask(task), formatName, ARRAYSIZE(formatName));
			swprintf_s(fileName, fileNameCount, L"%08x_%08x-%s.%s",
				static_cast<UINT>(huntHash),
				static_cast<UINT>(fileHash),
				formatName,
				extension);
			return;
		}
		swprintf_s(fileName, fileNameCount, L"%08x.%s", static_cast<UINT>(fileHash), extension);
	}
};



// ===================== Resources/DX12ResourceFormats.hpp =====================


#pragma pack(push, 1)
class DX12ResourceDumpDdsPixelFormat
{
public:
	uint32_t size = 0;
	uint32_t flags = 0;
	uint32_t fourCc = 0;
	uint32_t rgbBitCount = 0;
	uint32_t rBitMask = 0;
	uint32_t gBitMask = 0;
	uint32_t bBitMask = 0;
	uint32_t aBitMask = 0;
};

class DX12ResourceDumpDdsHeader
{
public:
	uint32_t size = 0;
	uint32_t flags = 0;
	uint32_t height = 0;
	uint32_t width = 0;
	uint32_t pitchOrLinearSize = 0;
	uint32_t depth = 0;
	uint32_t mipMapCount = 0;
	uint32_t reserved1[11] = {};
	DX12ResourceDumpDdsPixelFormat ddspf;
	uint32_t caps = 0;
	uint32_t caps2 = 0;
	uint32_t caps3 = 0;
	uint32_t caps4 = 0;
	uint32_t reserved2 = 0;
};

class DX12ResourceDumpDdsHeaderDxt10
{
public:
	uint32_t dxgiFormat = 0;
	uint32_t resourceDimension = 0;
	uint32_t miscFlag = 0;
	uint32_t arraySize = 0;
	uint32_t miscFlags2 = 0;
};
#pragma pack(pop)

static_assert(sizeof(DX12ResourceDumpDdsPixelFormat) == 32, "DDS pixel format layout mismatch");
static_assert(sizeof(DX12ResourceDumpDdsHeader) == 124, "DDS header layout mismatch");
static_assert(sizeof(DX12ResourceDumpDdsHeaderDxt10) == 20, "DDS DX10 header layout mismatch");

class DX12ResourceDumpDdsUtils
{
public:
	static constexpr uint32_t DDS_MAGIC = 0x20534444;
	static constexpr uint32_t DDS_FOURCC = 0x00000004;
	static constexpr uint32_t DDS_RGB = 0x00000040;
	static constexpr uint32_t DDS_RGBA = 0x00000041;
	static constexpr uint32_t DDS_LUMINANCE = 0x00020000;
	static constexpr uint32_t DDS_LUMINANCEA = 0x00020001;
	static constexpr uint32_t DDS_ALPHA = 0x00000002;
	static constexpr uint32_t DDS_BUMPDUDV = 0x00080000;
	static constexpr uint32_t DDS_HEADER_FLAGS_TEXTURE = 0x00001007;
	static constexpr uint32_t DDS_HEADER_FLAGS_MIPMAP = 0x00020000;
	static constexpr uint32_t DDS_HEADER_FLAGS_PITCH = 0x00000008;
	static constexpr uint32_t DDS_HEADER_FLAGS_LINEARSIZE = 0x00080000;
	static constexpr uint32_t DDS_SURFACE_FLAGS_TEXTURE = 0x00001000;

	static DX12ResourceDumpDdsPixelFormat PixelFormat(
		uint32_t flags, uint32_t fourCc, uint32_t rgbBitCount,
		uint32_t rBitMask, uint32_t gBitMask, uint32_t bBitMask, uint32_t aBitMask)
	{
		DX12ResourceDumpDdsPixelFormat format;
		format.size = sizeof(DX12ResourceDumpDdsPixelFormat);
		format.flags = flags;
		format.fourCc = fourCc;
		format.rgbBitCount = rgbBitCount;
		format.rBitMask = rBitMask;
		format.gBitMask = gBitMask;
		format.bBitMask = bBitMask;
		format.aBitMask = aBitMask;
		return format;
	}

	static DX12ResourceDumpDdsPixelFormat FourCcPixelFormat(char a, char b, char c, char d)
	{
		return PixelFormat(DDS_FOURCC, DX12ResourceDumpCore::MakeFourCC(a, b, c, d), 0, 0, 0, 0, 0);
	}

	static bool IsUnsupportedLegacyDdsFormat(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_AI44 ||
			format == DXGI_FORMAT_IA44 ||
			format == DXGI_FORMAT_P8 ||
			format == DXGI_FORMAT_A8P8;
	}

	static bool TryGetLegacyPixelFormat(
		DXGI_FORMAT format, DX12ResourceDumpDdsPixelFormat *pixelFormat)
	{
		if (!pixelFormat)
			return false;

		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			return true;
		case DXGI_FORMAT_R16G16_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000);
			return true;
		case DXGI_FORMAT_R8G8_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCEA, 0, 16, 0x00ff, 0x0000, 0x0000, 0xff00);
			return true;
		case DXGI_FORMAT_R16_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCE, 0, 16, 0xffff, 0x0000, 0x0000, 0x0000);
			return true;
		case DXGI_FORMAT_R8_UNORM:
			*pixelFormat = PixelFormat(DDS_LUMINANCE, 0, 8, 0xff, 0x00, 0x00, 0x00);
			return true;
		case DXGI_FORMAT_A8_UNORM:
			*pixelFormat = PixelFormat(DDS_ALPHA, 0, 8, 0x00, 0x00, 0x00, 0xff);
			return true;
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
			*pixelFormat = FourCcPixelFormat('R', 'G', 'B', 'G');
			return true;
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
			*pixelFormat = FourCcPixelFormat('G', 'R', 'G', 'B');
			return true;
		case DXGI_FORMAT_BC1_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '1');
			return true;
		case DXGI_FORMAT_BC2_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '3');
			return true;
		case DXGI_FORMAT_BC3_UNORM:
			*pixelFormat = FourCcPixelFormat('D', 'X', 'T', '5');
			return true;
		case DXGI_FORMAT_BC4_UNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '4', 'U');
			return true;
		case DXGI_FORMAT_BC4_SNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '4', 'S');
			return true;
		case DXGI_FORMAT_BC5_UNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '5', 'U');
			return true;
		case DXGI_FORMAT_BC5_SNORM:
			*pixelFormat = FourCcPixelFormat('B', 'C', '5', 'S');
			return true;
		case DXGI_FORMAT_B5G6R5_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000);
			return true;
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000);
			return true;
		case DXGI_FORMAT_R8G8_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 16, 0x00ff, 0xff00, 0x0000, 0x0000);
			return true;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			return true;
		case DXGI_FORMAT_R16G16_SNORM:
			*pixelFormat = PixelFormat(DDS_BUMPDUDV, 0, 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000);
			return true;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
			return true;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			*pixelFormat = PixelFormat(DDS_RGB, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000);
			return true;
		case DXGI_FORMAT_YUY2:
			*pixelFormat = FourCcPixelFormat('Y', 'U', 'Y', '2');
			return true;
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			*pixelFormat = PixelFormat(DDS_RGBA, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000);
			return true;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 116, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 113, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			*pixelFormat = PixelFormat(DDS_FOURCC, 36, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			*pixelFormat = PixelFormat(DDS_FOURCC, 110, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R32G32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 115, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16G16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 112, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R32_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 114, 0, 0, 0, 0, 0);
			return true;
		case DXGI_FORMAT_R16_FLOAT:
			*pixelFormat = PixelFormat(DDS_FOURCC, 111, 0, 0, 0, 0, 0);
			return true;
		default:
			return false;
		}
	}

	static bool IsBlockCompressedFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	static UINT BlockBytes(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 8;
		default:
			return 16;
		}
	}

	static UINT BitsPerPixel(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
			return 64;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_YUY2:
			return 32;
		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
			return 24;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_A8P8:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;
		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_NV11:
			return 12;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
			return 8;
		case DXGI_FORMAT_R1_UNORM:
			return 1;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;
		default:
			return 0;
		}
	}

	static bool GetSurfaceInfo(
		UINT width, UINT height, DXGI_FORMAT format,
		UINT64 *slicePitch, UINT64 *rowPitch, UINT64 *rowCount)
	{
		if (!width || !height || !slicePitch || !rowPitch || !rowCount)
			return false;
		if (IsBlockCompressedFormat(format)) {
			const UINT64 blocksWide = (std::max<UINT64>)(1, (static_cast<UINT64>(width) + 3) / 4);
			const UINT64 blocksHigh = (std::max<UINT64>)(1, (static_cast<UINT64>(height) + 3) / 4);
			*rowPitch = blocksWide * BlockBytes(format);
			*rowCount = blocksHigh;
			*slicePitch = (*rowPitch) * (*rowCount);
			return true;
		}
		const UINT bitsPerPixel = BitsPerPixel(format);
		if (!bitsPerPixel)
			return false;
		*rowPitch = (static_cast<UINT64>(width) * bitsPerPixel + 7) / 8;
		*rowCount = height;
		*slicePitch = (*rowPitch) * (*rowCount);
		return true;
	}

	static DXGI_FORMAT ResolveTextureFormat(const DumpTask &task)
	{
		UINT viewFormat = 0;
		if (task.sourceKind == DumpTaskSource::Output &&
				task.outputBinding.hasDescriptor &&
				task.outputBinding.descriptor.viewFormat)
			viewFormat = task.outputBinding.descriptor.viewFormat;
		else if (task.binding.hasDescriptor && task.binding.descriptor.viewFormat)
			viewFormat = task.binding.descriptor.viewFormat;
		return viewFormat ? static_cast<DXGI_FORMAT>(viewFormat) : task.desc.Format;
	}

	static bool BuildHeader(
		const DumpTask &task, DXGI_FORMAT format,
		DX12ResourceDumpDdsHeader *header,
		DX12ResourceDumpDdsHeaderDxt10 *dx10Header,
		bool *hasDx10Header,
		UINT64 *slicePitch,
		UINT64 *rowPitch,
		UINT64 *rowCount)
	{
		if (!header || !dx10Header || !hasDx10Header || !slicePitch || !rowPitch || !rowCount)
			return false;
		if (task.desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		if (task.desc.Width > UINT32_MAX || task.desc.Height > UINT32_MAX)
			return false;
		if (IsUnsupportedLegacyDdsFormat(format))
			return false;
		if (!GetSurfaceInfo(
				static_cast<UINT>(task.desc.Width), task.desc.Height, format,
				slicePitch, rowPitch, rowCount))
			return false;

		*header = DX12ResourceDumpDdsHeader();
		*dx10Header = DX12ResourceDumpDdsHeaderDxt10();
		*hasDx10Header = false;

		header->size = sizeof(DX12ResourceDumpDdsHeader);
		header->flags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_MIPMAP;
		header->height = task.desc.Height;
		header->width = static_cast<uint32_t>(task.desc.Width);
		header->mipMapCount = 1;
		header->caps = DDS_SURFACE_FLAGS_TEXTURE;

		if (!TryGetLegacyPixelFormat(format, &header->ddspf)) {
			header->ddspf = FourCcPixelFormat('D', 'X', '1', '0');
			*hasDx10Header = true;
			dx10Header->dxgiFormat = static_cast<uint32_t>(format);
			dx10Header->resourceDimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			dx10Header->arraySize = 1;
		}

		if (IsBlockCompressedFormat(format)) {
			header->flags |= DDS_HEADER_FLAGS_LINEARSIZE;
			header->pitchOrLinearSize = static_cast<uint32_t>(*slicePitch);
		} else {
			header->flags |= DDS_HEADER_FLAGS_PITCH;
			header->pitchOrLinearSize = static_cast<uint32_t>(*rowPitch);
		}
		return true;
	}

	static bool WriteZeroBytes(FILE *file, UINT64 byteCount)
	{
		if (!file)
			return false;
		static const unsigned char zeroes[256] = {};
		while (byteCount) {
			const size_t chunk = static_cast<size_t>((std::min<UINT64>)(byteCount, sizeof(zeroes)));
			if (fwrite(zeroes, 1, chunk, file) != chunk)
				return false;
			byteCount -= chunk;
		}
		return true;
	}

	static bool WriteTexture2DFile(FILE *file, const DumpTask &task, const void *mappedData)
	{
		if (!file || !mappedData || !task.isTexture)
			return false;
		if (task.textureLayouts.empty() || task.textureRows.empty() || task.textureRowSizes.empty())
			return false;

		const DXGI_FORMAT format = ResolveTextureFormat(task);
		DX12ResourceDumpDdsHeader header;
		DX12ResourceDumpDdsHeaderDxt10 dx10Header;
		bool hasDx10Header = false;
		UINT64 slicePitch = 0;
		UINT64 rowPitch = 0;
		UINT64 rowCount = 0;
		if (!BuildHeader(task, format, &header, &dx10Header, &hasDx10Header, &slicePitch, &rowPitch, &rowCount))
			return false;
		if (rowCount > task.textureRows[0])
			return false;

		const uint32_t magic = DDS_MAGIC;
		if (fwrite(&magic, sizeof(magic), 1, file) != 1)
			return false;
		if (fwrite(&header, sizeof(header), 1, file) != 1)
			return false;
		if (hasDx10Header && fwrite(&dx10Header, sizeof(dx10Header), 1, file) != 1)
			return false;

		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[0];
		const unsigned char *mapped = static_cast<const unsigned char*>(mappedData);
		const unsigned char *base = mapped + layout.Offset;
		const UINT64 sourceRowPitch = layout.Footprint.RowPitch;
		const UINT64 availableRowBytes = task.textureRowSizes[0];
		const UINT64 copyBytes = (std::min<UINT64>)(rowPitch, (std::min<UINT64>)(sourceRowPitch, availableRowBytes));
		for (UINT64 row = 0; row < rowCount; ++row) {
			const unsigned char *src = base + row * sourceRowPitch;
			if (copyBytes && fwrite(src, 1, static_cast<size_t>(copyBytes), file) != copyBytes)
				return false;
			if (copyBytes < rowPitch && !WriteZeroBytes(file, rowPitch - copyBytes))
				return false;
		}
		return true;
	}
};




class DX12ResourceDumpPolicy
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static bool ShouldDumpBinding(const DX12FrameResourceBinding &binding)
	{
		if (!binding.usageClass.empty()) {
			if (!binding.hasDescriptor)
				return true;
			return binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		}

		if (binding.bindSpace == "hunting_metadata" ||
			binding.bindSpace == "hunting_resource") {
			if (binding.bindSpace == "hunting_resource") {
				const bool isBufferResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
				const bool isTextureResource =
					binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
				return binding.hasDescriptor &&
					binding.descriptor.resource &&
					binding.descriptor.hasResourceDesc &&
					(isBufferResource || isTextureResource);
			}
			const bool isBufferResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
			const bool isTextureResource =
				binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				((isTextureResource && (
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV ||
					binding.descriptor.kind == DescriptorKind::RTV ||
					binding.descriptor.kind == DescriptorKind::DSV)) ||
				 (isBufferResource && (
					binding.descriptor.kind == DescriptorKind::CBV ||
					binding.descriptor.kind == DescriptorKind::SRV ||
					binding.descriptor.kind == DescriptorKind::UAV)));
		}

		const bool isDispatch = binding.dispatchId != 0;
		const bool isDraw = binding.drawId != 0;
		if (!isDispatch && !isDraw)
			return false;

		const bool isGraphicsSpace =
			binding.bindSpace == "graphics_cbv_srv_uav" ||
			binding.bindSpace == "graphics_sampler" ||
			binding.bindSpace == "graphics_root";
		const bool isComputeSpace =
			binding.bindSpace == "compute_cbv_srv_uav" ||
			binding.bindSpace == "compute_sampler" ||
			binding.bindSpace == "compute_root" ||
			binding.bindSpace == "hunting_uav_state";
		const bool isOutputSpace = binding.bindSpace == "output";
		if (isOutputSpace)
			return binding.hasDescriptor &&
				binding.descriptor.resource &&
				binding.descriptor.hasResourceDesc &&
				IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension) &&
				(binding.descriptor.kind == DescriptorKind::RTV ||
				 binding.descriptor.kind == DescriptorKind::DSV);
		if ((isDispatch && !isComputeSpace) || (!isDispatch && !isGraphicsSpace))
			return false;
		if (binding.rootDescriptor)
			return binding.gpuVirtualAddress != 0 &&
				(binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
				 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV);
		if (!binding.hasDescriptor)
			return false;
		if (!binding.descriptor.resource || !binding.descriptor.hasResourceDesc)
			return false;
		const bool isBufferResource =
			binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		const bool isTextureResource =
			IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
		if (!isBufferResource && !isTextureResource)
			return false;
		if (isDispatch)
			return binding.descriptor.kind == DescriptorKind::CBV ||
				binding.descriptor.kind == DescriptorKind::SRV ||
				binding.descriptor.kind == DescriptorKind::UAV;
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return isBufferResource;
		return binding.descriptor.kind == DescriptorKind::SRV ||
			binding.descriptor.kind == DescriptorKind::UAV;
	}

	static const char *ShortBindSpace(const std::string &bindSpace)
	{
		if (bindSpace == "graphics_cbv_srv_uav")
			return "gfx";
		if (bindSpace == "graphics_sampler")
			return "gsp";
		if (bindSpace == "compute_cbv_srv_uav")
			return "cmp";
		if (bindSpace == "compute_sampler")
			return "csp";
		if (bindSpace == "hunting_metadata")
			return "meta";
		if (bindSpace == "hunting_resource")
			return "res";
		return bindSpace.empty() ? "unk" : bindSpace.c_str();
	}

	static const char *ShortDescriptorKind(const std::string &kind)
	{
		if (kind == DescriptorKind::CBV || kind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return "cbv";
		if (kind == DescriptorKind::SRV || kind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return "srv";
		if (kind == DescriptorKind::UAV || kind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return "uav";
		if (kind == DescriptorKind::Sampler)
			return "smp";
		if (kind == DescriptorKind::RTV || kind == DescriptorKind::RENDER_TARGET_VIEW)
			return "rtv";
		if (kind == DescriptorKind::DSV || kind == DescriptorKind::DEPTH_STENCIL_VIEW)
			return "dsv";
		return kind.empty() ? "res" : kind.c_str();
	}

	static const char *ShaderVisibilityPrefix(UINT shaderVisibility, const std::string &bindSpace)
	{
		if (bindSpace.rfind("compute", 0) == 0)
			return "cs";
		switch (shaderVisibility) {
		case D3D12_SHADER_VISIBILITY_VERTEX:
			return "vs";
		case D3D12_SHADER_VISIBILITY_HULL:
			return "hs";
		case D3D12_SHADER_VISIBILITY_DOMAIN:
			return "ds";
		case D3D12_SHADER_VISIBILITY_GEOMETRY:
			return "gs";
		case D3D12_SHADER_VISIBILITY_PIXEL:
			return "ps";
		case D3D12_SHADER_VISIBILITY_ALL:
		default:
			return "all";
		}
	}

	static char RegisterPrefix(UINT rangeType, const std::string &descriptorKind)
	{
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return 't';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return 'u';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
				rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return 'b';
		if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			return 's';
		if (descriptorKind == DescriptorKind::SRV || descriptorKind == DescriptorKind::SHADER_RESOURCE_VIEW)
			return 't';
		if (descriptorKind == DescriptorKind::UAV || descriptorKind == DescriptorKind::UNORDERED_ACCESS_VIEW)
			return 'u';
		if (descriptorKind == DescriptorKind::CBV || descriptorKind == DescriptorKind::CONSTANT_BUFFER_VIEW)
			return 'b';
		if (descriptorKind == DescriptorKind::Sampler)
			return 's';
		return '\0';
	}

	static bool BuildSemanticSlot(
		const DX12FrameResourceBinding &binding, char *text, size_t textCount)
	{
		if (!text || textCount == 0)
			return false;
		text[0] = '\0';
		if (binding.bindSpace == "hunting_metadata") {
			sprintf_s(text, textCount, "meta-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.bindSpace == "hunting_resource") {
			sprintf_s(text, textCount, "res-d%llu",
				static_cast<unsigned long long>(binding.descriptorIndex));
			return true;
		}
		if (binding.shaderRegister == UINT_MAX)
			return false;
		const char registerPrefix = RegisterPrefix(binding.rangeType, binding.descriptor.kind);
		if (!registerPrefix)
			return false;
		const char *stage = ShaderVisibilityPrefix(binding.shaderVisibility, binding.bindSpace);
		sprintf_s(text, textCount, "%s-%c%u", stage, registerPrefix, binding.shaderRegister);
		return true;
	}
};




class DX12ResourceDumpNaming
{
public:
static const wchar_t *FileNameFromPath(const std::wstring &path)
{
	const wchar_t *text = path.c_str();
	const wchar_t *slash = wcsrchr(text, L'\\');
	return slash ? slash + 1 : text;
}

static bool IsHashChar(wchar_t value)
{
	return (value >= L'0' && value <= L'9') ||
		(value >= L'a' && value <= L'f') ||
		(value >= L'A' && value <= L'F');
}

static bool IsHashText(const wchar_t *text)
{
	if (!text)
		return false;
	for (size_t i = 0; i < 8; ++i) {
		if (!IsHashChar(text[i]))
			return false;
	}
	return true;
}

static void CopyHashText(const wchar_t *source, char *hash, size_t hashCount)
{
	if (!hash || hashCount == 0)
		return;
	hash[0] = '\0';
	if (!source)
		return;
	size_t i = 0;
	for (; i < hashCount - 1 && i < 8 && source[i]; ++i)
		hash[i] = static_cast<char>(source[i]);
	hash[i] = '\0';
}

static void FileHashFromPath(const std::wstring &path, char *hash, size_t hashCount)
{
	if (!hash || hashCount == 0)
		return;
	hash[0] = '\0';
	const wchar_t *name = FileNameFromPath(path);
	if (IsHashText(name) && name[8] == L'_' &&
			IsHashText(name + 9) &&
			(name[17] == L'-' || name[17] == L'.' || name[17] == L'\0')) {
		CopyHashText(name + 9, hash, hashCount);
		return;
	}
	size_t i = 0;
	for (; i < hashCount - 1 && i < 8 && name[i] && name[i] != L'-'; ++i)
		hash[i] = static_cast<char>(name[i]);
	hash[i] = '\0';
}

static uint32_t DescriptorHuntHash(const DX12FrameResourceBinding &binding, UINT64 copyBytes)
{
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	if (!binding.hasDescriptor) {
		uint32_t hash = DX12ResourceDumpCore::MakeFourCC('D', 'S', 'L', 'T');
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.psoIndex, sizeof(binding.psoIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rootParameterIndex, sizeof(binding.rootParameterIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeIndex, sizeof(binding.rangeIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeType, sizeof(binding.rangeType));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.shaderRegister, sizeof(binding.shaderRegister));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.registerSpace, sizeof(binding.registerSpace));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.descriptorIndex, sizeof(binding.descriptorIndex));
		return hash;
	}
	const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
		descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
	const UINT64 fallbackSize = descriptor.viewSize ? descriptor.viewSize : copyBytes;
	return DX12HashDescriptorBufferView(
		binding.hasDescriptor ? &descriptor : nullptr, fallbackGpuVa, fallbackSize);
}

static void FormatEventPrefix(UINT64 drawId, UINT64 dispatchId, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(0));
}

static void FormatMetadataPrefix(UINT64 descriptorIndex, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	swprintf_s(text, textCount, L"m%06llu",
		static_cast<unsigned long long>(descriptorIndex));
}

static void FormatEventPrefixWithSerial(
	UINT64 drawId, UINT64 dispatchId, UINT64 eventSerial, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(eventSerial));
}

static void AppendShaderHashNamePart(
	wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash)
{
	if (!text || textCount == 0 || !stage || !hasHash)
		return;
	const size_t used = wcslen(text);
	if (used >= textCount)
		return;
	swprintf_s(text + used, textCount - used, L"-%S%016llx",
		stage, static_cast<unsigned long long>(hash));
}

static bool HasAnyShaderHash(const DX12PsoShaderInfo &info)
{
	return info.hasVS || info.hasPS || info.hasCS;
}

static void BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

static void BuildFileName(const DumpTask &task, wchar_t *fileName, size_t fileNameCount)
{
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12BindingFrameExport::BuildIaBufferFileName(task.iaBuffer, fileName, fileNameCount);
		return;
	}

	const wchar_t *ext = task.isTexture ? L"dds" : L"buf";
	DX12FrameResourceBinding outputBinding;
	const DX12FrameResourceBinding *binding = &task.binding;
	wchar_t eventPrefix[32];
	if (task.sourceKind == DumpTaskSource::Resource) {
		FormatMetadataPrefix(task.binding.descriptorIndex,
			eventPrefix, ARRAYSIZE(eventPrefix));
	} else if (task.sourceKind == DumpTaskSource::Output) {
		outputBinding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(task.outputBinding);
		binding = &outputBinding;
		FormatEventPrefixWithSerial(
			task.outputBinding.drawId, task.outputBinding.dispatchId,
			task.outputBinding.eventSerial, eventPrefix, ARRAYSIZE(eventPrefix));
	} else {
		if (task.binding.bindSpace == "hunting_metadata")
			FormatMetadataPrefix(task.binding.descriptorIndex,
				eventPrefix, ARRAYSIZE(eventPrefix));
		else
			FormatEventPrefix(task.binding.drawId, task.binding.dispatchId,
				eventPrefix, ARRAYSIZE(eventPrefix));
	}
	wchar_t shaderPart[96];
	// Metadata texture discovery is not tied to a draw/dispatch PSO. Keeping
	// shader hashes out of those names avoids presenting missing context as a
	// bogus slot, which showed up as debug-fill cccccccc in hunting dumps.
	if (HasAnyShaderHash(binding->shaderInfo))
		BuildShaderHashNamePart(binding->shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	else
		shaderPart[0] = L'\0';
	char semanticSlot[32];
	if (DX12ResourceDumpPolicy::BuildSemanticSlot(*binding, semanticSlot, ARRAYSIZE(semanticSlot))) {
		swprintf_s(fileName, fileNameCount,
			L"%s-pso%llu%s-%S-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
			eventPrefix,
			static_cast<unsigned long long>(binding->psoIndex),
			shaderPart,
			DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
			semanticSlot,
			binding->rootParameterIndex,
			static_cast<unsigned long long>(binding->descriptorIndex),
			DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
			binding->descriptor.resource,
			static_cast<unsigned long long>(task.sourceOffset),
			static_cast<unsigned long long>(task.copyBytes),
			ext);
		return;
	}
	swprintf_s(fileName, fileNameCount,
		L"%s-pso%llu%s-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
		eventPrefix,
		static_cast<unsigned long long>(binding->psoIndex),
		shaderPart,
		DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
		binding->rootParameterIndex,
		static_cast<unsigned long long>(binding->descriptorIndex),
		DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
		binding->descriptor.resource,
		static_cast<unsigned long long>(task.sourceOffset),
		static_cast<unsigned long long>(task.copyBytes),
		ext);
}


static void BuildOutputDedupeKey(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	const char *kind = !binding.descriptor.kind.empty() ?
		binding.descriptor.kind.c_str() : output.kind.c_str();
	sprintf_s(key, keyCount, "output|%s|slot%u|%p|%llu|%llu|%u|%u",
		kind ? kind : "",
		output.slot,
		binding.descriptor.resource,
		static_cast<unsigned long long>(binding.descriptor.resourceOffset),
		static_cast<unsigned long long>(binding.descriptor.viewSize),
		binding.descriptor.viewFormat,
		binding.descriptor.viewDimension);
}

static void BuildDescriptorDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|%llu|%llu|%u|%u",
		descriptor.resource,
		static_cast<unsigned long long>(descriptor.resourceOffset),
		static_cast<unsigned long long>(descriptor.viewSize),
		descriptor.viewFormat,
		descriptor.viewDimension);
}

static void BuildDescriptorResourceDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		descriptor.resource,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Format) : 0,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Dimension) : 0);
}

static void BuildBindingDedupeKeyForCurrentMode(
	const DX12FrameResourceBinding &binding, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		// Hunting is a resource discovery pass. The descriptor slot and view
		// still describe where the texture was seen, but one ID3D12Resource must
		// produce one readback, matching RenderDoc-style capture granularity.
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildDescriptorDedupeKey(binding.descriptor, key, keyCount);
}

static void BuildOutputDedupeKeyForCurrentMode(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildOutputDedupeKey(output, binding, key, keyCount);
}

static void BuildResourceDedupeKey(
	const DX12ResourceSummary &resource, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		resource.resource,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Format) : 0,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Dimension) : 0);
}

static const char *IaRoleName(const DX12FrameIaBufferBinding &buffer)
{
	if (!buffer.role.empty())
		return buffer.role.c_str();
	return "UNKNOWN";
}

static bool BuildIaBufferDedupeKey(
	const DX12FrameIaBufferBinding &buffer,
	const DX12BufferResourceSummary *resolvedResource,
	bool resolved,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return false;

	key[0] = '\0';
	const char *role = DX12ResourceDumpNaming::IaRoleName(buffer);
	int written = 0;
	if (resolved && resolvedResource && resolvedResource->resource) {
		written = sprintf_s(key, keyCount, "ia|%s|%p|%llu|%llu|%u|%u",
			role,
			resolvedResource->resource,
			static_cast<unsigned long long>(resolvedResource->resourceOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			buffer.stride,
			buffer.format);
	} else {
		written = sprintf_s(key, keyCount, "ia|%s|0x%llx|%llu|%u|%u",
			role,
			static_cast<unsigned long long>(buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.viewSize),
			buffer.stride,
			buffer.format);
	}

	return written > 0 && key[0] != '\0';
}


};



// ===================== Resources/DX12ResourceFileIO.hpp =====================


class DX12TextureHashUtils
{
public:
	static bool IsBlockCompressedFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	static uint32_t BitsPerPixel(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
			return 64;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_YUY2:
			return 32;
		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
			return 24;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;
		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_NV11:
			return 12;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
			return 8;
		case DXGI_FORMAT_R1_UNORM:
			return 1;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;
		default:
			return 0;
		}
	}

	static UINT Texture2DTopMipRowCount(DXGI_FORMAT format, UINT height)
	{
		if (!height)
			return 0;
		if (IsBlockCompressedFormat(format))
			return (std::max<UINT>)(1, (height + 3) / 4);
		return height;
	}

	static UINT64 Texture2DTopMipRowBytes(DXGI_FORMAT format, UINT width)
	{
		if (!width)
			return 0;
		if (IsBlockCompressedFormat(format)) {
			const uint32_t bpp = BitsPerPixel(format);
			const UINT blockBytes = bpp == 4 ? 8 : 16;
			const UINT blocksWide = (std::max<UINT>)(1, (width + 3) / 4);
			return static_cast<UINT64>(blocksWide) * blockBytes;
		}
		const uint32_t bpp = BitsPerPixel(format);
		if (!bpp)
			return 0;
		return (static_cast<UINT64>(width) * bpp + 7) / 8;
	}

	static bool Texture2DTopMipHashFromRows(
		const D3D12_RESOURCE_DESC &desc,
		const uint8_t *base,
		UINT64 sourceRowPitch,
		UINT64 sourceRowBytes,
		UINT sourceRowCount,
		uint32_t *hashOut)
	{
		if (!hashOut)
			return false;
		*hashOut = 0;
		if (!base || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		if (desc.Width > UINT_MAX)
			return false;
		const UINT width = static_cast<UINT>(desc.Width);
		const UINT rowCount = Texture2DTopMipRowCount(desc.Format, desc.Height);
		const UINT64 rowBytes = Texture2DTopMipRowBytes(desc.Format, width);
		if (!rowCount || !rowBytes)
			return false;
		if (sourceRowCount < rowCount || sourceRowBytes < rowBytes || sourceRowPitch < rowBytes)
			return false;

		uint32_t hash = 0;
		for (UINT row = 0; row < rowCount; ++row) {
			const uint8_t *src = base + static_cast<size_t>(row) * sourceRowPitch;
			hash = crc32c_append(hash, src, static_cast<size_t>(rowBytes));
		}
		hash = Dx11StyleTexture2DDescHash(desc, hash);
		*hashOut = hash;
		return true;
	}

	static bool Texture2DTopMipHashFromPlacedFootprint(
		const D3D12_RESOURCE_DESC &desc,
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout,
		UINT rowCount,
		UINT64 rowBytes,
		const void *mappedData,
		uint32_t *hashOut)
	{
		if (!mappedData)
			return false;
		const uint8_t *base =
			static_cast<const uint8_t*>(mappedData) + layout.Offset;
		return Texture2DTopMipHashFromRows(
			desc, base, layout.Footprint.RowPitch, rowBytes, rowCount, hashOut);
	}

	// Result cache for TextureCopyableBytes. The device calls it makes
	// (CheckFeatureSupport + GetCopyableFootprints) cost ~10-20us each and
	// are pure functions of the (immutable) resource desc; the same descs are
	// hit repeatedly by the write-time hunt hash and by the draw-time
	// fallback. A 128-bit desc fingerprint makes aliasing negligible, and a
	// mismatch only degrades to a recompute, never a wrong value.
	struct CopyableBytesCacheEntry
	{
		UINT64 keyA = 0;
		UINT64 keyB = 0;
		UINT64 value = 0;
	};
	static constexpr UINT CopyableBytesCacheCount = 128;
	inline static CopyableBytesCacheEntry sCopyableBytesCache[CopyableBytesCacheCount] = {};

	static void CopyableBytesKey(
		const D3D12_RESOURCE_DESC &desc, UINT subresources, UINT64 *keyA, UINT64 *keyB)
	{
		const UINT64 a =
			(static_cast<UINT64>(desc.Dimension) << 56) |
			(static_cast<UINT64>(desc.Format) << 40) |
			(static_cast<UINT64>(desc.SampleDesc.Count) << 24) |
			(static_cast<UINT64>(desc.SampleDesc.Quality) << 8) |
			(static_cast<UINT64>(desc.Layout) & 0xFF);
		const UINT64 b =
			(static_cast<UINT64>(desc.Alignment) << 32) |
			(static_cast<UINT64>(desc.Flags) & 0xFFFFFFFFull);
		*keyA = a ^ (desc.Width * 0x9E3779B97F4A7C15ull) ^
			(static_cast<UINT64>(desc.MipLevels) << 32) ^
			static_cast<UINT64>(subresources);
		*keyB = b ^ (desc.Height * 0xC2B2AE3D27D4EB4Full) ^
			(static_cast<UINT64>(desc.DepthOrArraySize) * 0x165667B19E3779F9ull);
	}

	static UINT64 TextureCopyableBytes(ID3D12Device *device, const D3D12_RESOURCE_DESC &desc)
	{
		if (!device)
			return 0;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D &&
		    desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		    desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			return 0;
		if (desc.SampleDesc.Count > 1)
			return 0;
		UINT subresources = static_cast<UINT>(desc.MipLevels ? desc.MipLevels : 1);
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			subresources *= static_cast<UINT>(desc.DepthOrArraySize ? desc.DepthOrArraySize : 1);
		if (!subresources)
			return 0;
		UINT64 keyA = 0;
		UINT64 keyB = 0;
		CopyableBytesKey(desc, subresources, &keyA, &keyB);
		const UINT cacheIndex = static_cast<UINT>(
			(keyA ^ (keyB >> 7)) & (CopyableBytesCacheCount - 1));
		CopyableBytesCacheEntry &cached = sCopyableBytesCache[cacheIndex];
		if (cached.keyA == keyA && cached.keyB == keyB)
			return cached.value;
		D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {};
		formatInfo.Format = desc.Format;
		__try {
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) &&
			    formatInfo.PlaneCount > 1) {
				const UINT64 planeSubresources =
					static_cast<UINT64>(subresources) * formatInfo.PlaneCount;
				if (planeSubresources == 0 || planeSubresources > static_cast<UINT64>(UINT_MAX))
					return 0;
				subresources = static_cast<UINT>(planeSubresources);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
		UINT64 copyBytes = 0;
		__try {
			device->GetCopyableFootprints(
				&desc, 0, subresources, 0, nullptr, nullptr, nullptr, &copyBytes);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
		const UINT64 result = copyBytes == UINT64_MAX ? 0 : copyBytes;
		cached.keyA = keyA;
		cached.keyB = keyB;
		cached.value = result;
		return result;
	}

	static UINT64 DescriptorTextureHashFallbackSize(
		ID3D12Device *device,
		const DX12DescriptorSummary &descriptor)
	{
		if (!descriptor.hasResourceDesc)
			return descriptor.viewSize;
		const D3D12_RESOURCE_DESC &desc = descriptor.resourceDesc;
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		    desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		    desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
			const UINT64 copyBytes = TextureCopyableBytes(device, desc);
			if (copyBytes)
				return copyBytes;
		}
		return descriptor.viewSize;
	}

	static uint32_t DescriptorHuntHash(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding)
	{
		const DX12DescriptorSummary &descriptor = binding.descriptor;
		const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
			descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
		const UINT64 fallbackSize =
			DescriptorTextureHashFallbackSize(device, descriptor);
		return DX12HashDescriptorBufferView(
			binding.hasDescriptor ? &descriptor : nullptr,
			fallbackGpuVa,
			fallbackSize);
	}

	// Summary-based variant used at descriptor write time so the content hash
	// (and its fallback-size device calls) is computed once per Create*View
	// instead of once per draw per binding.
	static uint32_t DescriptorHuntHash(
		ID3D12Device *device,
		const DX12DescriptorSummary &descriptor)
	{
		const UINT64 fallbackSize =
			DescriptorTextureHashFallbackSize(device, descriptor);
		return DX12HashDescriptorBufferView(
			&descriptor, descriptor.gpuVirtualAddress, fallbackSize);
	}

	static bool FillD3D11Texture2DDesc(
		const D3D12_RESOURCE_DESC &desc,
		D3D11_TEXTURE2D_DESC *outDesc)
	{
		if (!outDesc)
			return false;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		D3D11_TEXTURE2D_DESC dx11Desc = {};
		dx11Desc.Width = static_cast<UINT>(desc.Width);
		dx11Desc.Height = desc.Height;
		dx11Desc.MipLevels = desc.MipLevels;
		dx11Desc.ArraySize = desc.DepthOrArraySize;
		dx11Desc.Format = desc.Format;
		dx11Desc.SampleDesc = desc.SampleDesc;
		dx11Desc.Usage = D3D11_USAGE_DEFAULT;
		dx11Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
			dx11Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		dx11Desc.CPUAccessFlags = 0;
		dx11Desc.MiscFlags = 0;
		*outDesc = dx11Desc;
		return true;
	}

	static uint32_t Dx11StyleTexture2DDescHash(
		const D3D12_RESOURCE_DESC &desc,
		uint32_t initialHash)
	{
		D3D11_TEXTURE2D_DESC dx11Desc = {};
		if (!FillD3D11Texture2DDesc(desc, &dx11Desc))
			return 0;
		return crc32c_append(
			initialHash,
			reinterpret_cast<const uint8_t*>(&dx11Desc),
			sizeof(dx11Desc));
	}

	static bool IsSrgbFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	static DXGI_FORMAT MakeSrgbFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_TYPELESS:
			return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_TYPELESS:
			return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_TYPELESS:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_TYPELESS:
			return DXGI_FORMAT_BC7_UNORM_SRGB;
		default:
			return format;
		}
	}

	static DXGI_FORMAT MakeUnormFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC1_TYPELESS:
			return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
			return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
			return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
		case DXGI_FORMAT_BC7_TYPELESS:
			return DXGI_FORMAT_BC7_UNORM;
		default:
			return format;
		}
	}

	static bool FormatsDifferOnlyBySrgb(DXGI_FORMAT lhs, DXGI_FORMAT rhs)
	{
		return MakeUnormFormat(lhs) == MakeUnormFormat(rhs);
	}

	static DXGI_FORMAT CompatibleReplacementSrvFormat(
		DXGI_FORMAT replacementResourceFormat,
		DXGI_FORMAT originalViewFormat)
	{
		if (originalViewFormat == DXGI_FORMAT_UNKNOWN)
			return replacementResourceFormat;
		if (replacementResourceFormat == originalViewFormat)
			return replacementResourceFormat;
		if (FormatsDifferOnlyBySrgb(replacementResourceFormat, originalViewFormat)) {
			return IsSrgbFormat(originalViewFormat) ?
				MakeSrgbFormat(replacementResourceFormat) :
				MakeUnormFormat(replacementResourceFormat);
		}
		return replacementResourceFormat;
	}
};




class DX12ResourceDumpFileWriter
{
public:
static void WriteU32(FILE *file, uint32_t value)
{
	fwrite(&value, sizeof(value), 1, file);
}

static bool IsBlockCompressedFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

static uint32_t BitsPerPixel(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
		return 32;
	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		return 24;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;
	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
		return 8;
	case DXGI_FORMAT_R1_UNORM:
		return 1;
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return 8;
	default:
		return 0;
	}
}

static UINT MipSize(UINT64 value, UINT mip)
{
	UINT64 size = value >> mip;
	return static_cast<UINT>(size ? size : 1);
}

static UINT64 TextureMipBytes(const D3D12_RESOURCE_DESC &desc, UINT mip)
{
	const UINT width = MipSize(desc.Width, mip);
	const UINT height = MipSize(desc.Height, mip);
	if (IsBlockCompressedFormat(desc.Format)) {
		const UINT blockBytes = BitsPerPixel(desc.Format) == 4 ? 8 : 16;
		const UINT blocksWide = std::max<UINT>(1, (width + 3) / 4);
		const UINT blocksHigh = std::max<UINT>(1, (height + 3) / 4);
		return static_cast<UINT64>(blocksWide) * blocksHigh * blockBytes;
	}
	const uint32_t bpp = BitsPerPixel(desc.Format);
	if (!bpp)
		return 0;
	return (static_cast<UINT64>(width) * height * bpp + 7) / 8;
}

static void WriteDDSHeader(FILE *file, const D3D12_RESOURCE_DESC &desc)
{
	WriteU32(file, DX12ResourceDumpCore::MakeFourCC('D', 'D', 'S', ' '));
	WriteU32(file, 124);
	uint32_t flags = 0x00001007;
	if (desc.MipLevels > 1)
		flags |= 0x00020000;
	if (IsBlockCompressedFormat(desc.Format))
		flags |= 0x00080000;
	else
		flags |= 0x00000008;
	WriteU32(file, flags);
	WriteU32(file, desc.Height);
	WriteU32(file, static_cast<uint32_t>(desc.Width));
	WriteU32(file, static_cast<uint32_t>(TextureMipBytes(desc, 0)));
	WriteU32(file, 0);
	WriteU32(file, desc.MipLevels ? desc.MipLevels : 1);
	for (int i = 0; i < 11; ++i)
		WriteU32(file, 0);

	WriteU32(file, 32);
	WriteU32(file, 0x00000004);
	WriteU32(file, DX12ResourceDumpCore::MakeFourCC('D', 'X', '1', '0'));
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	uint32_t caps = 0x00001000;
	if (desc.MipLevels > 1)
		caps |= 0x00400008;
	WriteU32(file, caps);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	WriteU32(file, static_cast<uint32_t>(desc.Format));
	WriteU32(file, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
	WriteU32(file, 0);
	WriteU32(file, desc.DepthOrArraySize ? desc.DepthOrArraySize : 1);
	WriteU32(file, 0);
}

static bool WriteTextureDDSFile(FILE *file, const DumpTask &task, const void *mappedData)
{
	return DX12ResourceDumpDdsUtils::WriteTexture2DFile(file, task, mappedData);
}

static bool WriteBufferFileData(FILE *file, const DumpTask &task, const void *mappedData)
{
	if (!file || !mappedData)
		return false;
	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	fwrite(src, 1, static_cast<size_t>(task.copyBytes), file);
	return true;
}

static void BuildTextFileName(const std::wstring &fileName, wchar_t *textName, size_t textNameCount)
{
	if (!textName || textNameCount == 0)
		return;
	textName[0] = L'\0';
	if (fileName.empty())
		return;
	const wchar_t *dot = wcsrchr(fileName.c_str(), L'.');
	if (dot) {
		const size_t prefixLength = static_cast<size_t>(dot - fileName.c_str());
		wcsncpy_s(textName, textNameCount, fileName.c_str(), prefixLength);
	} else {
		wcsncpy_s(textName, textNameCount, fileName.c_str(), _TRUNCATE);
	}
	wcscat_s(textName, textNameCount, L".txt");
}

static bool WriteGenericBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	fprintf(file, "bytes: %llu\n", static_cast<unsigned long long>(task.copyBytes));
	fprintf(file, "offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	const UINT stride = task.sourceKind == DumpTaskSource::InputAssembler ?
		task.iaBuffer.stride : task.binding.descriptor.structureByteStride;
	if (stride)
		fprintf(file, "stride: %u\n", stride);

	const UINT64 floatCount = task.copyBytes / sizeof(float);
	const float *floats = reinterpret_cast<const float*>(data);
	for (UINT64 i = 0; i < floatCount; ++i)
		fprintf(file, "buf[%llu]: %.9g\n", static_cast<unsigned long long>(i), floats[i]);
	return true;
}

static bool WriteIndexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "index count: %llu\n", static_cast<unsigned long long>(
		buffer.format == DXGI_FORMAT_R16_UINT ? task.copyBytes / 2 : task.copyBytes / 4));
	fprintf(file, "format: %s\n", buffer.format == DXGI_FORMAT_R16_UINT ?
		"DXGI_FORMAT_R16_UINT" : buffer.format == DXGI_FORMAT_R32_UINT ?
		"DXGI_FORMAT_R32_UINT" : "UNKNOWN");

	if (buffer.format == DXGI_FORMAT_R16_UINT) {
		const uint16_t *indices = reinterpret_cast<const uint16_t*>(data);
		const UINT64 count = task.copyBytes / 2;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	if (buffer.format == DXGI_FORMAT_R32_UINT) {
		const uint32_t *indices = reinterpret_cast<const uint32_t*>(data);
		const UINT64 count = task.copyBytes / 4;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	return WriteGenericBufferTextFile(file, task, data);
}

static bool WriteVertexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "slot: %u\n", buffer.slot);
	fprintf(file, "stride: %u\n", buffer.stride);

	const UINT stride = buffer.stride ? buffer.stride : 16;
	const UINT64 vertexCount = stride ? task.copyBytes / stride : 0;
	fprintf(file, "vertex count: %llu\n", static_cast<unsigned long long>(vertexCount));
	for (UINT64 v = 0; v < vertexCount; ++v) {
		const uint8_t *vertex = data + v * stride;
		fprintf(file, "vertex[%llu]", static_cast<unsigned long long>(v));
		const UINT floatCount = stride / sizeof(float);
		const float *floats = reinterpret_cast<const float*>(vertex);
		for (UINT c = 0; c < floatCount; ++c)
			fprintf(file, " f%u=%.9g", c, floats[c]);
		fprintf(file, "\n");
	}
	return true;
}

static bool WriteBufferTextFile(const wchar_t *path, const DumpTask &task, const void *mappedData)
{
	if (!path || !mappedData || task.isTexture)
		return false;

	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return false;

	const bool ok = DX12ResourceDumpBufferTextUtils::WriteBufferTextFile(file, task, mappedData);

	fclose(file);
	return ok;
}

static uint32_t HashDumpTaskData(const DumpTask &task, const void *mappedData)
{
	uint32_t hash = 0;
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.desc, sizeof(task.desc));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.sourceOffset, sizeof(task.sourceOffset));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.copyBytes, sizeof(task.copyBytes));
	hash = DX12ResourceDumpCore::HashBytes(hash, &task.isTexture, sizeof(task.isTexture));

	if (task.isTexture) {
		const uint8_t *mapped = static_cast<const uint8_t*>(mappedData);
		for (size_t subresource = 0; subresource < task.textureLayouts.size(); ++subresource) {
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[subresource];
			hash = DX12ResourceDumpCore::HashBytes(hash, &layout.Footprint.RowPitch, sizeof(layout.Footprint.RowPitch));
			hash = DX12ResourceDumpCore::HashBytes(hash, &task.textureRows[subresource], sizeof(task.textureRows[subresource]));
			hash = DX12ResourceDumpCore::HashBytes(hash, &task.textureRowSizes[subresource], sizeof(task.textureRowSizes[subresource]));
			const uint8_t *base = mapped + layout.Offset;
			for (UINT row = 0; row < task.textureRows[subresource]; ++row) {
				const uint8_t *src = base + static_cast<size_t>(row) * layout.Footprint.RowPitch;
				hash = DX12ResourceDumpCore::HashBytes(hash, src, static_cast<size_t>(task.textureRowSizes[subresource]));
			}
		}
		return hash;
	}

	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	return DX12ResourceDumpCore::HashBytes(hash, src, static_cast<size_t>(task.copyBytes));
}

static bool UpdateTextureContentHashFromMappedData(DumpTask *task, const void *mappedData)
{
	if (!task || !mappedData || !task->isTexture)
		return false;
	if (task->hasContentHash && task->contentHash)
		return true;

	uint32_t hash = 0;
	const bool hasTopMipLayout =
		task->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		!task->textureLayouts.empty() &&
		!task->textureRows.empty() &&
		!task->textureRowSizes.empty();
	if (hasTopMipLayout) {
		DX12TextureHashUtils::Texture2DTopMipHashFromPlacedFootprint(
			task->desc,
			task->textureLayouts[0],
			task->textureRows[0],
			task->textureRowSizes[0],
			mappedData,
			&hash);
	}
	if (!hash && DX12TryGetTextureHash(task->source, &hash)) {
		task->contentHash = hash;
		task->hasContentHash = true;
		return true;
	}
	if (!hash) {
		DX12LogManager::DebugFields("DX12TextureHashCache",
			"\"stage\":\"dump_hash_miss\",\"source\":\"%p\",\"dimension\":%u,\"format\":%u,\"width\":%llu,\"height\":%u,\"layouts\":%zu",
			task->source,
			static_cast<UINT>(task->desc.Dimension),
			static_cast<UINT>(task->desc.Format),
			static_cast<unsigned long long>(task->desc.Width),
			task->desc.Height,
			task->textureLayouts.size());
		return false;
	}

	task->contentHash = hash;
	task->hasContentHash = true;
	DX12CacheTextureHash(task->source, hash, &task->desc, "dump_readback");
	return true;
}

static void BuildDedupedRelativePath(const wchar_t *dedupeFileName, wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return;
	path[0] = L'\0';
	if (!dedupeFileName || !dedupeFileName[0])
		return;
	swprintf_s(path, pathCount, L"deduped\\%s", dedupeFileName);
}

static uint32_t HuntHashForTask(const DumpTask &task)
{
	if (!task.isTexture)
		return 0;
	if (task.hasContentHash && task.contentHash)
		return task.contentHash;
	uint32_t cachedHash = 0;
	if (DX12TryGetTextureHash(task.source, &cachedHash))
		return cachedHash;
	if (task.sourceKind == DumpTaskSource::Output) {
		DX12FrameResourceBinding outputBinding =
			DX12ResourceDumpCore::ResourceBindingFromOutputBinding(task.outputBinding);
		return DX12ResourceDumpNaming::DescriptorHuntHash(outputBinding, task.copyBytes);
	}
	if (task.sourceKind == DumpTaskSource::InputAssembler)
		return 0;
	return DX12ResourceDumpNaming::DescriptorHuntHash(task.binding, task.copyBytes);
}

static bool WriteDedupedResourceFile(
	const wchar_t *dedupeDir, const wchar_t *resourceDir,
	DumpTask *task, const void *mappedData,
	std::wstring *dedupePathOut = nullptr)
{
	if (!dedupeDir || !resourceDir || !task || !mappedData)
		return false;

	UpdateTextureContentHashFromMappedData(task, mappedData);
	const uint32_t fileHash = HashDumpTaskData(*task, mappedData);
	const uint32_t huntHash = HuntHashForTask(*task);
	wchar_t dedupeFileName[MAX_PATH];
	DX12ResourceDumpDedupedNamingUtils::BuildDedupedFileName(
		*task, huntHash, fileHash, dedupeFileName, ARRAYSIZE(dedupeFileName));
	wchar_t dedupePath[MAX_PATH];
	swprintf_s(dedupePath, L"%s\\%s", dedupeDir, dedupeFileName);
	if (dedupePathOut) {
		wchar_t relativePath[MAX_PATH];
		BuildDedupedRelativePath(dedupeFileName, relativePath, ARRAYSIZE(relativePath));
		*dedupePathOut = relativePath;
	}

	if (GetFileAttributesW(dedupePath) == INVALID_FILE_ATTRIBUTES) {
		FILE *file = _wfsopen(dedupePath, L"wb", _SH_DENYNO);
		if (!file)
			return false;
		bool ok = task->isTexture ? WriteTextureDDSFile(file, *task, mappedData) :
			WriteBufferFileData(file, *task, mappedData);
		fclose(file);
		if (!ok) {
			DeleteFileW(dedupePath);
			return false;
		}
	}
	if (!task->isTexture) {
		wchar_t dedupeTextPath[MAX_PATH];
		BuildTextFileName(dedupePath, dedupeTextPath, ARRAYSIZE(dedupeTextPath));
		if (GetFileAttributesW(dedupeTextPath) == INVALID_FILE_ATTRIBUTES)
			WriteBufferTextFile(dedupeTextPath, *task, mappedData);
	}
	return true;
}

static bool MapAndWriteTask(
	const wchar_t *dedupeDir, const wchar_t *resourceDir, DumpTask *task,
	std::wstring *dedupePath)
{
	if (!dedupeDir || !resourceDir || !task)
		return false;

	bool ok = false;
	__try {
		void *mapped = nullptr;
		D3D12_RANGE range = { static_cast<SIZE_T>(task->readbackOffset),
			static_cast<SIZE_T>(task->readbackOffset + task->copyBytes) };
		ID3D12Resource *mapResource = task->directMap ? task->source : task->readback;
		if (!mapResource) {
			task->skipNote = task->directMap ? "missing_direct_resource" : "missing_readback";
			DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"%s\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				task->skipNote,
				DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
			return false;
		}
	const HRESULT hr = mapResource->Map(0, &range, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		ok = WriteDedupedResourceFile(dedupeDir, resourceDir, task, mapped, dedupePath);
		D3D12_RANGE emptyRange = { 0, 0 };
			mapResource->Unmap(0, &emptyRange);
		} else {
			task->skipNote = "map_failed";
			DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"map_failed\",\"hr\":\"0x%lx\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				hr,
				DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "map_or_write_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceMapWriteSkipped",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
			GetExceptionCode(), task->source, task->readback, task->directMap ? 1 : 0,
			static_cast<unsigned long long>(task->readbackOffset),
			static_cast<unsigned long long>(task->copyBytes));
		ok = false;
	}
	return ok;
}


};



// ===================== Resources/DX12ResourceDump.hpp =====================


class DX12ResourceDumpPrepare
{
public:
static const char *RootDescriptorKindName(UINT rangeType)
{
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
		return DescriptorKind::UAV.data();
	return "";
}

static UINT DescriptorRangeTypeFromKind(const std::string &kind)
{
	if (kind == DescriptorKind::CBV || kind == DescriptorKind::CONSTANT_BUFFER_VIEW)
		return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	if (kind == DescriptorKind::UAV || kind == DescriptorKind::UNORDERED_ACCESS_VIEW)
		return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

static bool ResourceNeedsBarrier(const DX12DescriptorSummary &descriptor)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			descriptor.resourceHeapType, descriptor.hasResourceHeapType))
		return false;
	return true;
}

static bool ResourceNeedsBarrier(const DX12BufferResourceSummary &resource)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			resource.resourceHeapType, resource.hasResourceHeapType))
		return false;
	return true;
}

static bool StateTrackingAllowsCopy(const DX12DescriptorSummary &descriptor)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			descriptor.resourceHeapType, descriptor.hasResourceHeapType))
		return true;
	return descriptor.hasCurrentState;
}

static bool StateTrackingAllowsCopy(const DX12BufferResourceSummary &resource)
{
	if (DX12ResourceStateTracker::HeapTypeAllowsDirectCopy(
			resource.resourceHeapType, resource.hasResourceHeapType))
		return true;
	return resource.hasCurrentState;
}

static bool ResourceIsCpuVisible(const DX12DescriptorSummary &descriptor)
{
	return descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
}

static bool ResourceIsCpuVisible(const DX12BufferResourceSummary &resource)
{
	return resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
}

static bool CreateReadbackForTask(
	ID3D12Device *device, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;

	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}
	if (task->copyBytes > DX12ResourceDumpSafetyUtils::MaxSingleReadbackBytes()) {
		task->skipNote = "single_readback_too_large";
		return false;
	}
	if (frameReadbackBytes && frameReadbackBudgetBytes > 0 &&
		*frameReadbackBytes <= frameReadbackBudgetBytes &&
		task->copyBytes > frameReadbackBudgetBytes - *frameReadbackBytes) {
		task->skipNote = "frame_readback_budget_exceeded";
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC readbackDesc = {};
	readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readbackDesc.Width = task->copyBytes;
	readbackDesc.Height = 1;
	readbackDesc.DepthOrArraySize = 1;
	readbackDesc.MipLevels = 1;
	readbackDesc.SampleDesc.Count = 1;
	readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&task->readback));
	if (FAILED(hr) || !task->readback) {
		task->skipNote = "readback_create_failed";
		return false;
	}

	if (frameReadbackBytes)
		*frameReadbackBytes += task->copyBytes;
	task->ready = true;
	return true;
}

static bool RetainTaskSource(DumpTask *task)
{
	if (!task || !task->source)
		return false;
	if (task->sourceRetained)
		return true;
	if (!DX12RetainResource(task->source)) {
		task->skipNote = "source_retain_failed";
		return false;
	}
	task->sourceRetained = true;
	return true;
}

static bool PrepareDirectMapTask(DumpTask *task)
{
	if (!task || !task->source)
		return false;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "gpu_copy_disabled";
		return false;
	}
	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}

	task->directMap = true;
	task->ready = true;
	task->copied = true;
	task->readbackOffset = task->sourceOffset;
	return true;
}

static bool FillDescriptorDumpTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task)
{
	if (!device || !task || !DX12ResourceDumpPolicy::ShouldDumpBinding(binding))
		return false;

	task->sourceKind = DumpTaskSource::Descriptor;
	task->binding = binding;
	if (binding.rootDescriptor) {
		DX12BufferResourceSummary resource;
		if (!DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, 1, &resource) ||
			!resource.resource || !resource.hasResourceDesc) {
			task->skipNote = "unresolved_root_cbv";
			return false;
		}
		task->source = resource.resource;
		task->desc = resource.resourceDesc;
		task->binding.hasDescriptor = true;
		task->binding.descriptor.kind = RootDescriptorKindName(binding.rangeType);
		task->binding.descriptor.resource = resource.resource;
		task->binding.descriptor.resourceDesc = resource.resourceDesc;
		task->binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
		task->binding.descriptor.resourceHeapType = resource.resourceHeapType;
		task->binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		task->binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		task->binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = task->desc.Width > resource.resourceOffset ?
			task->desc.Width - resource.resourceOffset : 0;
		task->binding.descriptor.viewSize = availableBytes;
		task->binding.descriptor.currentState = resource.currentState;
		task->binding.descriptor.hasCurrentState = resource.hasCurrentState;
	} else {
		task->source = binding.descriptor.resource;
		task->desc = binding.descriptor.resourceDesc;
	}

	if (!task->source) {
		task->skipNote = "missing_resource_pointer";
		return false;
	}
	if (!DX12ResourceDumpSafetyUtils::ResourceDescLooksDumpable(task->desc)) {
		task->skipNote = "invalid_resource_desc";
		return false;
	}

	task->sourceState = task->binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->binding.descriptor.currentState) :
		DX12ResourceStateTracker::InferDescriptorSourceState(task->binding);
	task->stateKnown = StateTrackingAllowsCopy(task->binding.descriptor);
	task->needsBarrier = ResourceNeedsBarrier(task->binding.descriptor);
	if (!task->stateKnown) {
		// State unknown: COMMON is the only universally valid before-state guess.
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	if (DX12ResourceDumpPolicy::IsTextureResourceDimension(task->desc.Dimension)) {
		if (task->desc.SampleDesc.Count > 1) {
			task->skipNote = "multisampled_texture_not_supported";
			return false;
		}
		D3D12_RESOURCE_DESC copyDesc = task->desc;
		UINT subresourceCount = 0;
		if (!DX12ResourceDumpSafetyUtils::TextureSubresourceCountForDump(
				task->desc, &subresourceCount)) {
			task->skipNote = "invalid_texture_subresources";
			return false;
		}
		D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = {};
		formatInfo.Format = task->desc.Format;
		__try {
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) &&
				formatInfo.PlaneCount > 1) {
				const UINT64 planeSubresources =
					static_cast<UINT64>(subresourceCount) * formatInfo.PlaneCount;
				if (planeSubresources == 0 ||
						planeSubresources > DX12ResourceDumpSafetyUtils::MaxTextureSubresourcesForDump()) {
					task->skipNote = "too_many_texture_subresources";
					return false;
				}
				subresourceCount = static_cast<UINT>(planeSubresources);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			task->skipNote = "format_info_exception";
			return false;
		}
		task->textureLayouts.resize(subresourceCount);
		task->textureRows.resize(subresourceCount);
		task->textureRowSizes.resize(subresourceCount);
		// D3D12 copies planar formats as extra subresources; RenderDoc does
		// the same PlaneCount expansion before GetCopyableFootprints. Skipping
		// invalid footprints avoids recording command lists that fail at Close.
		__try {
			device->GetCopyableFootprints(&copyDesc, 0, subresourceCount, 0,
				task->textureLayouts.data(), task->textureRows.data(),
				task->textureRowSizes.data(), &task->copyBytes);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			task->skipNote = "copyable_footprints_exception";
			return false;
		}
		if (task->copyBytes == UINT64_MAX) {
			task->skipNote = "copyable_footprints_failed";
			return false;
		}
		if (task->copyBytes > DX12ResourceDumpSafetyUtils::MaxSingleReadbackBytes()) {
			task->skipNote = "single_readback_too_large";
			return false;
		}
		task->isTexture = true;
	} else if (task->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->sourceOffset = task->binding.descriptor.resourceOffset;
		task->copyBytes = task->binding.descriptor.viewSize ? task->binding.descriptor.viewSize : task->desc.Width;
		if (task->sourceOffset + task->copyBytes > task->desc.Width)
			task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
		task->isTexture = false;
	} else {
		task->skipNote = "unsupported_dimension";
		return false;
	}

	return true;
}

static bool FillIaDumpTask(const DX12FrameIaBufferBinding &buffer, DumpTask *task)
{
	if (!task)
		return false;

	DX12BufferResourceSummary resource = buffer.resource;
	bool resolved = buffer.resolved;
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		resolved = DX12BindingFrameExport::ResolveIaBufferResourceSummary(
			buffer, &resource);
	}
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		task->skipNote = "unresolved_gpu_va";
		return false;
	}

	task->sourceKind = DumpTaskSource::InputAssembler;
	task->iaBuffer = buffer;
	task->iaBuffer.resource = resource;
	task->iaBuffer.resolved = true;
	task->source = resource.resource;
	task->desc = resource.resourceDesc;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "unsupported_dimension";
		return false;
	}
	// IA dumps now use full view granularity for both VB and IB. Draw-specific
	// slicing stays in log.jsonl so the same .buf can serve multiple draws.
	task->sourceOffset = task->iaBuffer.resource.resourceOffset;
	task->copyBytes = buffer.viewSize;
	if (task->sourceOffset + task->copyBytes > task->desc.Width)
		task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
	task->sourceState = task->iaBuffer.resource.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->iaBuffer.resource.currentState) :
		DX12ResourceStateTracker::InferIaBufferSourceState(task->iaBuffer);
	task->stateKnown = StateTrackingAllowsCopy(task->iaBuffer.resource);
	task->needsBarrier = ResourceNeedsBarrier(task->iaBuffer.resource);
	if (!task->stateKnown)
		task->sourceState = D3D12_RESOURCE_STATE_COMMON;
	if (task->copyBytes == 0) {
		task->skipNote = "copy_size_zero";
		return false;
	}
	task->isTexture = false;
	return true;
}

static bool PrepareHuntingDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingMetadataTextureTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!descriptor.resource || !descriptor.hasResourceDesc ||
		!DX12ResourceDumpPolicy::IsTextureResourceDimension(descriptor.resourceDesc.Dimension))
		return false;
	if (!DX12ResourceDumpSafetyUtils::DescriptorResourceDescLooksDumpable(descriptor)) {
		task->skipNote = "invalid_metadata_texture_desc";
		return false;
	}

	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_metadata";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptor = descriptor;
	binding.hasDescriptor = true;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = DescriptorKind::SRV;

	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	// Hunting is resource-discovery oriented: metadata texture descriptors must
	// not depend on a draw event using the descriptor table in the captured frame.
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingMetadataBufferTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!descriptor.resource || !descriptor.hasResourceDesc ||
		descriptor.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;
	if (!DX12ResourceDumpSafetyUtils::DescriptorResourceDescLooksDumpable(descriptor)) {
		task->skipNote = "invalid_metadata_buffer_desc";
		return false;
	}

	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_metadata";
	binding.rangeType = DescriptorRangeTypeFromKind(descriptor.kind);
	binding.descriptor = descriptor;
	binding.hasDescriptor = true;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = DescriptorKind::SRV;

	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareHuntingResourceTask(
	ID3D12Device *device, const DX12ResourceSummary &resource, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;
	if (!resource.resource || !resource.hasResourceDesc ||
		(resource.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
		 !DX12ResourceDumpPolicy::IsTextureResourceDimension(resource.resourceDesc.Dimension)))
		return false;

	DX12DescriptorSummary descriptor;
	descriptor.kind = DescriptorKind::SRV;
	descriptor.resource = resource.resource;
	descriptor.resourceDesc = resource.resourceDesc;
	descriptor.hasResourceDesc = resource.hasResourceDesc;
	descriptor.resourceHeapType = resource.resourceHeapType;
	descriptor.hasResourceHeapType = resource.hasResourceHeapType;
	descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
	descriptor.viewSize = resource.size;
	descriptor.currentState = resource.currentState;
	descriptor.hasCurrentState = resource.hasCurrentState;
	descriptor.stateTrackedByBarrier = resource.stateTrackedByBarrier;

	if (!DX12ResourceDumpSafetyUtils::ResourceStateAllowsLiveDiscoveryCopy(resource)) {
		task->sourceKind = DumpTaskSource::Resource;
		task->binding.bindSpace = "hunting_resource";
		task->binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		task->binding.descriptor = descriptor;
		task->binding.hasDescriptor = true;
		task->skipNote = DX12ResourceDumpSafetyUtils::LiveDiscoveryCopySkipReason(resource);
		return false;
	}

	// Created resources may never have had a tracked descriptor. Project them
	// into a neutral SRV summary so persistent textures and buffers are dumped
	// from the live resource inventory, independent of current-frame bindings.
	if (resource.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		return PrepareHuntingMetadataBufferTask(
			device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
	return PrepareHuntingMetadataTextureTask(
		device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareModDescriptorTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!FillDescriptorDumpTask(device, binding, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareModIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !FillIaDumpTask(buffer, task))
		return false;
	if (!RetainTaskSource(task))
		return false;
	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareDescriptorTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12RuntimeState::IsModMode())
		return PrepareModDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12RuntimeState::IsHuntingMode())
		return PrepareHuntingDescriptorTask(device, binding, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

static bool PrepareIaBufferTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (DX12RuntimeState::IsModMode())
		return PrepareModIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	if (DX12RuntimeState::IsHuntingMode())
		return PrepareHuntingIaBufferTask(device, buffer, task,
			frameReadbackBytes, frameReadbackBudgetBytes);
	return false;
}

static void ReleaseTask(DumpTask *task)
{
	if (!task)
		return;
	if (task->keepReadbackAlive)
		return;
	if (task->sourceRetained && task->source) {
		DX12ReleaseRetainedResource(task->source);
		task->sourceRetained = false;
	}
	if (task->readback) {
		task->readback->Release();
		task->readback = nullptr;
	}
}

static void ReleaseTasks(std::vector<DumpTask> *tasks)
{
	if (!tasks)
		return;
	for (DumpTask &task : *tasks)
		ReleaseTask(&task);
}

static size_t CountCopiedTasks(const std::vector<DumpTask> &tasks)
{
	size_t copied = 0;
	for (const DumpTask &task : tasks) {
		if (task.copied)
			copied++;
	}
	return copied;
}



};

class DX12ResourceDumpPrepareGuardUtils
{
public:
static bool TryPrepareHuntingMetadataTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, bool isTexture,
	DumpTask *task, UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes,
	UINT64 descriptorIndex)
{
	if (!task)
		return false;
	bool prepared = false;
	__try {
		prepared = isTexture ?
			DX12ResourceDumpPrepare::PrepareHuntingMetadataTextureTask(
				device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes) :
			DX12ResourceDumpPrepare::PrepareHuntingMetadataBufferTask(
				device, descriptor, task, frameReadbackBytes, frameReadbackBudgetBytes);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12ResourceDumpPrepare::ReleaseTask(task);
		task->ready = false;
		task->copied = false;
		task->readback = nullptr;
		task->sourceRetained = false;
		task->skipNote = isTexture ?
			"metadata_texture_prepare_exception" :
			"metadata_buffer_prepare_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourcePrepareTaskFailed",
			"\"exception\":\"0x%lx\",\"stage\":\"metadata_prepare\",\"descriptorIndex\":%llu,\"kind\":\"%s\",\"resource\":\"%p\",\"dimension\":%u,\"width\":%llu,\"height\":%u,\"format\":%u,\"note\":\"%s\"",
			GetExceptionCode(),
			static_cast<unsigned long long>(descriptorIndex),
			descriptor.kind.c_str(),
			descriptor.resource,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Dimension) : 0,
			descriptor.hasResourceDesc ? static_cast<unsigned long long>(descriptor.resourceDesc.Width) : 0,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Height) : 0,
			descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Format) : 0,
			task->skipNote);
		prepared = false;
	}
	return prepared;
}

static bool TryBuildFileName(const DumpTask &task, wchar_t *fileName, size_t fileNameCount)
{
	if (!fileName || fileNameCount == 0)
		return false;
	fileName[0] = L'\0';
	bool ok = false;
	__try {
		DX12ResourceDumpNaming::BuildFileName(task, fileName, fileNameCount);
		ok = fileName[0] != L'\0';
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDumpFileNameFailed",
			"\"exception\":\"0x%lx\",\"taskSource\":\"%s\",\"source\":\"%p\",\"texture\":%u,\"bytes\":%llu",
			GetExceptionCode(),
			DX12ResourceDumpCore::DumpTaskSourceName(task.sourceKind),
			task.source,
			task.isTexture ? 1 : 0,
			static_cast<unsigned long long>(task.copyBytes));
		ok = false;
	}
	return ok;
}
};




class DX12ResourceDumpGpuCopy
{
public:
class RetiredCopy
{
public:
	ID3D12Fence *fence = nullptr;
	ID3D12Device *device = nullptr;
	HANDLE eventHandle = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12CommandAllocator *allocator = nullptr;
	std::vector<ID3D12Resource*> sources;
	std::vector<ID3D12Resource*> readbacks;
};

inline static SRWLOCK RetiredCopyLock = SRWLOCK_INIT;
inline static std::vector<RetiredCopy> RetiredCopies;
inline static std::atomic<size_t> RetiredCopyCount{0};

static void ReleaseRetiredCopy(RetiredCopy *copy)
{
	if (!copy)
		return;
	if (copy->eventHandle)
		CloseHandle(copy->eventHandle);
	if (copy->fence)
		copy->fence->Release();
	if (copy->device)
		copy->device->Release();
	if (copy->commandList)
		copy->commandList->Release();
	if (copy->allocator)
		copy->allocator->Release();
	for (ID3D12Resource *source : copy->sources) {
		if (source)
			DX12ReleaseRetainedResource(source);
	}
	for (ID3D12Resource *readback : copy->readbacks) {
		if (readback)
			readback->Release();
	}
	copy->sources.clear();
	copy->readbacks.clear();
	*copy = RetiredCopy();
}

static void DrainRetiredCopies()
{
	if (RetiredCopyCount.load(std::memory_order_acquire) == 0)
		return;
	AcquireSRWLockExclusive(&RetiredCopyLock);
	for (size_t i = 0; i < RetiredCopies.size();) {
		const UINT64 completed = RetiredCopies[i].fence ?
			RetiredCopies[i].fence->GetCompletedValue() : 0;
		const bool deviceRemoved = RetiredCopies[i].device &&
			FAILED(RetiredCopies[i].device->GetDeviceRemovedReason());
		if (completed < 1 && completed != UINT64_MAX && !deviceRemoved) {
			++i;
			continue;
		}
		ReleaseRetiredCopy(&RetiredCopies[i]);
		RetiredCopies[i] = RetiredCopies.back();
		RetiredCopies.pop_back();
	}
	RetiredCopyCount.store(RetiredCopies.size(), std::memory_order_release);
	ReleaseSRWLockExclusive(&RetiredCopyLock);
}

static void RetireCopy(
	DumpTask *task,
	ID3D12GraphicsCommandList *commandList,
	ID3D12CommandAllocator *allocator,
	ID3D12Fence *fence,
	HANDLE eventHandle,
	ID3D12Device *device)
{
	RetiredCopy copy;
	copy.fence = fence;
	copy.device = device;
	if (copy.device)
		copy.device->AddRef();
	copy.eventHandle = eventHandle;
	copy.commandList = commandList;
	copy.allocator = allocator;
	if (task) {
		copy.sources.push_back(task->sourceRetained ? task->source : nullptr);
		copy.readbacks.push_back(task->readback);
		task->source = nullptr;
		task->sourceRetained = false;
		task->readback = nullptr;
		task->keepReadbackAlive = false;
	}
	AcquireSRWLockExclusive(&RetiredCopyLock);
	RetiredCopies.push_back(copy);
	RetiredCopyCount.store(RetiredCopies.size(), std::memory_order_release);
	ReleaseSRWLockExclusive(&RetiredCopyLock);
}

static void RetireCopyBatch(
	const std::vector<DumpTask*> &tasks,
	ID3D12GraphicsCommandList *commandList,
	ID3D12CommandAllocator *allocator,
	ID3D12Fence *fence,
	HANDLE eventHandle,
	ID3D12Device *device)
{
	RetiredCopy copy;
	copy.fence = fence;
	copy.device = device;
	if (copy.device)
		copy.device->AddRef();
	copy.eventHandle = eventHandle;
	copy.commandList = commandList;
	copy.allocator = allocator;
	copy.sources.reserve(tasks.size());
	copy.readbacks.reserve(tasks.size());
	for (DumpTask *task : tasks) {
		if (!task)
			continue;
		copy.sources.push_back(task->sourceRetained ? task->source : nullptr);
		copy.readbacks.push_back(task->readback);
		task->source = nullptr;
		task->sourceRetained = false;
		task->readback = nullptr;
		task->keepReadbackAlive = false;
	}
	AcquireSRWLockExclusive(&RetiredCopyLock);
	RetiredCopies.push_back(std::move(copy));
	RetiredCopyCount.store(RetiredCopies.size(), std::memory_order_release);
	ReleaseSRWLockExclusive(&RetiredCopyLock);
}

static DWORD CopyFenceTimeoutMs()
{
	return 2000;
}

static const char *WaitResultName(DWORD waitResult)
{
	switch (waitResult) {
	case WAIT_OBJECT_0:
		return "signaled";
	case WAIT_TIMEOUT:
		return "timeout";
	case WAIT_FAILED:
		return "wait_failed";
	default:
		return "unexpected_wait_result";
	}
}

static bool DeviceRemoved(ID3D12Device *device, HRESULT *hrOut = nullptr)
{
	HRESULT hr = device ? device->GetDeviceRemovedReason() : E_POINTER;
	if (hrOut)
		*hrOut = hr;
	return FAILED(hr);
}

static void WriteResourceBarrier(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
	D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
	if (!commandList || !resource || beforeState == afterState)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	commandList->ResourceBarrier(1, &barrier);
}

static bool WaitForFence(
	ID3D12CommandQueue *queue,
	ID3D12Device *device,
	DWORD *waitResultOut = nullptr,
	ID3D12Fence **pendingFenceOut = nullptr,
	HANDLE *pendingEventOut = nullptr)
{
	if (pendingFenceOut)
		*pendingFenceOut = nullptr;
	if (pendingEventOut)
		*pendingEventOut = nullptr;
	ID3D12Fence *fence = nullptr;
	HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr) || !fence)
		return false;

	HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!eventHandle) {
		fence->Release();
		return false;
	}

	DWORD waitResult = WAIT_OBJECT_0;
	DWORD waitError = 0;
	UINT64 completedValue = 0;
	UINT64 completedBeforeSignal = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	LARGE_INTEGER signalBegin = DX12OuterTimingProbeUtils::Now();
	hr = queue->Signal(fence, 1);
	UINT64 completedAfterSignal = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogQueueFenceSlice(
		"Signal",
		"dump_copy_signal",
		queue,
		fence,
		1,
		completedBeforeSignal,
		completedAfterSignal,
		hr,
		DX12OuterTimingProbeUtils::ElapsedUs(signalBegin));
	if (SUCCEEDED(hr) && completedAfterSignal < 1) {
		LARGE_INTEGER setEventBegin = DX12OuterTimingProbeUtils::Now();
		hr = fence->SetEventOnCompletion(1, eventHandle);
		UINT64 completedAfterSetEvent = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
		DX12OuterTimingProbeUtils::LogFenceSlice(
			"SetEventOnCompletion",
			"dump_copy_set_event",
			fence,
			1,
			completedAfterSignal,
			completedAfterSetEvent,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(setEventBegin));
		if (SUCCEEDED(hr)) {
			LARGE_INTEGER waitBegin = DX12OuterTimingProbeUtils::Now();
			waitResult = WaitForSingleObject(eventHandle, CopyFenceTimeoutMs());
			if (waitResult == WAIT_FAILED)
				waitError = GetLastError();
			DX12OuterTimingProbeUtils::LogEventWaitSlice(
				"WaitForSingleObject",
				"dump_copy_wait_event",
				eventHandle,
				waitResult,
				waitError,
				CopyFenceTimeoutMs(),
				DX12OuterTimingProbeUtils::ElapsedUs(waitBegin));
		}
	}
	LARGE_INTEGER completedBegin = DX12OuterTimingProbeUtils::Now();
	completedValue = DX12OuterTimingProbeUtils::FenceCompletedValue(fence);
	DX12OuterTimingProbeUtils::LogFenceSlice(
		"GetCompletedValue",
		"dump_copy_final_completed",
		fence,
		1,
		completedAfterSignal,
		completedValue,
		S_OK,
		DX12OuterTimingProbeUtils::ElapsedUs(completedBegin));

	if (waitResultOut)
		*waitResultOut = waitResult;

	const bool completed = completedValue != UINT64_MAX && completedValue >= 1;
	const bool ok = SUCCEEDED(hr) && waitResult == WAIT_OBJECT_0 && completed;
	if (!ok) {
		HRESULT removedHr = S_OK;
		DeviceRemoved(device, &removedHr);
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyFenceWaitFailed",
			"\"hr\":\"0x%lx\",\"deviceHr\":\"0x%lx\",\"wait\":\"%s\",\"waitResult\":%lu,\"waitError\":%lu,\"completed\":%llu,\"timeoutMs\":%lu",
			hr, removedHr, WaitResultName(waitResult), waitResult, waitError,
			static_cast<unsigned long long>(completedValue),
			CopyFenceTimeoutMs());
	}
	if (ok || !pendingFenceOut || !pendingEventOut) {
		CloseHandle(eventHandle);
		fence->Release();
	} else {
		*pendingFenceOut = fence;
		*pendingEventOut = eventHandle;
	}
	return ok;
}


static bool ExecuteCopyTask(
	ID3D12Device *device, ID3D12CommandQueue *queue, DumpTask *task)
{
	if (!device || !queue || !task || !task->ready || !task->readback)
		return false;
	DrainRetiredCopies();
	task->copied = false;

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	HRESULT hr = S_OK;
	if (DX12ResourceDumpGpuCopy::DeviceRemoved(device, &hr)) {
		task->skipNote = "device_removed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopySkipped",
			"\"reason\":\"device_removed\",\"hr\":\"0x%lx\"", hr);
		return false;
	}

	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	if (SUCCEEDED(hr))
		hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&commandList));
	if (FAILED(hr) || !allocator || !commandList) {
		DX12FrameAnalysis::LogJsonFunc("ResourceCopySetupFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes));
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	__try {
		if (task->needsBarrier)
			DX12ResourceDumpGpuCopy::WriteResourceBarrier(commandList, task->source, task->sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

		if (task->isTexture) {
			for (UINT subresource = 0; subresource < task->textureLayouts.size(); ++subresource) {
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = task->source;
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = subresource;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = task->readback;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = task->textureLayouts[subresource];
				commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
		} else {
			commandList->CopyBufferRegion(task->readback, 0, task->source,
				task->sourceOffset, task->copyBytes);
		}

		if (task->needsBarrier)
			DX12ResourceDumpGpuCopy::WriteResourceBarrier(commandList, task->source, D3D12_RESOURCE_STATE_COPY_SOURCE, task->sourceState);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_record_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_record_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		task->skipNote = "copy_close_failed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_close_failed\"",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}
	ID3D12CommandList *lists[] = { commandList };
	bool ok = false;
	DWORD waitResult = WAIT_FAILED;
	ID3D12Fence *pendingFence = nullptr;
	HANDLE pendingEvent = nullptr;
	__try {
		LARGE_INTEGER executeBegin = DX12OuterTimingProbeUtils::Now();
		queue->ExecuteCommandLists(1, lists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"dump_copy_execute",
			queue,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(executeBegin));
		ok = DX12ResourceDumpGpuCopy::WaitForFence(
			queue, device, &waitResult, &pendingFence, &pendingEvent);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_execute_exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_execute_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		ok = false;
	}

	if (!ok) {
		HRESULT removedHr = S_OK;
		const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
		if (deviceRemoved)
			task->skipNote = "device_removed";
		else if (!task->skipNote && waitResult == WAIT_TIMEOUT)
			task->skipNote = "copy_wait_timeout";
		else if (!task->skipNote)
			task->skipNote = "copy_execute_failed";
		DX12FrameAnalysis::LogJsonFunc("ResourceCopyTaskFailed",
			"\"source\":\"%p\",\"taskSource\":\"%s\",\"bind\":\"%s\",\"texture\":%u,\"width\":%llu,\"height\":%u,\"depthOrArray\":%u,\"mips\":%u,\"format\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"stateKnown\":%u,\"stateTrackedByBarrier\":%u,\"note\":\"%s\",\"deviceHr\":\"0x%lx\",\"wait\":\"%s\"",
			task->source,
			DX12ResourceDumpCore::DumpTaskSourceName(task->sourceKind),
			task->binding.bindSpace.c_str(),
			task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->desc.Width),
			static_cast<UINT>(task->desc.Height),
			static_cast<UINT>(task->desc.DepthOrArraySize),
			static_cast<UINT>(task->desc.MipLevels),
			static_cast<UINT>(task->desc.Format),
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState),
			task->stateKnown ? 1 : 0,
			task->binding.descriptor.stateTrackedByBarrier ? 1 : 0,
			task->skipNote ? task->skipNote : "copy_execute_failed",
			removedHr,
			WaitResultName(waitResult));
		if (deviceRemoved) {
			if (pendingEvent)
				CloseHandle(pendingEvent);
			if (pendingFence)
				pendingFence->Release();
			commandList->Release();
			allocator->Release();
		} else {
			ID3D12Resource *retiredSource = task->source;
			RetireCopy(
				task, commandList, allocator, pendingFence, pendingEvent, device);
			DX12FrameAnalysis::LogJsonFunc("ResourceCopyObjectsRetained",
				"\"reason\":\"gpu_work_not_completed\",\"source\":\"%p\",\"bytes\":%llu",
				retiredSource, static_cast<unsigned long long>(task->copyBytes));
		}
		return false;
	}

	commandList->Release();
	allocator->Release();
	task->copied = true;
	task->skipNote = nullptr;
	return true;
}

static bool TryExecuteCopyBatch(
	ID3D12Device *device,
	ID3D12CommandQueue *queue,
	const std::vector<DumpTask*> &eligibleTasks,
	bool *readbackOkOut,
	bool *batchAbortedOut,
	size_t *copiedOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (batchAbortedOut)
		*batchAbortedOut = false;
	if (copiedOut)
		*copiedOut = 0;
	if (!device || !queue || eligibleTasks.size() <= 1)
		return false;

	DrainRetiredCopies();

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	HRESULT hr = S_OK;
	if (!DX12ResourceDumpGpuCopy::DeviceRemoved(device, &hr)) {
		hr = device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&allocator));
		if (SUCCEEDED(hr)) {
			hr = device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				allocator,
				nullptr,
				IID_PPV_ARGS(&commandList));
		}
	}

	if (FAILED(hr) || !allocator || !commandList) {
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	bool recorded = true;
	for (DumpTask *task : eligibleTasks) {
		if (!task || !task->source || !task->readback) {
			recorded = false;
			break;
		}
		__try {
			if (task->needsBarrier) {
				DX12ResourceDumpGpuCopy::WriteResourceBarrier(
					commandList,
					task->source,
					task->sourceState,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
			}

			if (task->isTexture) {
				for (UINT subresource = 0;
					 subresource < task->textureLayouts.size();
					 ++subresource) {
					D3D12_TEXTURE_COPY_LOCATION src = {};
					src.pResource = task->source;
					src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					src.SubresourceIndex = subresource;
					D3D12_TEXTURE_COPY_LOCATION dst = {};
					dst.pResource = task->readback;
					dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					dst.PlacedFootprint = task->textureLayouts[subresource];
					commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				}
			} else {
				commandList->CopyBufferRegion(
					task->readback,
					0,
					task->source,
					task->sourceOffset,
					task->copyBytes);
			}

			if (task->needsBarrier) {
				DX12ResourceDumpGpuCopy::WriteResourceBarrier(
					commandList,
					task->source,
					D3D12_RESOURCE_STATE_COPY_SOURCE,
					task->sourceState);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recorded = false;
		}
		if (!recorded)
			break;
	}

	if (!recorded) {
		commandList->Release();
		allocator->Release();
		return false;
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		commandList->Release();
		allocator->Release();
		return false;
	}

	ID3D12CommandList *lists[] = { commandList };
	DWORD waitResult = WAIT_FAILED;
	ID3D12Fence *pendingFence = nullptr;
	HANDLE pendingEvent = nullptr;
	bool executed = false;
	__try {
		LARGE_INTEGER executeBegin = DX12OuterTimingProbeUtils::Now();
		queue->ExecuteCommandLists(1, lists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"dump_copy_execute_batch",
			queue,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(executeBegin));
		executed = DX12ResourceDumpGpuCopy::WaitForFence(
			queue,
			device,
			&waitResult,
			&pendingFence,
			&pendingEvent);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		executed = false;
	}

	if (executed) {
		commandList->Release();
		allocator->Release();
		for (DumpTask *task : eligibleTasks) {
			task->copied = true;
			task->skipNote = nullptr;
		}
		if (copiedOut)
			*copiedOut = eligibleTasks.size();
		DX12FrameAnalysis::LogJsonFunc(
			"ResourceCopyResultDetail",
			"\"mode\":\"batched\",\"eligible\":%zu,\"copied\":%zu,\"failed\":0",
			eligibleTasks.size(),
			eligibleTasks.size());
		return true;
	}

	HRESULT removedHr = S_OK;
	const bool deviceRemoved =
		DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
	const bool timedOut = waitResult == WAIT_TIMEOUT;
	const char *failureNote = deviceRemoved ?
		"copy_aborted_device_removed" :
		(timedOut ? "copy_aborted_wait_timeout" : "copy_execute_failed");
	for (DumpTask *task : eligibleTasks) {
		if (!task->skipNote)
			task->skipNote = failureNote;
	}
	if (readbackOkOut)
		*readbackOkOut = false;
	if (batchAbortedOut)
		*batchAbortedOut = deviceRemoved || timedOut;

	if (deviceRemoved) {
		if (pendingEvent)
			CloseHandle(pendingEvent);
		if (pendingFence)
			pendingFence->Release();
		commandList->Release();
		allocator->Release();
	} else {
		RetireCopyBatch(
			eligibleTasks,
			commandList,
			allocator,
			pendingFence,
			pendingEvent,
			device);
		DX12FrameAnalysis::LogJsonFunc(
			"ResourceCopyObjectsRetained",
			"\"reason\":\"gpu_work_not_completed\",\"tasks\":%zu,\"mode\":\"batched\"",
			eligibleTasks.size());
	}

	DX12FrameAnalysis::LogJsonFunc(
		"ResourceCopyResultDetail",
		"\"mode\":\"batched\",\"eligible\":%zu,\"copied\":0,\"failed\":%zu",
		eligibleTasks.size(),
		eligibleTasks.size());
	return true;
}

static size_t ExecuteReadbackCopyTasks(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks,
	bool *readbackOkOut, bool *batchAbortedOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (batchAbortedOut)
		*batchAbortedOut = false;
	if (!tasks)
		return 0;

	std::vector<DumpTask*> eligibleTasks;
	eligibleTasks.reserve(tasks->size());
	for (DumpTask &task : *tasks) {
		if (!task.directMap && task.ready && task.readback) {
			task.copied = false;
			eligibleTasks.push_back(&task);
		}
	}
	if (eligibleTasks.empty())
		return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);

	size_t batchCopied = 0;
	if (TryExecuteCopyBatch(
			device,
			queue,
			eligibleTasks,
			readbackOkOut,
			batchAbortedOut,
			&batchCopied)) {
		return batchCopied;
	}

	size_t eligible = 0;
	size_t copied = 0;
	for (size_t i = 0; i < tasks->size(); ++i) {
		DumpTask &task = (*tasks)[i];
		if (task.directMap || !task.ready || !task.readback)
			continue;
		eligible++;
		if (DX12ResourceDumpGpuCopy::ExecuteCopyTask(device, queue, &task)) {
			copied++;
			continue;
		}

		HRESULT removedHr = S_OK;
		const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
		const bool timedOut = task.skipNote && !strcmp(task.skipNote, "copy_wait_timeout");
		if (deviceRemoved || timedOut) {
			const char *abortNote = deviceRemoved ?
				"copy_aborted_device_removed" : "copy_aborted_wait_timeout";
			for (size_t j = i + 1; j < tasks->size(); ++j) {
				DumpTask &pending = (*tasks)[j];
				if (!pending.directMap && pending.ready && pending.readback)
					pending.skipNote = abortNote;
			}
			if (batchAbortedOut)
				*batchAbortedOut = true;
			DX12FrameAnalysis::LogJsonFunc("ResourceCopyBatchAborted",
				"\"reason\":\"%s\",\"deviceHr\":\"0x%lx\",\"copied\":%zu,\"eligibleSeen\":%zu,\"tasks\":%zu",
				abortNote, removedHr, copied, eligible, tasks->size());
			break;
		}
	}

	if (eligible == 0)
		return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);

	if (readbackOkOut)
		*readbackOkOut = eligible == copied;
	DX12FrameAnalysis::LogJsonFunc("ResourceCopyResultDetail",
		"\"mode\":\"per_resource\",\"eligible\":%zu,\"copied\":%zu,\"failed\":%zu",
		eligible, copied, eligible >= copied ? eligible - copied : 0);
	return DX12ResourceDumpPrepare::CountCopiedTasks(*tasks);
}


};



class DX12ResourceDumpIndexWriter
{
public:
static void WriteIndexRow(
	FILE *index, const char *status, const DumpTask &task, const D3D12_RESOURCE_DESC &desc,
	UINT64 sourceOffset, UINT64 copyBytes, D3D12_RESOURCE_STATES sourceState,
	bool hasCurrentState, const wchar_t *semanticName, const wchar_t *filePath, const char *note)
{
	(void)index;
	(void)status;
	(void)semanticName;
	(void)note;
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12FrameAnalysisManifest::WriteIaBinding(
			task.iaBuffer, desc, sourceOffset, copyBytes, sourceState,
			hasCurrentState, filePath);
		return;
	}

	DX12FrameAnalysisManifest::WriteResourceBinding(
		task.binding, desc, sourceOffset, copyBytes, sourceState,
		hasCurrentState, filePath, status, note);
}


};




class DX12ResourceDump
{
public:
static bool IsDiscoveryDuplicateTask(const DumpTask &task)
{
	if (task.sourceKind == DumpTaskSource::Resource)
		return task.binding.bindSpace == "hunting_resource";
	if (task.sourceKind != DumpTaskSource::Descriptor)
		return false;
	return task.binding.bindSpace == "hunting_metadata" ||
		task.binding.bindSpace == "hunting_resource";
}

static bool IsComputeCbvSrvUavBinding(const DX12FrameResourceBinding &binding)
{
	return binding.bindSpace == "compute_cbv_srv_uav";
}

static bool IsComputeCbvSrvUavBufferBinding(const DX12FrameResourceBinding &binding)
{
	if (!DX12ResourceDump::IsComputeCbvSrvUavBinding(binding))
		return false;
	return binding.hasDescriptor &&
		binding.descriptor.hasResourceDesc &&
		binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
}

static bool IsComputeCbvSrvUavBufferTask(const DumpTask &task)
{
	return task.sourceKind == DumpTaskSource::Descriptor &&
		DX12ResourceDump::IsComputeCbvSrvUavBufferBinding(task.binding);
}

static DX12FrameAnalysisDumpResult DumpCurrentFrameResources(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<DX12FrameResourceBinding> bindings;
	DX12BindingResourceExport::GetCurrentFrameResourceBindings(&bindings);
	std::vector<DX12FrameIaBufferBinding> iaBuffers;
	DX12BindingFrameExport::GetCurrentFrameIaBuffersWithResourceBindings(&iaBuffers, bindings);
	std::vector<DX12FrameOutputBinding> outputBindings;
	DX12BindingResourceExport::GetCurrentFrameOutputBindings(&outputBindings);

	wchar_t resourceDir[MAX_PATH];
	wcsncpy_s(resourceDir, dir, _TRUNCATE);
	if (!DX12ResourceDumpCore::EnsureDirectory(resourceDir)) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"resource_dir_create_failed\",\"error\":%lu",
			GetLastError());
		result.failed = 1;
		result.reason = "resource_dir_create_failed";
		return result;
	}
	wchar_t dedupeDir[MAX_PATH];
	DX12ResourceDumpCore::GetDedupedDirectory(dir, dedupeDir, ARRAYSIZE(dedupeDir));

	const UINT64 frameReadbackBudget = DX12ResourceDumpCore::FrameReadbackBudgetBytes();
	UINT64 frameReadbackBytes = 0;
	const char *dumpMode = DX12RuntimeState::IsModMode() ? "Mod" : "Hunting";
	DX12FrameAnalysis::LogJsonFunc("ResourceDump",
		"\"mode\":\"%s\",\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu,\"unsafeCopy\":%u,\"gpuCopy\":%u,\"stateTracking\":1",
		dumpMode,
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget),
		1,
		1);
	DX12LogManager::Fields("DX12ResourceDump",
		"\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu",
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget));

	ID3D12CommandQueue *queue = DX12CommandQueueState::Acquire();
	ID3D12Device *device = nullptr;
	if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"missing_command_queue\"");
		if (queue)
			queue->Release();
		result.failed = 1;
		result.reason = "missing_command_queue";
		return result;
	}

	std::vector<DumpTask> tasks;
	std::vector<DumpTask> duplicateTasks;
	std::vector<DumpTask> skippedTasks;
	std::unordered_map<std::string, size_t> canonicalTaskByKey;
	UINT duplicates = 0;
	UINT candidateCbv = 0;
	UINT candidateSrv = 0;
	UINT candidateUav = 0;
	UINT candidateTextures = 0;
	UINT candidateRtv = 0;
	UINT candidateDsv = 0;
	UINT metadataTextureCandidates = 0;
	UINT metadataBufferCandidates = 0;
	UINT metadataCbvBufferCandidates = 0;
	UINT metadataSrvBufferCandidates = 0;
	UINT metadataUavBufferCandidates = 0;
	UINT metadataMissingResource = 0;
	UINT metadataMissingResourceDesc = 0;
	UINT metadataUnsupportedKindOrDimension = 0;
	UINT metadataPrepareFailed = 0;
	UINT metadataBufferCopiesQueued = 0;
	UINT metadataBufferCopyBudgetSkipped = 0;
	UINT64 metadataBufferCopyBytesQueued = 0;
	UINT64 metadataTextureCopyBytesQueued = 0;
	UINT metadataCbvBufferCopiesQueued = 0;
	UINT64 metadataCbvBufferCopyBytesQueued = 0;
	UINT metadataSrvUavBufferCopiesQueued = 0;
	UINT64 metadataSrvUavBufferCopyBytesQueued = 0;
	UINT bindingTasksQueued = 0;
	UINT iaTasksQueued = 0;
	UINT outputTasksQueued = 0;
	UINT metadataTextureTasksQueued = 0;
	UINT metadataBufferTasksQueued = 0;
	UINT resourceTasksQueued = 0;
	UINT iaCandidates = 0;
	UINT iaResolvedCandidates = 0;
	UINT iaDuplicateAliases = 0;
	UINT iaKeyBuildFailures = 0;
	UINT untrackedDescriptorSlots = 0;
	UINT untrackedComputeUavSlots = 0;
	UINT computeBindingCandidates = 0;
	UINT computeBufferBindingCandidates = 0;
	UINT computeBindingResolved = 0;
	UINT computeCbvBufferCandidates = 0;
	UINT computeSrvBufferCandidates = 0;
	UINT computeUavBufferCandidates = 0;

	for (const DX12FrameResourceBinding &binding : bindings) {
		if (DX12ResourceDump::IsComputeCbvSrvUavBinding(binding)) {
			computeBindingCandidates++;
			if (binding.hasDescriptor)
				computeBindingResolved++;
			if (DX12ResourceDump::IsComputeCbvSrvUavBufferBinding(binding)) {
				computeBufferBindingCandidates++;
				if (binding.descriptor.kind == DescriptorKind::CBV)
					computeCbvBufferCandidates++;
				else if (binding.descriptor.kind == DescriptorKind::SRV)
					computeSrvBufferCandidates++;
				else if (binding.descriptor.kind == DescriptorKind::UAV)
					computeUavBufferCandidates++;
			}
		}
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			if (!binding.hasDescriptor && !binding.rootDescriptor &&
					(binding.bindSpace == "compute_cbv_srv_uav" ||
					 binding.bindSpace == "graphics_cbv_srv_uav")) {
				DumpTask skipped;
				skipped.sourceKind = DumpTaskSource::Descriptor;
				skipped.binding = binding;
				skipped.skipNote = "untracked_descriptor";
				skippedTasks.push_back(std::move(skipped));
				untrackedDescriptorSlots++;
				if (binding.bindSpace == "compute_cbv_srv_uav" &&
						binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
					untrackedComputeUavSlots++;
			}
			continue;
		}
		const char *bindingKind = DX12ResourceDumpCore::BindingKindName(binding);
		if (!strcmp(bindingKind, DescriptorKind::CBV.data()))
			candidateCbv++;
		else if (!strcmp(bindingKind, DescriptorKind::SRV.data()))
			candidateSrv++;
		else if (!strcmp(bindingKind, DescriptorKind::UAV.data()))
			candidateUav++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;
		if (!binding.hasDescriptor && !binding.rootDescriptor) {
			DumpTask skipped;
			skipped.binding = binding;
			skipped.skipNote = binding.rootDescriptor ? "root_descriptor_summary_only" : "untracked_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}

		char key[128];
		if (binding.rootDescriptor) {
			sprintf_s(key, "root|0x%llx",
				static_cast<unsigned long long>(binding.gpuVirtualAddress));
		} else {
			DX12ResourceDumpNaming::BuildBindingDedupeKeyForCurrentMode(binding, key, ARRAYSIZE(key));
		}
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.binding = binding;
			if (binding.rootDescriptor) {
				duplicate.binding.hasDescriptor = true;
				duplicate.binding.descriptor = tasks[canonicalIt->second].binding.descriptor;
			}
			duplicate.sourceKind = DumpTaskSource::Descriptor;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			bindingTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}
	DX12FrameAnalysis::LogJsonFunc("ResourceDumpComputeBindingsCollected",
		"\"bindings\":%u,\"resolved\":%u,\"buffer_bindings\":%u,\"cbv_buffers\":%u,\"srv_buffers\":%u,\"uav_buffers\":%u,\"queued_so_far\":%u,\"untracked_compute_uav\":%u",
		computeBindingCandidates,
		computeBindingResolved,
		computeBufferBindingCandidates,
		computeCbvBufferCandidates,
		computeSrvBufferCandidates,
		computeUavBufferCandidates,
		bindingTasksQueued,
		untrackedComputeUavSlots);

	for (const DX12FrameIaBufferBinding &buffer : iaBuffers) {
		iaCandidates++;
		DX12BufferResourceSummary iaResource = {};
		const bool iaResolved =
			DX12BindingFrameExport::ResolveIaBufferResourceSummary(buffer, &iaResource);
		if (iaResolved)
			iaResolvedCandidates++;
		char key[128];
		const bool keyBuilt = DX12ResourceDumpNaming::BuildIaBufferDedupeKey(
			buffer, &iaResource, iaResolved, key, ARRAYSIZE(key));
		if (!keyBuilt) {
			iaKeyBuildFailures++;
			DumpTask skipped;
			skipped.sourceKind = DumpTaskSource::InputAssembler;
			skipped.iaBuffer = buffer;
			skipped.skipNote = "ia_dedupe_key_build_failed";
			DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
				"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"note\":\"%s\"",
				static_cast<unsigned long long>(buffer.eventSerial),
				buffer.role.c_str(),
				static_cast<unsigned long long>(buffer.viewGpuVa),
				static_cast<unsigned long long>(buffer.viewSize),
				static_cast<unsigned long long>(buffer.drawGpuVa),
				static_cast<unsigned long long>(buffer.drawSize),
				buffer.format,
				buffer.stride,
				iaResolved ? 1 : 0,
				skipped.skipNote);
			skippedTasks.push_back(std::move(skipped));
			continue;
		}
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.sourceKind = DumpTaskSource::InputAssembler;
			duplicate.iaBuffer = buffer;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			iaDuplicateAliases++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareIaBufferTaskForCurrentMode(device, buffer, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12BindingFrameExport::BuildIaBufferFileName(buffer, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			iaTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::InputAssembler;
			task.iaBuffer = buffer;
			if (task.skipNote == nullptr)
				task.skipNote = buffer.resolved ? "unsupported_or_no_resource_pointer" : "unresolved_gpu_va";
			if (task.skipNote && !strcmp(task.skipNote, "unresolved_gpu_va")) {
				UINT64 nearestBegin = 0;
				UINT64 nearestEnd = 0;
				UINT64 nearestResource = 0;
				const UINT64 diagnoseSize = buffer.viewSize ? buffer.viewSize :
					(buffer.drawSize ? buffer.drawSize : 1);
				const char *reason = DX12DiagnoseBufferResolveByGpuVa(
					buffer.viewGpuVa,
					diagnoseSize,
					&nearestBegin,
					&nearestEnd,
					&nearestResource);
				DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"reason\":\"%s\",\"nearest_begin\":\"0x%llx\",\"nearest_end\":\"0x%llx\",\"nearest_resource\":\"0x%llx\",\"note\":\"%s\"",
					static_cast<unsigned long long>(buffer.eventSerial),
					buffer.role.c_str(),
					static_cast<unsigned long long>(buffer.viewGpuVa),
					static_cast<unsigned long long>(buffer.viewSize),
					static_cast<unsigned long long>(buffer.drawGpuVa),
					static_cast<unsigned long long>(buffer.drawSize),
					buffer.format,
					buffer.stride,
					iaResolved ? 1 : 0,
					reason ? reason : "",
					static_cast<unsigned long long>(nearestBegin),
					static_cast<unsigned long long>(nearestEnd),
					static_cast<unsigned long long>(nearestResource),
					task.skipNote);
			} else {
				DX12FrameAnalysis::LogJsonFunc("IaBindingPrepareSkipped",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"resolved\":%u,\"note\":\"%s\"",
					static_cast<unsigned long long>(buffer.eventSerial),
					buffer.role.c_str(),
					static_cast<unsigned long long>(buffer.viewGpuVa),
					static_cast<unsigned long long>(buffer.viewSize),
					static_cast<unsigned long long>(buffer.drawGpuVa),
					static_cast<unsigned long long>(buffer.drawSize),
					buffer.format,
					buffer.stride,
					iaResolved ? 1 : 0,
					task.skipNote);
			}
			skippedTasks.push_back(std::move(task));
		}
	}
	DX12FrameAnalysis::LogJsonFunc("IaBindingsCollected",
		"\"candidates\":%u,\"resolved\":%u,\"queued\":%u,\"duplicate_aliases\":%u,\"key_failures\":%u,\"skipped_so_far\":%zu",
		iaCandidates,
		iaResolvedCandidates,
		iaTasksQueued,
		iaDuplicateAliases,
		iaKeyBuildFailures,
		skippedTasks.size());

	for (const DX12FrameOutputBinding &output : outputBindings) {
		DX12FrameResourceBinding binding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(output);
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			DumpTask skipped;
			skipped.sourceKind = DumpTaskSource::Output;
			skipped.outputBinding = output;
			skipped.binding = binding;
			skipped.skipNote = output.hasDescriptor ?
				"unsupported_or_no_resource_pointer" : "untracked_output_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}
		if (binding.descriptor.kind == DescriptorKind::RTV)
			candidateRtv++;
		else if (binding.descriptor.kind == DescriptorKind::DSV)
			candidateDsv++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;

		char key[160];
		DX12ResourceDumpNaming::BuildOutputDedupeKeyForCurrentMode(output, binding, key, ARRAYSIZE(key));
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.readback = nullptr;
			duplicate.sourceRetained = false;
			duplicate.keepReadbackAlive = false;
			duplicate.sourceKind = DumpTaskSource::Output;
			duplicate.outputBinding = output;
			duplicate.binding = binding;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			outputTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			task.binding = binding;
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}

	DX12FrameAnalysis::LogJsonFunc("ResourceDumpDiscoverySkipped",
		"\"reason\":\"single_frame_event_scope\",\"metadataDescriptors\":0,\"trackedResources\":0");
	DX12FrameAnalysis::LogJsonFunc("ResourceDumpTasksQueued",
		"\"tasks\":%zu,\"binding\":%u,\"ia\":%u,\"output\":%u,\"metadataTexture\":%u,\"metadataBuffer\":%u,\"metadataTextureBytes\":%llu,\"metadataBufferBytes\":%llu,\"metadataCbvBuffer\":%u,\"metadataCbvBufferBytes\":%llu,\"metadataSrvUavBuffer\":%u,\"metadataSrvUavBufferBytes\":%llu,\"resource\":%u,\"duplicates\":%u,\"skippedSoFar\":%zu",
		tasks.size(), bindingTasksQueued, iaTasksQueued, outputTasksQueued,
		metadataTextureTasksQueued, metadataBufferTasksQueued,
		static_cast<unsigned long long>(metadataTextureCopyBytesQueued),
		static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
		metadataCbvBufferCopiesQueued,
		static_cast<unsigned long long>(metadataCbvBufferCopyBytesQueued),
		metadataSrvUavBufferCopiesQueued,
		static_cast<unsigned long long>(metadataSrvUavBufferCopyBytesQueued),
		resourceTasksQueued, duplicates, skippedTasks.size());

	UINT dumpedTextures = 0;
	UINT dumpedBuffers = 0;
	UINT failed = 0;
	UINT linked = 0;
	UINT skipped = 0;
	UINT skippedDiscoveryDuplicates = 0;
	UINT skippedAbortedDuplicateAliases = 0;
	UINT copiedTasks = 0;
	UINT copyFailedTasks = 0;
	UINT directMapTasks = 0;
	UINT computeBindingFilesDumped = 0;
	UINT computeBindingFilesLinked = 0;
	UINT computeBindingFilesSkipped = 0;
	UINT computeBindingFilesFailed = 0;
	bool readbackCopyOk = true;
	bool copyBatchAborted = false;
	std::unordered_map<std::string, std::wstring> dedupePathByKey;
	std::unordered_map<std::string, const char*> dedupeFailureReasonByKey;
	std::unordered_set<std::wstring> loggedFileDumps;
	copiedTasks = static_cast<UINT>(DX12ResourceDumpGpuCopy::ExecuteReadbackCopyTasks(
		device,
		queue,
		&tasks,
		&readbackCopyOk,
		&copyBatchAborted));
	for (DumpTask &task : tasks) {
		bool ok = task.directMap || task.copied;
		if (task.directMap)
			directMapTasks++;
		else if (task.ready && task.readback && !task.copied)
			copyFailedTasks++;
		if (!ok && !task.skipNote) {
			task.skipNote = copyBatchAborted ? "copy_aborted" : "copy_execute_failed";
		}
		std::wstring dedupePath;
		if (ok) {
			ok = DX12ResourceDumpFileWriter::MapAndWriteTask(dedupeDir, resourceDir, &task, &dedupePath);
			if (ok && !task.dedupeKey.empty())
				dedupePathByKey[task.dedupeKey] = dedupePath;
		}

		if (ok && task.isTexture)
			dumpedTextures++;
		else if (ok)
			dumpedBuffers++;
		else
			failed++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task)) {
			if (ok)
				computeBindingFilesDumped++;
			else
				computeBindingFilesFailed++;
		}

		if (!ok) {
			if (!task.dedupeKey.empty())
				dedupeFailureReasonByKey[task.dedupeKey] =
					task.skipNote ? task.skipNote : "resource_dump_failed";
			continue;
		}

		if (loggedFileDumps.insert(dedupePath).second) {
			DX12FrameAnalysisManifest::WriteFileDump(
				dedupePath.c_str(), task.isTexture, task.copyBytes, "dumped", "");
		}
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath.c_str(), "");
	}

	DX12FrameAnalysis::LogJsonFunc("ResourceCopyResult",
		"\"gpuCopy\":%u,\"readbackOk\":%u,\"copied\":%u,\"copyFailed\":%u,\"directMap\":%u,\"tasks\":%zu,\"duplicates\":%u,\"streamedWrite\":1",
		1,
		readbackCopyOk ? 1 : 0, copiedTasks, copyFailedTasks, directMapTasks,
		tasks.size(), duplicates);

	for (DumpTask &task : duplicateTasks) {
		if (DX12ResourceDump::IsDiscoveryDuplicateTask(task)) {
			skipped++;
			skippedDiscoveryDuplicates++;
			continue;
		}

		auto dedupeIt = dedupePathByKey.find(task.dedupeKey);
		if (dedupeIt == dedupePathByKey.end()) {
			if (copyBatchAborted) {
				skipped++;
				skippedAbortedDuplicateAliases++;
				if (task.sourceKind == DumpTaskSource::InputAssembler) {
					DX12FrameAnalysis::LogJsonFunc("IaBindingAliasSkipped",
						"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"dedupe_key\":\"%s\",\"note\":\"copy_batch_aborted\"",
						static_cast<unsigned long long>(task.iaBuffer.eventSerial),
						task.iaBuffer.role.c_str(),
						static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
						static_cast<unsigned long long>(task.iaBuffer.viewSize),
						static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
						static_cast<unsigned long long>(task.iaBuffer.drawSize),
						task.dedupeKey.c_str());
				}
				continue;
			}
			if (task.sourceKind == DumpTaskSource::InputAssembler) {
				DX12FrameAnalysis::LogJsonFunc("IaBindingAliasMissingCanonicalDump",
					"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"dedupe_key\":\"%s\",\"note\":\"canonical_dedupe_path_missing\"",
					static_cast<unsigned long long>(task.iaBuffer.eventSerial),
					task.iaBuffer.role.c_str(),
					static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
					static_cast<unsigned long long>(task.iaBuffer.viewSize),
					static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
					static_cast<unsigned long long>(task.iaBuffer.drawSize),
					task.dedupeKey.c_str());
			}
			failed++;
			continue;
		}

		linked++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task))
			computeBindingFilesLinked++;
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second.c_str(), "already_dumped");
	}

	for (const DumpTask &task : skippedTasks) {
		skipped++;
		if (DX12ResourceDump::IsComputeCbvSrvUavBufferTask(task))
			computeBindingFilesSkipped++;
		if (task.sourceKind == DumpTaskSource::Descriptor) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::InputAssembler) {
			DX12FrameAnalysis::LogJsonFunc("IaBindingSkipped",
				"\"call_index\":%llu,\"role\":\"%s\",\"view_gpu\":\"0x%llx\",\"view_bytes\":%llu,\"draw_gpu\":\"0x%llx\",\"draw_bytes\":%llu,\"fmt\":%u,\"stride\":%u,\"note\":\"%s\"",
				static_cast<unsigned long long>(task.iaBuffer.eventSerial),
				task.iaBuffer.role.c_str(),
				static_cast<unsigned long long>(task.iaBuffer.viewGpuVa),
				static_cast<unsigned long long>(task.iaBuffer.viewSize),
				static_cast<unsigned long long>(task.iaBuffer.drawGpuVa),
				static_cast<unsigned long long>(task.iaBuffer.drawSize),
				task.iaBuffer.format,
				task.iaBuffer.stride,
				task.skipNote ? task.skipNote : "skipped");
			continue;
		} else if (task.sourceKind == DumpTaskSource::Output) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Resource) {
			const UINT64 skippedOffset = task.copyBytes ? task.sourceOffset : task.binding.descriptor.resourceOffset;
			const UINT64 skippedBytes = task.copyBytes ? task.copyBytes : task.binding.descriptor.viewSize;
			const D3D12_RESOURCE_STATES skippedState = task.stateKnown ?
				task.sourceState :
				(task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding));
			const bool skippedStateKnown = task.stateKnown || task.binding.descriptor.hasCurrentState;
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				skippedOffset, skippedBytes, skippedState, skippedStateKnown, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
		}
	}

	DX12ResourceDumpPrepare::ReleaseTasks(&tasks);
	DX12ResourceDumpPrepare::ReleaseTasks(&skippedTasks);
	device->Release();
	queue->Release();

	DX12FrameAnalysis::LogJsonFunc("ResourceDumpComputeBindingFiles",
		"\"buffer_bindings\":%u,\"dumped\":%u,\"linked\":%u,\"skipped\":%u,\"failed\":%u,\"file_rows\":%u",
		computeBufferBindingCandidates,
		computeBindingFilesDumped,
		computeBindingFilesLinked,
		computeBindingFilesSkipped,
		computeBindingFilesFailed,
		computeBindingFilesDumped + computeBindingFilesLinked);
	DX12FrameAnalysis::LogJsonFunc("ResourceSummary",
		"\"textures\":%u,\"buffers\":%u,\"linked\":%u,\"skipped\":%u,\"duplicates\":%u,\"failed\":%u,\"readbackBytes\":%llu,\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"candidate_cbv\":%u,\"candidate_srv\":%u,\"candidate_uav\":%u,\"candidate_rtv\":%u,\"candidate_dsv\":%u,\"candidate_textures\":%u,\"compute_bindings\":%u,\"compute_buffer_bindings\":%u,\"compute_cbv_buffer_bindings\":%u,\"compute_srv_buffer_bindings\":%u,\"compute_uav_buffer_bindings\":%u,\"compute_binding_file_rows\":%u,\"metadata_texture_candidates\":%u,\"metadata_buffer_candidates\":%u,\"metadata_cbv_buffer_candidates\":%u,\"metadata_srv_buffer_candidates\":%u,\"metadata_uav_buffer_candidates\":%u,\"metadata_prepare_failed\":%u,\"metadata_buffer_copies_queued\":%u,\"metadata_buffer_copy_bytes_queued\":%llu,\"metadata_buffer_copy_budget_skipped\":%u,\"metadata_missing_resource\":%u,\"metadata_missing_resource_desc\":%u,\"metadata_unsupported_kind_or_dimension\":%u,\"untracked_descriptor_slots\":%u,\"untracked_compute_uav_slots\":%u,\"skipped_discovery_duplicate_aliases\":%u,\"skipped_aborted_duplicate_aliases\":%u",
		dumpedTextures, dumpedBuffers, linked, skipped, duplicates, failed,
		static_cast<unsigned long long>(frameReadbackBytes),
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		candidateCbv, candidateSrv, candidateUav, candidateRtv, candidateDsv,
		candidateTextures,
		computeBindingCandidates, computeBufferBindingCandidates,
		computeCbvBufferCandidates, computeSrvBufferCandidates,
		computeUavBufferCandidates, computeBindingFilesDumped + computeBindingFilesLinked,
		metadataTextureCandidates, metadataBufferCandidates,
		metadataCbvBufferCandidates, metadataSrvBufferCandidates,
		metadataUavBufferCandidates, metadataPrepareFailed,
		metadataBufferCopiesQueued,
		static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
		metadataBufferCopyBudgetSkipped,
		metadataMissingResource, metadataMissingResourceDesc,
		metadataUnsupportedKindOrDimension,
		untrackedDescriptorSlots, untrackedComputeUavSlots,
		skippedDiscoveryDuplicates, skippedAbortedDuplicateAliases);
	result.ok = failed == 0;
	result.written = static_cast<size_t>(dumpedTextures) + dumpedBuffers + linked;
	result.skipped = skipped;
	result.failed = failed;
	result.reason = failed == 0 ? "" : "resource_failures";
	return result;
}





};



// ===================== Runtime/DX12ResourceRuntime.hpp =====================


// Deferred initial mod load (startup hang workaround). Declared here so every
// TU that compiles DX12ResourceRuntime.hpp sees them regardless of include
// order; the (extern) definitions live in DX12ModState.inl /
// DX12ModConfiguration.inl, which are only included by DX12ModRuntime.cpp.
extern DWORD gDeferredInitialModLoadDelayMs;
extern void DX12ModScheduleInitialLoad(const wchar_t *configPath);
extern void DX12ModMaybeRunDeferredInitialLoad();

class DX12ResourceHookSlotTypes
{
public:
	class ID3D12Resource
	{
	public:
		// IUnknown: slots 0-2
		// ID3D12Object: slots 3-6
		// ID3D12DeviceChild: slot 7 (GetDevice)
		// ID3D12Pageable: no methods
		// ID3D12Resource methods start at slot 8
		static constexpr UINT Map = 8;
		static constexpr UINT Unmap = 9;
	};
};


class DX12ResourceTrackerHookCallbacks
{
public:
	static HRESULT STDMETHODCALLTYPE HookedCreateDescriptorHeap(
		ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap);
	static HRESULT STDMETHODCALLTYPE HookedCreateRootSignature(
		ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
		REFIID riid, void **rootSignature);
	static void STDMETHODCALLTYPE HookedCreateConstantBufferView(
		ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateShaderResourceView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateUnorderedAccessView(
		ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateRenderTargetView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateDepthStencilView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateSampler(
		ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCopyDescriptors(
		ID3D12Device *device, UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
		ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
		ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
		ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource(
		ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateFence(
		ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
		REFIID riid, void **fence);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource1(
		ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource1(
		ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource2(
		ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource1(
		ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource3(
		ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource2(
		ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
		const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource2(
		ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
};



class DX12ResourceTrackerHookDiagnostics
{
public:
	static void LogResourceHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogResourceHookCallWhenTracking(const char *api, const void *object)
	{
		if (DX12HotPathState::skipAll &&
				!DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
			return;
		LogResourceHookCall(api, object);
	}
};



class DX12ResourceTrackerHookRegistry
{
public:
	template <typename T>
	static T GetDeviceOriginal(void *device, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::Resolve(device, slot, fallback, name);
	}
};



class DX12ResourceTrackerHookRuntime
{
public:
	static const D3D12_UNORDERED_ACCESS_VIEW_DESC *PrepareUavDesc(
		ID3D12Resource *resource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC *PrepareResourceDesc(
		const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC1 *PrepareResourceDesc1(
		const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_DESC1 *storage,
		const char *source);
	static bool ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc);
	static void RecordCreatedDescriptorHeap(
		HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap);
	static void RecordCreatedRootSignature(
		HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature);
	static void RecordCreatedConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedShaderResourceView(
		ID3D12Device *device,
		ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedUnorderedAccessView(
		ID3D12Device *device,
		ID3D12Resource *resource,
		ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedRenderTargetView(
		ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedDepthStencilView(
		ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedSampler(
		const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCopiedDescriptors(
		ID3D12Device *device,
		UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes,
		UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCopiedDescriptorsSimple(
		ID3D12Device *device,
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCreatedResource(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static PlacedResourceInfo MakePlacedResourceInfo(ID3D12Heap *heap, UINT64 heapOffset);
	static void LogResourceCreateStage(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void LogResourceCreateStage1(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void RecordCreatedResource1(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResourceWithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);
	static void RecordCreatedResource1WithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);

	static void InstallMapUnmapHooks(ID3D12Resource *resource);

	// Wrapper-mode Map/Unmap interception. ResourceWrapper calls these so
	// buffer content capture keeps working without patching the shared
	// ID3D12Resource code (RenderDoc-style, DLSS FG compatible).
	static HRESULT WrappedResourceMap(
		ID3D12Resource *resource, UINT subresource, const D3D12_RANGE *readRange, void **data);
	static void WrappedResourceUnmap(
		ID3D12Resource *resource, UINT subresource,
		const D3D12_RANGE *writtenRange, const void *caller);
};


class DX12ResourceTrackerHookSignatures
{
public:
	using CreateDescriptorHeap = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	using CreateRootSignature = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);
	using CreateConstantBufferView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateShaderResourceView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateUnorderedAccessView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateRenderTargetView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateDepthStencilView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateSampler = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CopyDescriptors = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*,
		UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	using CopyDescriptorsSimple = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
		D3D12_DESCRIPTOR_HEAP_TYPE);
	using CreateCommittedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		REFIID, void**);
	using CreatePlacedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateReservedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateFence = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT64, D3D12_FENCE_FLAGS, REFIID, void**);
	using CreateCommittedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateReservedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateCommittedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreatePlacedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateCommittedResource3 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, UINT32, const DXGI_FORMAT*, REFIID, void**);
	using CreatePlacedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*, UINT32, const DXGI_FORMAT*,
		REFIID, void**);
	using CreateReservedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_RESOURCE_DESC*, D3D12_BARRIER_LAYOUT,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, UINT32,
		const DXGI_FORMAT*, REFIID, void**);
};


class DX12ResourceTrackerHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateDescriptorHeap = 14;
		static constexpr UINT CreateRootSignature = 16;
		static constexpr UINT CreateConstantBufferView = 17;
		static constexpr UINT CreateShaderResourceView = 18;
		static constexpr UINT CreateUnorderedAccessView = 19;
		static constexpr UINT CreateRenderTargetView = 20;
		static constexpr UINT CreateDepthStencilView = 21;
		static constexpr UINT CreateSampler = 22;
		static constexpr UINT CopyDescriptors = 23;
		static constexpr UINT CopyDescriptorsSimple = 24;
		static constexpr UINT CreateCommittedResource = 27;
		static constexpr UINT CreatePlacedResource = 29;
		static constexpr UINT CreateReservedResource = 30;
		static constexpr UINT CreateFence = 36;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommittedResource1 = 53;
		static constexpr UINT CreateReservedResource1 = 55;
	};

	class ID3D12Device8
	{
	public:
		static constexpr UINT CreateCommittedResource2 = 69;
		static constexpr UINT CreatePlacedResource1 = 70;
	};

	class ID3D12Device10
	{
	public:
		static constexpr UINT CreateCommittedResource3 = 76;
		static constexpr UINT CreatePlacedResource2 = 77;
		static constexpr UINT CreateReservedResource2 = 78;
	};
};

class DX12ResourceTrackerHookState
{
public:
	inline static DX12ResourceTrackerHookSignatures::CreateDescriptorHeap OrigCreateDescriptorHeap = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRootSignature OrigCreateRootSignature = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateConstantBufferView OrigCreateConstantBufferView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateShaderResourceView OrigCreateShaderResourceView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateUnorderedAccessView OrigCreateUnorderedAccessView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRenderTargetView OrigCreateRenderTargetView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateDepthStencilView OrigCreateDepthStencilView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateSampler OrigCreateSampler = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptors OrigCopyDescriptors = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptorsSimple OrigCopyDescriptorsSimple = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource OrigCreateCommittedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource OrigCreatePlacedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource OrigCreateReservedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateFence OrigCreateFence = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource1 OrigCreateCommittedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource1 OrigCreateReservedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource2 OrigCreateCommittedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource1 OrigCreatePlacedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource3 OrigCreateCommittedResource3 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource2 OrigCreatePlacedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource2 OrigCreateReservedResource2 = nullptr;

	// Per-resource Map/Unmap originals for buffer content capture
	struct {
		void *Map = nullptr;
		void *Unmap = nullptr;
	} inline static ResourceOriginals;
};


class DXGIHooks
{
public:
	static void Install();
	static void Shutdown();
};

class DX12WaitHooks
{
public:
	static void Install();
	static void Shutdown();
};



namespace Bunny {

class LoaderConfig
{
public:
	std::wstring target;
	std::wstring module;
	std::wstring entryPoint = L"CBTProc";
	std::wstring launch;
	std::wstring workingDirectory;
	bool checkVersion = true;
	bool requireAdmin = false;
	bool waitForTarget = true;
	int hookProc = 5;
	int delay = 0;
};

class RuntimeConfig
{
public:
	bool dx12ProxyMode = true;
	bool dx12SafeMode = false;
	bool enableOverlay = false;
	int dx12ConfigInitializationDelayMs = 2000;
	std::wstring configPath;
};

class MigotoConfig {
public:
	bool Load(const wchar_t *path);

	const IniDocument &Ini() const { return mIni; }
	const LoaderConfig &Loader() const { return mLoader; }
	const RuntimeConfig &Runtime() const { return mRuntime; }

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	void ReadLoaderConfig();
	void ReadRuntimeConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

class MigotoConfigLocator
{
public:
	static std::wstring FindDefaultConfigPath(HINSTANCE module);
};

}


namespace Bunny {

class MigotoConfigReader
{
public:
static void ReadString(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, std::wstring *value)
{
	std::wstring text;
	if (ini.GetString(section, key, &text) && value)
		*value = text;
}

static void ReadBool(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, bool *value)
{
	bool parsed = false;
	if (ini.GetBool(section, key, &parsed) && value)
		*value = parsed;
}

static void ReadInt(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, int *value)
{
	int parsed = 0;
	if (ini.GetInt(section, key, &parsed) && value)
		*value = parsed;
}

};

inline bool MigotoConfig::Load(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();
	mLoader = LoaderConfig();
	mRuntime = RuntimeConfig();
	mRuntime.configPath = mPath;

	if (!mIni.LoadFromFile(mPath.c_str())) {
		mError = mIni.Error();
		return false;
	}

	ReadLoaderConfig();
	ReadRuntimeConfig();
	return true;
}

inline void MigotoConfig::ReadLoaderConfig()
{
	MigotoConfigReader::ReadString(mIni, L"Loader", L"target", &mLoader.target);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"module", &mLoader.module);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

inline void MigotoConfig::ReadRuntimeConfig()
{
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"overlay", &mRuntime.enableOverlay);
	MigotoConfigReader::ReadInt(mIni, L"DX12", L"config_initialization_delay_ms", &mRuntime.dx12ConfigInitializationDelayMs);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_overlay", &mRuntime.enableOverlay);
	MigotoConfigReader::ReadInt(mIni, L"System", L"dx12_config_initialization_delay_ms", &mRuntime.dx12ConfigInitializationDelayMs);
	if (mRuntime.dx12ConfigInitializationDelayMs < 0)
		mRuntime.dx12ConfigInitializationDelayMs = 0;
}

inline std::wstring MigotoConfigLocator::FindDefaultConfigPath(HINSTANCE module)
{
	wchar_t path[MAX_PATH] = {};
	wchar_t cwdPath[MAX_PATH] = {};

	if (GetCurrentDirectoryW(MAX_PATH, cwdPath)) {
		PathAppendW(cwdPath, L"d3dx.ini");
		if (PathFileExistsW(cwdPath))
			return cwdPath;
	}

	if (module && GetModuleFileNameW(module, path, MAX_PATH)) {
		PathRemoveFileSpecW(path);
		PathAppendW(path, L"d3dx.ini");
		if (PathFileExistsW(path))
			return path;
	}

	return L"d3dx.ini";
}

} // namespace Bunny



class DX12Runtime
{
public:
	using LoadRealD3D12Fn = HMODULE (*)();

	inline static bool Initialized = false;
	inline static Bunny::MigotoConfig Config;
	inline static bool ConfigLoaded = false;
	inline static LoadRealD3D12Fn LoadRealD3D12 = nullptr;

	static DWORD WINAPI WorkerThread(void*)
	{
		DX12LogManager::Text("DX12 worker thread started\n");
		if (LoadRealD3D12)
			LoadRealD3D12();

		for (int i = 0; i < 200; ++i) {
			DXGIHooks::Install();
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			// Wait-object instrumentation hooks kernel32 exports process-wide;
			// it exists to attribute stall stacks in TIMING builds and must not
			// ship in normal Release (DX11 has no equivalent global wait hook).
			// Safe to install since Nektra now suspends all threads while
			// patching (DX12HookRegistry ctor), eliminating the torn-patch race
			// that previously corrupted waits (permanent 100ms timeout polling,
			// GPU pegged, flicker).
			// DX12WaitHooks::Install(); // CONTROLLED-EXPERIMENT: disabled
#endif
			Sleep(50);
		}

		return 0;
	}

	static void Initialize(HINSTANCE module, LoadRealD3D12Fn loadRealD3D12)
	{
		if (Initialized)
			return;
		Initialized = true;
		LoadRealD3D12 = loadRealD3D12;

		DX12RuntimeState::SetModule(module);
		DX12LogManager::Open();
		DX12Profiling::Init();
		DX12LogManager::Text(
			"\n*** 3DMigoto DX12 runtime initialized - build descriptor-churn+ia-reuse-v2 ***\n");
		DX12HangWatchdog::Start();

		std::wstring configPath = Bunny::MigotoConfigLocator::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12ModSetSafeMode(Config.Runtime().dx12SafeMode);
			gDeferredInitialModLoadDelayMs =
				static_cast<DWORD>(Config.Runtime().dx12ConfigInitializationDelayMs);
			DX12LogManager::Fields("DX12Config",
				"\"path\":\"%S\",\"safeMode\":%s,\"overlay\":%s,\"configInitializationDelayMs\":%lu,\"overlayStarted\":true",
				Config.Path().c_str(),
				Config.Runtime().dx12SafeMode ? "true" : "false",
				Config.Runtime().enableOverlay ? "true" : "false",
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
			DX12ModScheduleInitialLoad(Config.Path().c_str());
			DX12OverlayStatusState::SetStatus(
				L"DX12 mod load deferred until startup reload");
			DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
				"\"status\":\"scheduled\",\"path\":\"%S\",\"delayMs\":%lu",
				Config.Path().c_str(),
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
		} else {
			DX12LogManager::Fields("DX12Config",
				"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
				configPath.c_str(), Config.Error().c_str());
		}
		DX12HotPathState::Update();

		// Start persistent resource/descriptor tracking queue (wrapper mode)
		DX12ResourceTrackerHooks::Install(nullptr);

		// CreateThread requires a DWORD WINAPI thread entry. Keep the entry
		// as a class method so runtime startup state stays in one module.
		HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		DX12HookRegistry::FinalizeHooks();

		DX12LogManager::EndReleaseStartupLogging();
	}

	static void Shutdown()
	{
		DX12LogManager::Text("*** 3DMigoto DX12 runtime shutting down ***\n");
		DX12NativeOverlayShutdown();
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12WaitHooks::Shutdown();
#endif
		DXGIHooks::Shutdown();
		DX12CommandListHookState::ReleaseKnownCommandQueue();
		DX12HookRegistry::UnhookAll();
		DX12LogManager::Close();
		DX12LogUtils::Close();
		LoadRealD3D12 = nullptr;
		Initialized = false;
	}
};

inline void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = InterlockedExchange(&runtimeMode, value);
	if (old != value) {
		DX12LogManager::Fields("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}



enum class DX12UiActionBits : LONG
{
	HuntToggle = 1 << 0,
	HuntPreviousVS = 1 << 1,
	HuntNextVS = 1 << 2,
	HuntPreviousPS = 1 << 3,
	HuntNextPS = 1 << 4,
	HuntPreviousCS = 1 << 5,
	HuntNextCS = 1 << 6,
	HuntPreviousIB = 1 << 7,
	HuntNextIB = 1 << 8,
	HuntPreviousVB = 1 << 9,
	HuntNextVB = 1 << 10,
	HuntCopy = 1 << 11,
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
	Profiling = 1 << 16,
};

class DX12UiActions
{
public:
	inline static volatile LONG PendingActions = 0;

	static void Request(DX12UiActionBits action)
	{
		InterlockedOr(&PendingActions, static_cast<LONG>(action));
	}

	static void RequestHuntToggle() { Request(DX12UiActionBits::HuntToggle); }
	static void RequestHuntPreviousVS() { Request(DX12UiActionBits::HuntPreviousVS); }
	static void RequestHuntNextVS() { Request(DX12UiActionBits::HuntNextVS); }
	static void RequestHuntPreviousPS() { Request(DX12UiActionBits::HuntPreviousPS); }
	static void RequestHuntNextPS() { Request(DX12UiActionBits::HuntNextPS); }
	static void RequestHuntPreviousCS() { Request(DX12UiActionBits::HuntPreviousCS); }
	static void RequestHuntNextCS() { Request(DX12UiActionBits::HuntNextCS); }
	static void RequestHuntPreviousIB() { Request(DX12UiActionBits::HuntPreviousIB); }
	static void RequestHuntNextIB() { Request(DX12UiActionBits::HuntNextIB); }
	static void RequestHuntPreviousVB() { Request(DX12UiActionBits::HuntPreviousVB); }
	static void RequestHuntNextVB() { Request(DX12UiActionBits::HuntNextVB); }
	static void RequestHuntCopySelectedHash() { Request(DX12UiActionBits::HuntCopy); }
	static void RequestHuntResetSelection() { Request(DX12UiActionBits::HuntReset); }
	static void RequestFrameAnalysis() { Request(DX12UiActionBits::FrameAnalysis); }
	static void RequestShaderDump() { Request(DX12UiActionBits::ShaderDump); }
	// The overlay runs on its own Win32 thread. Reloading changes the live
	// command/descriptor state, so defer it to the render thread's Present
	// boundary just like every other UI action.
	static void RequestModReload() { Request(DX12UiActionBits::ModReload); }
	static void RequestProfilingToggle() { Request(DX12UiActionBits::Profiling); }

	static bool Consume()
	{
		const LONG actions = InterlockedExchange(&PendingActions, 0);
		if (!actions)
			return false;

		const bool huntToggleRequested = HasAction(actions, DX12UiActionBits::HuntToggle);

		if (huntToggleRequested)
			DX12HuntToggle();
		if (!huntToggleRequested && DX12HuntIsEnabled()) {
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVS))
				DX12HuntPreviousVS();
			if (HasAction(actions, DX12UiActionBits::HuntNextVS))
				DX12HuntNextVS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousPS))
				DX12HuntPreviousPS();
			if (HasAction(actions, DX12UiActionBits::HuntNextPS))
				DX12HuntNextPS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousCS))
				DX12HuntPreviousCS();
			if (HasAction(actions, DX12UiActionBits::HuntNextCS))
				DX12HuntNextCS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousIB))
				DX12HuntPreviousIB();
			if (HasAction(actions, DX12UiActionBits::HuntNextIB))
				DX12HuntNextIB();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVB))
				DX12HuntPreviousVB();
			if (HasAction(actions, DX12UiActionBits::HuntNextVB))
				DX12HuntNextVB();
			if (HasAction(actions, DX12UiActionBits::HuntCopy))
				DX12HuntCopySelectedHash();
			if (HasAction(actions, DX12UiActionBits::HuntReset))
				DX12HuntResetSelection();
		}
		if (HasAction(actions, DX12UiActionBits::FrameAnalysis))
			RunFrameAnalysisRequest();
		if (HasAction(actions, DX12UiActionBits::ShaderDump))
			DX12RequestShaderDump();
		if (HasAction(actions, DX12UiActionBits::ModReload))
			DX12Input::RequestModReload();
		// Profiling stays enabled and is no longer toggled by a hotkey.
		return true;
	}

	static bool HasAction(LONG actions, DX12UiActionBits action)
	{
		return (actions & static_cast<LONG>(action)) != 0;
	}

	static void RunFrameAnalysisRequest()
	{
		DX12LogManager::Fields("DX12UiAction", "\"action\":\"FrameAnalysisRequest\"");
		if (!DX12RuntimeState::IsHuntingMode()) {
			DX12LogManager::Fields("FrameAnalysisBeginFailed", "\"reason\":\"requires_hunting_mode\"");
			DX12OverlayStatusState::SetWarning(L"Enable Hunting before Frame Dump");
			return;
		}
		if (DX12FrameAnalysis::Begin()) {
			DX12FrameAnalysis::LogJsonFunc("FrameAnalysisArmed", nullptr);
			DX12FrameAnalysis::RequestCapture();
			DX12OverlayStatusState::SetStatus(L"Frame analysis armed");
		} else {
			DX12LogManager::Fields("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
			DX12OverlayStatusState::SetError(L"Frame analysis failed: cannot create directory");
		}
	}
};



