#pragma once

#include "DX12RuntimeInterfaces.hpp"
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
		if (cache->entries.size() < DX12PreSkinBindingCacheUtils::CacheCapacity) {
			DX12PreSkinBindingCacheEntry entry;
			cache->entries.push_back(entry);
			targetIndex = cache->entries.size() - 1;
			target = &cache->entries[targetIndex];
		} else {
			UINT64 oldestAge = cache->entries[0].age;
			for (size_t index = 1; index < cache->entries.size(); ++index) {
				if (cache->entries[index].age < oldestAge) {
					oldestAge = cache->entries[index].age;
					targetIndex = index;
				}
			}
			target = &cache->entries[targetIndex];
			const auto oldMappings =
				cache->fingerprintIndex.equal_range(target->key.fingerprint);
			for (auto mapping = oldMappings.first;
					mapping != oldMappings.second;) {
				if (mapping->second == targetIndex)
					mapping = cache->fingerprintIndex.erase(mapping);
				else
					++mapping;
			}
		}

		target->valid = true;
		target->age = ++cache->clock;
		target->foundAny = foundAny;
		target->key = key;
		target->uavs = uavs;
		target->srvs = srvs;
		target->cbvs = cbvs;
		target->constants = constants;
		cache->fingerprintIndex.emplace(key.fingerprint, targetIndex);
	}
};


class DX12PreSkinOutputResizeCacheIdentity
{
public:
	std::uintptr_t originalResource = 0;
	std::uint64_t requiredByteWidth = 0;
	std::uint32_t stride = 0;

	bool operator==(const DX12PreSkinOutputResizeCacheIdentity &rhs) const
	{
		return originalResource == rhs.originalResource &&
			requiredByteWidth == rhs.requiredByteWidth &&
			stride == rhs.stride;
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
		return stride < rhs.stride;
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
	static constexpr size_t MaxEntriesPerKind = 4096;
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
		if (DX12PsoDecisionCacheUtils::ReloadGeneration != reloadGeneration) {
			DX12PsoDecisionCacheUtils::ReloadGeneration = reloadGeneration;
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
				DX12PsoDecisionCacheUtils::DispatchEntries.end() &&
			DX12PsoDecisionCacheUtils::DispatchEntries.size() >= MaxEntriesPerKind) {
			DX12PsoDecisionCacheUtils::DispatchEntries.erase(
				DX12PsoDecisionCacheUtils::DispatchEntries.begin());
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
				DX12PsoDecisionCacheUtils::DrawEntries.end() &&
			DX12PsoDecisionCacheUtils::DrawEntries.size() >= MaxEntriesPerKind) {
			DX12PsoDecisionCacheUtils::DrawEntries.erase(
				DX12PsoDecisionCacheUtils::DrawEntries.begin());
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
