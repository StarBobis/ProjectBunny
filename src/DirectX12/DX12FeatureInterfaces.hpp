#pragma once

#include "DX12RuntimeInterfaces.hpp"

// Consolidated DirectX 12 interface layer.
// Sections retain their original file names in #line diagnostics.

// ---- DX12NativeOverlay.hpp ----
#line 1 "DX12NativeOverlay.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain);
void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12NativeOverlayShutdown();
bool DX12NativeOverlayIsOverlayWindow(HWND hwnd);

// ---- DX12ResourceTrackerReleaseTrackers.hpp ----
#line 1 "DX12ResourceTrackerReleaseTrackers.hpp"
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

// ---- DX12ResourceTrackerPrivateDataGuids.hpp ----
#line 1 "DX12ResourceTrackerPrivateDataGuids.hpp"
#include <guiddef.h>

class DX12ResourceTrackerPrivateDataGuids
{
public:
	inline static const GUID DescriptorHeapReleaseTracker =
	{0x28c66065, 0x55b7, 0x46df, {0x9e, 0x1e, 0x08, 0x52, 0x8d, 0x31, 0xcb, 0x1a}};
	inline static const GUID ResourceReleaseTracker =
	{0xe1a4bb6a, 0xf6fc, 0x4a80, {0x90, 0xa4, 0xf5, 0xb9, 0x13, 0x32, 0xf1, 0x5c}};
};

// ---- DX12ObjectLifetimeTrackerUtils.hpp ----
#line 1 "DX12ObjectLifetimeTrackerUtils.hpp"
#include <d3d12.h>
#include <new>


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

		DX12LogManager::JsonFunc("DX12ObjectLifetimeTracker",
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

		DX12LogManager::JsonFunc("DX12ObjectLifetimeTracker",
			"kind=descriptor_heap,stage=install,heap=%p,hr=0x%lx",
			heap, hr);
		return hr;
	}
};

// ---- DX12PreSkinBindingCacheUtils.hpp ----
#line 1 "DX12PreSkinBindingCacheUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <stdint.h>
#include <string.h>

#include <vector>
#include <unordered_map>


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

// ---- DX12PreSkinOutputResizeCacheIdentity.hpp ----
#line 1 "DX12PreSkinOutputResizeCacheIdentity.hpp"
#include <cstdint>

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

// ---- DX12PsoDecisionCacheUtils.hpp ----
#line 1 "DX12PsoDecisionCacheUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>


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

// ---- DX12PsoInputLayoutUtils.hpp ----
#line 1 "DX12PsoInputLayoutUtils.hpp"
#include <Windows.h>
#include <dxgiformat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

// ---- DX12ResourceCollectQueue.hpp ----
#line 1 "DX12ResourceCollectQueue.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


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

// ---- DX12ResourceDumpTypes.hpp ----
#line 1 "DX12ResourceDumpTypes.hpp"
#include <d3d12.h>

#include <string>
#include <vector>


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

// ---- DX12ResourceDumpSafetyUtils.hpp ----
#line 1 "DX12ResourceDumpSafetyUtils.hpp"
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>


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

// ---- DX12ResourceDumpCore.hpp ----
#line 1 "DX12ResourceDumpCore.hpp"
#include <Windows.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include "crc32c.h"

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

// ---- DX12ResourceDumpBufferTextUtils.hpp ----
#line 1 "DX12ResourceDumpBufferTextUtils.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>


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

// ---- DX12ResourceDumpDedupedNamingUtils.hpp ----
#line 1 "DX12ResourceDumpDedupedNamingUtils.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>
#include <wchar.h>


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

// ---- DX12ResourceDumpDdsUtils.hpp ----
#line 1 "DX12ResourceDumpDdsUtils.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>


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

// ---- DX12ResourceDumpPolicy.hpp ----
#line 1 "DX12ResourceDumpPolicy.hpp"
#include <d3d12.h>

#include <string>


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

// ---- DX12ResourceDumpNaming.hpp ----
#line 1 "DX12ResourceDumpNaming.hpp"
#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include <string>


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

// ---- DX12TextureHashUtils.hpp ----
#line 1 "DX12TextureHashUtils.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <d3d11.h>

#include <algorithm>
#include <stdint.h>

#include "crc32c.h"

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
		return copyBytes == UINT64_MAX ? 0 : copyBytes;
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

// ---- DX12ResourceDumpFileWriter.hpp ----
#line 1 "DX12ResourceDumpFileWriter.hpp"
#include <Shlwapi.h>
#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include <algorithm>
#include <string>


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
		DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
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
