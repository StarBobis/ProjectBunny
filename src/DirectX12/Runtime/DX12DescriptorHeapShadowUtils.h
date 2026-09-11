// Heap-slot shadow storage for runtime descriptors (replaces the CPU-handle hash map).
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "../DX12Tracking.h"
#include "../Config/DX12Config.h"
#include "MigotoCommandVmUtils.hpp"

// ---------------------------------------------------------------------------
// DX12DescriptorHeapShadowUtils
// ---------------------------------------------------------------------------
// Replaces the gRuntimeDescriptorByCpuHandle hash map with direct heap-slot
// array access. A shared structure lock keeps heap indexes stable while
// a lock owned by each heap record serializes descriptor slot operations.
//
// Key insight: gDescriptorHeaps[].slots[] is already a flat array indexed by
// descriptor heap offset.  The runtime hash map was an unnecessary duplicate
// layer that serialised all descriptor copy traffic through a single lock.
//
// This utility class operates synchronously (no async queues).  All methods
// are static; all data members are public (per project convention).


class DX12DescriptorHeapShadowUtils
{
public:
	struct ResolvedHeapSlot
	{
		DescriptorHeapRecord *heap = nullptr;
		size_t heapIndex = 0;
		UINT slot = 0;
	};

	struct HeapResolveCacheEntry
	{
		DescriptorHeapRecord *heap = nullptr;
		size_t heapIndex = 0;
		SIZE_T cpuStart = 0;
		UINT increment = 0;
		UINT numDescriptors = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	};

	struct HeapResolveTlsCache
	{
		// Descriptor creation/copy worker threads can rotate across considerably
		// more than eight CPU heaps. Keep the active heap working set thread-local
		// so handle resolution does not fall back to gDescriptorLock/map lookup.
		static constexpr size_t EntryCount = 32;
		std::array<HeapResolveCacheEntry, EntryCount> entries;
	};

	struct CopyTelemetryBatch
	{
		LONG64 rangeCalls = 0;
		LONG64 slots = 0;
		LONG64 singleRanges = 0;
		LONG64 multiRanges = 0;
		LONG64 resolvedRanges = 0;
		LONG64 sameHeapRanges = 0;
		LONG64 overlapRanges = 0;
		LONG64 sourceFound = 0;
		LONG64 sourceMissing = 0;
		LONG64 changed = 0;
		LONG64 unchanged = 0;
		LONG64 erased = 0;
		LONG64 alreadyEmpty = 0;
		LONG64 memoHits = 0;
		LONG64 memoMisses = 0;
		LONG64 repeatHits = 0;

		void RecordRange(UINT count, bool resolved, bool sameHeap, bool overlaps)
		{
			++rangeCalls;
			slots += count;
			if (count == 1) ++singleRanges; else ++multiRanges;
			if (resolved) ++resolvedRanges;
			if (sameHeap) ++sameHeapRanges;
			if (overlaps) ++overlapRanges;
		}

		void RecordMemo(bool hit)
		{
			if (hit) ++memoHits; else ++memoMisses;
		}

		void RecordRepeatHit()
		{
			++repeatHits;
		}

		void RecordResults(
			UINT found, UINT missing, UINT changedCount, UINT unchangedCount,
			UINT erasedCount, UINT alreadyEmptyCount)
		{
			sourceFound += found;
			sourceMissing += missing;
			changed += changedCount;
			unchanged += unchangedCount;
			erased += erasedCount;
			alreadyEmpty += alreadyEmptyCount;
		}

		void Flush() const
		{
			DX12_PROF_ADD_ITEMS("Desc.CopyRangeCalls", rangeCalls);
			DX12_PROF_ADD_ITEMS("Desc.CopySlots", slots);
			DX12_PROF_ADD_ITEMS("Desc.CopySingleRangeCalls", singleRanges);
			DX12_PROF_ADD_ITEMS("Desc.CopyMultiRangeCalls", multiRanges);
			DX12_PROF_ADD_ITEMS("Desc.CopyResolvedRangeCalls", resolvedRanges);
			DX12_PROF_ADD_ITEMS("Desc.CopySameHeapRangeCalls", sameHeapRanges);
			DX12_PROF_ADD_ITEMS("Desc.CopyOverlapRangeCalls", overlapRanges);
			DX12_PROF_ADD_ITEMS("Desc.CopySourceFoundSlots", sourceFound);
			DX12_PROF_ADD_ITEMS("Desc.CopySourceMissingSlots", sourceMissing);
			DX12_PROF_ADD_ITEMS("Desc.CopyChangedSlots", changed);
			DX12_PROF_ADD_ITEMS("Desc.CopyUnchangedSlots", unchanged);
			DX12_PROF_ADD_ITEMS("Desc.CopyErasedSlots", erased);
			DX12_PROF_ADD_ITEMS("Desc.CopyAlreadyEmptySlots", alreadyEmpty);
			DX12_PROF_ADD_ITEMS("Desc.CopyMemoHits", memoHits);
			DX12_PROF_ADD_ITEMS("Desc.CopyMemoMisses", memoMisses);
			DX12_PROF_ADD_ITEMS("Desc.CopyRepeatHits", repeatHits);
		}
	};

	// CopyDescriptors API calls routinely carry dozens of single-slot ranges
	// into the same destination heap. Publishing each changed slot with
	// PublishValue also pays a fetch_add on the shared heap-wide invalidation
	// token, which is a contended RMW once several worker threads copy into
	// the same ring heap. Consumers only require that token to change once
	// per completed API call - the guarantee the multi-range path already
	// provides via CommitPublishedRange - so queue the touched heaps here and
	// let the caller commit each of them once after the range walk.
	struct HeapVersionCommitSet
	{
		static constexpr size_t Capacity = 16;
		std::array<DescriptorHeapRecord *, Capacity> heaps = {};
		size_t count = 0;

		void Add(DescriptorHeapRecord *heap)
		{
			if (!heap)
				return;
			for (size_t index = 0; index < count; ++index)
				if (heaps[index] == heap)
					return;
			if (count < Capacity) {
				heaps[count++] = heap;
				return;
			}
			// Overflow keeps correct (unbatched) semantics.
			DX12DescriptorHeapStateUtils::CommitPublishedRange(heap);
		}

		void CommitAll()
		{
			for (size_t index = 0; index < count; ++index)
				DX12DescriptorHeapStateUtils::CommitPublishedRange(heaps[index]);
			count = 0;
		}
	};

	// Descriptor handle increments are device constants per heap type
	// (ID3D12Device::GetDescriptorHandleIncrementSize). Cache them per type so
	// descriptor-copy bookkeeping never pays a COM call (multi-range copy path)
	// or a global-lock heap scan (fallback path) just to step CPU handles.
	// The registry is process-global (single-device assumption), so a global
	// per-type cache is consistent with gDescriptorHeaps.
	inline static std::atomic<UINT> sIncrementByType[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};
	inline static std::atomic<bool> sIncrementResolved[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	static UINT CachedIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		if (type >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
			return 0;
		if (sIncrementResolved[type].load(std::memory_order_acquire))
			return sIncrementByType[type].load(std::memory_order_relaxed);
		return 0;
	}

	static void CacheIncrementForType(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT increment)
	{
		if (type >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || !increment)
			return;
		if (sIncrementResolved[type].load(std::memory_order_acquire))
			return;
		sIncrementByType[type].store(increment, std::memory_order_relaxed);
		sIncrementResolved[type].store(true, std::memory_order_release);
	}

	// Global set-associative CPU-handle -> heap cache. The per-thread scan
	// cache holds only the last 32 heaps, but the target workload rotates
	// through ~100 descriptor heaps per frame; every TLS miss pays the global
	// lock plus an ordered-map lookup. The multiplicative hash scatters heap
	// bases across 4096 sets (consecutive handles of one heap map to
	// consecutive sets, so a heap never thrashes its own ways). Each way is
	// fully range-validated, so an aliased way degrades to a miss and can
	// never misattribute a handle to the wrong heap (heap CPU ranges are
	// disjoint). Plain 64-bit aligned fields are atomic on x64; concurrent
	// inserters of the same handle race benignly (any winner is a valid
	// entry), and retired heaps are rejected through stateLocks->IsActive().
	static constexpr UINT HeapCacheSetCount = 4096;
	static constexpr UINT HeapCacheWayCount = 4;
	struct HeapCacheEntry
	{
		SIZE_T cpuStart = 0;
		UINT increment = 0;
		UINT numDescriptors = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		size_t heapIndex = 0;
		DescriptorHeapRecord *heap = nullptr;
	};
	struct HeapCacheSet
	{
		HeapCacheEntry ways[HeapCacheWayCount];
		UINT insertCounter = 0;
	};
	inline static HeapCacheSet heapCache[HeapCacheSetCount] = {};

	static UINT HeapCacheSetIndex(SIZE_T cpuHandle)
	{
		// Knuth multiplicative hash of the handle's 16-byte-granular bits;
		// the top 12 bits select the set.
		const UINT64 mixed =
			(static_cast<UINT64>(cpuHandle) >> 4) * 0x9E3779B97F4A7C15ull;
		return static_cast<UINT>((mixed >> 52) & (HeapCacheSetCount - 1));
	}

	static bool HeapCacheLookup(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		ResolvedHeapSlot *out)
	{
		if (!out)
			return false;
		HeapCacheSet &set = heapCache[HeapCacheSetIndex(cpuHandle)];
		for (UINT way = 0; way < HeapCacheWayCount; ++way) {
			const HeapCacheEntry &entry = set.ways[way];
			if (!entry.heap || entry.cpuStart == 0 || entry.increment == 0 ||
				(requiredType != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES &&
					entry.type != requiredType) ||
				cpuHandle < entry.cpuStart ||
				!entry.heap->stateLocks ||
				!entry.heap->stateLocks->IsActive())
				continue;
			const UINT64 rangeBytes = static_cast<UINT64>(entry.increment) *
				static_cast<UINT64>(entry.numDescriptors);
			if (static_cast<UINT64>(cpuHandle - entry.cpuStart) >= rangeBytes)
				continue;
			out->heap = entry.heap;
			out->heapIndex = entry.heapIndex;
			out->slot = static_cast<UINT>(
				(cpuHandle - entry.cpuStart) / entry.increment);
			return true;
		}
		return false;
	}

	static void HeapCacheStore(
		SIZE_T cpuHandle,
		const ResolvedHeapSlot &resolved)
	{
		if (!resolved.heap)
			return;
		HeapCacheSet &set = heapCache[HeapCacheSetIndex(cpuHandle)];
		HeapCacheEntry &entry =
			set.ways[set.insertCounter++ & (HeapCacheWayCount - 1)];
		entry.cpuStart = resolved.heap->cpuStart;
		entry.increment = resolved.heap->increment;
		entry.numDescriptors = resolved.heap->desc.NumDescriptors;
		entry.type = resolved.heap->desc.Type;
		entry.heapIndex = resolved.heapIndex;
		entry.heap = resolved.heap;
	}

	static SRWLOCK *HeapLock(DescriptorHeapRecord *heap, UINT slot)
	{
		return heap && heap->stateLocks ?
			heap->stateLocks->LockForSlot(slot) : nullptr;
	}

	class RangeCopyMemo
	{
	public:
		class Entry
		{
		public:
			const void *sourceHeap = nullptr;
			const void *destinationHeap = nullptr;
			UINT sourceSlot = 0;
			UINT destinationSlot = 0;
			UINT count = 0;
			UINT sourceFound = 0;
			UINT sourceMissing = 0;
			UINT unchanged = 0;
			UINT alreadyEmpty = 0;
			std::vector<UINT64> sourceVersions;
			std::vector<UINT64> destinationVersions;
			bool occupied = false;
		};

		bool TryLoad(
			const DescriptorHeapRecord &sourceHeap,
			UINT sourceSlot,
			const DescriptorHeapRecord &destinationHeap,
			UINT destinationSlot,
			UINT count,
			UINT *sourceFound,
			UINT *sourceMissing,
			UINT *unchanged,
			UINT *alreadyEmpty) const
		{
			const Entry &entry = entries[EntryIndex(
				&sourceHeap,
				sourceSlot,
				&destinationHeap,
				destinationSlot,
				count)];
			if (!entry.occupied ||
				entry.sourceHeap != &sourceHeap ||
				entry.destinationHeap != &destinationHeap ||
				entry.sourceSlot != sourceSlot ||
				entry.destinationSlot != destinationSlot ||
				entry.count != count ||
				entry.sourceVersions.size() != count ||
				entry.destinationVersions.size() != count)
				return false;
			for (UINT index = 0; index < count; ++index) {
				if (sourceHeap.stateLocks->ReadSlotVersion(sourceSlot + index) !=
						entry.sourceVersions[index] ||
					destinationHeap.stateLocks->ReadSlotVersion(destinationSlot + index) !=
						entry.destinationVersions[index])
					return false;
			}
			if (sourceFound)
				*sourceFound = entry.sourceFound;
			if (sourceMissing)
				*sourceMissing = entry.sourceMissing;
			if (unchanged)
				*unchanged = entry.unchanged;
			if (alreadyEmpty)
				*alreadyEmpty = entry.alreadyEmpty;
			return true;
		}

		void Store(
			const DescriptorHeapRecord &sourceHeap,
			UINT sourceSlot,
			const DescriptorHeapRecord &destinationHeap,
			UINT destinationSlot,
			UINT count,
			UINT sourceFound,
			UINT sourceMissing,
			UINT unchanged,
			UINT alreadyEmpty)
		{
			Entry &entry = entries[EntryIndex(
				&sourceHeap,
				sourceSlot,
				&destinationHeap,
				destinationSlot,
				count)];
			entry.sourceHeap = &sourceHeap;
			entry.destinationHeap = &destinationHeap;
			entry.sourceSlot = sourceSlot;
			entry.destinationSlot = destinationSlot;
			entry.count = count;
			entry.sourceFound = sourceFound;
			entry.sourceMissing = sourceMissing;
			entry.unchanged = unchanged;
			entry.alreadyEmpty = alreadyEmpty;
			entry.sourceVersions.resize(count);
			entry.destinationVersions.resize(count);
			for (UINT index = 0; index < count; ++index) {
				entry.sourceVersions[index] =
					sourceHeap.stateLocks->ReadSlotVersion(sourceSlot + index);
				entry.destinationVersions[index] =
					destinationHeap.stateLocks->ReadSlotVersion(destinationSlot + index);
			}
			entry.occupied = true;
		}

	private:
		static constexpr size_t EntryCount = 1024;

		static size_t EntryIndex(
			const void *sourceHeap,
			UINT sourceSlot,
			const void *destinationHeap,
			UINT destinationSlot,
			UINT count)
		{
			UINT64 hash = static_cast<UINT64>(reinterpret_cast<uintptr_t>(sourceHeap)) >> 4;
			hash ^= static_cast<UINT64>(reinterpret_cast<uintptr_t>(destinationHeap)) *
				0x9e3779b97f4a7c15ull;
			hash ^= (static_cast<UINT64>(sourceSlot) << 32) | destinationSlot;
			hash ^= static_cast<UINT64>(count) * 0xc2b2ae3d27d4eb4full;
			hash ^= hash >> 33;
			hash *= 0xff51afd7ed558ccdull;
			hash ^= hash >> 33;
			return static_cast<size_t>(hash) & (EntryCount - 1);
		}

		std::array<Entry, EntryCount> entries = {};
	};

	static RangeCopyMemo &RangeMemo()
	{
		static thread_local RangeCopyMemo memo;
		return memo;
	}

	// gDescriptorLock must be held while resolving this pointer. The lock object
	// itself is heap allocated, and the owning deque keeps record addresses stable.
	static SRWLOCK *HeapLock(size_t heapIndex, UINT slot)
	{
		if (heapIndex >= gDescriptorHeaps.size() ||
			!gDescriptorHeaps[heapIndex].stateLocks)
			return nullptr;
		return gDescriptorHeaps[heapIndex].stateLocks->LockForSlot(slot);
	}


	static void LockShared(size_t heapIndex, UINT slot)
	{
		SRWLOCK *lock = HeapLock(heapIndex, slot);
		if (lock)
			DX12_LOCK_ACQUIRE_SHARED(lock, "Desc.HeapShared");
	}

	static void UnlockShared(size_t heapIndex, UINT slot)
	{
		SRWLOCK *lock = HeapLock(heapIndex, slot);
		if (lock)
			ReleaseSRWLockShared(lock);
	}

	// ---- Handle -> slot resolution ----

	static bool ResolveHeapSlot(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		size_t *outHeapIndex,
		UINT *outSlot)
	{
		if (!cpuHandle || !outHeapIndex || !outSlot) {
			return false;
		}

		std::map<SIZE_T, size_t>::const_iterator heapByStart =
			gDescriptorHeapByCpuStart.upper_bound(cpuHandle);
		if (heapByStart == gDescriptorHeapByCpuStart.begin()) {
			return false;
		}
		--heapByStart;

		const size_t heapIndex = heapByStart->second;
		if (heapIndex >= gDescriptorHeaps.size()) {
			return false;
		}

		const DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		if (heap.cpuStart == 0 || heap.increment == 0) {
			return false;
		}

		if (cpuHandle < heap.cpuStart) {
			return false;
		}

		const SIZE_T offset = cpuHandle - heap.cpuStart;
		const UINT slot = static_cast<UINT>(offset / heap.increment);
		if (slot >= heap.desc.NumDescriptors) {
			return false;
		}

		if (requiredType != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES &&
			heap.desc.Type != requiredType) {
			return false;
		}

		*outHeapIndex = heapIndex;
		*outSlot = slot;
		return true;
	}

	static bool ResolveHeapSlotFast(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		ResolvedHeapSlot *out)
	{
		if (!cpuHandle || !out)
			return false;
		// Sequential-handle prediction: descriptor rings are usually written
		// with monotonically increasing handles, so the very next handle
		// resolves to the very next slot of the same heap. This skips the
		// 32-entry TLS scan for the dominant CBV/copy ring pattern. The memo
		// is refreshed on every successful resolve below and validated against
		// the same invariants the cache scan uses (activity, type, range).
		const SequentialResolveMemo &memo = sSequentialResolve;
		if (memo.heap && memo.handle != 0 &&
				cpuHandle == memo.handle + memo.increment &&
				memo.slot + 1 < memo.numDescriptors &&
				(requiredType == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ||
					memo.type == requiredType) &&
				memo.heap->stateLocks &&
				memo.heap->stateLocks->IsActive()) {
			out->heap = memo.heap;
			out->heapIndex = memo.heapIndex;
			out->slot = memo.slot + 1;
			sSequentialResolve.handle = cpuHandle;
			sSequentialResolve.slot = out->slot;
			return true;
		}
		static thread_local HeapResolveTlsCache cache;
		for (size_t index = 0; index < cache.entries.size(); ++index) {
			HeapResolveCacheEntry &entry = cache.entries[index];
			// Reject non-candidates using plain TLS data first. IsActive() is an
			// acquire atomic and was previously paid for every entry in the
			// 32-element cache, twice per descriptor copy.
			if (!entry.heap || entry.cpuStart == 0 || entry.increment == 0 ||
				cpuHandle < entry.cpuStart ||
				(requiredType != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES &&
					entry.type != requiredType))
				continue;
			const SIZE_T offset = cpuHandle - entry.cpuStart;
			const UINT64 rangeBytes = static_cast<UINT64>(entry.increment) *
				static_cast<UINT64>(entry.numDescriptors);
			if (static_cast<UINT64>(offset) >= rangeBytes ||
				!entry.heap->stateLocks ||
				!entry.heap->stateLocks->IsActive())
				continue;
			const UINT64 slot = offset / entry.increment;
			out->heap = entry.heap;
			out->heapIndex = entry.heapIndex;
			out->slot = static_cast<UINT>(slot);
			// Descriptor traffic normally alternates between one staging heap and
			// one shader-visible heap. Promote the hit so that pattern resolves in
			// one or two probes instead of scanning all cached heaps under /Od.
			// Entry 0/1 normally form the stable destination/source heap pair.
			// Swapping an index-1 hit made alternating CopyDescriptors traffic
			// exchange both cache records twice for every copied slot.
			if (index > 1)
				std::swap(cache.entries[0], cache.entries[index]);
			RememberSequentialResolve(cpuHandle, *out);
			return true;
		}

		// Second level: global set-associative cache shared by all threads.
		// Also insert into the TLS cache with an O(1) round-robin victim so
		// the scan cache keeps learning new heaps; a full shift-insert here
		// would cost a 32-entry memmove on every second-level hit.
		if (HeapCacheLookup(cpuHandle, requiredType, out)) {
			DX12_PROF_COUNT("Desc.HeapCacheHit");
			static thread_local UINT tlsVictim = 0;
			HeapResolveCacheEntry &victim = cache.entries[
				tlsVictim++ & (HeapResolveTlsCache::EntryCount - 1)];
			victim.heap = out->heap;
			victim.heapIndex = out->heapIndex;
			victim.cpuStart = out->heap->cpuStart;
			victim.increment = out->heap->increment;
			victim.numDescriptors = out->heap->desc.NumDescriptors;
			victim.type = out->heap->desc.Type;
			RememberSequentialResolve(cpuHandle, *out);
			return true;
		}

		DX12_PROF_COUNT("Desc.ResolveMiss");
		DX12_LOCK_ACQUIRE_SHARED(&gDescriptorLock, "Desc.GlobalShared");
		size_t heapIndex = 0;
		UINT slot = 0;
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot)) {
			DX12_PROF_COUNT("Desc.ResolveFailure");
			ReleaseSRWLockShared(&gDescriptorLock);
			return false;
		}
		DescriptorHeapRecord *heap = &gDescriptorHeaps[heapIndex];
		static_assert(HeapResolveTlsCache::EntryCount >= 2,
			"heap resolve cache needs one MRU and at least one victim");
		for (size_t index = HeapResolveTlsCache::EntryCount - 1;
				index > 0; --index)
			cache.entries[index] = cache.entries[index - 1];
		HeapResolveCacheEntry &entry = cache.entries[0];
		entry.heap = heap;
		entry.heapIndex = heapIndex;
		entry.cpuStart = heap->cpuStart;
		entry.increment = heap->increment;
		entry.numDescriptors = heap->desc.NumDescriptors;
		entry.type = heap->desc.Type;
		out->heap = heap;
		out->heapIndex = heapIndex;
		out->slot = slot;
		HeapCacheStore(cpuHandle, *out);
		ReleaseSRWLockShared(&gDescriptorLock);
		RememberSequentialResolve(cpuHandle, *out);
		return true;
	}

private:
	// Refresh the sequential-handle prediction memo from a fully validated
	// resolve. The remembered heap record stays alive in gDescriptorHeaps for
	// the process lifetime; the activity check on the prediction path rejects
	// retired heaps.
	struct SequentialResolveMemo
	{
		SIZE_T handle = 0;
		UINT slot = 0;
		UINT increment = 0;
		UINT numDescriptors = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		size_t heapIndex = 0;
		DescriptorHeapRecord *heap = nullptr;
	};
	inline static thread_local SequentialResolveMemo sSequentialResolve;

	static void RememberSequentialResolve(
		SIZE_T cpuHandle, const ResolvedHeapSlot &resolved)
	{
		if (!resolved.heap)
			return;
		SequentialResolveMemo &memo = sSequentialResolve;
		memo.handle = cpuHandle;
		memo.slot = resolved.slot;
		memo.increment = resolved.heap->increment;
		memo.numDescriptors = resolved.heap->desc.NumDescriptors;
		memo.type = resolved.heap->desc.Type;
		memo.heapIndex = resolved.heapIndex;
		memo.heap = resolved.heap;
	}

public:

	static bool ResolveAndLockHeapSlot(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		bool exclusive,
		ResolvedHeapSlot *out)
	{
		if (!out)
			return false;
		for (UINT attempt = 0; attempt < 2; ++attempt) {
			ResolvedHeapSlot resolved;
			if (!ResolveHeapSlotFast(cpuHandle, requiredType, &resolved))
				return false;
			SRWLOCK *lock = HeapLock(resolved.heap, resolved.slot);
			if (!lock)
				return false;
			if (exclusive)
				DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, "Desc.HeapExclusive");
			else
				DX12_LOCK_ACQUIRE_SHARED(lock, "Desc.HeapShared");
			if (resolved.heap->stateLocks->IsActive()) {
				*out = resolved;
				return true;
			}
			if (exclusive)
				ReleaseSRWLockExclusive(lock);
			else
				ReleaseSRWLockShared(lock);
		}
		return false;
	}

	// Lock a slot which has already been resolved by the caller.  Creation hot
	// paths first consult the lock-free published identity and used to resolve
	// the same CPU handle a second time on every actual change.  Retiring a heap
	// is still handled safely by validating IsActive() after acquiring its shard.
	static bool LockResolvedHeapSlotExclusive(
		const ResolvedHeapSlot &resolved,
		ResolvedHeapSlot *out)
	{
		if (!out || !resolved.heap || !resolved.heap->stateLocks)
			return false;
		SRWLOCK *lock = HeapLock(resolved.heap, resolved.slot);
		if (!lock)
			return false;
		DX12_LOCK_ACQUIRE_EXCLUSIVE(lock, "Desc.HeapExclusive");
		if (!resolved.heap->stateLocks->IsActive()) {
			ReleaseSRWLockExclusive(lock);
			return false;
		}
		*out = resolved;
		return true;
	}

	static void UnlockHeapSlot(const ResolvedHeapSlot &resolved, bool exclusive)
	{
		SRWLOCK *lock = HeapLock(resolved.heap, resolved.slot);
		if (!lock)
			return;
		if (exclusive)
			ReleaseSRWLockExclusive(lock);
		else
			ReleaseSRWLockShared(lock);
	}

	// ---- Unified Descriptor State recording ----
	// Known heaps own their current values. The global CPU ledger is only a
	// compatibility Adapter for handles that cannot be assigned to a heap.
	static bool RecordCreatedDescriptorIfChanged(DescriptorRecord &&record)
	{
		if (!record.cpuHandle || record.valueId == 0) {
			ReleaseDescriptorRecordResources(&record);
			return false;
		}

		const SIZE_T cpuHandle = record.cpuHandle;
		ResolvedHeapSlot resolved;
		const bool resolvedKnownHeap = ResolveHeapSlotFast(
			cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
			&resolved);
		if (resolvedKnownHeap && resolved.heap && resolved.heap->stateLocks &&
			resolved.heap->stateLocks->PublishedSlotValueEquals(
				resolved.slot,
				record.valueId)) {
			ReleaseDescriptorRecordResources(&record);
			return false;
		}
		// Lock-free dense write: materialized heaps have stable arrays and the
		// seqlock publication makes the write visible to lock-free readers.
		if (resolvedKnownHeap && resolved.heap &&
				resolved.heap->IsMaterialized() &&
				resolved.heap->stateLocks &&
				resolved.heap->stateLocks->IsActive()) {
			DescriptorHeapRecord &heap = *resolved.heap;
			const bool changed =
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&heap, resolved.slot, record, cpuHandle) ==
					DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored;
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
			ReleaseDescriptorRecordResources(&record);
			return changed;
		}
		// Chunked lock-free write: normal Mod mode keeps heaps unmaterialized,
		// so the per-chunk dense store provides the same single-writer seqlock
		// protocol as the materialized path. This fast path is only enabled
		// outside Dump/Hunt tracking, where whole-heap materialization can
		// never run concurrently with chunked accesses. Chunk presence is
		// handled by StoreCurrentRecordIfChanged itself: a missing chunk
		// yields LockRequired and falls through to the locked allocation path.
		if (resolvedKnownHeap && resolved.heap &&
				resolved.heap->stateLocks &&
				resolved.heap->stateLocks->IsActive() &&
				!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking()) {
			DescriptorHeapRecord &heap = *resolved.heap;
			const DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult result =
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&heap, resolved.slot, record, cpuHandle);
			if (result ==
					DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored) {
				// A hunt/dump activation can materialize this heap while this
				// write is in flight (the mode gate above was still clean).
				// Redo under the shard lock so the materialized dense adapter
				// - now authoritative - holds the record, and publish
				// unconditionally; otherwise publish lock-free.
				if (!heap.IsMaterialized()) {
					DX12DescriptorHeapStateUtils::PublishValue(
						&heap, resolved.slot, record.valueId);
					ReleaseDescriptorRecordResources(&record);
					return true;
				}
				ResolvedHeapSlot locked;
				bool lockedKnownHeap =
					LockResolvedHeapSlotExclusive(resolved, &locked);
				if (!lockedKnownHeap)
					lockedKnownHeap = ResolveAndLockHeapSlot(
						cpuHandle,
						D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
						true,
						&locked);
				if (lockedKnownHeap) {
					DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
						locked.heap, locked.slot, record, cpuHandle);
					DX12DescriptorHeapStateUtils::BumpVersion(
						locked.heap, locked.slot);
					UnlockHeapSlot(locked, true);
					ReleaseDescriptorRecordResources(&record);
				} else {
					// Heap retired mid-write: keep the value reachable through
					// the CPU ledger, matching the slow-path fallback.
					WriteToCpuLedger(std::move(record));
				}
				return true;
			}
			if (result ==
					DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Unchanged) {
				ReleaseDescriptorRecordResources(&record);
				return false;
			}
			// LockRequired: fall through to the locked path. Races a chunk
			// first-touch only; chunks never disappear while the heap is
			// active.
		}
		ResolvedHeapSlot locked;
		bool lockedKnownHeap = resolvedKnownHeap &&
			LockResolvedHeapSlotExclusive(resolved, &locked);
		if (!lockedKnownHeap)
			lockedKnownHeap = ResolveAndLockHeapSlot(
				cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				true,
				&locked);
		if (!lockedKnownHeap) {
			WriteToCpuLedger(std::move(record));
			return true;
		}
		resolved = locked;

		DescriptorHeapRecord &heap = *resolved.heap;
		if (!heap.IsMaterialized())
			heap.chunkedSlots.EnsureChunkForSlot(resolved.slot);
		const bool changed =
			DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
				&heap, resolved.slot, record, cpuHandle) ==
				DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored;
		if (changed)
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
		UnlockHeapSlot(resolved, true);

		ReleaseDescriptorRecordResources(&record);
		return changed;
	}

	// Keep CBV recording on the common descriptor path so slot updates,
	// deduplication, and heap-version invalidation have one implementation.
	static bool RecordCreatedConstantBufferViewIfChanged(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
	{
		if (!desc || !destDescriptor.ptr)
			return false;

		const UINT64 valueId =
			DX12DescriptorValueHashUtils::HashConstantBufferView(*desc);
		// Unchanged early-out without materializing the 264-byte record: the
		// game re-creates identical CBVs into ring slots thousands of times
		// per frame, and the published-value probe only needs the hash.
		ResolvedHeapSlot resolved;
		if (ResolveHeapSlotFast(
				destDescriptor.ptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&resolved) &&
			resolved.heap && resolved.heap->stateLocks &&
			resolved.heap->stateLocks->PublishedSlotValueEquals(
				resolved.slot, valueId)) {
			return false;
		}

		DescriptorRecord record;
		SetDescriptorKind(&record, DescriptorKind::CBV_ID);
		record.valueId = valueId;
		record.cpuHandle = destDescriptor.ptr;
		record.cbv = *desc;
		record.hasDesc = true;
		record.gpuVirtualAddress = desc->BufferLocation;
		record.viewSize = desc->SizeInBytes;
		return RecordCreatedDescriptorIfChanged(std::move(record));
	}

	// Unified entry point for Create*View hooks.
	// Tries heap-slot fast path first; falls back to CPU ledger if the handle
	// is not in any known descriptor heap.
	static void RecordDescriptor(DescriptorRecord &&record)
	{
		RecordCreatedDescriptorIfChanged(std::move(record));
	}

	static bool WriteDescriptorToSlot(
		SIZE_T cpuHandle,
		DescriptorRecord &&record)
	{
		ResolvedHeapSlot resolved;
		if (!ResolveHeapSlotFast(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &resolved)) {
			return false;
		}
		DescriptorHeapRecord &heap = *resolved.heap;
		if (heap.IsMaterialized()) {
			const bool changed =
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&heap, resolved.slot, record, cpuHandle) ==
					DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored;
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
			ReleaseDescriptorRecordResources(&record);
			return true;
		}
		// Chunked lock-free write (normal Mod mode); see
		// RecordCreatedDescriptorIfChanged for the gating rationale. A missing
		// chunk yields LockRequired and falls through to the locked path.
		if (heap.stateLocks &&
				heap.stateLocks->IsActive() &&
				!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking()) {
			const DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult result =
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&heap, resolved.slot, record, cpuHandle);
			if (result ==
					DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored &&
					!heap.IsMaterialized()) {
				DX12DescriptorHeapStateUtils::PublishValue(
					&heap, resolved.slot, record.valueId);
				ReleaseDescriptorRecordResources(&record);
				return true;
			}
			// Stored but a hunt/dump materialization landed mid-write, or the
			// slot was unchanged: fall through to the locked path, which
			// ensures the authoritative store receives the record and
			// publishes it.
		}

		if (!ResolveAndLockHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, true, &resolved)) {
			return false;
		}
		if (!heap.IsMaterialized())
			heap.chunkedSlots.EnsureChunkForSlot(resolved.slot);

		const bool changed =
			DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
				&heap, resolved.slot, record, cpuHandle) ==
				DX12DescriptorSlotArrayUtils::StoreCurrentSlotResult::Stored;
		if (changed)
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
		else if (heap.IsMaterialized())
			// Raced a materialization after a lock-free chunk write: the dense
			// adapter already holds the record from the lock-free pass, but the
			// identity must be republished so readers see the new value.
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);

		UnlockHeapSlot(resolved, true);
		ReleaseDescriptorRecordResources(&record);
		return true;
	}

	static bool ReadDescriptorFromSlot(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		DescriptorRecord *outRecord)
	{
		if (!outRecord) {
			return false;
		}

		ResolvedHeapSlot resolved;
		if (!ResolveHeapSlotFast(cpuHandle, requiredType, &resolved))
			return false;
		DescriptorHeapRecord &heap = *resolved.heap;
		if (heap.IsMaterialized()) {
			// Lock-free read: copy the dense slot under a seqlock identity
			// bracket and accept the result only when the version is stable.
			// Writers publish via the same identity protocol, so no shard
			// lock is required for materialized heaps.
			for (UINT attempt = 0; attempt < 4; ++attempt) {
				DescriptorHeapRecord::StateLocks::SlotIdentity before;
				if (!heap.stateLocks ||
						!heap.stateLocks->ObserveSlotIdentity(
							resolved.slot, &before))
					break;
				DescriptorRecord record;
				const bool found =
					DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
						heap, resolved.slot, &record);
				DescriptorHeapRecord::StateLocks::SlotIdentity after;
				if (heap.stateLocks->ObserveSlotIdentity(
						resolved.slot, &after) &&
						before.version == after.version) {
					if (found)
						*outRecord = std::move(record);
					return found;
				}
			}
			return false;
		}

		// Chunked lock-free read (normal Mod mode): writers publish the same
		// per-slot seqlock identity, so the read needs no shard lock. Gated on
		// !NeedsDumpOrHuntingTracking like the chunked write path so a
		// whole-heap materialization can never race the chunked store.
		if (heap.stateLocks &&
				!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking() &&
				heap.chunkedSlots.ChunkExists(resolved.slot)) {
			for (UINT attempt = 0; attempt < 4; ++attempt) {
				DescriptorHeapRecord::StateLocks::SlotIdentity before;
				if (!heap.stateLocks->ObserveSlotIdentity(
						resolved.slot, &before))
					break;
				DescriptorRecord record;
				const bool found =
					DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
						heap, resolved.slot, &record);
				DescriptorHeapRecord::StateLocks::SlotIdentity after;
				if (heap.stateLocks->ObserveSlotIdentity(
						resolved.slot, &after) &&
						before.version == after.version) {
					if (found)
						*outRecord = std::move(record);
					return found;
				}
			}
			return false;
		}

		// Sparse heap or in-flight materialization: locked read path.
		if (!ResolveAndLockHeapSlot(
				cpuHandle, requiredType, false, &resolved))
			return false;
		const bool found =
			DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
				*resolved.heap, resolved.slot, outRecord);
		UnlockHeapSlot(resolved, false);
		return found;
	}

	// Variant for batched callers that already hold gDescriptorLock shared.
	static bool ReadDescriptorFromSlotLocked(
		SIZE_T cpuHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE requiredType,
		DescriptorRecord *outRecord)
	{
		if (!outRecord)
			return false;
		size_t heapIndex = 0;
		UINT slot = 0;
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot))
			return false;
		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		if (heap.IsMaterialized()) {
			// Lock-free seqlock read (same protocol as ReadDescriptorFromSlot).
			for (UINT attempt = 0; attempt < 4; ++attempt) {
				DescriptorHeapRecord::StateLocks::SlotIdentity before;
				if (!heap.stateLocks ||
						!heap.stateLocks->ObserveSlotIdentity(
							slot, &before))
					break;
				DescriptorRecord record;
				const bool found =
					DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
						heap, slot, &record);
				DescriptorHeapRecord::StateLocks::SlotIdentity after;
				if (heap.stateLocks->ObserveSlotIdentity(
						slot, &after) &&
						before.version == after.version) {
					if (found)
						*outRecord = std::move(record);
					return found;
				}
			}
			return false;
		}
		// Chunked lock-free read (normal Mod mode); the caller already holds
		// gDescriptorLock shared, but the seqlock read itself needs no lock.
		if (heap.stateLocks &&
				!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking() &&
				heap.chunkedSlots.ChunkExists(slot)) {
			for (UINT attempt = 0; attempt < 4; ++attempt) {
				DescriptorHeapRecord::StateLocks::SlotIdentity before;
				if (!heap.stateLocks->ObserveSlotIdentity(slot, &before))
					break;
				DescriptorRecord record;
				const bool found =
					DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
						heap, slot, &record);
				DescriptorHeapRecord::StateLocks::SlotIdentity after;
				if (heap.stateLocks->ObserveSlotIdentity(slot, &after) &&
						before.version == after.version) {
					if (found)
						*outRecord = std::move(record);
					return found;
				}
			}
			return false;
		}
		LockShared(heapIndex, slot);
		const bool found =
			DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
				heap, slot, outRecord);
		UnlockShared(heapIndex, slot);
		return found;
	}

	// ---- Copy range (heap-slot fast path + CPU ledger fallback) ----
	enum class CopyIdentityResult : uint8_t
	{
		Unknown,
		Unchanged,
		SourceMissingDestinationEmpty
	};

	static CopyIdentityResult ComparePublishedSlotIdentities(
		const DescriptorHeapRecord &sourceHeap,
		UINT sourceSlot,
		const DescriptorHeapRecord &destinationHeap,
		UINT destinationSlot,
		UINT64 *sourceVersionOut = nullptr,
		UINT64 *destinationVersionOut = nullptr)
	{
		// Both endpoints came from ResolveHeapSlotFast, which has already checked
		// activity. The identities below are seqlock snapshots; repeating the two
		// activity atomics for every copied descriptor provided no stronger value
		// consistency guarantee.
		if (!sourceHeap.stateLocks || !destinationHeap.stateLocks)
			return CopyIdentityResult::Unknown;
		DescriptorHeapRecord::StateLocks::SlotIdentity sourceIdentity;
		DescriptorHeapRecord::StateLocks::SlotIdentity destinationIdentity;
		if (!sourceHeap.stateLocks->ObserveSlotIdentity(
				sourceSlot, &sourceIdentity) ||
				!destinationHeap.stateLocks->ObserveSlotIdentity(
					destinationSlot, &destinationIdentity) ||
				sourceIdentity.valueId != destinationIdentity.valueId)
			return CopyIdentityResult::Unknown;
		if (sourceVersionOut)
			*sourceVersionOut = sourceIdentity.version;
		if (destinationVersionOut)
			*destinationVersionOut = destinationIdentity.version;
		return sourceIdentity.valueId ?
			CopyIdentityResult::Unchanged :
			CopyIdentityResult::SourceMissingDestinationEmpty;
	}

	static bool ComparePublishedRangeIdentities(
		const DescriptorHeapRecord &sourceHeap,
		UINT sourceSlot,
		const DescriptorHeapRecord &destinationHeap,
		UINT destinationSlot,
		UINT count,
		UINT *sourceFound,
		UINT *sourceMissing,
		UINT *unchanged,
		UINT *alreadyEmpty)
	{
		if (!count || !sourceHeap.stateLocks || !destinationHeap.stateLocks)
			return false;
		UINT localSourceFound = 0;
		UINT localSourceMissing = 0;
		UINT localUnchanged = 0;
		UINT localAlreadyEmpty = 0;
		for (UINT index = 0; index < count; ++index) {
			DescriptorHeapRecord::StateLocks::SlotIdentity sourceIdentity;
			DescriptorHeapRecord::StateLocks::SlotIdentity destinationIdentity;
			if (!sourceHeap.stateLocks->ObserveSlotIdentity(
					sourceSlot + index, &sourceIdentity) ||
				!destinationHeap.stateLocks->ObserveSlotIdentity(
					destinationSlot + index, &destinationIdentity) ||
				sourceIdentity.valueId != destinationIdentity.valueId) {
				return false;
			}
			if (sourceIdentity.valueId) {
				++localSourceFound;
				++localUnchanged;
			} else {
				++localSourceMissing;
				++localAlreadyEmpty;
			}
		}
		if (sourceFound)
			*sourceFound = localSourceFound;
		if (sourceMissing)
			*sourceMissing = localSourceMissing;
		if (unchanged)
			*unchanged = localUnchanged;
		if (alreadyEmpty)
			*alreadyEmpty = localAlreadyEmpty;
		return true;
	}

	// ---- Find latest descriptor (2-tier lookup) ----

	static bool FindLatestDescriptor(
		SIZE_T cpuHandle,
		DescriptorRecord *outRecord)
	{
		if (!cpuHandle || !outRecord) {
			return false;
		}

		// Tier 1: try heap slot (per-heap shared lock)
		DescriptorRecord temp;
		if (ReadDescriptorFromSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &temp)) {
			*outRecord = CloneDescriptorRecord(temp);
			return true;
		}

		// Tier 2: try CPU ledger (global shared lock)
		DX12_LOCK_ACQUIRE_SHARED(&gDescriptorLock, "Desc.GlobalShared");
		std::unordered_map<SIZE_T, DescriptorRecord>::const_iterator found =
			gDescriptorByCpuHandle.find(cpuHandle);
		if (found != gDescriptorByCpuHandle.end()) {
			*outRecord = CloneDescriptorRecord(found->second);
			ReleaseSRWLockShared(&gDescriptorLock);
			return true;
		}
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}

	static bool EraseDescriptor(SIZE_T cpuHandle)
	{
		if (!cpuHandle)
			return false;
		ResolvedHeapSlot resolved;
		if (ResolveHeapSlotFast(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &resolved)) {
			DescriptorHeapRecord &heap = *resolved.heap;
			const bool chunkedErase = heap.stateLocks &&
				heap.stateLocks->IsActive() &&
				!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking() &&
				heap.chunkedSlots.ChunkExists(resolved.slot);
			if (heap.IsMaterialized() || chunkedErase) {
				const bool erased =
					DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
						&heap, resolved.slot);
				if (erased) {
					DX12DescriptorHeapStateUtils::BumpVersion(
						&heap, resolved.slot);
				}
				// A hunt/dump materialization can land mid-erase; redo under
				// the shard lock so the dense adapter (now authoritative)
				// reflects the erase and republishes the identity.
				if (chunkedErase && heap.IsMaterialized()) {
					ResolvedHeapSlot locked;
					if (LockResolvedHeapSlotExclusive(resolved, &locked)) {
						DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
							locked.heap, locked.slot);
						DX12DescriptorHeapStateUtils::BumpVersion(
							locked.heap, locked.slot);
						UnlockHeapSlot(locked, true);
					}
				}
				return erased;
			}
		}
		if (ResolveAndLockHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, true, &resolved)) {
			const bool erased = DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
				resolved.heap, resolved.slot);
			if (erased) {
				DX12DescriptorHeapStateUtils::BumpVersion(
					resolved.heap, resolved.slot);
			}
			UnlockHeapSlot(resolved, true);
			return erased;
		}

		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gDescriptorLock, "Desc.GlobalExclusive");
		auto found = gDescriptorByCpuHandle.find(cpuHandle);
		if (found == gDescriptorByCpuHandle.end()) {
			ReleaseSRWLockExclusive(&gDescriptorLock);
			return false;
		}
		ReleaseDescriptorRecordResources(&found->second);
		gDescriptorByCpuHandle.erase(found);
		BumpDescriptorQuerySerial();
		ReleaseSRWLockExclusive(&gDescriptorLock);
		return true;
	}

	// ---- Serial number bump ----
	static void BumpDescriptorQuerySerial()
	{
		UINT64 current = gDescriptorQuerySerial.load(std::memory_order_relaxed);
		for (;;) {
			UINT64 next = (current == UINT64_MAX) ? 1 : current + 1;
			if (gDescriptorQuerySerial.compare_exchange_weak(
					current, next,
					std::memory_order_release,
					std::memory_order_relaxed)) {
				return;
			}
		}
	}

	// ---- No-op visible check ----
	static void EnsureVisible(const SIZE_T *, size_t) {}

	// ---- CPU ledger fallback ----
	static void WriteToCpuLedger(DescriptorRecord &&record)
	{
		if (!record.cpuHandle) {
			ReleaseDescriptorRecordResources(&record);
			return;
		}

		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gDescriptorLock, "Desc.GlobalExclusive");
		std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
			gDescriptorByCpuHandle.find(record.cpuHandle);
		if (found != gDescriptorByCpuHandle.end()) {
			ReleaseDescriptorRecordResources(&found->second);
		}
		gDescriptorByCpuHandle[record.cpuHandle] = std::move(record);
		BumpDescriptorQuerySerial();
		ReleaseSRWLockExclusive(&gDescriptorLock);
	}
};


