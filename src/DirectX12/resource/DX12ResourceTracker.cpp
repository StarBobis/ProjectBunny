#include "DX12ResourceTrackerPrivate.hpp"

#include "DX12ObjectLifetimeTrackerUtils.hpp"

#include "DX12BindingCollectQueue.hpp"
#include "DX12ResourceCollectQueue.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"
#include "../utils/DX12DescriptorHeapShadowUtils.hpp"

uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

SRWLOCK gResourceLock = SRWLOCK_INIT;
SRWLOCK gDescriptorLock = SRWLOCK_INIT;
std::vector<RootSignatureRecord> gRootSignatures;
std::deque<DescriptorHeapRecord> gDescriptorHeaps;
std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
std::vector<PsoRootRecord> gPsoRoots;
std::unordered_map<UINT64, size_t> gPsoRootByIndex;
std::vector<ResourceRecord> gResources;
std::vector<size_t> gFreeResourceSlots;
size_t gLiveResourceCount = 0;
std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
// Reverse ownership for gResourceByPtr. Resource invalidation used to scan
// every live pointer (roughly 20k in the target game) to find the one or two
// aliases for a released resource. Keep those aliases at their owning index
// so release is O(alias count), not O(all live resources).
static std::vector<std::vector<ID3D12Resource*>> gResourceAliasesByIndex;
std::map<UINT64, size_t> gBufferResourceByGpuVa;
std::map<UINT64, size_t> gBufferResourceByGpuVaComplete;
std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
std::atomic<UINT64> gBufferResolveGeneration{1};
std::atomic<UINT64> gDescriptorQuerySerial{1};
bool gCleanupRegistered = false;
UINT64 gResourcesRecordedFromCreate = 0;
UINT64 gDescriptorRecordsSeen = 0;
UINT64 gDescriptorCopyRecordsSeen = 0;
UINT64 gDescriptorHeapRecordsSeen = 0;
UINT64 gRootSignatureRecordsSeen = 0;
UINT64 gPsoRootRecordsSeen = 0;
UINT64 gDescriptorEpoch = 0;
LONG gLastResourceStatsPresent = -1;
static UINT64 gDescriptorCopySpanFastPathCalls = 0;
static UINT64 gDescriptorCopySpanFastPathSlots = 0;
static UINT64 gDescriptorCopySpanFallbackCalls = 0;
static constexpr LONG kResourceStatsLogInterval = 300;
UINT64 gBufferResolveCacheInvalidations = 0;
UINT64 gBufferResolveCacheInvalidationEntries = 0;
UINT64 gBufferResolveTlsHits = 0;
UINT64 gBufferResolveGlobalHits = 0;
UINT64 gBufferResolveBackwardScanHits = 0;
UINT64 gBufferResolveMisses = 0;
UINT64 gBufferResolveFallbackScanHits = 0;
UINT64 gBufferResolveGenerationResets = 0;
UINT64 gBufferResolveRangeInvalidations = 0;
UINT64 gBufferResolveRangeInvalidationEntries = 0;
UINT64 gBufferResolveIndexNoopUpdates = 0;
UINT64 gBufferResolveStateOnlyUpdates = 0;
UINT64 gBufferResolveInvalidationEpoch = 0;
std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic{0};
UINT64 gBufferResolveInvalidationHistoryLost = 0;
DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity] = {};
UINT64 gResourceExclusiveLockSlowCount = 0;
UINT64 gResourceExclusiveLockSlowMaxUs = 0;
UINT64 gDescriptorExclusiveLockSlowCount = 0;
UINT64 gDescriptorExclusiveLockSlowMaxUs = 0;

// Canonical identity cache: avoids per-call COM QueryInterface overhead.
static std::unordered_map<ID3D12Resource*, ID3D12Resource*> gCanonicalCache;
static SRWLOCK gCanonicalCacheLock = SRWLOCK_INIT;

class DX12BufferAddressRangeLogUtils
{
public:
	inline static volatile LONG64 AddedCalls = 0;
	inline static volatile LONG64 AddedBytes = 0;
	inline static volatile LONG LastPresentLogged = -1;
	static constexpr LONG PresentInterval = 300;

	static void Record(ID3D12Resource *resource, const ResourceRecord &record)
	{
		InterlockedIncrement64(&AddedCalls);
		InterlockedAdd64(&AddedBytes, static_cast<LONG64>(record.size));
		const LONG present = DX12RuntimeState::GetPresentCount();
		const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
		const bool shouldLog = last < 0 || present >= last + PresentInterval;
		if (!shouldLog)
			return;
		if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
			return;
		DX12LogManager::JsonFunc("DX12BufferAddressRangeAdded",
			"mode=aggregate,present=%ld,threadId=%lu,resource=%p,gpuVa=0x%llX,size=%llu,realEnd=0x%llX,oobEnd=0x%llX,calls=%lld,bytes=%lld,hasPlacedHeap=%u",
			present,
			GetCurrentThreadId(),
			resource,
			static_cast<unsigned long long>(record.gpuVirtualAddress),
			static_cast<unsigned long long>(record.size),
			static_cast<unsigned long long>(record.gpuVirtualAddressRealEnd),
			static_cast<unsigned long long>(record.gpuVirtualAddressOobEnd),
			static_cast<long long>(InterlockedCompareExchange64(&AddedCalls, 0, 0)),
			static_cast<long long>(InterlockedCompareExchange64(&AddedBytes, 0, 0)),
			record.hasPlacedHeapRange ? 1 : 0);
	}
};

void LogResourceTrackerStatsLocked();

static bool DescriptorHeapSlotOccupied(const DescriptorHeapRecord &heap, UINT slot)
{
	return slot < heap.occupied.size() && heap.occupied[slot] != 0;
}

static bool DescriptorHeapIsShaderVisible(const DescriptorHeapRecord &heap)
{
	return (heap.desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
}

static bool ShouldStoreCreatedDescriptorInHeapSlot(const DescriptorHeapRecord &heap)
{
	return DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() &&
		(DescriptorHeapIsShaderVisible(heap) ||
		 DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory());
}

static DescriptorRecord *DescriptorHeapSlot(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static const DescriptorRecord *DescriptorHeapSlot(const DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return nullptr;
	return &heap->slots[slot];
}

static void ClearDescriptorHeapSlotLocked(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap || !DescriptorHeapSlotOccupied(*heap, slot) || slot >= heap->slots.size())
		return;
	DescriptorRecord &record = heap->slots[slot];
	ReleaseDescriptorRecordResources(&record);
	record = DescriptorRecord();
	heap->occupied[slot] = 0;
	DX12DescriptorSlotArrayUtils::DecrementLiveSlots(heap);
}

static void ClearDescriptorHeapLocked(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	for (UINT slot = 0; slot < heap->occupied.size(); ++slot)
		ClearDescriptorHeapSlotLocked(heap, slot);
	for (std::vector<DescriptorRecord> &history : heap->slotHistory) {
		for (DescriptorRecord &record : history)
			ReleaseDescriptorRecordResources(&record);
	}
	heap->slots.clear();
	heap->occupied.clear();
	DX12DescriptorSlotArrayUtils::ClearSparseRecords(heap);
	heap->slotHistory.clear();
	heap->slotHistoryEpochs.clear();
	DX12DescriptorSlotArrayUtils::SetLiveSlots(heap, 0);
}

void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return;
	record->currentDescriptors.Initialize(record->desc.NumDescriptors);
	record->stateLocks = std::make_unique<DescriptorHeapRecord::StateLocks>();
	DX12DescriptorSlotArrayUtils::SetLiveSlots(record, 0);
	DX12DescriptorHeapStateUtils::ResetVersion(record);
}

bool EnsureDescriptorHeapStorageLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	DescriptorHeapRecord::StateLocks *stateLocks = record->stateLocks.get();
	if (stateLocks)
		stateLocks->LockAllExclusive();
	if (stateLocks && !stateLocks->IsActive()) {
		stateLocks->UnlockAllExclusive();
		return false;
	}
	if (!record->slots.empty()) {
		const bool ready = record->slots.size() == record->desc.NumDescriptors;
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
		return ready;
	}
	// Allocate descriptor slots only after a real tracking need appears. Games
	// can create million-entry heaps during normal Mod mode; keeping only heap
	// boundaries until Mod/Dump needs the slots avoids turning capacity into CPU
	// work and memory pressure.
	record->slots.resize(record->desc.NumDescriptors);
	record->occupied.assign(record->desc.NumDescriptors, 0);
	const size_t sparseCount = record->currentDescriptors.Size();
	record->currentDescriptors.ForEach(
		[record](uint32_t slot, const DescriptorSlotBinding &binding) {
			if (slot >= record->slots.size() || !binding.value)
				return;
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&record->slots[slot], binding.value->value,
				record->cpuStart + static_cast<SIZE_T>(slot) * record->increment);
			record->occupied[slot] = 1;
		});
	DX12DescriptorSlotArrayUtils::ClearSparseRecords(record);
	DX12DescriptorSlotArrayUtils::SetLiveSlots(
		record, static_cast<LONG64>(sparseCount));
	if (stateLocks)
		stateLocks->UnlockAllExclusive();
	return true;
}

static bool EnsureDescriptorHeapHistoryLocked(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return false;
	if (record->slotHistory.empty())
		record->slotHistory.resize(record->desc.NumDescriptors);
	if (record->slotHistoryEpochs.empty())
		record->slotHistoryEpochs.resize(record->desc.NumDescriptors);
	return record->slotHistory.size() == record->desc.NumDescriptors &&
		record->slotHistoryEpochs.size() == record->desc.NumDescriptors;
}

static void AppendDescriptorHistoryLocked(DescriptorHeapRecord *heap, UINT slot, const DescriptorRecord &record)
{
	if (!DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return;
	if (!heap || !EnsureDescriptorHeapHistoryLocked(heap) ||
			slot >= heap->slotHistory.size() || slot >= heap->slotHistoryEpochs.size())
		return;
	// Hunting needs RenderDoc-like event granularity: later descriptor heap
	// reuse must not rewrite what an earlier dispatch saw.
	++gDescriptorEpoch;
	heap->slotHistory[slot].push_back(CloneDescriptorRecord(record));
	heap->slotHistoryEpochs[slot].push_back(gDescriptorEpoch);
}

static void StoreDescriptorAtHeapSlotLocked(
	DescriptorHeapRecord *heap, UINT slot, DescriptorRecord &&record)
{
	if (!heap || !EnsureDescriptorHeapStorageLocked(heap) || slot >= heap->slots.size()) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	const bool wasOccupied = DescriptorHeapSlotOccupied(*heap, slot);
	AppendDescriptorHistoryLocked(heap, slot, record);
	DescriptorRecord slotRecord;
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&slotRecord, record, record.cpuHandle);
	heap->slots[slot] = std::move(slotRecord);
	ReleaseDescriptorRecordResources(&record);
	heap->occupied[slot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(heap);
	DX12DescriptorHeapStateUtils::BumpVersion(heap, slot);
}

static size_t LiveDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		live += static_cast<size_t>(
			DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	return live;
}

static size_t LiveShaderVisibleDescriptorHeapSlotsLocked()
{
	size_t live = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		if (DescriptorHeapIsShaderVisible(heap))
			live += static_cast<size_t>(
				DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	}
	return live;
}

static size_t DescriptorHeapSlotCapacityLocked()
{
	size_t capacity = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		capacity += heap.slots.size();
	return capacity;
}

static size_t DescriptorHeapHistoryEntriesLocked()
{
	size_t entries = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		for (const std::vector<DescriptorRecord> &history : heap.slotHistory)
			entries += history.size();
	}
	return entries;
}

static void ClearCpuDescriptorLedgerLocked()
{
	for (std::unordered_map<SIZE_T, DescriptorRecord>::value_type &entry : gDescriptorByCpuHandle)
		ReleaseDescriptorRecordResources(&entry.second);
	gDescriptorByCpuHandle.clear();
	// gRuntimeDescriptorByCpuHandle removed - heap slots are the single source of truth
}

static DescriptorRecord CloneRuntimeDescriptorRecord(const DescriptorRecord &record)
{
	DescriptorRecord clone = record;
	clone.ownsResource = false;
	clone.ownsCounterResource = false;
	if (clone.resource) {
		if (DX12ResourceSnapshotUtils::TryAddRef(clone.resource))
			clone.ownsResource = true;
		else
			clone.resource = nullptr;
	}
	if (clone.counterResource) {
		if (DX12ResourceSnapshotUtils::TryAddRef(clone.counterResource))
			clone.ownsCounterResource = true;
		else
			clone.counterResource = nullptr;
	}
	return clone;
}

static void StoreRuntimeDescriptorRecordLocked(const DescriptorRecord &record)
{
	// Forward to ShadowUtils for heap-slot storage
	DescriptorRecord temp = CloneDescriptorRecord(record);
	DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(record.cpuHandle, std::move(temp));
}

void StoreCpuDescriptorRecordLocked(DescriptorRecord &&record)
{
	if (!record.cpuHandle)
		return;
	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(record.cpuHandle);
	if (found != gDescriptorByCpuHandle.end())
		ReleaseDescriptorRecordResources(&found->second);
	gDescriptorByCpuHandle[record.cpuHandle] = std::move(record);
}

void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	auto found = gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() || found->second >= gDescriptorHeaps.size())
		return;
	DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	const SIZE_T cpuStart = record.cpuStart;
	gDescriptorHeapByCpuStart.erase(cpuStart);
	gDescriptorHeapByPtr.erase(found);
	DescriptorHeapRecord::StateLocks *stateLocks = record.stateLocks.get();
	if (stateLocks)
		stateLocks->LockAllExclusive();
	if (stateLocks)
		stateLocks->Deactivate();
	// gRuntimeDescriptorByCpuHandle removed - heap slots self-clean via ClearDescriptorHeapLocked
	ClearDescriptorHeapLocked(&record);
	record.heap = nullptr;
	record.desc = D3D12_DESCRIPTOR_HEAP_DESC();
	record.cpuStart = 0;
	record.gpuStart = 0;
	record.increment = 0;
	DX12DescriptorHeapStateUtils::ResetVersion(&record);
	if (stateLocks)
		stateLocks->UnlockAllExclusive();
	BumpDescriptorQuerySerial();
}

void InvalidateResourceLocked(ID3D12Resource *resource)
{
	if (!resource)
		return;
	ID3D12Resource *canonical = CanonicalResource(resource);
	auto found = gResourceByPtr.find(canonical);
	if (found == gResourceByPtr.end())
		found = gResourceByPtr.find(resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size())
		return;

	const size_t index = found->second;
	ResourceRecord &record = gResources[index];
	if (!record.resource)
		return;
	if (record.resource != resource && record.resource != canonical) {
		DX12LogManager::DebugJsonFunc("DX12ResourceInvalidateSkipped",
			"reason=resource_mismatch,resource=%p,canonical=%p,recordResource=%p,index=%zu",
			resource,
			canonical,
			record.resource,
			index);
		return;
	}
	const UINT64 invalidatedGpuVa = record.gpuVirtualAddress;
	ResourceRecord oldRecord = record;
	if (record.gpuVirtualAddress) {
		gBufferResourceByGpuVa.erase(record.gpuVirtualAddress);
		gBufferResourceByGpuVaComplete.erase(record.gpuVirtualAddress);
	}
	if (index < gResourceAliasesByIndex.size()) {
		for (ID3D12Resource *alias : gResourceAliasesByIndex[index]) {
			auto aliasFound = gResourceByPtr.find(alias);
			if (aliasFound != gResourceByPtr.end() &&
				aliasFound->second == index)
				gResourceByPtr.erase(aliasFound);
		}
		gResourceAliasesByIndex[index].clear();
	} else {
		// Compatibility fallback for records created before the reverse index.
		gResourceByPtr.erase(resource);
		if (canonical != resource)
			gResourceByPtr.erase(canonical);
	}
	// Invalidate canonical cache entries for this resource
	AcquireSRWLockExclusive(&gCanonicalCacheLock);
	gCanonicalCache.erase(resource);
	if (canonical != resource)
		gCanonicalCache.erase(canonical);
	ReleaseSRWLockExclusive(&gCanonicalCacheLock);
	DX12InvalidateTextureHash(record.resource);
	if (canonical != record.resource)
		DX12InvalidateTextureHash(canonical);
	record = ResourceRecord();
	gFreeResourceSlots.push_back(index);
	if (gLiveResourceCount > 0)
		gLiveResourceCount--;
	DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
		&oldRecord, nullptr, "resource_invalidate", resource);
	DX12LogManager::DebugJsonFunc("DX12ResourceInvalidated",
		"resource=%p,canonical=%p,index=%zu,gpuVa=0x%llx,liveResources=%zu",
		resource,
		canonical,
		index,
		static_cast<unsigned long long>(invalidatedGpuVa),
		gLiveResourceCount);
}

typedef HRESULT(WINAPI *PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);

static HMODULE gD3D12ForDeserialization = nullptr;
static PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateRootSignatureDeserializer = nullptr;
static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL gCreateVersionedRootSignatureDeserializer = nullptr;
UINT64 Fnv1a64(const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	UINT64 hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

void ReleaseDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->Release();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->Release();
	record->resource = nullptr;
	record->counterResource = nullptr;
	record->ownsResource = false;
	record->ownsCounterResource = false;
}

static void RetainDescriptorRecordResources(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->ownsResource && record->resource)
		record->resource->AddRef();
	if (record->ownsCounterResource && record->counterResource)
		record->counterResource->AddRef();
}

DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record)
{
	DescriptorRecord clone = record;
	RetainDescriptorRecordResources(&clone);
	return clone;
}

void PrepareDescriptorValueId(DescriptorRecord *record)
{
	if (!record)
		return;
	if (record->valueId == 0)
		record->valueId = DX12DescriptorValueHashUtils::HashRecord(*record);
}

D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc)
{
	D3D12_RESOURCE_DESC out = {};
	if (!desc)
		return out;
	out.Dimension = desc->Dimension;
	out.Alignment = desc->Alignment;
	out.Width = desc->Width;
	out.Height = desc->Height;
	out.DepthOrArraySize = desc->DepthOrArraySize;
	out.MipLevels = desc->MipLevels;
	out.Format = desc->Format;
	out.SampleDesc = desc->SampleDesc;
	out.Layout = desc->Layout;
	out.Flags = desc->Flags;
	return out;
}

void SetDescriptorKind(DescriptorRecord *record, UINT kindId)
{
	if (!record)
		return;
	record->kindId = kindId;
	switch (kindId) {
	case DescriptorKind::CBV_ID: record->kind = "CBV"; break;
	case DescriptorKind::SRV_ID: record->kind = "SRV"; break;
	case DescriptorKind::UAV_ID: record->kind = "UAV"; break;
	case DescriptorKind::RTV_ID: record->kind = "RTV"; break;
	case DescriptorKind::DSV_ID: record->kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: record->kind = "Sampler"; break;
	default: record->kind.clear(); break;
	}
}

ID3D12Resource *CanonicalResource(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;

	// Fast path: cache lookup (shared lock)
	AcquireSRWLockShared(&gCanonicalCacheLock);
	std::unordered_map<ID3D12Resource*, ID3D12Resource*>::const_iterator it =
		gCanonicalCache.find(resource);
	if (it != gCanonicalCache.end()) {
		ID3D12Resource *canonical = it->second;
		ReleaseSRWLockShared(&gCanonicalCacheLock);
		return canonical;
	}
	ReleaseSRWLockShared(&gCanonicalCacheLock);

	// Slow path: QI + cache insert (exclusive lock)
	IUnknown *unknown = nullptr;
	if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&unknown))) && unknown) {
		ID3D12Resource *canonical = reinterpret_cast<ID3D12Resource*>(unknown);
		unknown->Release();

		AcquireSRWLockExclusive(&gCanonicalCacheLock);
		gCanonicalCache[resource] = canonical;
		ReleaseSRWLockExclusive(&gCanonicalCacheLock);

		return canonical;
	}
	return resource;
}

static size_t *FindTrackedResourceIndexLocked(ID3D12Resource *resource)
{
	if (!resource)
		return nullptr;

	ID3D12Resource *canonical = CanonicalResource(resource);
	auto found = gResourceByPtr.find(canonical);
	if (found == gResourceByPtr.end())
		found = gResourceByPtr.find(resource);
	if (found == gResourceByPtr.end())
		return nullptr;
	return &found->second;
}

void RecordResource(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	if (!resource)
		return;

	ResourceRecord record;
	record.resource = resource;
	record.desc = desc ? *desc : resource->GetDesc();
	const bool bufferResource = record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
	const bool textureResource = IsTextureResourceDimension(record.desc.Dimension);
	if (!bufferResource && !textureResource)
		return;
	if (heapProperties) {
		record.heapType = heapProperties->Type;
		record.hasHeapType = true;
	}

	if (record.hasHeapType && record.heapType == D3D12_HEAP_TYPE_READBACK)
		return;

	DX12ObjectLifetimeTrackerUtils::InstallResourceReleaseTracker(resource);

	record.currentState = initialState;
	record.hasCurrentState = true;
	if (bufferResource) {
		record.gpuVirtualAddress = resource->GetGPUVirtualAddress();
		record.size = record.desc.Width;
		record.gpuVirtualAddressRealEnd = record.gpuVirtualAddress + record.size;
		record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		if (record.gpuVirtualAddress == 0 && record.size > 0) {
			DX12FrameAnalysis::LogJsonFunc("TrackedBufferResourceZeroGpuVa",
				"\"resource\":\"%p\",\"size\":%llu,\"heapType\":%u,\"hasHeapType\":%u,\"dimension\":%u,\"format\":%u",
				resource,
				static_cast<unsigned long long>(record.size),
				record.hasHeapType ? static_cast<UINT>(record.heapType) : UINT_MAX,
				record.hasHeapType ? 1 : 0,
				static_cast<UINT>(record.desc.Dimension),
				static_cast<UINT>(record.desc.Format));
		}
		if (placedInfo && placedInfo->hasPlacedHeap) {
			record.hasPlacedHeapRange = true;
			record.placedHeapOffset = placedInfo->heapOffset;
			record.placedHeapSize = placedInfo->heapSize;
			if (placedInfo->heapSize > placedInfo->heapOffset)
				record.gpuVirtualAddressOobEnd =
					record.gpuVirtualAddress + (placedInfo->heapSize - placedInfo->heapOffset);
			if (record.gpuVirtualAddressOobEnd < record.gpuVirtualAddressRealEnd)
				record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		}
		DX12BufferAddressRangeLogUtils::Record(resource, record);
		if (record.gpuVirtualAddress >= 0x10a4000000ull &&
			record.gpuVirtualAddress <= 0x10aaffffffull &&
			record.size >= 65536ull) {
			DX12FrameAnalysis::LogJsonFunc("TrackedBufferResourceRecorded",
				"\"resource\":\"%p\",\"gpuVa\":\"0x%llX\",\"size\":%llu,\"realEnd\":\"0x%llX\",\"oobEnd\":\"0x%llX\",\"heapType\":%u,\"hasPlacedHeap\":%u",
				resource,
				static_cast<unsigned long long>(record.gpuVirtualAddress),
				static_cast<unsigned long long>(record.size),
				static_cast<unsigned long long>(record.gpuVirtualAddressRealEnd),
				static_cast<unsigned long long>(record.gpuVirtualAddressOobEnd),
				record.hasHeapType ? static_cast<UINT>(record.heapType) : UINT_MAX,
				record.hasPlacedHeapRange ? 1 : 0);
		}
	}

	ID3D12Resource *canonical = CanonicalResource(resource);
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	ResourceRecord oldRecord = {};
	bool hadOldRecord = false;
	if (found != gResourceByPtr.end()) {
		const size_t index = found->second;
		ResourceRecord &existing = gResources[found->second];
		oldRecord = existing;
		hadOldRecord = true;
		if (existing.gpuVirtualAddress && existing.gpuVirtualAddress != record.gpuVirtualAddress) {
			gBufferResourceByGpuVa.erase(existing.gpuVirtualAddress);
			gBufferResourceByGpuVaComplete.erase(existing.gpuVirtualAddress);
		}
		gResources[found->second] = record;
		auto addAlias = [index](ID3D12Resource *alias) {
			if (!alias)
				return;
			gResourceByPtr[alias] = index;
			if (index >= gResourceAliasesByIndex.size())
				gResourceAliasesByIndex.resize(index + 1);
			auto &aliases = gResourceAliasesByIndex[index];
			if (std::find(aliases.begin(), aliases.end(), alias) == aliases.end())
				aliases.push_back(alias);
		};
		addAlias(resource);
		addAlias(canonical);
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = found->second;
			gBufferResourceByGpuVaComplete[record.gpuVirtualAddress] = found->second;
		}
	} else {
		size_t index = 0;
		if (!gFreeResourceSlots.empty()) {
			index = gFreeResourceSlots.back();
			gFreeResourceSlots.pop_back();
			gResources[index] = record;
		} else {
			index = gResources.size();
			gResources.push_back(record);
		}
		gLiveResourceCount++;
		if (index >= gResourceAliasesByIndex.size())
			gResourceAliasesByIndex.resize(index + 1);
		auto addAlias = [index](ID3D12Resource *alias) {
			if (!alias)
				return;
			gResourceByPtr[alias] = index;
			auto &aliases = gResourceAliasesByIndex[index];
			if (std::find(aliases.begin(), aliases.end(), alias) == aliases.end())
				aliases.push_back(alias);
		};
		addAlias(resource);
		addAlias(canonical);
		if (record.gpuVirtualAddress && record.size) {
			gBufferResourceByGpuVa[record.gpuVirtualAddress] = index;
			gBufferResourceByGpuVaComplete[record.gpuVirtualAddress] = index;
		}
	}
	DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
		hadOldRecord ? &oldRecord : nullptr, &record, "resource_record_update", resource);
	gResourcesRecordedFromCreate++;
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gResourceLock);
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gResourceExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gResourceExclusiveLockSlowMaxUs)
			gResourceExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12ResourceLockSlow",
			"source=RecordResource,elapsedUs=%lld,resource=%p,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			resource,
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void CleanupTrackedResources()
{
	DX12BindingCollectQueue::Stop();
	DX12ResourceCollectQueue::Stop();
	// DX12DescriptorCollectPipeline::Stop() removed - pipeline was dead code
	AcquireSRWLockExclusive(&gResourceLock);
	AcquireSRWLockExclusive(&gDescriptorLock);
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks)
			stateLocks->Deactivate();
		ClearDescriptorHeapLocked(&heap);
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	ClearCpuDescriptorLedgerLocked();
	gRootSignatures.clear();
	gDescriptorHeaps.clear();
	gRootSignatureByPtr.clear();
	gDescriptorHeapByPtr.clear();
	gDescriptorHeapByCpuStart.clear();
	gPsoRoots.clear();
	gPsoRootByIndex.clear();
	gResources.clear();
	gFreeResourceSlots.clear();
	gLiveResourceCount = 0;
	gResourceByPtr.clear();
	gResourceAliasesByIndex.clear();
	gBufferResourceByGpuVa.clear();
	gBufferResourceByGpuVaComplete.clear();
	DX12BufferResolveCacheUtils::InvalidateAllLocked("cleanup");
	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockExclusive(&gResourceLock);

	if (gD3D12ForDeserialization) {
		FreeLibrary(gD3D12ForDeserialization);
		gD3D12ForDeserialization = nullptr;
		gCreateRootSignatureDeserializer = nullptr;
		gCreateVersionedRootSignatureDeserializer = nullptr;
	}
}

static bool EnsureRootSignatureDeserializerFunctions()
{
	if (gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer)
		return true;

	if (!gD3D12ForDeserialization) {
		wchar_t path[MAX_PATH];
		if (GetSystemDirectoryW(path, MAX_PATH)) {
			PathAppendW(path, L"d3d12.dll");
			gD3D12ForDeserialization = LoadLibraryW(path);
		}
	}

	if (!gD3D12ForDeserialization)
		return false;

	gCreateRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateRootSignatureDeserializer"));
	gCreateVersionedRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gD3D12ForDeserialization, "D3D12CreateVersionedRootSignatureDeserializer"));
	return gCreateRootSignatureDeserializer || gCreateVersionedRootSignatureDeserializer;
}

static UINT RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION version)
{
	switch (version) {
	case D3D_ROOT_SIGNATURE_VERSION_1_0:
		return 0x10;
	case D3D_ROOT_SIGNATURE_VERSION_1_1:
		return 0x11;
	default:
		return static_cast<UINT>(version);
	}
}

static void RecordDescriptorRange(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void RecordDescriptorRange1(
	DX12RootParameterSummary *parameter, UINT rangeIndex,
	const D3D12_DESCRIPTOR_RANGE1 &range, UINT *appendOffset)
{
	if (!parameter || !appendOffset)
		return;

	DX12RootDescriptorRangeSummary summary;
	summary.rootParameterIndex = parameter->rootParameterIndex;
	summary.rangeIndex = rangeIndex;
	summary.rangeType = static_cast<UINT>(range.RangeType);
	summary.numDescriptors = range.NumDescriptors;
	summary.baseShaderRegister = range.BaseShaderRegister;
	summary.registerSpace = range.RegisterSpace;
	summary.offsetInDescriptorsFromTableStart = range.OffsetInDescriptorsFromTableStart;
	summary.flags = static_cast<UINT>(range.Flags);
	summary.shaderVisibility = parameter->shaderVisibility;
	if (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		summary.effectiveOffset = *appendOffset;
	else
		summary.effectiveOffset = range.OffsetInDescriptorsFromTableStart;

	if (range.NumDescriptors != UINT_MAX) {
		const UINT64 end = static_cast<UINT64>(summary.effectiveOffset) + range.NumDescriptors;
		*appendOffset = end > UINT_MAX ? UINT_MAX : static_cast<UINT>(end);
	}

	parameter->ranges.push_back(summary);
}

static void ParseRootParameter(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

static void ParseRootParameter1(
	RootSignatureRecord *record, UINT index, const D3D12_ROOT_PARAMETER1 &source)
{
	if (!record)
		return;

	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = index;
	parameter.parameterType = static_cast<UINT>(source.ParameterType);
	parameter.shaderVisibility = static_cast<UINT>(source.ShaderVisibility);
	switch (source.ParameterType) {
	case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
		UINT appendOffset = 0;
		for (UINT i = 0; i < source.DescriptorTable.NumDescriptorRanges; ++i)
			RecordDescriptorRange1(&parameter, i, source.DescriptorTable.pDescriptorRanges[i], &appendOffset);
		break;
	}
	case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
		parameter.shaderRegister = source.Constants.ShaderRegister;
		parameter.registerSpace = source.Constants.RegisterSpace;
		parameter.num32BitValues = source.Constants.Num32BitValues;
		break;
	case D3D12_ROOT_PARAMETER_TYPE_CBV:
	case D3D12_ROOT_PARAMETER_TYPE_SRV:
	case D3D12_ROOT_PARAMETER_TYPE_UAV:
		parameter.shaderRegister = source.Descriptor.ShaderRegister;
		parameter.registerSpace = source.Descriptor.RegisterSpace;
		parameter.rootDescriptorFlags = static_cast<UINT>(source.Descriptor.Flags);
		break;
	default:
		break;
	}
	record->parameters.push_back(std::move(parameter));
}

void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength)
{
	if (!record || !blob || blobLength == 0)
		return;
	if (!EnsureRootSignatureDeserializerFunctions())
		return;

	ID3D12VersionedRootSignatureDeserializer *versioned = nullptr;
	HRESULT hr = gCreateVersionedRootSignatureDeserializer ?
		gCreateVersionedRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&versioned)) :
		E_NOINTERFACE;
	if (SUCCEEDED(hr) && versioned) {
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc = nullptr;
		hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
		if (FAILED(hr))
			hr = versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc);
		if (SUCCEEDED(hr) && desc) {
			record->version = RootSignatureVersionNumber(desc->Version);
			if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
				record->flags = static_cast<UINT>(desc->Desc_1_1.Flags);
				record->staticSamplerCount = desc->Desc_1_1.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_1.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_1.NumParameters; ++i)
					ParseRootParameter1(record, i, desc->Desc_1_1.pParameters[i]);
			} else {
				record->flags = static_cast<UINT>(desc->Desc_1_0.Flags);
				record->staticSamplerCount = desc->Desc_1_0.NumStaticSamplers;
				record->parameters.reserve(desc->Desc_1_0.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_0.NumParameters; ++i)
					ParseRootParameter(record, i, desc->Desc_1_0.pParameters[i]);
			}
			record->parsed = true;
		}
		versioned->Release();
		return;
	}

	ID3D12RootSignatureDeserializer *legacy = nullptr;
	hr = gCreateRootSignatureDeserializer ?
		gCreateRootSignatureDeserializer(blob, blobLength, IID_PPV_ARGS(&legacy)) :
		E_NOINTERFACE;
	if (FAILED(hr) || !legacy)
		return;

	const D3D12_ROOT_SIGNATURE_DESC *desc = legacy->GetRootSignatureDesc();
	if (desc) {
		record->version = RootSignatureVersionNumber(D3D_ROOT_SIGNATURE_VERSION_1_0);
		record->flags = static_cast<UINT>(desc->Flags);
		record->staticSamplerCount = desc->NumStaticSamplers;
		record->parameters.reserve(desc->NumParameters);
		for (UINT i = 0; i < desc->NumParameters; ++i)
			ParseRootParameter(record, i, desc->pParameters[i]);
		record->parsed = true;
	}
	legacy->Release();
}

void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	AcquireSRWLockShared(&gResourceLock);
	size_t *index = FindTrackedResourceIndexLocked(resource);
	if (index && *index < gResources.size()) {
		const ResourceRecord &resourceRecord = gResources[*index];
		record->resourceDesc = resourceRecord.desc;
		record->hasResourceDesc = true;
		if (resourceRecord.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			record->gpuVirtualAddress = resourceRecord.gpuVirtualAddress;
		if (resourceRecord.hasHeapType) {
			record->resourceHeapType = static_cast<UINT>(resourceRecord.heapType);
			record->hasResourceHeapType = true;
		}
		record->currentState = resourceRecord.currentState;
		record->hasCurrentState = resourceRecord.hasCurrentState;
		record->stateTrackedByBarrier = resourceRecord.stateTrackedByBarrier;
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;

	record->resource = resource;
	if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata()) {
		FillResourceInfo(record, resource);
	} else {
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record->resourceDesc = desc;
			record->hasResourceDesc = true;
			record->gpuVirtualAddress = resource->GetGPUVirtualAddress();
		}
	}
}

void CaptureDescriptorResourcePointer(DescriptorRecord *record, ID3D12Resource *resource)
{
	if (!record || !resource)
		return;
	// Descriptor creation already captures the descriptor value itself. Keep
	// only the resource identity on the caller thread and defer resource-desc /
	// state completion to query or dump time.
	record->resource = resource;
}

bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record)
{
	if (!record || gpuVa == 0)
		return false;

	bool resolved = false;
	const UINT64 requestedEnd = gpuVa + size;
	if (requestedEnd < gpuVa)
		return false;

	AcquireSRWLockShared(&gResourceLock);
	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVa);
	if (foundBuffer != gBufferResourceByGpuVa.begin()) {
		--foundBuffer;
		if (foundBuffer->second < gResources.size()) {
			const ResourceRecord &resource = gResources[foundBuffer->second];
			const UINT64 begin = resource.gpuVirtualAddress;
			const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
				resource.gpuVirtualAddressRealEnd : begin + resource.size;
			const UINT64 oobEnd = resource.gpuVirtualAddressOobEnd ?
				resource.gpuVirtualAddressOobEnd : realEnd;
			if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				begin == 0 || resource.size == 0 ||
				gpuVa < begin || gpuVa >= realEnd) {
				ReleaseSRWLockShared(&gResourceLock);
				return false;
			}
			record->resource = resource.resource;
			record->resourceOffset = gpuVa - begin;
			const UINT64 available = realEnd - gpuVa;
			record->viewSize = (available && size > available) ? available : size;
			record->gpuVirtualAddress = gpuVa;
			if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata()) {
				record->resourceDesc = resource.desc;
				record->hasResourceDesc = true;
			}
			if (resource.hasHeapType) {
				record->resourceHeapType = static_cast<UINT>(resource.heapType);
				record->hasResourceHeapType = true;
			}
			record->currentState = resource.currentState;
			record->hasCurrentState = resource.hasCurrentState;
			record->stateTrackedByBarrier = resource.stateTrackedByBarrier;
			resolved = true;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
	return resolved;
}

static UINT DxgiFormatBytesPerElement(DXGI_FORMAT format)
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
	case DXGI_FORMAT_B4G4R4A4_UNORM:
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

static UINT SrvBufferBytesPerElement(const D3D12_SHADER_RESOURCE_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

static UINT UavBufferBytesPerElement(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc)
{
	if (desc.Buffer.StructureByteStride)
		return desc.Buffer.StructureByteStride;
	if (desc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)
		return 4;
	return DxgiFormatBytesPerElement(desc.Format);
}

void FillSrvBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->srv.ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_SRV &buffer = record->srv.Buffer;
	const UINT bytesPerElement = SrvBufferBytesPerElement(record->srv);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void FillUavBufferView(DescriptorRecord *record)
{
	if (!record || !record->hasDesc ||
		record->uav.ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return;

	const D3D12_BUFFER_UAV &buffer = record->uav.Buffer;
	const UINT bytesPerElement = UavBufferBytesPerElement(record->uav);
	record->resourceOffset = buffer.FirstElement * bytesPerElement;
	record->viewSize = static_cast<UINT64>(buffer.NumElements) * bytesPerElement;
	if (record->viewSize == 0 && record->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		record->viewSize = record->resourceDesc.Width > record->resourceOffset ?
			record->resourceDesc.Width - record->resourceOffset : 0;
}

void LogResourceTrackerStatsLocked()
{
	const LONG present = DX12RuntimeState::GetPresentCount();
	const LONG last = InterlockedCompareExchange(&gLastResourceStatsPresent, 0, 0);
	if (present < last + kResourceStatsLogInterval)
		return;
	if (InterlockedCompareExchange(&gLastResourceStatsPresent, present, last) != last)
		return;
	DX12LogManager::DebugJsonFunc("DX12ResourceTrackerStats",
		"present=%ld,roots=%zu,rootRecordsSeen=%llu,"
		"heaps=%zu,heapRecordsSeen=%llu,"
		"descriptorSlots=%zu,shaderVisibleDescriptorSlots=%zu,descriptorSlotCapacity=%zu,descriptorHistoryEntries=%zu,"
		"descriptorHeapRanges=%zu,"
		"descriptorRecordsSeen=%llu,descriptorCopyRecordsSeen=%llu,"
		"descriptorCopySpanFastPathCalls=%llu,descriptorCopySpanFastPathSlots=%llu,descriptorCopySpanFallbackCalls=%llu,"
		"bufferResolveCacheEntries=%zu,bufferResolveGeneration=%llu,"
		"bufferResolveInvalidations=%llu,bufferResolveInvalidationEntries=%llu,"
		"bufferResolveRangeInvalidations=%llu,bufferResolveRangeInvalidationEntries=%llu,"
		"bufferResolveIndexNoopUpdates=%llu,bufferResolveStateOnlyUpdates=%llu,"
		"bufferResolveInvalidationEpoch=%llu,bufferResolveInvalidationHistoryLost=%llu,"
		"bufferResolveTlsHits=%llu,bufferResolveGlobalHits=%llu,bufferResolveBackwardScanHits=%llu,bufferResolveFallbackScanHits=%llu,bufferResolveMisses=%llu,bufferResolveGenerationResets=%llu,"
		"resourceExclusiveLockSlowCount=%llu,resourceExclusiveLockSlowMaxUs=%llu,descriptorExclusiveLockSlowCount=%llu,descriptorExclusiveLockSlowMaxUs=%llu,"
		"resources=%zu,resourceSlots=%zu,freeResourceSlots=%zu,"
		"resourcesFromCreate=%llu,cpuDescriptorLedger=%zu,"
		"psoRoots=%zu,psoRootRecordsSeen=%llu",
		present,
		gRootSignatures.size(),
		static_cast<unsigned long long>(gRootSignatureRecordsSeen),
		gDescriptorHeaps.size(),
		static_cast<unsigned long long>(gDescriptorHeapRecordsSeen),
		LiveDescriptorHeapSlotsLocked(),
		LiveShaderVisibleDescriptorHeapSlotsLocked(),
		DescriptorHeapSlotCapacityLocked(),
		DescriptorHeapHistoryEntriesLocked(),
		gDescriptorHeapByCpuStart.size(),
		static_cast<unsigned long long>(gDescriptorRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopyRecordsSeen),
		static_cast<unsigned long long>(gDescriptorCopySpanFastPathCalls),
		static_cast<unsigned long long>(gDescriptorCopySpanFastPathSlots),
		static_cast<unsigned long long>(gDescriptorCopySpanFallbackCalls),
		gBufferResolveCache.size(),
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(gBufferResolveCacheInvalidations),
		static_cast<unsigned long long>(gBufferResolveCacheInvalidationEntries),
		static_cast<unsigned long long>(gBufferResolveRangeInvalidations),
		static_cast<unsigned long long>(gBufferResolveRangeInvalidationEntries),
		static_cast<unsigned long long>(gBufferResolveIndexNoopUpdates),
		static_cast<unsigned long long>(gBufferResolveStateOnlyUpdates),
		static_cast<unsigned long long>(gBufferResolveInvalidationEpoch),
		static_cast<unsigned long long>(gBufferResolveInvalidationHistoryLost),
		static_cast<unsigned long long>(gBufferResolveTlsHits),
		static_cast<unsigned long long>(gBufferResolveGlobalHits),
		static_cast<unsigned long long>(gBufferResolveBackwardScanHits),
		static_cast<unsigned long long>(gBufferResolveFallbackScanHits),
		static_cast<unsigned long long>(gBufferResolveMisses),
		static_cast<unsigned long long>(gBufferResolveGenerationResets),
		static_cast<unsigned long long>(gResourceExclusiveLockSlowCount),
		static_cast<unsigned long long>(gResourceExclusiveLockSlowMaxUs),
		static_cast<unsigned long long>(gDescriptorExclusiveLockSlowCount),
		static_cast<unsigned long long>(gDescriptorExclusiveLockSlowMaxUs),
		gLiveResourceCount,
		gResources.size(),
		gFreeResourceSlots.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gDescriptorByCpuHandle.size(),
		gPsoRoots.size(),
		static_cast<unsigned long long>(gPsoRootRecordsSeen));
}

static bool DescriptorSlotFromHeapLocked(
	size_t heapIndex, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (heapIndex >= gDescriptorHeaps.size() || !cpuHandle || !location)
		return false;
	DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
	if ((requiredType && heap.desc.Type != *requiredType) ||
		heap.cpuStart == 0 || heap.increment == 0 || heap.desc.NumDescriptors == 0)
		return false;
	const SIZE_T begin = heap.cpuStart;
	const SIZE_T offset = cpuHandle >= begin ? cpuHandle - begin : 0;
	if (cpuHandle < begin || offset % heap.increment != 0)
		return false;
	const SIZE_T slot = offset / heap.increment;
	if (slot >= heap.desc.NumDescriptors || slot > UINT_MAX)
		return false;
	location->heapIndex = heapIndex;
	location->slot = static_cast<UINT>(slot);
	if (slotsAvailable)
		*slotsAvailable = heap.desc.NumDescriptors - location->slot;
	return true;
}

bool ResolveDescriptorSlotForCpuHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	if (!cpuHandle || !location)
		return false;
	struct DescriptorHeapLookupCache
	{
		size_t heapIndex = SIZE_MAX;
	};
	static thread_local DescriptorHeapLookupCache cache;
	if (DescriptorSlotFromHeapLocked(
			cache.heapIndex, cpuHandle, requiredType, location, slotsAvailable))
		return true;
	auto heapByStart = gDescriptorHeapByCpuStart.upper_bound(cpuHandle);
	if (heapByStart == gDescriptorHeapByCpuStart.begin())
		return false;
	--heapByStart;
	if (DescriptorSlotFromHeapLocked(
			heapByStart->second, cpuHandle, requiredType, location, slotsAvailable)) {
		cache.heapIndex = heapByStart->second;
		return true;
	}
	return false;
}

static bool DescriptorSlotForHandleLocked(SIZE_T cpuHandle, DescriptorSlotLocation *location)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, location, nullptr);
}

static bool DescriptorSlotForHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DescriptorSlotLocation *location, UINT *slotsAvailable)
{
	return ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, &type, location, slotsAvailable);
}

static bool FindLatestDescriptorAtSlotLocked(
	const DescriptorSlotLocation &location, DescriptorRecord *record)
{
	if (!record || location.heapIndex >= gDescriptorHeaps.size())
		return false;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	if (!slot)
		return false;
	*record = CloneDescriptorRecord(*slot);
	return true;
}

static UINT64 DescriptorValueIdAtSlotLocked(const DescriptorSlotLocation &location)
{
	if (location.heapIndex >= gDescriptorHeaps.size())
		return 0;
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	const DescriptorRecord *slot = DescriptorHeapSlot(&heap, location.slot);
	return slot ? slot->valueId : 0;
}

bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId)
{
	if (!cpuHandle || valueId == 0)
		return false;

	DescriptorRecord current;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlot(
			cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &current))
		return current.valueId == valueId;

	AcquireSRWLockShared(&gDescriptorLock);
	const auto found = gDescriptorByCpuHandle.find(cpuHandle);
	const bool ledgerMatches = found != gDescriptorByCpuHandle.end() &&
		found->second.valueId == valueId;
	ReleaseSRWLockShared(&gDescriptorLock);
	return ledgerMatches;
}

static void RecordDescriptorAtSlotLocked(
	DescriptorRecord &&record, const DescriptorSlotLocation &location)
{
	if (record.valueId == 0)
		record.valueId = DX12DescriptorValueHashUtils::HashRecord(record);
	DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&heap)) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
	if (existing) {
		if (existing->valueId == record.valueId &&
			existing->cpuHandle == record.cpuHandle) {
			ReleaseDescriptorRecordResources(&record);
			return;
		}
		ClearDescriptorHeapSlotLocked(&heap, location.slot);
	}
	if (location.slot >= heap.slots.size())
		return;
	++gDescriptorCopyRecordsSeen;
	StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
}

static void RecordDescriptorInCacheLocked(DescriptorRecord &&record, bool copied)
{
	if (record.valueId == 0)
		record.valueId = DX12DescriptorValueHashUtils::HashRecord(record);

	DescriptorSlotLocation location = {};
	if (DescriptorSlotForHandleLocked(record.cpuHandle, &location)) {
		DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
		const bool storeInHeapSlot = copied || ShouldStoreCreatedDescriptorInHeapSlot(heap);
		if (!storeInHeapSlot) {
			// Non shader-visible/staging heap descriptors can become CopyDescriptors
			// sources later, so keep them in the CPU ledger as the fallback source.
			StoreCpuDescriptorRecordLocked(std::move(record));
			++gDescriptorRecordsSeen;
			return;
		}
		DescriptorRecord *existing = DescriptorHeapSlot(&heap, location.slot);
		if (existing) {
			if (existing->valueId == record.valueId &&
				existing->cpuHandle == record.cpuHandle) {
				ReleaseDescriptorRecordResources(&record);
				return;
			}
			ClearDescriptorHeapSlotLocked(&heap, location.slot);
		}
		if (copied)
			++gDescriptorCopyRecordsSeen;
		else
			++gDescriptorRecordsSeen;
		if (location.slot >= heap.slots.size())
			return;
		StoreDescriptorAtHeapSlotLocked(&heap, location.slot, std::move(record));
		return;
	}

	// Heap-external CPU descriptors are only discoverable through the ledger.
	StoreCpuDescriptorRecordLocked(std::move(record));
}

void RecordDescriptor(DescriptorRecord &&record)
{
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gDescriptorLock);
	RecordDescriptorInCacheLocked(std::move(record), false);
	BumpDescriptorQuerySerial();
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gDescriptorExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gDescriptorExclusiveLockSlowMaxUs)
			gDescriptorExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12DescriptorLockSlow",
			"source=RecordDescriptor,elapsedUs=%lld,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void RecordDescriptorBatch(std::vector<DescriptorRecord> *records)
{
	if (!records || records->empty())
		return;

	const size_t recordCount = records->size();
	const auto lockBegin = std::chrono::steady_clock::now();
	AcquireSRWLockExclusive(&gDescriptorLock);
	for (DescriptorRecord &queued : *records) {
		DescriptorRecord apply = queued;
		apply.ownsResource = false;
		apply.ownsCounterResource = false;
		RecordDescriptorInCacheLocked(std::move(apply), false);
		ReleaseDescriptorRecordResources(&queued);
	}
	BumpDescriptorQuerySerial();
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
	records->clear();
	const auto lockEnd = std::chrono::steady_clock::now();
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(lockEnd - lockBegin).count();
	if (elapsedUs >= 1000) {
		gDescriptorExclusiveLockSlowCount++;
		if (static_cast<UINT64>(elapsedUs) > gDescriptorExclusiveLockSlowMaxUs)
			gDescriptorExclusiveLockSlowMaxUs = static_cast<UINT64>(elapsedUs);
		DX12LogManager::DebugJsonFunc("DX12DescriptorLockSlow",
			"source=RecordDescriptorBatch,elapsedUs=%lld,records=%zu,present=%ld,threadId=%lu",
			static_cast<long long>(elapsedUs),
			recordCount,
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId());
	}
}

void DX12RecordRootSignatureImmediate(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength)
{
	if (!rootSignature || !blob || blobLength == 0)
		return;

	RootSignatureRecord record;
	record.rootSignature = rootSignature;
	record.hash = Fnv1a64(blob, blobLength);
	record.size = blobLength;
	record.nodeMask = nodeMask;
	ParseRootSignatureBlob(&record, blob, blobLength);

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gRootSignatureRecordsSeen;
	auto found = gRootSignatureByPtr.find(record.rootSignature);
	if (found != gRootSignatureByPtr.end()) {
		gRootSignatures[found->second] = std::move(record);
	}
	else {
		gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
		gRootSignatures.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12PrimeDescriptorHistoryForHunting()
{
	if (!DX12RuntimeState::IsHuntingMode())
		return;

	AcquireSRWLockExclusive(&gDescriptorLock);
	size_t occupied = 0;
	size_t primed = 0;
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		// Hunting history uses the dense compatibility adapter. Materialise the
		// current sparse state once when hunting is actually requested.
		if (!EnsureDescriptorHeapStorageLocked(&heap))
			continue;
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks && !stateLocks->IsActive()) {
			stateLocks->UnlockAllExclusive();
			continue;
		}
		const UINT slotCount = static_cast<UINT>(
			min(heap.slots.size(), heap.occupied.size()));
		for (UINT slot = 0; slot < slotCount; ++slot) {
			if (!DescriptorHeapSlotOccupied(heap, slot))
				continue;
			occupied++;
			if (slot < heap.slotHistory.size() && !heap.slotHistory[slot].empty())
				continue;
			const DescriptorRecord *record = DescriptorHeapSlot(&heap, slot);
			if (!record)
				continue;
			AppendDescriptorHistoryLocked(&heap, slot, *record);
			primed++;
		}
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	DX12LogManager::JsonFunc("DX12DescriptorHistoryPrime",
		"heaps=%zu,occupied=%zu,primed=%zu,epoch=%llu,historyEntries=%zu",
		gDescriptorHeaps.size(),
		occupied,
		primed,
		static_cast<unsigned long long>(gDescriptorEpoch),
		DescriptorHeapHistoryEntriesLocked());
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

UINT64 CurrentDescriptorEpoch()
{
	AcquireSRWLockShared(&gDescriptorLock);
	const UINT64 epoch = gDescriptorEpoch;
	ReleaseSRWLockShared(&gDescriptorLock);
	return epoch;
}

UINT64 CurrentDescriptorQuerySerial()
{
	return gDescriptorQuerySerial.load(std::memory_order_acquire);
}

UINT64 DX12GetDescriptorHeapVersion(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return 0;

	UINT64 version = 0;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
		found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap)
				version = DX12DescriptorHeapStateUtils::ReadVersion(record);
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return version;
}

void BumpDescriptorQuerySerial()
{
	UINT64 current = gDescriptorQuerySerial.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gDescriptorQuerySerial.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}

static bool FindLatestDescriptorLocked(SIZE_T cpuHandle, DescriptorRecord *record)
{
	return DX12DescriptorHeapShadowUtils::FindLatestDescriptor(cpuHandle, record);
}

class DX12DescriptorCopyRangeUtils
{
public:
static bool RangesOverlap(
	const DescriptorSlotLocation &srcLocation,
	const DescriptorSlotLocation &destLocation,
	UINT count)
{
	if (srcLocation.heapIndex != destLocation.heapIndex || count == 0)
		return false;
	const UINT64 srcBegin = srcLocation.slot;
	const UINT64 srcEnd = srcBegin + count;
	const UINT64 destBegin = destLocation.slot;
	const UINT64 destEnd = destBegin + count;
	return srcBegin < destEnd && destBegin < srcEnd;
}

static bool GetSourceRecordLocked(
	const DescriptorHeapRecord &srcHeap,
	const DescriptorSlotLocation &srcLocation,
	SIZE_T srcCpuHandle,
	DescriptorRecord *storage,
	const DescriptorRecord **source)
{
	if (!storage || !source)
		return false;
	*source = DescriptorHeapSlot(&srcHeap, srcLocation.slot);
	if (*source)
		return true;
	if (!FindLatestDescriptorLocked(srcCpuHandle, storage))
		return false;
	*source = storage;
	return true;
}

static void StoreSlotValueLocked(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	SIZE_T destCpuHandle,
	const DescriptorRecord &source,
	bool appendHistory)
{
	if (!destHeap || destSlot >= destHeap->slots.size())
		return;

	DescriptorRecord *existing = DescriptorHeapSlot(destHeap, destSlot);
	if (existing && DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
			*existing, source, destCpuHandle)) {
		++gDescriptorCopyRecordsSeen;
		return;
	}

	if (existing)
		ClearDescriptorHeapSlotLocked(destHeap, destSlot);

	if (appendHistory) {
		DescriptorRecord historyRecord;
		DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
			&historyRecord, source, destCpuHandle);
		AppendDescriptorHistoryLocked(destHeap, destSlot, historyRecord);
	}

	const bool wasOccupied = DescriptorHeapSlotOccupied(*destHeap, destSlot);
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&destHeap->slots[destSlot], source, destCpuHandle);
	destHeap->occupied[destSlot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(destHeap);
	++gDescriptorCopyRecordsSeen;
	DX12DescriptorHeapStateUtils::BumpVersion(destHeap, destSlot);
}

static bool SourceHeapSpanReadyLocked(
	const DescriptorHeapRecord &srcHeap,
	UINT srcSlot,
	UINT count)
{
	if (count == 0)
		return false;
	if (srcSlot >= srcHeap.slots.size() || srcSlot >= srcHeap.occupied.size())
		return false;
	if (count > srcHeap.slots.size() - srcSlot ||
			count > srcHeap.occupied.size() - srcSlot)
		return false;
	for (UINT i = 0; i < count; ++i) {
		if (!DescriptorHeapSlotOccupied(srcHeap, srcSlot + i))
			return false;
	}
	return true;
}

static void CopySlotSpanValueLocked(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	SIZE_T destCpuHandle,
	const DescriptorRecord &source)
{
	if (!destHeap || destSlot >= destHeap->slots.size() ||
			destSlot >= destHeap->occupied.size())
		return;

	const bool wasOccupied = DescriptorHeapSlotOccupied(*destHeap, destSlot);
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&destHeap->slots[destSlot], source, destCpuHandle);
	destHeap->occupied[destSlot] = 1;
	if (!wasOccupied)
		DX12DescriptorSlotArrayUtils::IncrementLiveSlots(destHeap);
	DX12DescriptorHeapStateUtils::BumpVersion(destHeap, destSlot);
}

static bool TryCopyHeapSlotSpanLocked(
	const DescriptorHeapRecord &srcHeap,
	DescriptorHeapRecord *destHeap,
	UINT srcSlot,
	UINT destSlot,
	SIZE_T destHandle,
	UINT count,
	UINT increment)
{
	if (!destHeap || count == 0 || increment == 0)
		return false;
	if (DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory())
		return false;
	if (destSlot >= destHeap->slots.size() ||
			destSlot >= destHeap->occupied.size() ||
			count > destHeap->slots.size() - destSlot ||
			count > destHeap->occupied.size() - destSlot)
		return false;
	if (!SourceHeapSpanReadyLocked(srcHeap, srcSlot, count))
		return false;

	const bool sameHeap = &srcHeap == destHeap;
	const UINT64 srcBegin = srcSlot;
	const UINT64 srcEnd = srcBegin + count;
	const UINT64 destBegin = destSlot;
	const UINT64 destEnd = destBegin + count;
	const bool overlap = sameHeap && srcBegin < destEnd && destBegin < srcEnd;
	std::vector<DescriptorRecord> sourceSnapshot;
	if (overlap) {
		sourceSnapshot.resize(count);
		for (UINT i = 0; i < count; ++i) {
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&sourceSnapshot[i], srcHeap.slots[srcSlot + i],
				srcHeap.slots[srcSlot + i].cpuHandle);
		}
	}

	for (UINT i = 0; i < count; ++i) {
		const DescriptorRecord &source = sourceSnapshot.empty() ?
			srcHeap.slots[srcSlot + i] : sourceSnapshot[i];
		const SIZE_T currentDestHandle =
			destHandle + static_cast<SIZE_T>(i) * increment;
		CopySlotSpanValueLocked(destHeap, destSlot + i, currentDestHandle, source);
	}

	gDescriptorCopyRecordsSeen += count;
	gDescriptorCopySpanFastPathCalls++;
	gDescriptorCopySpanFastPathSlots += count;
	return true;
}

static bool CopyHeapRangeLocked(
	const DescriptorSlotLocation &srcLocation,
	const DescriptorSlotLocation &destLocation,
	SIZE_T srcHandle,
	SIZE_T destHandle,
	UINT count,
	UINT increment)
{
	if (count == 0 || increment == 0 ||
			srcLocation.heapIndex >= gDescriptorHeaps.size() ||
			destLocation.heapIndex >= gDescriptorHeaps.size())
		return false;

	const DescriptorHeapRecord &srcHeap = gDescriptorHeaps[srcLocation.heapIndex];
	DescriptorHeapRecord &destHeap = gDescriptorHeaps[destLocation.heapIndex];
	if (!EnsureDescriptorHeapStorageLocked(&destHeap) ||
			destLocation.slot >= destHeap.slots.size())
		return false;

	const size_t destSlotsAvailable = destHeap.slots.size() - destLocation.slot;
	const UINT maxDestCount = destSlotsAvailable < count ?
		static_cast<UINT>(destSlotsAvailable) : count;
	const bool appendHistory = DX12ResourceTrackingPolicyUtils::NeedsFullDescriptorHistory();
	if (!appendHistory && TryCopyHeapSlotSpanLocked(
			srcHeap, &destHeap, srcLocation.slot, destLocation.slot,
			destHandle, maxDestCount, increment))
		return true;
	gDescriptorCopySpanFallbackCalls++;
	const bool overlap = RangesOverlap(srcLocation, destLocation, maxDestCount);
	std::vector<DescriptorRecord> sourceSnapshot;
	std::vector<uint8_t> sourceValid;
	if (overlap) {
		sourceSnapshot.resize(maxDestCount);
		sourceValid.assign(maxDestCount, 0);
		for (UINT i = 0; i < maxDestCount; ++i) {
			DescriptorSlotLocation currentSrc = srcLocation;
			currentSrc.slot += i;
			DescriptorRecord ledgerSource;
			const DescriptorRecord *source = nullptr;
			const SIZE_T currentSrcHandle = srcHandle + static_cast<SIZE_T>(i) * increment;
			if (!GetSourceRecordLocked(srcHeap, currentSrc, currentSrcHandle, &ledgerSource, &source)) {
				ReleaseDescriptorRecordResources(&ledgerSource);
				continue;
			}
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&sourceSnapshot[i], *source, currentSrcHandle);
			sourceValid[i] = 1;
			ReleaseDescriptorRecordResources(&ledgerSource);
		}
	}

	for (UINT i = 0; i < maxDestCount; ++i) {
		const SIZE_T currentSrcHandle = srcHandle + static_cast<SIZE_T>(i) * increment;
		const SIZE_T currentDestHandle = destHandle + static_cast<SIZE_T>(i) * increment;
		const DescriptorRecord *source = nullptr;
		DescriptorRecord ledgerSource;
		if (overlap) {
			if (!sourceValid[i])
				continue;
			source = &sourceSnapshot[i];
		} else {
			DescriptorSlotLocation currentSrc = srcLocation;
			currentSrc.slot += i;
			if (!GetSourceRecordLocked(srcHeap, currentSrc, currentSrcHandle, &ledgerSource, &source)) {
				ReleaseDescriptorRecordResources(&ledgerSource);
				continue;
			}
		}
		StoreSlotValueLocked(&destHeap, destLocation.slot + i, currentDestHandle, *source, appendHistory);
		ReleaseDescriptorRecordResources(&ledgerSource);
	}
	return true;
}
};

static void RecordDescriptorLocked(DescriptorRecord &&record)
{
	RecordDescriptorInCacheLocked(std::move(record), true);
}

void RecordRuntimeDescriptor(DescriptorRecord &&record)
{
	if (!record.cpuHandle) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(record.cpuHandle, std::move(record));
}

void RecordRuntimeDescriptorCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count)
{
	(void)device; // No longer needed - ShadowUtils resolves handles via gDescriptorHeapByCpuStart
	DX12DescriptorHeapShadowUtils::CopyDescriptorRange(type, destStart, srcStart, count);
}

void RecordDescriptorCopyRange(
	ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count)
{
	(void)device;
	DX12DescriptorHeapShadowUtils::CopyDescriptorRange(type, destStart, srcStart, count);
}

void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
		"event=PsoRootSignatureImmediate,pso=%llu,kind=%s,pipelineState=%p,rootSignature=%p,threadId=%lu",
		static_cast<unsigned long long>(psoIndex),
		kind ? kind : "",
		pipelineState,
		rootSignature,
		GetCurrentThreadId());
	DX12RecordPsoRootSignatureImmediate(psoIndex, kind, pipelineState, rootSignature);
}

void DX12RecordPsoRootSignatureImmediate(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	PsoRootRecord record;
	record.psoIndex = psoIndex;
	record.kind = kind ? kind : "";
	record.pipelineState = pipelineState;
	record.rootSignature = rootSignature;

	AcquireSRWLockExclusive(&gDescriptorLock);
	++gPsoRootRecordsSeen;
	auto found = gPsoRootByIndex.find(psoIndex);
	if (found != gPsoRootByIndex.end()) {
		gPsoRoots[found->second] = std::move(record);
	} else {
		gPsoRootByIndex[psoIndex] = gPsoRoots.size();
		gPsoRoots.push_back(std::move(record));
	}
	LogResourceTrackerStatsLocked();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	if (!barriers || numBarriers == 0)
		return;

	AcquireSRWLockExclusive(&gResourceLock);
	bool resourceStateChanged = false;
	for (UINT i = 0; i < numBarriers; ++i) {
		const D3D12_RESOURCE_BARRIER &barrier = barriers[i];
		if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
			!barrier.Transition.pResource)
			continue;

		size_t *index = FindTrackedResourceIndexLocked(barrier.Transition.pResource);
		if (!index || *index >= gResources.size())
			continue;

		ResourceRecord &record = gResources[*index];
		if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
			record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record.currentState = barrier.Transition.StateAfter;
			record.hasCurrentState = true;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		} else {
			// DX12 state is resource/subresource state, not descriptor state.
			// Keep the hot path O(barriers); dump/query code joins descriptors
			// with ResourceRecord when it needs a current state snapshot.
			record.hasCurrentState = false;
			record.stateTrackedByBarrier = true;
			resourceStateChanged = true;
		}
	}
	if (resourceStateChanged) {
		DX12BufferResolveCacheUtils::RecordStateOnlyUpdateLocked();
		DX12LogManager::DebugJsonFunc("BufferResolveCacheInvalidated",
			"reason=resource_barrier_state_update,mode=state_only,present=%ld,"
			"threadId=%lu,generation=%llu,gpuVaIndexSize=%zu,liveResources=%zu",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
			gBufferResourceByGpuVa.size(),
			gLiveResourceCount);
	}
	ReleaseSRWLockExclusive(&gResourceLock);
}



// RecordDescriptorMerged removed - dead code.  Hooks now call
// DX12DescriptorHeapShadowUtils::RecordDescriptor directly.
