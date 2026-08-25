// ============================================================
// DX12Resources.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Runtime.h"
#include "Config/DX12ConfigLoader.h"
#include "Resources/DX12ResourceDump.h"

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


// Deferred initial mod load (startup hang workaround). Declared here so every
// TU that compiles DX12ResourceRuntime.hpp sees them regardless of include
// order; the (extern) definitions live in DX12ModRuntime.cpp.
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
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType,
		UINT descriptorIncrement);
	static void RecordCreatedResource(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static PlacedResourceInfo MakePlacedResourceInfo(ID3D12Heap *heap, UINT64 heapOffset);
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			// Wait-object instrumentation hooks kernel32 exports process-wide;
			// it exists to attribute stall stacks in TIMING builds and must not
			// ship in normal Release (DX11 has no equivalent global wait hook).
			// Safe to install since Nektra now suspends all threads while
			// patching (DX12HookRegistry ctor), eliminating the torn-patch race
			// that previously corrupted waits (permanent 100ms timeout polling,
			// GPU pegged, flicker).
			DX12WaitHooks::Install();
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
		DX12HangWatchdog::Stop();
		DX12AsyncReadbackShutdown();
		DX12NativeOverlayShutdown();
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
	const LONG old = runtimeMode.exchange(value);
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
