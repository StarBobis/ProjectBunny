// DX12ResourceTrackingCore: resource/descriptor tracking state, recording and invalidation.
// Split from DX12ResourceTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <intrin.h>



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
static std::vector<std::unique_ptr<DX12ResourcePublishedState>> gResourcePublishedStates;
std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
// Reverse ownership for gResourceByPtr. Resource invalidation used to scan
// every live pointer (roughly 20k in the target game) to find the one or two
// aliases for a released resource. Keep those aliases at their owning index
// so release is O(alias count), not O(all live resources).
static std::vector<std::vector<ID3D12Resource*>> gResourceAliasesByIndex;
std::map<UINT64, size_t> gBufferResourceByGpuVa;
std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
GpuVaWrapperShard gGpuVaWrapperShards[kGpuVaWrapperShardCount];
std::atomic<UINT64> gGpuVaWrapperSerial{1};
std::atomic<UINT64> gBufferResolveGeneration{1};
std::atomic<UINT64> gResourceIdentityGeneration{1};
std::atomic<UINT64> gDescriptorQuerySerial{1};
bool gCleanupRegistered = false;
UINT64 gResourcesRecordedFromCreate = 0;
UINT64 gDescriptorEpoch = 0;

// A buffer is registered in every 64KiB shard it spans (capped): views inside
// a buffer (common for 100KB+ streaming vertex buffers) query their own
// offset's shard, and a base-only registration made every such query miss the
// wrapper index and fall through to the slow locked fallbacks.
static constexpr UINT kMaxGpuVaWrapperShardSpan = 16;

static UINT GpuVaWrapperShardIndex(UINT64 gpuVa)
{
	return static_cast<UINT>(
		(gpuVa >> 16) & (kGpuVaWrapperShardCount - 1));
}

void RegisterGpuVaWrapper(UINT64 gpuVa, void *wrapper, const ResourceRecord *record)
{
	if (!gpuVa || !wrapper || !record)
		return;
	GpuVaWrapperEntry entry{gpuVa, wrapper, record,
		gGpuVaWrapperSerial.fetch_add(1, std::memory_order_relaxed)};
	UINT shard = GpuVaWrapperShardIndex(gpuVa);
	const UINT lastShard = record->size ?
		GpuVaWrapperShardIndex(gpuVa + record->size - 1) :
		shard;
	UINT spanCount = 0;
	for (;;) {
		AcquireSRWLockExclusive(&gGpuVaWrapperShards[shard].lock);
		gGpuVaWrapperShards[shard].wrappers.emplace(gpuVa, entry);
		ReleaseSRWLockExclusive(&gGpuVaWrapperShards[shard].lock);
		++spanCount;
		if (shard == lastShard || spanCount >= kMaxGpuVaWrapperShardSpan)
			break;
		shard = (shard + 1) & (kGpuVaWrapperShardCount - 1);
	}
}

void UnregisterGpuVaWrapper(UINT64 gpuVa, void *wrapper, UINT64 size)
{
	if (!gpuVa || !wrapper)
		return;
	UINT shard = GpuVaWrapperShardIndex(gpuVa);
	const UINT lastShard = size ?
		GpuVaWrapperShardIndex(gpuVa + size - 1) :
		shard;
	UINT spanCount = 0;
	for (;;) {
		GpuVaWrapperShard &current = gGpuVaWrapperShards[shard];
		AcquireSRWLockExclusive(&current.lock);
		auto range = current.wrappers.equal_range(gpuVa);
		for (auto it = range.first; it != range.second; ++it) {
			if (it->second.wrapper == wrapper) {
				current.wrappers.erase(it);
				break;
			}
		}
		ReleaseSRWLockExclusive(&current.lock);
		++spanCount;
		if (shard == lastShard || spanCount >= kMaxGpuVaWrapperShardSpan)
			break;
		shard = (shard + 1) & (kGpuVaWrapperShardCount - 1);
	}
}

// Fast path: resolve GPU VA from the sorted per-shard wrapper vectors using
// the cached ResourceRecord pointer stored in each entry.  Avoids both the
// std::map red-black tree and the gResources array indirection.  A miss here
// (e.g. a query offset that crosses a shard boundary) falls through to the
// gBufferResourceByGpuVa / resolve-cache paths.
bool DX12TryResolveFromGpuVaWrapper(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	if (!summary || gpuVirtualAddress == 0)
		return false;

	*summary = DX12BufferResourceSummary();
	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return false;

	GpuVaWrapperShard &shard = GpuVaWrapperShardFor(gpuVirtualAddress);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.wrappers.lower_bound(gpuVirtualAddress);

	if (it != shard.wrappers.begin() &&
		(it == shard.wrappers.end() || it->first > gpuVirtualAddress))
		--it;

	const ResourceRecord *best = nullptr;
	UINT64 bestSerial = 0;
	for (; it != shard.wrappers.end() && it->first <= gpuVirtualAddress; ++it) {
		const ResourceRecord *rec = it->second.record;
		if (!rec || !rec->resource)
			continue;
		if (rec->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
			continue;
		const UINT64 begin = rec->gpuVirtualAddress;
		const UINT64 end = rec->gpuVirtualAddressRealEnd;
		if (begin == 0 || end <= begin)
			continue;
		if (gpuVirtualAddress < begin || gpuVirtualAddress >= end)
			continue;
		// VA reuse: a stale entry from a released resource can share the same
		// VA with the current resource.  The newest registration wins so a
		// lingering entry can never shadow the live resource.
		if (it->second.serial >= bestSerial) {
			best = rec;
			bestSerial = it->second.serial;
		}
	}

	if (!best) {
		ReleaseSRWLockShared(&shard.lock);
		return false;
	}

	summary->resource = best->resource;
	summary->resourceGeneration = best->identityGeneration;
	summary->publishedState = best->publishedState;
	summary->resourceDesc = best->desc;
	summary->hasResourceDesc = true;
	summary->resourceHeapType = best->hasHeapType ?
		static_cast<UINT>(best->heapType) : 0;
	summary->hasResourceHeapType = best->hasHeapType;
	summary->gpuVirtualAddress = best->gpuVirtualAddress;
	summary->resourceOffset = gpuVirtualAddress - best->gpuVirtualAddress;
	summary->viewSize = size;
	summary->currentState = best->hasCurrentState ?
		static_cast<UINT>(best->currentState) :
		static_cast<UINT>(D3D12_RESOURCE_STATE_COMMON);
	summary->hasCurrentState = best->hasCurrentState;
	summary->stateTrackedByBarrier = best->stateTrackedByBarrier;

	ReleaseSRWLockShared(&shard.lock);
	return true;
}
UINT64 gBufferResolveInvalidationEpoch = 0;
std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic{0};
DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity] = {};

// Canonical identity cache: avoids per-call COM QueryInterface overhead.
static std::unordered_map<ID3D12Resource*, ID3D12Resource*> gCanonicalCache;
static SRWLOCK gCanonicalCacheLock = SRWLOCK_INIT;

static DX12ResourcePublishedState *EnsureResourcePublishedStateLocked(size_t index)
{
	if (index >= gResourcePublishedStates.size())
		gResourcePublishedStates.resize(index + 1);
	if (!gResourcePublishedStates[index])
		gResourcePublishedStates[index] =
			std::make_unique<DX12ResourcePublishedState>();
	return gResourcePublishedStates[index].get();
}

static bool DescriptorHeapSlotOccupied(const DescriptorHeapRecord &heap, UINT slot)
{
	return slot < heap.occupied.size() && heap.occupied[slot] != 0;
}

static bool DescriptorHeapIsShaderVisible(const DescriptorHeapRecord &heap)
{
	return (heap.desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
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
	// Chunked store: release any resources the records may carry (slot records
	// normally do not own), then drop every chunk and the pointer table.
	heap->chunkedSlots.ForEachOccupied(
		[](uint32_t, DescriptorRecord &record) {
			ReleaseDescriptorRecordResources(&record);
		});
	heap->chunkedSlots.Reset();
	heap->slotHistory.clear();
	heap->slotHistoryEpochs.clear();
	DX12DescriptorSlotArrayUtils::SetLiveSlots(heap, 0);
}

void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record)
{
	if (!record || record->desc.NumDescriptors == 0)
		return;
	record->chunkedSlots.Initialize(record->desc.NumDescriptors);
	record->stateLocks = std::make_unique<DescriptorHeapRecord::StateLocks>(
		record->desc.NumDescriptors);
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
	//
	// The allocation itself (potentially MBs of DescriptorRecord) runs OUTSIDE
	// the all-shard lock: while it is in flight, other threads must not block
	// on every shard of this heap. A second thread may allocate concurrently;
	// the re-check below keeps exactly one result.
	const size_t slotCount = record->desc.NumDescriptors;
	if (stateLocks)
		stateLocks->UnlockAllExclusive();

	std::vector<DescriptorRecord> newSlots;
	std::vector<uint8_t> newOccupied;
	newSlots.resize(slotCount);
	newOccupied.assign(slotCount, 0);
	DX12_PROF_ADD_ITEMS("Desc.slotAllocations", 1);
	DX12_PROF_ADD_ITEMS("Desc.slotAllocationBytes",
		static_cast<LONG64>(slotCount * (sizeof(DescriptorRecord) + 1)));

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
	record->slots.swap(newSlots);
	record->occupied.swap(newOccupied);
	// Migrate the current chunked values into the dense adapter. The chunked
	// store is left intact: this materialization only runs under Dump/Hunt
	// tracking, where the lock-free chunked fast path is disabled, and keeping
	// the chunks alive removes any lifetime hazard for in-flight accesses.
	// Every access prefers the materialized vectors once they exist.
	size_t denseCount = 0;
	record->chunkedSlots.ForEachOccupied(
		[record, &denseCount](uint32_t slot, const DescriptorRecord &value) {
			if (slot >= record->slots.size())
				return;
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&record->slots[slot], value, value.cpuHandle);
			record->occupied[slot] = 1;
			++denseCount;
		});
	DX12DescriptorSlotArrayUtils::SetLiveSlots(
		record, static_cast<LONG64>(denseCount));
	// Publish dense readiness before releasing the all-shard lock so any
	// thread that acquires the flag can safely access slots/occupied.
	if (record->stateLocks)
		record->stateLocks->MarkMaterialized();
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
	UnregisterCpuMappableResource(resource, canonical);
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
		return;
	}
	ResourceRecord oldRecord = record;
	if (record.publishedState)
		record.publishedState->Invalidate();
	if (record.gpuVirtualAddress)
		gBufferResourceByGpuVa.erase(record.gpuVirtualAddress);
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
	DX12InvalidateBufferContent(record.resource);
	if (canonical != record.resource)
		DX12InvalidateTextureHash(canonical);
	if (canonical != record.resource)
		DX12InvalidateBufferContent(canonical);
	record = ResourceRecord();
	gFreeResourceSlots.push_back(index);
	if (gLiveResourceCount > 0)
		gLiveResourceCount--;
	// Diagnostic only: every caller-side cache keyed on the global buffer
	// resolve epoch (the IA view-hash slot memos and the IA-state negative
	// verdict) is invalidated by this bump whether or not any range its key
	// covers was touched, so its rate decides whether those caches need a
	// range-scoped validity test instead of the coarse epoch.
	const DX12BufferResolveCacheInvalidationStats invalidation =
		DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
			&oldRecord, nullptr, "resource_invalidate", resource);
	if (invalidation.rangeChanged)
		DX12_PROF_COUNT("Rm.BufferRangeInvalidations");
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
			if (placedInfo->heapSize > placedInfo->heapOffset)
				record.gpuVirtualAddressOobEnd =
					record.gpuVirtualAddress + (placedInfo->heapSize - placedInfo->heapOffset);
			if (record.gpuVirtualAddressOobEnd < record.gpuVirtualAddressRealEnd)
				record.gpuVirtualAddressOobEnd = record.gpuVirtualAddressRealEnd;
		}
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
	AcquireSRWLockExclusive(&gResourceLock);
	auto found = gResourceByPtr.find(canonical);
	ResourceRecord oldRecord = {};
	bool hadOldRecord = false;
	if (found != gResourceByPtr.end()) {
		const size_t index = found->second;
		ResourceRecord &existing = gResources[found->second];
		oldRecord = existing;
		hadOldRecord = true;
		const bool sameIdentityRange =
			DX12BufferResolveCacheUtils::SameBufferRange(existing, record);
		record.identityGeneration = sameIdentityRange ?
			existing.identityGeneration :
			gResourceIdentityGeneration.fetch_add(
				1, std::memory_order_relaxed) + 1;
		record.publishedState = EnsureResourcePublishedStateLocked(index);
		if (existing.gpuVirtualAddress && existing.gpuVirtualAddress != record.gpuVirtualAddress) {
			gBufferResourceByGpuVa.erase(existing.gpuVirtualAddress);
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
		}
		record.publishedState->Publish(record);
	} else {
		record.identityGeneration = gResourceIdentityGeneration.fetch_add(
			1, std::memory_order_relaxed) + 1;
		size_t index = 0;
		if (!gFreeResourceSlots.empty()) {
			index = gFreeResourceSlots.back();
			gFreeResourceSlots.pop_back();
			record.publishedState = EnsureResourcePublishedStateLocked(index);
			gResources[index] = record;
		} else {
			index = gResources.size();
			record.publishedState = EnsureResourcePublishedStateLocked(index);
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
		}
		record.publishedState->Publish(record);
	}
	// See the release path above: this is the other producer of the global
	// resolve-epoch bumps that caller-side IA caches key on.
	const DX12BufferResolveCacheInvalidationStats invalidation =
		DX12BufferResolveCacheUtils::InvalidateResourceRangeChangeLocked(
			hadOldRecord ? &oldRecord : nullptr, &record, "resource_record_update", resource);
	if (invalidation.rangeChanged)
		DX12_PROF_COUNT("Rm.BufferRangeInvalidations");
	gResourcesRecordedFromCreate++;
	ReleaseSRWLockExclusive(&gResourceLock);
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
	for (std::unique_ptr<DX12ResourcePublishedState> &publishedState :
			gResourcePublishedStates) {
		if (publishedState)
			publishedState->Invalidate();
	}
	gResources.clear();
	gFreeResourceSlots.clear();
	gLiveResourceCount = 0;
	gResourceByPtr.clear();
	gResourceAliasesByIndex.clear();
	gBufferResourceByGpuVa.clear();
	DX12BufferResolveCacheUtils::InvalidateAllLocked("cleanup");
	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockExclusive(&gResourceLock);
	AcquireSRWLockExclusive(&gCanonicalCacheLock);
	gCanonicalCache.clear();
	ReleaseSRWLockExclusive(&gCanonicalCacheLock);

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
				record->parameters.reserve(desc->Desc_1_1.NumParameters);
				for (UINT i = 0; i < desc->Desc_1_1.NumParameters; ++i)
					ParseRootParameter1(record, i, desc->Desc_1_1.pParameters[i]);
			} else {
				record->flags = static_cast<UINT>(desc->Desc_1_0.Flags);
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
	// One-entry TLS cache of the heap that resolved the previous handle:
	// games walk descriptors sequentially, so most lookups hit it directly.
	static thread_local size_t cachedHeapIndex = SIZE_MAX;
	if (DescriptorSlotFromHeapLocked(
			cachedHeapIndex, cpuHandle, requiredType, location, slotsAvailable))
		return true;
	auto heapByStart = gDescriptorHeapByCpuStart.upper_bound(cpuHandle);
	if (heapByStart == gDescriptorHeapByCpuStart.begin())
		return false;
	--heapByStart;
	if (DescriptorSlotFromHeapLocked(
			heapByStart->second, cpuHandle, requiredType, location, slotsAvailable)) {
		cachedHeapIndex = heapByStart->second;
		return true;
	}
	return false;
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
	auto found = gRootSignatureByPtr.find(record.rootSignature);
	if (found != gRootSignatureByPtr.end()) {
		gRootSignatures[found->second] = std::move(record);
	}
	else {
		gRootSignatureByPtr[record.rootSignature] = gRootSignatures.size();
		gRootSignatures.push_back(std::move(record));
	}
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12PrimeDescriptorHistoryForHunting()
{
	if (!DX12HuntNeedsHotPath())
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
	DX12_LOG("DX12DescriptorHistoryPrime",
		"heaps=%zu,occupied=%zu,primed=%zu,epoch=%llu,historyEntries=%zu",
		gDescriptorHeaps.size(),
		occupied,
		primed,
		static_cast<unsigned long long>(gDescriptorEpoch),
		DescriptorHeapHistoryEntriesLocked());
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

bool DX12GetDescriptorSlotVersions(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions)
{
	if (!heap || (!slots && count) || (!versions && count))
		return false;
	for (size_t index = 0; index < count; ++index)
		versions[index] = 0;

	bool foundHeap = false;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
			found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap && record.stateLocks) {
			foundHeap = true;
			for (size_t index = 0; index < count; ++index) {
				if (slots[index] >= record.desc.NumDescriptors) {
					foundHeap = false;
					break;
				}
				versions[index] = record.stateLocks->ReadSlotVersion(slots[index]);
			}
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return foundHeap;
}

bool DX12GetDescriptorSlotIdentities(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds)
{
	if (!heap || (!slots && count) || (!versions && count) || (!valueIds && count))
		return false;
	for (size_t index = 0; index < count; ++index) {
		versions[index] = 0;
		valueIds[index] = 0;
	}

	bool foundHeap = false;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found != gDescriptorHeapByPtr.end() &&
			found->second < gDescriptorHeaps.size()) {
		const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
		if (record.heap == heap && record.stateLocks) {
			foundHeap = true;
			for (size_t index = 0; index < count; ++index) {
				if (slots[index] >= record.desc.NumDescriptors) {
					foundHeap = false;
					break;
				}
				DescriptorHeapRecord::StateLocks::SlotIdentity identity;
				if (!record.stateLocks->ObserveSlotIdentity(slots[index], &identity)) {
					foundHeap = false;
					break;
				}
				versions[index] = identity.version;
				valueIds[index] = identity.valueId;
			}
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);
	return foundHeap;
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

void RecordRuntimeDescriptor(DescriptorRecord &&record)
{
	if (!record.cpuHandle) {
		ReleaseDescriptorRecordResources(&record);
		return;
	}
	DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(record.cpuHandle, std::move(record));
}

void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature)
{
	// Hot-path dispatch decisions depend on PSO->root-signature identity. Keep
	// that mapping synchronous at PSO creation time instead of letting queue
	// latency leak into per-dispatch binding collection.
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
	auto found = gPsoRootByIndex.find(psoIndex);
	if (found != gPsoRootByIndex.end()) {
		gPsoRoots[found->second] = std::move(record);
	} else {
		gPsoRootByIndex[psoIndex] = gPsoRoots.size();
		gPsoRoots.push_back(std::move(record));
	}
	ReleaseSRWLockExclusive(&gDescriptorLock);
}
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	if (DX12ResourceCollectQueue::EnqueueResourceBarriers(
			numBarriers,
			barriers,
			"DX12RecordResourceBarrier")) {
		return;
	}

	if (!barriers || numBarriers == 0)
		return;

	AcquireSRWLockExclusive(&gResourceLock);
	for (UINT i = 0; i < numBarriers; ++i) {
		const D3D12_RESOURCE_BARRIER &barrier = barriers[i];
		if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
			!barrier.Transition.pResource)
			continue;

		// Fast path: wrapper-cached record (no gResourceByPtr hash map lookup).
		ResourceRecord *fastRecord =
			DX12FindWrapperResourceRecord(barrier.Transition.pResource);
		if (fastRecord) {
			if (barrier.Transition.Subresource ==
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
				fastRecord->desc.Dimension ==
					D3D12_RESOURCE_DIMENSION_BUFFER) {
				fastRecord->currentState = barrier.Transition.StateAfter;
				fastRecord->hasCurrentState = true;
				fastRecord->stateTrackedByBarrier = true;
			} else {
				fastRecord->hasCurrentState = false;
				fastRecord->stateTrackedByBarrier = true;
			}
			if (fastRecord->publishedState)
				fastRecord->publishedState->PublishState(*fastRecord);
			continue;
		}

		size_t *index = FindTrackedResourceIndexLocked(barrier.Transition.pResource);
		if (!index || *index >= gResources.size())
			continue;

		ResourceRecord &record = gResources[*index];
		if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
			record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			record.currentState = barrier.Transition.StateAfter;
			record.hasCurrentState = true;
			record.stateTrackedByBarrier = true;
		} else {
			// DX12 state is resource/subresource state, not descriptor state.
			// Keep the hot path O(barriers); dump/query code joins descriptors
			// with ResourceRecord when it needs a current state snapshot.
			record.hasCurrentState = false;
			record.stateTrackedByBarrier = true;
		}
		if (record.publishedState)
			record.publishedState->PublishState(record);
	}
	ReleaseSRWLockExclusive(&gResourceLock);
}

// State query for the async readback pipeline (DX12AsyncContentReadback.inl).
// Returns the tracked current state of a resource from the same records the
// barrier recorder maintains. knownOut=false when no state is known (created
// before tracking was enabled, or never observed); barrierTrackedOut=true
// when the state came from an explicit ResourceBarrier transition (creation
// initialState alone cannot distinguish COMMON-promotion engines from
// explicit-state engines that never barriered this resource yet).
bool DX12GetResourceCurrentState(
	ID3D12Resource *resource,
	D3D12_RESOURCE_STATES *stateOut,
	bool *knownOut,
	bool *barrierTrackedOut)
{
	if (stateOut)
		*stateOut = D3D12_RESOURCE_STATE_COMMON;
	if (knownOut)
		*knownOut = false;
	if (barrierTrackedOut)
		*barrierTrackedOut = false;
	if (!resource)
		return false;

	// Fast path: wrapper-cached record (no gResourceLock, mirrors the barrier
	// recorder's hot path). The wrapper record only carries states observed
	// from barriers; before the first barrier (or for resources created
	// before tracking enabled) it has no state, so fall through to the
	// tracked record which holds the creation initialState.
	if (ResourceRecord *fastRecord = DX12FindWrapperResourceRecord(resource)) {
		if (fastRecord->hasCurrentState) {
			if (stateOut)
				*stateOut = fastRecord->currentState;
			if (knownOut)
				*knownOut = true;
			if (barrierTrackedOut)
				*barrierTrackedOut = fastRecord->stateTrackedByBarrier;
			return true;
		}
	}

	AcquireSRWLockShared(&gResourceLock);
	size_t *index = FindTrackedResourceIndexLocked(resource);
	bool found = false;
	if (index && *index < gResources.size()) {
		const ResourceRecord &record = gResources[*index];
		if (record.resource && record.hasCurrentState) {
			if (stateOut)
				*stateOut = record.currentState;
			if (knownOut)
				*knownOut = true;
			if (barrierTrackedOut)
				*barrierTrackedOut = record.stateTrackedByBarrier;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
	return found;
}

bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary)
{
	if (!rootSignature || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12RootSignature*, size_t>::iterator found =
		gRootSignatureByPtr.find(rootSignature);
	if (found == gRootSignatureByPtr.end() ||
		found->second >= gRootSignatures.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const RootSignatureRecord &record = gRootSignatures[found->second];
	summary->rootSignature = record.rootSignature;
	summary->hash = record.hash;
	summary->size = record.size;
	summary->nodeMask = record.nodeMask;
	summary->version = record.version;
	summary->flags = record.flags;
	summary->parsed = record.parsed;
	summary->parameters = record.parameters;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}

bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature)
{
	if (!psoIndex || !rootSignature)
		return false;

	*rootSignature = nullptr;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<UINT64, size_t>::iterator found =
		gPsoRootByIndex.find(psoIndex);
	if (found == gPsoRootByIndex.end() ||
		found->second >= gPsoRoots.size() ||
		!gPsoRoots[found->second].rootSignature) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	*rootSignature = gPsoRoots[found->second].rootSignature;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}
