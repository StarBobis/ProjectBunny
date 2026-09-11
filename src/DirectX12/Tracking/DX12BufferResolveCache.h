// GPU-VA buffer resolve cache, wrapper shards and stats.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12ResourcePublishedState.h"

struct BufferResolveCacheEntry
{
	UINT64 begin = 0;
	UINT64 end = 0;
	DX12BufferResourceSummary summary = {};
};

struct DX12BufferResolveCacheInvalidationStats
{
	size_t removedEntries = 0;
	bool rangeChanged = false;
};

struct DX12BufferResolveInvalidatedRange
{
	UINT64 epoch = 0;
	UINT64 begin = 0;
	UINT64 end = 0;
};

constexpr size_t kDX12BufferResolveInvalidationHistoryCapacity = 4096;

struct PsoRootRecord
{
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

extern SRWLOCK gResourceLock;
extern SRWLOCK gDescriptorLock;
extern std::vector<RootSignatureRecord> gRootSignatures;
extern std::deque<DescriptorHeapRecord> gDescriptorHeaps;
extern std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
extern std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
extern std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
extern std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
// Heap slots are the single source of truth; the old CPU-handle map was removed.
// Use DX12DescriptorHeapShadowUtils::FindLatestDescriptor instead.
extern std::vector<PsoRootRecord> gPsoRoots;
extern std::unordered_map<UINT64, size_t> gPsoRootByIndex;
extern std::vector<ResourceRecord> gResources;
extern std::vector<size_t> gFreeResourceSlots;
extern size_t gLiveResourceCount;
extern std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
extern std::map<UINT64, size_t> gBufferResourceByGpuVa;
extern std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
// Fast GPU VA 鈫?ResourceWrapper index: sorted vector for cache-friendly
// binary search.  Stores a direct pointer to the wrapper's cached
// ResourceRecord so the hot lookup path never needs the wrapper type.
struct GpuVaWrapperEntry {
	UINT64 gpuVa;
	void *wrapper;
	const ResourceRecord *record;
	// Registration serial: a released resource's VA can be reused by a newer
	// resource while a lingering wrapper entry still exists.  Resolution
	// picks the newest registration so stale entries cannot shadow the
	// current resource.
	UINT64 serial;
};
// Sharded by VA (64 KiB granularity): concurrent resource churn on different
// address ranges no longer serializes on one global lock, and each shard's
// insert/erase cost is O(n/shardCount) instead of O(n).  Resolutions that
// span a shard boundary (rare large-offset queries) simply miss and fall
// back to the gBufferResourceByGpuVa / resolve-cache paths.
static constexpr UINT kGpuVaWrapperShardBits = 8;
static constexpr UINT kGpuVaWrapperShardCount = 1u << kGpuVaWrapperShardBits;
struct GpuVaWrapperShard {
	SRWLOCK lock = SRWLOCK_INIT;
	// Sorted by GPU VA. A multimap (not a sorted vector): the game's per-frame
	// buffer churn registers/unregisters dozens of buffers, and the vector's
	// O(n) middle insert/erase memmoves held the shard lock for tens-to-
	// hundreds of microseconds, blocking every GPUVA resolve on the draw
	// thread (measured 50-240us per pre-skin slot resolve). Tree mutations
	// are O(log n), keeping mutator critical sections in the nanosecond
	// range; the read path (lower_bound + forward scan) is unchanged in cost.
	std::multimap<UINT64, GpuVaWrapperEntry> wrappers;
};
extern GpuVaWrapperShard gGpuVaWrapperShards[kGpuVaWrapperShardCount];
extern std::atomic<UINT64> gGpuVaWrapperSerial;
// 64 KiB shard granularity: queries with small offsets (the common case for
// BufferLocation == resource base VA) resolve in the same shard as the
// resource registration.
inline GpuVaWrapperShard &GpuVaWrapperShardFor(UINT64 gpuVa)
{
	const UINT index = static_cast<UINT>(
		(gpuVa >> 16) & (kGpuVaWrapperShardCount - 1));
	return gGpuVaWrapperShards[index];
}
void RegisterGpuVaWrapper(UINT64 gpuVa, void *wrapper, const ResourceRecord *record);
void UnregisterGpuVaWrapper(UINT64 gpuVa, void *wrapper, UINT64 size);// Returns the cached ResourceRecord for a resource that has an active wrapper,
// or nullptr.  Skips the gResourceByPtr hash map and gResources array lookup.
ResourceRecord *DX12FindWrapperResourceRecord(ID3D12Resource *resource);
// Locked variants for Tracking TUs whose callers already hold gResourceLock
// (defined in DX12ResourceContentHashing.cpp).
bool DX12FillBufferSummaryFromRecordLocked(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
const char *DX12DiagnoseBufferResolveByGpuVaLocked(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource);
// IA-hash reuse cache stored on ResourceWrapper.  Returns true and fills
// *hash on a full match of the view parameters and the buffer resolve epoch.
// The buffer content generation is deliberately not part of the key: the IA
// hash is metadata-only, so content writes cannot change the cached value.
bool DX12TryGetWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 resolveGeneration, uint32_t *hash);
void DX12StoreWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 resolveGeneration, uint32_t hash);
extern std::atomic<UINT64> gBufferResolveGeneration;
extern std::atomic<UINT64> gResourceIdentityGeneration;
extern std::atomic<UINT64> gDescriptorQuerySerial;
extern bool gCleanupRegistered;
extern UINT64 gResourcesRecordedFromCreate;
extern UINT64 gDescriptorEpoch;
extern UINT64 gBufferResolveInvalidationEpoch;
extern std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic;
extern DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity];

class DX12BufferResolveCacheUtils
{
public:
static bool RangesOverlap(UINT64 beginA, UINT64 endA, UINT64 beginB, UINT64 endB)
{
	return beginA < endB && beginB < endA;
}

static UINT64 NormalizeRangeEnd(UINT64 begin, UINT64 end)
{
	if (end > begin)
		return end;
	return UINT64_MAX;
}

static bool SameBufferRange(const ResourceRecord &a, const ResourceRecord &b)
{
	return a.gpuVirtualAddress == b.gpuVirtualAddress &&
		a.size == b.size &&
		a.gpuVirtualAddressRealEnd == b.gpuVirtualAddressRealEnd &&
		a.gpuVirtualAddressOobEnd == b.gpuVirtualAddressOobEnd &&
		a.desc.Dimension == b.desc.Dimension;
}

static bool HasBufferRange(const ResourceRecord &record)
{
	return record.resource &&
		record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		record.gpuVirtualAddress != 0 &&
		record.size != 0;
}

static UINT64 RangeEnd(const ResourceRecord &record)
{
	if (record.gpuVirtualAddressRealEnd)
		return record.gpuVirtualAddressRealEnd;
	return record.gpuVirtualAddress + record.size;
}

static size_t PurgeRangeLocked(UINT64 begin, UINT64 end)
{
	if (!begin || end <= begin)
		return 0;

	size_t removed = 0;
	// gBufferResolveCache is keyed by entry begin address.
	// Entries overlapping [begin, end) must satisfy: key < end AND entry.end > begin.
	// Find the entry with the largest key that is <= begin, then iterate forward
	// until key >= end (entries starting at or past 'end' cannot overlap).
	std::map<UINT64, BufferResolveCacheEntry>::iterator it =
		gBufferResolveCache.lower_bound(begin);
	if (it != gBufferResolveCache.begin())
		--it;
	while (it != gBufferResolveCache.end() && it->first < end) {
		if (DX12BufferResolveCacheUtils::RangesOverlap(
				it->second.begin, it->second.end, begin, end)) {
			it = gBufferResolveCache.erase(it);
			removed++;
		} else {
			++it;
		}
	}
	return removed;
}

static void AppendInvalidatedRangeLocked(UINT64 epoch, UINT64 begin, UINT64 end)
{
	if (!epoch || !begin || end <= begin)
		return;
	const size_t slot = static_cast<size_t>(
		epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
	gBufferResolveInvalidatedRanges[slot].epoch = epoch;
	gBufferResolveInvalidatedRanges[slot].begin = begin;
	gBufferResolveInvalidatedRanges[slot].end = end;
}

static bool RangeInvalidatedSinceLocked(UINT64 begin, UINT64 end, UINT64 sinceEpoch)
{
	if (!begin)
		return true;
	end = DX12BufferResolveCacheUtils::NormalizeRangeEnd(begin, end);
	const UINT64 currentEpoch = gBufferResolveInvalidationEpoch;
	if (sinceEpoch == currentEpoch)
		return false;
	if (sinceEpoch > currentEpoch)
		return true;
	if (currentEpoch - sinceEpoch >=
		static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity)) {
		return true;
	}

	for (UINT64 epoch = sinceEpoch + 1; epoch <= currentEpoch; ++epoch) {
		const size_t slot = static_cast<size_t>(
			epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
		const DX12BufferResolveInvalidatedRange &range = gBufferResolveInvalidatedRanges[slot];
		if (range.epoch != epoch) {
			return true;
		}
		if (DX12BufferResolveCacheUtils::RangesOverlap(begin, end, range.begin, range.end))
			return true;
	}
	return false;
}

static UINT64 BumpInvalidationEpochLocked()
{
	gBufferResolveInvalidationEpoch++;
	if (!gBufferResolveInvalidationEpoch)
		gBufferResolveInvalidationEpoch = 1;
	gBufferResolveInvalidationEpochAtomic.store(
		gBufferResolveInvalidationEpoch, std::memory_order_release);
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	return gBufferResolveInvalidationEpoch;
}

static DX12BufferResolveCacheInvalidationStats InvalidateResourceRangeChangeLocked(
	const ResourceRecord *oldRecord,
	const ResourceRecord *newRecord,
	const char *reason,
	ID3D12Resource *resource)
{
	DX12BufferResolveCacheInvalidationStats stats;
	const bool oldHasRange = oldRecord && DX12BufferResolveCacheUtils::HasBufferRange(*oldRecord);
	const bool newHasRange = newRecord && DX12BufferResolveCacheUtils::HasBufferRange(*newRecord);
	if (oldHasRange && newHasRange &&
		DX12BufferResolveCacheUtils::SameBufferRange(*oldRecord, *newRecord)) {
		return stats;
	}

	stats.rangeChanged = oldHasRange || newHasRange;
	if (!stats.rangeChanged) {
		return stats;
	}

	if (oldHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	if (!oldHasRange && stats.removedEntries == 0) {
		return stats;
	}

	const UINT64 invalidationEpoch =
		DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	if (oldHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	return stats;
}

static void InvalidateAllLocked(const char *reason)
{
	gBufferResolveCache.clear();
	const UINT64 invalidationEpoch = DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(invalidationEpoch, 1, UINT64_MAX);
}
};
