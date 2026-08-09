// ============================================================
// DX12Runtime.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Tracking.h"

// ===================== Runtime/DX12DeviceRuntime.hpp =====================


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
			DX12DescriptorShadowTelemetryUtils::RecordCopyBatch(
				rangeCalls, slots, singleRanges, multiRanges,
				resolvedRanges, sameHeapRanges, overlapRanges,
				sourceFound, sourceMissing, changed, unchanged,
				erased, alreadyEmpty, memoHits, memoMisses,
				repeatHits);
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

	// ---- Lock helpers (per-heap descriptor shard) ----
	static void LockExclusive(size_t heapIndex, UINT slot)
	{
		SRWLOCK *lock = HeapLock(heapIndex, slot);
		if (lock)
			DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(lock);
	}

	static void UnlockExclusive(size_t heapIndex, UINT slot)
	{
		SRWLOCK *lock = HeapLock(heapIndex, slot);
		if (lock)
			ReleaseSRWLockExclusive(lock);
	}

	static void LockShared(size_t heapIndex, UINT slot)
	{
		SRWLOCK *lock = HeapLock(heapIndex, slot);
		if (lock)
			DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(lock);
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
			return true;
		}

		// Second level: global set-associative cache shared by all threads.
		// Also insert into the TLS cache with an O(1) round-robin victim so
		// the scan cache keeps learning new heaps; a full shift-insert here
		// would cost a 32-entry memmove on every second-level hit.
		if (HeapCacheLookup(cpuHandle, requiredType, out)) {
			DX12DescriptorShadowTelemetryUtils::RecordHeapCacheHit();
			static thread_local UINT tlsVictim = 0;
			HeapResolveCacheEntry &victim = cache.entries[
				tlsVictim++ & (HeapResolveTlsCache::EntryCount - 1)];
			victim.heap = out->heap;
			victim.heapIndex = out->heapIndex;
			victim.cpuStart = out->heap->cpuStart;
			victim.increment = out->heap->increment;
			victim.numDescriptors = out->heap->desc.NumDescriptors;
			victim.type = out->heap->desc.Type;
			return true;
		}

		DX12DescriptorShadowTelemetryUtils::RecordResolveMiss();
		DX12DescriptorShadowTelemetryUtils::AcquireGlobalShared(&gDescriptorLock);
		size_t heapIndex = 0;
		UINT slot = 0;
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot)) {
			DX12DescriptorShadowTelemetryUtils::RecordResolveFailure();
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
		return true;
	}

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
				DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(lock);
			else
				DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(lock);
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
		DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(lock);
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
					&heap, resolved.slot, record, cpuHandle);
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
			ReleaseDescriptorRecordResources(&record);
			return changed;
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
		const bool changed =
			DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
				&heap, resolved.slot, record, cpuHandle);
		if (changed)
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
		UnlockHeapSlot(resolved, true);

		ReleaseDescriptorRecordResources(&record);
		return changed;
	}

	// CBVs dominate descriptor creation in the target workload. Build the same
	// content hash as the generic DescriptorRecord path, but keep the hot path
	// free of std::string construction and full-record materialization.
	static bool RecordCreatedConstantBufferViewIfChanged(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
	{
		if (!desc || !destDescriptor.ptr)
			return false;

		// CBV content-hash TLS cache: games re-create the same constant
		// buffer views every frame (ZZZ issues ~18K CreateCBV per frame, most
		// repeating the same BufferLocation/SizeInBytes). Hashing the desc is
		// cheap, but 18K hash computations per frame still add up; caching
		// the last desc + hash skips the Mix chain when the view repeats.
		struct CbvHashCache
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			UINT64 valueId = 0;
			bool valid = false;
		};
		static thread_local CbvHashCache hashCache;
		UINT64 valueId = 0;
		if (hashCache.valid &&
			hashCache.desc.BufferLocation == desc->BufferLocation &&
			hashCache.desc.SizeInBytes == desc->SizeInBytes) {
			valueId = hashCache.valueId;
		} else {
			valueId = DX12DescriptorValueHashUtils::HashConstantBufferView(*desc);
			hashCache.desc = *desc;
			hashCache.valueId = valueId;
			hashCache.valid = true;
		}

		// Sequential-slot TLS fast path: engines fill descriptor arenas
		// front-to-back every frame, so the workload is thousands of CreateCBV
		// calls landing on consecutive slots of the same heap (the ZZZ
		// character scene issues ~18K CreateCBV per frame). When the
		// destination is exactly the next slot of the same heap, resolve the
		// slot from the previous call's identity and skip the heap-resolve
		// scan entirely; the seqlock value compare below still guards against
		// redundant shadow writes.
		struct CbvSequentialState
		{
			SIZE_T lastHandle = 0;
			UINT increment = 0;
			UINT numDescriptors = 0;
			UINT slot = 0;
			DescriptorHeapRecord *heap = nullptr;
			size_t heapIndex = 0;
			bool valid = false;
		};
		static thread_local CbvSequentialState seq;

		ResolvedHeapSlot resolved;
		bool resolvedKnownHeap = false;
		if (seq.valid && seq.heap && seq.increment &&
			destDescriptor.ptr == seq.lastHandle + seq.increment &&
			seq.slot + 1 < seq.numDescriptors &&
			seq.heap->stateLocks && seq.heap->stateLocks->IsActive()) {
			resolved.heap = seq.heap;
			resolved.heapIndex = seq.heapIndex;
			resolved.slot = seq.slot + 1;
			resolvedKnownHeap = true;
		} else {
			resolvedKnownHeap = ResolveHeapSlotFast(
				destDescriptor.ptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&resolved);
			if (resolvedKnownHeap && resolved.heap) {
				seq.valid = true;
				seq.heap = resolved.heap;
				seq.heapIndex = resolved.heapIndex;
				seq.increment = resolved.heap->increment;
				seq.numDescriptors = resolved.heap->desc.NumDescriptors;
				seq.slot = resolved.slot;
			} else {
				seq.valid = false;
			}
		}
		if (resolvedKnownHeap) {
			seq.lastHandle = destDescriptor.ptr;
			seq.slot = resolved.slot;
		}
		// ResolveHeapSlotFast already validates the heap activity immediately
		// before returning. Prefer the flat post-publication snapshot over the
		// seqlock read when available: every publish writes flatValueIds +
		// flatVersions atomically with the completing version, so a slot whose
		// version is unchanged since its last publication has an authoritative
		// flat valueId. Re-creating the same CBV into the same slot therefore
		// hits a couple of loads instead of the full three-load seqlock
		// protocol. When the slot was never published, or was republished
		// (content changed), fall back to the seqlock identity read.
		if (resolvedKnownHeap &&
				resolved.heap && resolved.heap->stateLocks) {
			UINT64 flatValueId = 0;
			if (resolved.heap->stateLocks->ReadFlatValueId(
					resolved.slot, &flatValueId) &&
					flatValueId == valueId) {
				return false;
			}
			if (resolved.heap->stateLocks->PublishedSlotValueEquals(
					resolved.slot, valueId)) {
				return false;
			}
		}
		// Ensure the dense shadow array exists BEFORE taking the per-slot
		// shard lock: StoreCurrentValueIfChanged then takes the array path
		// instead of the global sparse store / value pool, and
		// EnsureDescriptorHeapStorageLocked needs the all-shard lock, which
		// must not be acquired while a single shard is held (non-reentrant
		// SRWLOCK). The empty() check is a cheap lock-free gate; the function
		// itself re-checks under the lock.
		if (resolvedKnownHeap && resolved.heap &&
				resolved.heap->slots.empty()) {
			EnsureDescriptorHeapStorageLocked(resolved.heap);
		}
		// Lock-free dense write once the heap is materialized.
		if (resolvedKnownHeap && resolved.heap &&
				resolved.heap->IsMaterialized() &&
				resolved.heap->stateLocks &&
				resolved.heap->stateLocks->IsActive()) {
			DescriptorHeapRecord &heap = *resolved.heap;
			const DX12DescriptorValue value =
				DX12DescriptorSlotArrayUtils::MakeConstantBufferViewValue(
					*desc, valueId);
			const bool changed =
				DX12DescriptorSlotArrayUtils::StoreCurrentValueIfChanged(
					&heap, resolved.slot, value, destDescriptor.ptr);
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(
					&heap, resolved.slot);
			return changed;
		}
		ResolvedHeapSlot locked;
		bool lockedKnownHeap = resolvedKnownHeap &&
			LockResolvedHeapSlotExclusive(resolved, &locked);
		// A heap can retire between the optimistic identity read and its shard
		// lock. Re-resolve in that rare case instead of misclassifying the handle
		// as a CPU-ledger-only descriptor.
		if (!lockedKnownHeap)
			lockedKnownHeap = ResolveAndLockHeapSlot(
				destDescriptor.ptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				true,
				&locked);
		if (lockedKnownHeap) {
			resolved = locked;
			DescriptorHeapRecord &heap = *resolved.heap;
			const DX12DescriptorValue value =
				DX12DescriptorSlotArrayUtils::MakeConstantBufferViewValue(
					*desc, valueId);
			const bool changed =
				DX12DescriptorSlotArrayUtils::StoreCurrentValueIfChanged(
					&heap, resolved.slot, value, destDescriptor.ptr);
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(
					&heap, resolved.slot);
			UnlockHeapSlot(resolved, true);
			return changed;
		}

		// Handles outside known heaps retain the legacy CPU-ledger behavior.
		DescriptorRecord record;
		SetDescriptorKind(&record, DescriptorKind::CBV_ID);
		record.valueId = valueId;
		record.cpuHandle = destDescriptor.ptr;
		record.cbv = *desc;
		record.hasDesc = true;
		record.gpuVirtualAddress = desc->BufferLocation;
		record.viewSize = desc->SizeInBytes;
		if (DescriptorValueUnchanged(destDescriptor.ptr, valueId))
			return false;
		WriteToCpuLedger(std::move(record));
		return true;
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
					&heap, resolved.slot, record, cpuHandle);
			if (changed)
				DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
			ReleaseDescriptorRecordResources(&record);
			return true;
		}

		if (!ResolveAndLockHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, true, &resolved)) {
			return false;
		}

		const bool changed =
			DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
				&heap, resolved.slot, record, cpuHandle);
		if (changed)
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

	static void CopyDescriptorRange(
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		CopyTelemetryBatch *telemetryBatch = nullptr)
	{
		if (count == 0) {
			return;
		}

		ResolvedHeapSlot destResolved;
		ResolvedHeapSlot srcResolved;
		const bool hasDestination = ResolveHeapSlotFast(
			destStart.ptr, type, &destResolved);
		const bool hasSource = ResolveHeapSlotFast(
			srcStart.ptr, type, &srcResolved);

		// If either range endpoint cannot be resolved to a known heap slot,
		// fall back to per-descriptor lookup via FindLatestDescriptor which
		// checks both heap slots and the CPU ledger.
		if (!hasDestination || !hasSource) {
			if (telemetryBatch)
				telemetryBatch->RecordRange(count, false, false, false);
			else
				DX12DescriptorShadowTelemetryUtils::RecordCopyRange(
					count, false, false, false);
			DX12DescriptorShadowTelemetryUtils::RecordFallback(count);
			CopyDescriptorRangeWithFallback(type, destStart, srcStart, count);
			return;
		}

		DescriptorHeapRecord &destHeap = *destResolved.heap;
		DescriptorHeapRecord &srcHeap = *srcResolved.heap;
		const UINT destSlot = destResolved.slot;
		const UINT srcSlot = srcResolved.slot;
		const UINT64 srcEnd = static_cast<UINT64>(srcSlot) + count;
		const UINT64 destEnd = static_cast<UINT64>(destSlot) + count;
		const bool sameHeap = &destHeap == &srcHeap;
		const bool overlaps = sameHeap &&
			static_cast<UINT64>(destSlot) < srcEnd &&
			static_cast<UINT64>(srcSlot) < destEnd;
		if (telemetryBatch)
			telemetryBatch->RecordRange(count, true, sameHeap, overlaps);
		else
			DX12DescriptorShadowTelemetryUtils::RecordCopyRange(
				count, true, sameHeap, overlaps);

		// CopyDescriptorsSimple is commonly used for one descriptor at a time.
		// Keep that case out of the generic range lambdas/loop: under Debug /Od
		// their call machinery is measurable across thousands of calls per frame.
		if (count == 1) {
			// DXVK-style flat snapshot fast path: after any publication the
			// slot's valueId and completing version are mirrored into a flat
			// per-slot array. When both slots' versions are unchanged since
			// their last publication and the valueIds are equal, the
			// destination already holds the source's exact content: the copy
			// is a no-op. This is a few relaxed/acquire loads indexed by
			// slot - no hash table, no capacity limits, no seqlock protocol -
			// mirroring DXVK's views[slot] != resView comparison.
			uint64_t flatDestValueId = 0;
			uint64_t flatSrcValueId = 0;
			if (srcHeap.stateLocks && destHeap.stateLocks &&
				destHeap.stateLocks->ReadFlatValueId(
					destSlot, &flatDestValueId) &&
				srcHeap.stateLocks->ReadFlatValueId(
					srcSlot, &flatSrcValueId) &&
				flatDestValueId == flatSrcValueId) {
				if (telemetryBatch) {
					telemetryBatch->RecordRepeatHit();
					telemetryBatch->RecordMemo(true);
					telemetryBatch->RecordResults(
						flatDestValueId ? 1 : 0,
						flatDestValueId ? 0 : 1,
						0,
						flatDestValueId ? 1 : 0,
						0,
						flatDestValueId ? 0 : 1);
				} else {
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
					DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
						flatDestValueId ? 1 : 0,
						flatDestValueId ? 0 : 1,
						0,
						flatDestValueId ? 1 : 0,
						0,
						flatDestValueId ? 0 : 1);
				}
				return;
			}
			// Single-descriptor copies are 99.5% of all copy ranges. The
			// 1024-slot RangeMemo below keys on (srcHeap, srcSlot,
			// destHeap, destSlot) and validates freshness with two cheap
			// ReadSlotVersion loads instead of the seqlock identity reads.
			// Games re-issue the same descriptor copies every frame, so the
			// same pair recurs with unchanged slot versions and the memo
			// serves the overwhelming majority of calls.
			UINT memoSourceFound = 0;
			UINT memoSourceMissing = 0;
			UINT memoUnchanged = 0;
			UINT memoAlreadyEmpty = 0;
			if (RangeMemo().TryLoad(
					srcHeap, srcSlot, destHeap, destSlot, 1,
					&memoSourceFound, &memoSourceMissing,
					&memoUnchanged, &memoAlreadyEmpty)) {
				if (telemetryBatch) {
					telemetryBatch->RecordRepeatHit();
					telemetryBatch->RecordMemo(true);
					telemetryBatch->RecordResults(
						memoSourceFound, memoSourceMissing,
						0, memoUnchanged, 0, memoAlreadyEmpty);
				} else {
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
					DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
						memoSourceFound, memoSourceMissing,
						0, memoUnchanged, 0, memoAlreadyEmpty);
				}
				return;
			}
			UINT64 srcVersion = 0;
			UINT64 destVersion = 0;
			const CopyIdentityResult identityResult =
				ComparePublishedSlotIdentities(
					srcHeap, srcSlot, destHeap, destSlot,
					&srcVersion, &destVersion);
			if (identityResult != CopyIdentityResult::Unknown) {
				// Seed the RangeMemo so the next identical copy skips the
				// seqlock identity reads entirely.
				RangeMemo().Store(
					srcHeap, srcSlot, destHeap, destSlot, 1,
					identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
					identityResult == CopyIdentityResult::Unchanged ? 0 : 1,
					identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
					identityResult == CopyIdentityResult::Unchanged ? 0 : 1);
				if (telemetryBatch)
					telemetryBatch->RecordMemo(true);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
				if (telemetryBatch)
					telemetryBatch->RecordResults(
						identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
						identityResult == CopyIdentityResult::Unchanged ? 0 : 1,
						0,
						identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
						0,
						identityResult == CopyIdentityResult::Unchanged ? 0 : 1);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
						identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
						identityResult == CopyIdentityResult::Unchanged ? 0 : 1,
						0,
						identityResult == CopyIdentityResult::Unchanged ? 1 : 0,
						0,
						identityResult == CopyIdentityResult::Unchanged ? 0 : 1);
				return;
			}
			if (telemetryBatch)
				telemetryBatch->RecordMemo(false);
			else
				DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(false);
			// Dense fast path: once both heaps have materialized their slot
			// arrays they are stable for the heap lifetime, so the copy is a
			// plain seqlock-published write with no shard locks. Sparse heaps
			// and in-flight materialization keep the locked path below.
			if (srcHeap.IsMaterialized() &&
					destHeap.IsMaterialized()) {
				UINT sourceFound = 0;
				UINT sourceMissing = 0;
				UINT changed = 0;
				UINT unchanged = 0;
				UINT erased = 0;
				UINT alreadyEmpty = 0;
				UINT64 publishedValueId = 0;
				if (srcHeap.stateLocks->IsActive() &&
						destHeap.stateLocks->IsActive() &&
						destSlot < destHeap.desc.NumDescriptors) {
					const DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult copyResult =
						DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
							&destHeap, destSlot, srcHeap, srcSlot,
							destStart.ptr, &publishedValueId);
					if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed) {
						sourceFound = 1;
						changed = 1;
						DX12DescriptorHeapStateUtils::PublishValue(
							&destHeap, destSlot, publishedValueId);
					} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
						sourceFound = 1;
						unchanged = 1;
					} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
						sourceMissing = 1;
						erased = 1;
						DX12DescriptorHeapStateUtils::PublishValue(
							&destHeap, destSlot, 0);
					} else {
						sourceMissing = 1;
						alreadyEmpty = 1;
					}
				}
				if (telemetryBatch)
					telemetryBatch->RecordResults(
						sourceFound, sourceMissing, changed, unchanged,
						erased, alreadyEmpty);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
						sourceFound, sourceMissing, changed, unchanged,
						erased, alreadyEmpty);
				return;
			}
			// Prefer the dense array path for the destination: materialize it
			// before any shard lock is taken (all-shard lock is not reentrant
			// with a held single shard).
			if (destHeap.slots.empty())
				EnsureDescriptorHeapStorageLocked(&destHeap);
			SRWLOCK *sourceLock = HeapLock(&srcHeap, srcSlot);
			SRWLOCK *destLock = HeapLock(&destHeap, destSlot);
			bool sourceShared = false;
			bool destExclusive = false;
			bool oneExclusive = false;
			bool sourceFirst = false;
			if (sourceLock == destLock) {
				if (sourceLock) {
					DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(sourceLock);
					oneExclusive = true;
				}
			} else if (reinterpret_cast<uintptr_t>(sourceLock) <
					reinterpret_cast<uintptr_t>(destLock)) {
				sourceFirst = true;
				if (sourceLock) {
					DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(sourceLock);
					sourceShared = true;
				}
				if (destLock) {
					DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(destLock);
					destExclusive = true;
				}
			} else {
				if (destLock) {
					DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(destLock);
					destExclusive = true;
				}
				if (sourceLock) {
					DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(sourceLock);
					sourceShared = true;
				}
			}

			UINT sourceFound = 0;
			UINT sourceMissing = 0;
			UINT changed = 0;
			UINT unchanged = 0;
			UINT erased = 0;
			UINT alreadyEmpty = 0;
			DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult copyResult =
				DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationEmpty;
			UINT64 publishedValueId = 0;
			if (srcHeap.stateLocks->IsActive() && destHeap.stateLocks->IsActive() &&
					destSlot < destHeap.desc.NumDescriptors) {
				copyResult =
					DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
						&destHeap, destSlot, srcHeap, srcSlot, destStart.ptr,
						&publishedValueId);
				if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed) {
					sourceFound = 1;
					changed = 1;
					DX12DescriptorHeapStateUtils::PublishValue(
						&destHeap, destSlot, publishedValueId);
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
					sourceFound = 1;
					unchanged = 1;
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
					sourceMissing = 1;
					erased = 1;
					DX12DescriptorHeapStateUtils::PublishValue(&destHeap, destSlot, 0);
				} else {
					sourceMissing = 1;
					alreadyEmpty = 1;
				}
			}
			if (sourceFirst) {
				if (destExclusive)
					ReleaseSRWLockExclusive(destLock);
				if (sourceShared)
					ReleaseSRWLockShared(sourceLock);
			} else {
				if (sourceShared)
					ReleaseSRWLockShared(sourceLock);
				if (destExclusive)
					ReleaseSRWLockExclusive(destLock);
			}
			if (oneExclusive)
				ReleaseSRWLockExclusive(sourceLock);
			if (telemetryBatch)
				telemetryBatch->RecordResults(
					sourceFound, sourceMissing, changed, unchanged, erased, alreadyEmpty);
			else
				DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
					sourceFound, sourceMissing, changed, unchanged, erased, alreadyEmpty);
			return;
		}

		UINT sourceFoundCount = 0;
		UINT sourceMissingCount = 0;
		UINT changedCount = 0;
		UINT unchangedCount = 0;
		UINT erasedCount = 0;
		UINT alreadyEmptyCount = 0;
		if (RangeMemo().TryLoad(
				srcHeap,
				srcSlot,
				destHeap,
				destSlot,
				count,
				&sourceFoundCount,
				&sourceMissingCount,
				&unchangedCount,
				&alreadyEmptyCount)) {
			if (telemetryBatch) {
				telemetryBatch->RecordMemo(true);
				telemetryBatch->RecordResults(
					sourceFoundCount,
					sourceMissingCount,
					0,
					unchangedCount,
					0,
					alreadyEmptyCount);
			} else {
				DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
				DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
					sourceFoundCount,
					sourceMissingCount,
					0,
					unchangedCount,
					0,
					alreadyEmptyCount);
			}
			return;
		}
		if (ComparePublishedRangeIdentities(
				srcHeap,
				srcSlot,
				destHeap,
				destSlot,
				count,
				&sourceFoundCount,
				&sourceMissingCount,
				&unchangedCount,
				&alreadyEmptyCount)) {
			if (telemetryBatch) {
				telemetryBatch->RecordMemo(true);
				telemetryBatch->RecordResults(
					sourceFoundCount,
					sourceMissingCount,
					0,
					unchangedCount,
					0,
					alreadyEmptyCount);
			} else {
				DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
				DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
					sourceFoundCount,
					sourceMissingCount,
					0,
					unchangedCount,
					0,
					alreadyEmptyCount);
			}
			RangeMemo().Store(
				srcHeap,
				srcSlot,
				destHeap,
				destSlot,
				count,
				sourceFoundCount,
				sourceMissingCount,
				unchangedCount,
				alreadyEmptyCount);
			return;
		}
		if (telemetryBatch)
			telemetryBatch->RecordMemo(false);
		else
			DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(false);
		bool rangeVersionCommitted = false;
		auto invalidateRangeOnce = [&]() {
			if (!rangeVersionCommitted) {
				DX12DescriptorHeapStateUtils::CommitPublishedRange(&destHeap);
				rangeVersionCommitted = true;
			}
		};
		auto accountCopyResult = [&](
			DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult result,
			UINT dSlot,
			UINT64 publishedValueId) {
			if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed) {
				sourceFoundCount++;
				changedCount++;
				invalidateRangeOnce();
				DX12DescriptorHeapStateUtils::PublishRangeValue(
					&destHeap, dSlot, publishedValueId);
			} else if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
				sourceFoundCount++;
				unchangedCount++;
			} else if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
				sourceMissingCount++;
				erasedCount++;
				invalidateRangeOnce();
				DX12DescriptorHeapStateUtils::PublishRangeValue(&destHeap, dSlot, 0);
			} else {
				sourceMissingCount++;
				alreadyEmptyCount++;
			}
		};

		auto copySource = [&](UINT i, DescriptorRecord *source) -> bool {
			const UINT sSlot = srcSlot + i;
			return sSlot < srcHeap.desc.NumDescriptors &&
				DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
					srcHeap, sSlot, source);
		};

			auto applyDestination = [&](UINT i, const DescriptorRecord *source) {
			const UINT dSlot = destSlot + i;
			if (dSlot >= destHeap.desc.NumDescriptors)
				return;
			if (!source) {
				sourceMissingCount++;
				if (DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
						&destHeap, dSlot)) {
					invalidateRangeOnce();
					DX12DescriptorHeapStateUtils::PublishRangeValue(
						&destHeap, dSlot, 0);
					erasedCount++;
				} else {
					alreadyEmptyCount++;
				}
				return;
			}
			sourceFoundCount++;

			const SIZE_T destHandle = destStart.ptr +
				static_cast<SIZE_T>(i) * destHeap.increment;
			if (DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&destHeap, dSlot, *source, destHandle)) {
				invalidateRangeOnce();
				DX12DescriptorHeapStateUtils::PublishRangeValue(
					&destHeap, dSlot, source->valueId);
				changedCount++;
			} else {
				unchangedCount++;
			}
		};
		struct HeldShardLock
		{
			SRWLOCK *lock = nullptr;
			bool exclusive = false;
		};
			auto acquireHeldLock = [](const HeldShardLock &held) {
			if (!held.lock)
				return;
			if (held.exclusive)
				DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(held.lock);
			else
				DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(held.lock);
		};
		auto releaseHeldLock = [](const HeldShardLock &held) {
			if (!held.lock)
				return;
			if (held.exclusive)
				ReleaseSRWLockExclusive(held.lock);
			else
				ReleaseSRWLockShared(held.lock);
		};
		auto collectHeldLock = [](std::vector<HeldShardLock> *locks,
			SRWLOCK *lock, bool exclusive) {
			if (!locks || !lock)
				return;
			for (HeldShardLock &held : *locks) {
				if (held.lock != lock)
					continue;
				held.exclusive = held.exclusive || exclusive;
				return;
			}
			HeldShardLock held;
			held.lock = lock;
			held.exclusive = exclusive;
			std::vector<HeldShardLock>::iterator insertAt = locks->begin();
			for (; insertAt != locks->end(); ++insertAt) {
				if (reinterpret_cast<uintptr_t>(lock) <
						reinterpret_cast<uintptr_t>(insertAt->lock))
					break;
			}
			locks->insert(insertAt, held);
		};
		auto acquireHeldLocks = [&](const std::vector<HeldShardLock> &locks) {
			for (const HeldShardLock &held : locks)
				acquireHeldLock(held);
		};
		auto releaseHeldLocks = [&](const std::vector<HeldShardLock> &locks) {
			for (std::vector<HeldShardLock>::const_reverse_iterator it =
					locks.rbegin(); it != locks.rend(); ++it)
				releaseHeldLock(*it);
		};
		auto lockSlotPair = [&](UINT sSlot, UINT dSlot,
			HeldShardLock *first, HeldShardLock *second) {
			*first = HeldShardLock();
			*second = HeldShardLock();
			SRWLOCK *sourceLock = HeapLock(&srcHeap, sSlot);
			SRWLOCK *destLock = HeapLock(&destHeap, dSlot);
			if (sourceLock == destLock) {
				first->lock = sourceLock;
				first->exclusive = true;
				acquireHeldLock(*first);
				return;
			}
			if (reinterpret_cast<uintptr_t>(sourceLock) <
					reinterpret_cast<uintptr_t>(destLock)) {
				first->lock = sourceLock;
				first->exclusive = false;
				second->lock = destLock;
				second->exclusive = true;
			} else {
				first->lock = destLock;
				first->exclusive = true;
				second->lock = sourceLock;
				second->exclusive = false;
			}
			acquireHeldLock(*first);
			acquireHeldLock(*second);
		};
		auto unlockSlotPair = [&](const HeldShardLock &first,
			const HeldShardLock &second) {
			releaseHeldLock(second);
			releaseHeldLock(first);
		};

		if (!overlaps) {
			// Lock-free dense path: materialized heaps have stable arrays, so
			// each dirty slot is copied and published directly. In-flight
			// materialization and sparse heaps use the locked dirty-span path.
			if (srcHeap.IsMaterialized() &&
					destHeap.IsMaterialized()) {
				for (UINT i = 0; i < count; ++i) {
					const CopyIdentityResult identityResult =
						ComparePublishedSlotIdentities(
							srcHeap, srcSlot + i, destHeap, destSlot + i);
					if (identityResult == CopyIdentityResult::Unchanged) {
						++sourceFoundCount;
						++unchangedCount;
						continue;
					}
					if (identityResult ==
							CopyIdentityResult::SourceMissingDestinationEmpty) {
						++sourceMissingCount;
						++alreadyEmptyCount;
						continue;
					}
					const UINT currentDestSlot = destSlot + i;
					const SIZE_T destHandle = destStart.ptr +
						static_cast<SIZE_T>(i) * destHeap.increment;
					UINT64 publishedValueId = 0;
					accountCopyResult(
						DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
							&destHeap,
							currentDestSlot,
							srcHeap,
							srcSlot + i,
							destHandle,
							&publishedValueId),
						currentDestSlot,
						publishedValueId);
				}
			} else {
			// The overwhelmingly common path, especially CopyDescriptorsSimple:
			// first classify contiguous dirty spans with published identities, then
			// hold the involved shard locks once per dirty run instead of once per
			// slot. Sequential descriptors stripe across shard locks, so this cuts
			// hot-path lock traffic by turning O(slots) acquisitions into O(shards).
			UINT dirtyStart = UINT_MAX;
			bool rangeAborted = false;
			auto flushDirtySpan = [&](UINT dirtyEnd) {
				if (dirtyStart == UINT_MAX || dirtyStart >= dirtyEnd)
					return;
				std::vector<HeldShardLock> heldLocks;
				heldLocks.reserve((dirtyEnd - dirtyStart) * 2);
				for (UINT dirtyIndex = dirtyStart; dirtyIndex < dirtyEnd; ++dirtyIndex) {
					collectHeldLock(
						&heldLocks,
						HeapLock(&srcHeap, srcSlot + dirtyIndex),
						false);
					collectHeldLock(
						&heldLocks,
						HeapLock(&destHeap, destSlot + dirtyIndex),
						true);
				}
				acquireHeldLocks(heldLocks);
				if (!srcHeap.stateLocks->IsActive() ||
						!destHeap.stateLocks->IsActive()) {
					releaseHeldLocks(heldLocks);
					rangeAborted = true;
					dirtyStart = UINT_MAX;
					return;
				}
				for (UINT dirtyIndex = dirtyStart; dirtyIndex < dirtyEnd; ++dirtyIndex) {
					const UINT currentDestSlot = destSlot + dirtyIndex;
					const SIZE_T destHandle = destStart.ptr +
						static_cast<SIZE_T>(dirtyIndex) * destHeap.increment;
					UINT64 publishedValueId = 0;
					accountCopyResult(
						DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
							&destHeap,
							currentDestSlot,
							srcHeap,
							srcSlot + dirtyIndex,
							destHandle,
							&publishedValueId),
						currentDestSlot,
						publishedValueId);
				}
				releaseHeldLocks(heldLocks);
				dirtyStart = UINT_MAX;
			};
			for (UINT i = 0; i < count; ++i) {
				const CopyIdentityResult identityResult =
					ComparePublishedSlotIdentities(
						srcHeap, srcSlot + i, destHeap, destSlot + i);
				if (identityResult == CopyIdentityResult::Unchanged) {
					flushDirtySpan(i);
					++sourceFoundCount;
					++unchangedCount;
					continue;
				}
				if (identityResult ==
						CopyIdentityResult::SourceMissingDestinationEmpty) {
					flushDirtySpan(i);
					++sourceMissingCount;
					++alreadyEmptyCount;
					continue;
				}
				if (dirtyStart == UINT_MAX)
					dirtyStart = i;
				if (i + 1 == count) {
					flushDirtySpan(i + 1);
					if (rangeAborted) {
						rangeAborted = true;
						break;
					}
				}
			}
			if (rangeAborted)
				return;
			}
		} else {
			// Snapshot first so an overlapping same-heap copy cannot overwrite a
			// later source. Unknown sources deliberately clear stale destinations.
			using SnapshotEntry =
				DX12SparseDescriptorStore<DescriptorRecord>::SnapshotEntry;
			std::vector<SnapshotEntry> snapshot(count);
			for (UINT i = 0; i < count; ++i) {
				SRWLOCK *sourceLock = HeapLock(&srcHeap, srcSlot + i);
				if (sourceLock)
					DX12DescriptorShadowTelemetryUtils::AcquireHeapShared(sourceLock);
				if (!srcHeap.stateLocks->IsActive()) {
					if (sourceLock)
						ReleaseSRWLockShared(sourceLock);
					return;
				}
				DescriptorRecord source;
				if (!copySource(i, &source)) {
					if (sourceLock)
						ReleaseSRWLockShared(sourceLock);
					continue;
				}
				snapshot[i].occupied = true;
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&snapshot[i].value, source, source.cpuHandle);
				if (sourceLock)
					ReleaseSRWLockShared(sourceLock);
			}
			for (UINT i = 0; i < count; ++i) {
				SRWLOCK *destLock = HeapLock(&destHeap, destSlot + i);
				if (destLock)
					DX12DescriptorShadowTelemetryUtils::AcquireHeapExclusive(destLock);
				if (!destHeap.stateLocks->IsActive()) {
					if (destLock)
						ReleaseSRWLockExclusive(destLock);
					return;
				}
				applyDestination(i, snapshot[i].occupied ?
					&snapshot[i].value : nullptr);
				if (destLock)
					ReleaseSRWLockExclusive(destLock);
			}
		}
		if (telemetryBatch)
			telemetryBatch->RecordResults(
				sourceFoundCount, sourceMissingCount,
				changedCount, unchangedCount, erasedCount, alreadyEmptyCount);
		else
			DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
				sourceFoundCount, sourceMissingCount,
				changedCount, unchangedCount, erasedCount, alreadyEmptyCount);
		if (changedCount == 0 && erasedCount == 0) {
			RangeMemo().Store(
				srcHeap,
				srcSlot,
				destHeap,
				destSlot,
				count,
				sourceFoundCount,
				sourceMissingCount,
				unchangedCount,
				alreadyEmptyCount);
		}
	}

	// Per-descriptor fallback when bulk heap-slot resolution fails.
	// Uses FindLatestDescriptor for each source and WriteDescriptorToSlot
	// for each destination.
	static void CopyDescriptorRangeWithFallback(
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count)
	{
		(void)type;

		// The descriptor handle increment is a device constant per heap type.
		// Use the cached value so the fallback path does not take the global
		// lock and linearly scan every registered heap just to step CPU
		// handles; the scan only runs once as the cache fill.
		UINT increment = CachedIncrementForType(type);
		if (!increment) {
			AcquireSRWLockShared(&gDescriptorLock);
			for (size_t hi = 0; hi < gDescriptorHeaps.size(); ++hi) {
				if (gDescriptorHeaps[hi].desc.Type == type &&
					gDescriptorHeaps[hi].increment > 0) {
					increment = gDescriptorHeaps[hi].increment;
					break;
				}
			}
			ReleaseSRWLockShared(&gDescriptorLock);
			CacheIncrementForType(type, increment);
		}

		UINT copiedCount = 0;
		for (UINT i = 0; i < count; ++i) {
			const SIZE_T srcHandle = srcStart.ptr +
				(increment ? static_cast<SIZE_T>(i) * increment : 0);
			const SIZE_T destHandle = destStart.ptr +
				(increment ? static_cast<SIZE_T>(i) * increment : 0);

			DescriptorRecord srcRecord;
			if (FindLatestDescriptor(srcHandle, &srcRecord)) {
				if (DescriptorValueUnchanged(destHandle, srcRecord.valueId)) {
					ReleaseDescriptorRecordResources(&srcRecord);
					continue;
				}
				srcRecord.cpuHandle = destHandle;
				if (!WriteDescriptorToSlot(destHandle, std::move(srcRecord)))
					WriteToCpuLedger(std::move(srcRecord));
				copiedCount++;
			} else {
				EraseDescriptor(destHandle);
			}
		}
		(void)copiedCount;
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
		DX12DescriptorShadowTelemetryUtils::AcquireGlobalShared(&gDescriptorLock);
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
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &resolved) &&
				resolved.heap->IsMaterialized()) {
			const bool erased = DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
				resolved.heap, resolved.slot);
			if (erased) {
				DX12DescriptorHeapStateUtils::BumpVersion(
					resolved.heap, resolved.slot);
			}
			return erased;
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

		DX12DescriptorShadowTelemetryUtils::AcquireGlobalExclusive(&gDescriptorLock);
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

		DX12DescriptorShadowTelemetryUtils::AcquireGlobalExclusive(&gDescriptorLock);
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


class DX12DeviceHookSignatures
{
public:
	using CreateGraphicsPipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreateComputePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreatePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	using DeviceFactoryCreateDevice = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12DeviceFactory*, IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
};

class DX12DeviceHookState
{
public:
	inline static DX12DeviceHookSignatures::CreateGraphicsPipelineState OrigCreateGraphicsPipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreateComputePipelineState OrigCreateComputePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreatePipelineState OrigCreatePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::DeviceFactoryCreateDevice OrigDeviceFactoryCreateDevice = nullptr;
};



class DX12GraphicsPsoReplacement
{
public:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;
};

class DX12ComputePsoReplacement
{
public:
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	bool changed = false;
};


class DX12DeviceHooks
{
public:
	static HRESULT CreateGraphicsPipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState);
	static HRESULT CreateComputePipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState);
	static void HookDevice(IUnknown *device);
	static void HookDeviceFactory(IUnknown *factory);
	static void HookDeviceFromCommandQueue(IUnknown *commandQueue);
};


class DX12DeviceHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateGraphicsPipelineState = 10;
		static constexpr UINT CreateComputePipelineState = 11;
	};

	class ID3D12Device2
	{
	public:
		static constexpr UINT CreatePipelineState = 47;
	};

	class ID3D12DeviceFactory
	{
	public:
		static constexpr UINT CreateDevice = 9;
	};
};



class DX12DeviceHookCallbacks
{
public:
	static HRESULT CreateGraphicsPipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device::CreateGraphicsPipelineState,
			DX12DeviceHookState::OrigCreateGraphicsPipelineState,
			"ID3D12Device::CreateGraphicsPipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT CreateComputePipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device::CreateComputePipelineState,
			DX12DeviceHookState::OrigCreateComputePipelineState,
			"ID3D12Device::CreateComputePipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreateGraphicsPipelineState(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device::CreateGraphicsPipelineState", device);
		if (DX12InternalReplayState::IsActive())
			return CreateGraphicsPipelineStateOriginal(device, desc, riid, pipelineState);
		DX12GraphicsPsoReplacement replacement;
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc = desc;
		{
			DX12_CREATE_TIMING_SCOPE(GraphicsPsoHook, Prepare);
			if (desc) {
				replacement.desc = *desc;
				if (DX12ModReplaceShaderBytecode("vs", desc->VS, &replacement.vs, &replacement.vsBytes)) {
					replacement.desc.VS = replacement.vs;
					replacement.changed = true;
				}
				if (DX12ModReplaceShaderBytecode("ps", desc->PS, &replacement.ps, &replacement.psBytes)) {
					replacement.desc.PS = replacement.ps;
					replacement.changed = true;
				}
				if (replacement.changed)
					activeDesc = &replacement.desc;
			}
		}

		HRESULT hr = E_FAIL;
		{
			DX12_CREATE_TIMING_SCOPE(GraphicsPsoHook, Original);
			hr = CreateGraphicsPipelineStateOriginal(device, activeDesc, riid, pipelineState);
		}
		{
			DX12_CREATE_TIMING_SCOPE(GraphicsPsoHook, Record);
			if (SUCCEEDED(hr) && activeDesc) {
				ID3D12PipelineState *created =
					pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
				if (desc)
					DX12ModRecordGraphicsPipelineState(device, created, desc);
				DX12RecordGraphicsPipelineState(created, activeDesc);
			}
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreateComputePipelineState(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device::CreateComputePipelineState", device);
		if (DX12InternalReplayState::IsActive())
			return CreateComputePipelineStateOriginal(device, desc, riid, pipelineState);
		DX12ComputePsoReplacement replacement;
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc = desc;
		{
			DX12_CREATE_TIMING_SCOPE(ComputePsoHook, Prepare);
			if (desc) {
				replacement.desc = *desc;
				if (DX12ModReplaceShaderBytecode("cs", desc->CS, &replacement.cs, &replacement.csBytes)) {
					replacement.desc.CS = replacement.cs;
					replacement.changed = true;
					activeDesc = &replacement.desc;
				}
			}
		}

		HRESULT hr = E_FAIL;
		{
			DX12_CREATE_TIMING_SCOPE(ComputePsoHook, Original);
			hr = CreateComputePipelineStateOriginal(device, activeDesc, riid, pipelineState);
		}
		{
			DX12_CREATE_TIMING_SCOPE(ComputePsoHook, Record);
			if (SUCCEEDED(hr) && activeDesc) {
				ID3D12PipelineState *created =
					pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
				if (desc)
					DX12ModRecordComputePipelineState(device, created, desc);
				DX12RecordComputePipelineState(created, activeDesc);
			}
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreatePipelineState(
		ID3D12Device2 *device,
		const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device2::CreatePipelineState", device);
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device2::CreatePipelineState,
			DX12DeviceHookState::OrigCreatePipelineState,
			"ID3D12Device2::CreatePipelineState");
		if (DX12InternalReplayState::IsActive())
			return original ? original(device, desc, riid, pipelineState) : E_FAIL;
		HRESULT hr = E_FAIL;
		{
			DX12_CREATE_TIMING_SCOPE(PsoStreamHook, Original);
			hr = original ? original(device, desc, riid, pipelineState) : E_FAIL;
		}
		{
			DX12_CREATE_TIMING_SCOPE(PsoStreamHook, Record);
			if (SUCCEEDED(hr) && desc)
				DX12RecordPipelineStateStream(
					pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr, desc);
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedDeviceFactoryCreateDevice(
		ID3D12DeviceFactory *factory,
		IUnknown *adapter,
		D3D_FEATURE_LEVEL featureLevel,
		REFIID riid,
		void **device)
	{
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12DeviceFactory::CreateDevice", factory);
		auto original = DX12OriginalFunctionRegistry::Resolve(
			factory, DX12DeviceHookSlotTypes::ID3D12DeviceFactory::CreateDevice,
			DX12DeviceHookState::OrigDeviceFactoryCreateDevice,
			"ID3D12DeviceFactory::CreateDevice");
		HRESULT hr = original ? original(factory, adapter, featureLevel, riid, device) : E_FAIL;
		DX12LogManager::Fields("ID3D12DeviceFactory::CreateDevice",
			"\"adapter\":\"%p\",\"featureLevel\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
			adapter, featureLevel, hr, device ? *device : nullptr);
		if (SUCCEEDED(hr) && device && *device)
			DX12DeviceHooks::HookDevice(static_cast<IUnknown*>(*device));
		return hr;
	}
};


class DX12ResourceTrackerHooks
{
public:
	static void Install(ID3D12Device *device);
};



inline HRESULT DX12DeviceHooks::CreateGraphicsPipelineStateOriginal(
	ID3D12Device *device,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
	REFIID riid,
	void **pipelineState)
{
	return DX12DeviceHookCallbacks::CreateGraphicsPipelineStateOriginal(device, desc, riid, pipelineState);
}

inline HRESULT DX12DeviceHooks::CreateComputePipelineStateOriginal(
	ID3D12Device *device,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
	REFIID riid,
	void **pipelineState)
{
	return DX12DeviceHookCallbacks::CreateComputePipelineStateOriginal(device, desc, riid, pipelineState);
}

inline void DX12DeviceHooks::HookDevice(IUnknown *device)
{
	if (!device)
		return;

	ID3D12Device *baseDevice = nullptr;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&baseDevice))))
		return;

	DX12LogManager::Fields("DeviceHooksReady", "\"device\":\"%p\",\"mode\":\"wrapper_only\"", baseDevice);
	baseDevice->Release();
}

inline void DX12DeviceHooks::HookDeviceFactory(IUnknown *factory)
{
	(void)factory;
}

inline void DX12DeviceHooks::HookDeviceFromCommandQueue(IUnknown *commandQueue)
{
	(void)commandQueue;
}



// ===================== Mods/DX12ConfigModel.hpp =====================

#include "MigotoIniCommandListUtils.hpp"

#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Bunny {

class IniEntry {
public:
	std::wstring key;
	std::wstring value;
	std::wstring rawLine;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	int lineNumber = 0;
	bool hasEquals = false;
};

class IniSection {
public:
	std::wstring name;
	std::wstring originalName;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<IniEntry> entries;
};

class IniDocument {
public:
	bool LoadFromFile(const wchar_t *path);
	bool Parse(const wchar_t *text);
	bool AppendParse(
		const wchar_t *text,
		const std::wstring &sourcePath,
		const std::wstring &sourceDir,
		const std::wstring &iniNamespace);

	const IniSection *FindSection(const wchar_t *section) const;
	const std::vector<IniSection> &Sections() const { return mSections; }

	bool GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const;
	bool GetBool(const wchar_t *section, const wchar_t *key, bool *value) const;
	bool GetInt(const wchar_t *section, const wchar_t *key, int *value) const;

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	std::vector<IniSection> mSections;
	std::map<std::wstring, size_t> mSectionIndex;
	std::wstring mPath;
	std::wstring mError;
};

class IniDocumentText
{
public:
	static std::wstring ToLower(std::wstring value);
	static std::wstring Trim(const std::wstring &value);
	static const wchar_t *MigotoSectionPrefix(const std::wstring &section);
	static std::wstring MakeNamespacedSectionName(
		const std::wstring &section, const std::wstring &iniNamespace);
	static std::wstring ResolveNamespacedSectionReference(
		const std::wstring &reference,
		const std::wstring &prefix,
		const std::wstring &iniNamespace);
};

} // namespace Bunny



namespace Bunny {

inline std::wstring IniDocumentText::ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	return value;
}

inline std::wstring IniDocumentText::Trim(const std::wstring &value)
{
	const wchar_t *spaces = L" \t\r\n";
	size_t first = value.find_first_not_of(spaces);
	if (first == std::wstring::npos)
		return L"";
	size_t last = value.find_last_not_of(spaces);
	return value.substr(first, last - first + 1);
}

class IniDocumentParser
{
public:
static bool IsCommentOrEmpty(const std::wstring &line)
{
	std::wstring trimmed = IniDocumentText::Trim(line);
	return trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#';
}

class SectionPrefixEntry {
public:
	const wchar_t *prefix;
	bool isPrefix;
};

inline static const SectionPrefixEntry kMigotoCommandListSections[] = {
	{L"TextureOverride", true},
	{L"CommandList", true},
	{L"Constants", false},
	{L"Present", false},
	{L"ShaderOverride", true},
	{L"CustomShader", true},
	{L"ShaderRegex", true},
	{L"BuiltInCommandList", true},
	{L"BuiltInCustomShader", true},
	{L"ClearRenderTargetView", false},
	{L"ClearDepthStencilView", false},
	{L"ClearUnorderedAccessViewUint", false},
	{L"ClearUnorderedAccessViewFloat", false},
};

inline static const SectionPrefixEntry kMigotoRegularSections[] = {
	{L"Pool", true},
	{L"Resource", true},
	{L"Key", true},
	{L"Include", true},
	{L"Preset", true},
	{L"Hunting", false},
	{L"Logging", false},
	{L"System", false},
	{L"Device", false},
	{L"Rendering", false},
	{L"Loader", false},
	{L"Profile", false},
	{L"Stereo", false},
	{L"ConvergenceMap", false},
};

static const wchar_t *SectionPrefixFromList(
	const std::wstring &section, const SectionPrefixEntry *entries, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		const SectionPrefixEntry &entry = entries[i];
		std::wstring prefix = entry.prefix;
		if (entry.isPrefix) {
			if (IniDocumentText::ToLower(section).rfind(IniDocumentText::ToLower(prefix), 0) == 0)
				return entry.prefix;
		} else if (IniDocumentText::ToLower(section) == IniDocumentText::ToLower(prefix)) {
			return entry.prefix;
		}
	}
	return nullptr;
}
};

inline const wchar_t *IniDocumentText::MigotoSectionPrefix(const std::wstring &section)
{
	const wchar_t *prefix = IniDocumentParser::SectionPrefixFromList(
		section, IniDocumentParser::kMigotoCommandListSections,
		sizeof(IniDocumentParser::kMigotoCommandListSections) / sizeof(IniDocumentParser::kMigotoCommandListSections[0]));
	if (prefix)
		return prefix;
	return IniDocumentParser::SectionPrefixFromList(
		section, IniDocumentParser::kMigotoRegularSections,
		sizeof(IniDocumentParser::kMigotoRegularSections) / sizeof(IniDocumentParser::kMigotoRegularSections[0]));
}

inline std::wstring IniDocumentText::MakeNamespacedSectionName(
	const std::wstring &section, const std::wstring &iniNamespace)
{
	if (iniNamespace.empty())
		return section;

	const wchar_t *prefix = IniDocumentText::MigotoSectionPrefix(section);
	if (!prefix)
		return section;

	size_t prefixLength = wcslen(prefix);
	if (section.size() < prefixLength)
		return section;
	return std::wstring(prefix) + L"\\" + iniNamespace + L"\\" + section.substr(prefixLength);
}

inline std::wstring IniDocumentText::ResolveNamespacedSectionReference(
	const std::wstring &reference,
	const std::wstring &prefix,
	const std::wstring &iniNamespace)
{
	std::wstring ref = IniDocumentText::Trim(reference);
	if (ref.empty())
		return L"";

	if (IniDocumentText::ToLower(ref).rfind(IniDocumentText::ToLower(prefix) + L"\\", 0) == 0)
		return ref;
	if (IniDocumentText::ToLower(ref).rfind(IniDocumentText::ToLower(prefix), 0) == 0)
		return IniDocumentText::MakeNamespacedSectionName(ref, iniNamespace);
	return IniDocumentText::MakeNamespacedSectionName(prefix + ref, iniNamespace);
}

inline bool IniDocument::LoadFromFile(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();

	std::ifstream file(mPath, std::ios::binary);
	if (!file.is_open()) {
		mError = L"Unable to open ini file";
		return false;
	}

	std::string bytes(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
	}

	UINT codePage = CP_UTF8;
	int length = MultiByteToWideChar(
		codePage, MB_ERR_INVALID_CHARS, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) {
		codePage = CP_ACP;
		length = MultiByteToWideChar(
			codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
			nullptr, 0);
	}
	if (length < 0) {
		mError = L"Unable to decode ini file";
		return false;
	}

	std::wstring text(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &text[0], length)) {
			mError = L"Unable to decode ini file";
			return false;
		}
	}
	return Parse(text.c_str());
}

inline bool IniDocument::Parse(const wchar_t *text)
{
	mSections.clear();
	mSectionIndex.clear();
	mError.clear();
	return AppendParse(text, mPath, L"", L"");
}

inline bool IniDocument::AppendParse(
	const wchar_t *text,
	const std::wstring &sourcePath,
	const std::wstring &sourceDir,
	const std::wstring &iniNamespace)
{
	if (!text)
		return true;

	IniSection *current = nullptr;
	std::wstring currentNamespace = iniNamespace;
	std::wistringstream stream(text);
	std::wstring line;
	int lineNumber = 0;

	while (std::getline(stream, line)) {
		lineNumber++;
		if (lineNumber == 1 && !line.empty() && line.front() == L'\xfeff')
			line.erase(line.begin());
		if (lineNumber == 1 && line.size() >= 3 &&
		    line[0] == L'\xef' && line[1] == L'\xbb' && line[2] == L'\xbf')
			line.erase(0, 3);
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();

		std::wstring trimmed = IniDocumentText::Trim(line);
		if (IniDocumentParser::IsCommentOrEmpty(trimmed))
			continue;

		if (trimmed.size() >= 2 && trimmed.front() == L'[' && trimmed.back() == L']') {
			std::wstring originalName = IniDocumentText::Trim(trimmed.substr(1, trimmed.size() - 2));
			std::wstring sectionName = IniDocumentText::MakeNamespacedSectionName(originalName, currentNamespace);
			std::wstring sectionKey = IniDocumentText::ToLower(sectionName);
			auto found = mSectionIndex.find(sectionKey);
			if (found == mSectionIndex.end()) {
				IniSection section;
				section.name = sectionName;
				section.originalName = originalName;
				section.sourcePath = sourcePath;
				section.sourceDir = sourceDir;
				section.iniNamespace = sectionName == originalName ? L"" : currentNamespace;
				mSections.push_back(section);
				mSectionIndex[sectionKey] = mSections.size() - 1;
				current = &mSections.back();
			} else {
				current = &mSections[found->second];
			}
			continue;
		}

		if (!current) {
			size_t equals = line.find(L'=');
			if (equals != std::wstring::npos) {
				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(line.substr(0, equals)));
				std::wstring value = IniDocumentText::Trim(line.substr(equals + 1));
				if (key == L"namespace") {
					currentNamespace = value;
					continue;
				}
				if (key == L"condition")
					continue;
			}
			mError = L"Ini entry found before first section";
			return false;
		}

		IniEntry entry;
		entry.rawLine = line;
		entry.sourcePath = sourcePath;
		entry.sourceDir = sourceDir;
		entry.iniNamespace = currentNamespace;
		entry.lineNumber = lineNumber;

		size_t equals = line.find(L'=');
		if (equals == std::wstring::npos) {
			entry.key = IniDocumentText::Trim(line);
			entry.hasEquals = false;
		} else {
			entry.key = IniDocumentText::Trim(line.substr(0, equals));
			entry.value = IniDocumentText::Trim(line.substr(equals + 1));
			entry.hasEquals = true;
		}
		current->entries.push_back(entry);
	}

	return true;
}

inline const IniSection *IniDocument::FindSection(const wchar_t *section) const
{
	if (!section)
		return nullptr;
	auto found = mSectionIndex.find(IniDocumentText::ToLower(section));
	if (found == mSectionIndex.end())
		return nullptr;
	return &mSections[found->second];
}

inline bool IniDocument::GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const
{
	const IniSection *iniSection = FindSection(section);
	if (!iniSection || !key)
		return false;

	std::wstring keyLower = IniDocumentText::ToLower(key);
	for (auto it = iniSection->entries.rbegin(); it != iniSection->entries.rend(); ++it) {
		if (!it->hasEquals)
			continue;
		if (IniDocumentText::ToLower(it->key) == keyLower) {
			if (value)
				*value = it->value;
			return true;
		}
	}
	return false;
}

inline bool IniDocument::GetBool(const wchar_t *section, const wchar_t *key, bool *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	text = IniDocumentText::ToLower(IniDocumentText::Trim(text));
	bool parsed = text == L"1" || text == L"true" || text == L"yes" || text == L"on";
	if (!parsed && !(text == L"0" || text == L"false" || text == L"no" || text == L"off"))
		return false;
	if (value)
		*value = parsed;
	return true;
}

inline bool IniDocument::GetInt(const wchar_t *section, const wchar_t *key, int *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	wchar_t *end = nullptr;
	long parsed = std::wcstol(text.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;
	if (value)
		*value = static_cast<int>(parsed);
	return true;
}

} // namespace Bunny




namespace Bunny {

enum class CommandListTargetKind
{
	Unknown,
	IndexBuffer,
	VertexBuffer,
	ConstantBuffer,
	ShaderResource,
	UnorderedAccessView
};

enum class CommandListShaderStage
{
	Unknown,
	Vertex,
	Pixel,
	Compute
};

class CommandListTarget
{
public:
	CommandListTargetKind kind = CommandListTargetKind::Unknown;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	uint32_t slot = 0;
};

enum class CommandListActionKind
{
	Run,
	CheckTextureOverride,
	HandlingSkip,
	SetIndexBuffer,
	SetVertexBuffer,
	Draw,
	DrawIndexed,
	DrawFromCaller,
	DrawIndexedFromCaller,
	Dispatch,
	ControlFlow,
	VariableAssignment,
	PostVariableAssignment,
	LocalVariableAssignment,
	ResourceAssignment,
	SetConstantBuffer,
	SetShaderResource,
	SetUnorderedAccessView,
	SetShader,
	SetShaderConstant,
	SetRenderState
};

class CommandListAction
{
public:
	CommandListActionKind kind = CommandListActionKind::Run;
	std::wstring commandList;
	CommandListTarget target;
	std::wstring resource;
	std::wstring key;
	std::wstring value;
	std::wstring rawLine;
	std::wstring iniNamespace;
	uint32_t args[5] = {};
	uint32_t argCount = 0;
};

class CommandListConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<CommandListAction> actions;
};

class CommandListLinks
{
public:
	std::vector<std::wstring> pre;
	std::vector<std::wstring> main;
	std::vector<std::wstring> post;
};

using CommandListMap = std::unordered_map<std::wstring, CommandListConfig>;

class MigotoCommandListParser
{
public:
	static bool ParseTarget(const std::wstring &text, CommandListTarget *target);
	static bool ParseActionFromRawLine(const std::wstring &rawLine, CommandListAction *action);
	static bool ParseActionFromEntry(
		const std::wstring &key, const std::wstring &value, CommandListAction *action);
	static void ParseLinksFromEntry(
		const std::wstring &key, const std::wstring &value, CommandListLinks *links);
	static void ResolveLinks(
		CommandListLinks *links, const std::wstring &iniNamespace);
	static void ResolveActionReferences(
		CommandListAction *action, const std::wstring &iniNamespace);
	static void ParseSections(const IniDocument &ini, CommandListMap *commandLists);
};

}

namespace Bunny {

class MigotoCommandListParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	return MigotoIniCommandListUtils::StartsWithI(value, prefix);
}

static bool EqualsI(const std::wstring &value, const wchar_t *text)
{
	return MigotoIniCommandListUtils::EqualsI(value, text);
}

	static bool IsExplicitCommandListSection(const std::wstring &section)
	{
		return MigotoIniCommandListUtils::IsExplicitCommandListSection(section);
	}

static bool ParseVertexBufferTarget(const std::wstring &text, uint32_t *slot)
{
	return MigotoIniCommandListUtils::ParseVertexBufferTarget(text, slot);
}

static bool ParseTargetSlot(const std::wstring &text, size_t start, uint32_t *slot)
{
	return MigotoIniCommandListUtils::ParseUnsignedSuffix(text, start, slot);
}

static bool ParseShaderStagePrefix(
	const std::wstring &text, size_t *prefixLength, CommandListShaderStage *stage)
{
	wchar_t parsedStage = 0;
	if (!MigotoIniCommandListUtils::ParseShaderStagePrefix(text, prefixLength, &parsedStage))
		return false;

	if (parsedStage == L'v') {
		*stage = CommandListShaderStage::Vertex;
		return true;
	}
	if (parsedStage == L'p') {
		*stage = CommandListShaderStage::Pixel;
		return true;
	}
	if (parsedStage == L'c') {
		*stage = CommandListShaderStage::Compute;
		return true;
	}
	return false;
}

static bool StartsWithTokenI(const std::wstring &value, const wchar_t *prefix)
{
	return MigotoIniCommandListUtils::StartsWithTokenI(value, prefix);
}

	static bool IsVariableAssignmentKey(const std::wstring &value)
	{
		return MigotoIniCommandListUtils::IsVariableAssignmentKey(value);
	}

	static bool IsVariableDeclarationKey(const std::wstring &value)
	{
		return MigotoIniCommandListUtils::IsVariableDeclarationKey(value);
	}

static bool IsPostVariableAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsPostVariableAssignmentKey(value);
}

static bool IsLocalVariableAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsLocalVariableAssignmentKey(value);
}

static bool IsResourceAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsResourceAssignmentKey(value);
}

static bool IsShaderConstantKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsShaderConstantKey(value);
}

static bool IsRenderStateKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsRenderStateKey(value);
}

static void AppendRunList(const std::wstring &value, std::vector<std::wstring> *lists);
static std::wstring ResolveCommandListReference(
	const std::wstring &value, const std::wstring &iniNamespace);
static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace);
static bool ParseUInt(const std::wstring &text, uint32_t *value);
static bool ParseDrawArgs(
	const std::wstring &value, uint32_t expectedArgs, CommandListAction *action);
static void ResolveCommandListVector(
	std::vector<std::wstring> *lists, const std::wstring &iniNamespace);

};

inline bool MigotoCommandListParser::ParseTarget(const std::wstring &text, CommandListTarget *target)
{
	if (!target)
		return false;
	*target = CommandListTarget();

	std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(text));
	if (value == L"ib") {
		target->kind = CommandListTargetKind::IndexBuffer;
		return true;
	}

	uint32_t slot = 0;
	if (MigotoCommandListParserText::ParseVertexBufferTarget(value, &slot)) {
		target->kind = CommandListTargetKind::VertexBuffer;
		target->slot = slot;
		return true;
	}

	size_t prefixLength = 0;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	if (MigotoCommandListParserText::ParseShaderStagePrefix(value, &prefixLength, &stage)) {
		if (value.rfind(L"cb", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 2, &slot)) {
			target->kind = CommandListTargetKind::ConstantBuffer;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"t", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 1, &slot)) {
			target->kind = CommandListTargetKind::ShaderResource;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"u", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 1, &slot) &&
		    (stage == CommandListShaderStage::Pixel ||
		     stage == CommandListShaderStage::Compute)) {
			target->kind = CommandListTargetKind::UnorderedAccessView;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
	}
	return false;
}

inline void MigotoCommandListParserText::AppendRunList(const std::wstring &value, std::vector<std::wstring> *lists)
{
	if (!lists)
		return;
	std::wstring list = IniDocumentText::Trim(value);
	if (!list.empty())
		lists->push_back(list);
}

inline std::wstring MigotoCommandListParserText::ResolveCommandListReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	std::wstring ref = IniDocumentText::Trim(value);
	if (ref.empty())
		return L"";

	if (MigotoCommandListParserText::IsExplicitCommandListSection(ref))
		return IniDocumentText::MakeNamespacedSectionName(ref, iniNamespace);
	return IniDocumentText::ResolveNamespacedSectionReference(ref, L"CommandList", iniNamespace);
}

inline std::wstring MigotoCommandListParserText::ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return IniDocumentText::ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

inline bool MigotoCommandListParserText::ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

inline bool MigotoCommandListParserText::ParseDrawArgs(
	const std::wstring &value, uint32_t expectedArgs, CommandListAction *action)
{
	if (!action || expectedArgs > 5)
		return false;

	std::wstring text = value;
	size_t start = 0;
	uint32_t count = 0;
	while (start <= text.size()) {
		size_t comma = text.find(L',', start);
		std::wstring token = comma == std::wstring::npos ?
			text.substr(start) : text.substr(start, comma - start);
		if (count >= expectedArgs)
			return false;
		uint32_t parsed = 0;
		if (!MigotoCommandListParserText::ParseUInt(token, &parsed))
			return false;
		action->args[count++] = parsed;
		if (comma == std::wstring::npos)
			break;
		start = comma + 1;
	}

	if (count != expectedArgs)
		return false;
	action->argCount = count;
	return true;
}

inline bool MigotoCommandListParser::ParseActionFromRawLine(
	const std::wstring &rawLine, CommandListAction *action)
{
	if (!action)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(rawLine);
	std::wstring lower = IniDocumentText::ToLower(trimmed);
	if (lower.empty())
		return false;

	if (MigotoIniCommandListUtils::IsControlFlowRawLine(lower)) {
		action->kind = CommandListActionKind::ControlFlow;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	if (MigotoCommandListParserText::IsLocalVariableAssignmentKey(lower)) {
		action->kind = CommandListActionKind::LocalVariableAssignment;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	if (MigotoCommandListParserText::IsVariableDeclarationKey(lower)) {
		action->kind = CommandListActionKind::VariableAssignment;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	return false;
}

inline bool MigotoCommandListParser::ParseActionFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListAction *action)
{
	if (!action)
		return false;

	action->key = key;
	action->value = value;

	if (key == L"run") {
		action->kind = CommandListActionKind::Run;
		action->commandList = value;
		return !action->commandList.empty();
	}

	if (key == L"checktextureoverride") {
		action->kind = CommandListActionKind::CheckTextureOverride;
		return MigotoCommandListParser::ParseTarget(value, &action->target);
	}

	if (key == L"handling" && IniDocumentText::ToLower(value) == L"skip") {
		action->kind = CommandListActionKind::HandlingSkip;
		return true;
	}

	if (key == L"draw") {
		if (IniDocumentText::ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::Draw;
		return MigotoCommandListParserText::ParseDrawArgs(value, 2, action);
	}

	if (key == L"drawindexed") {
		if (IniDocumentText::ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawIndexedFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::DrawIndexed;
		return MigotoCommandListParserText::ParseDrawArgs(value, 3, action);
	}

	if (key == L"dispatch") {
		action->kind = CommandListActionKind::Dispatch;
		return MigotoCommandListParserText::ParseDrawArgs(value, 3, action);
	}

	if (key == L"vs" || key == L"ps" || key == L"cs") {
		action->kind = CommandListActionKind::SetShader;
		action->resource = value;
		return !action->resource.empty();
	}

	if (MigotoCommandListParserText::IsPostVariableAssignmentKey(key)) {
		action->kind = CommandListActionKind::PostVariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsLocalVariableAssignmentKey(key)) {
		action->kind = CommandListActionKind::LocalVariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsVariableAssignmentKey(key) ||
		MigotoCommandListParserText::IsVariableDeclarationKey(key)) {
		action->kind = CommandListActionKind::VariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsResourceAssignmentKey(key)) {
		action->kind = CommandListActionKind::ResourceAssignment;
		action->resource = key;
		return !value.empty();
	}

	if (MigotoCommandListParserText::IsShaderConstantKey(key)) {
		action->kind = CommandListActionKind::SetShaderConstant;
		return true;
	}

	if (MigotoCommandListParserText::IsRenderStateKey(key)) {
		action->kind = CommandListActionKind::SetRenderState;
		return true;
	}

	CommandListTarget target;
	if (MigotoCommandListParser::ParseTarget(key, &target)) {
		switch (target.kind) {
		case CommandListTargetKind::IndexBuffer:
			action->kind = CommandListActionKind::SetIndexBuffer;
			break;
		case CommandListTargetKind::VertexBuffer:
			action->kind = CommandListActionKind::SetVertexBuffer;
			break;
		case CommandListTargetKind::ConstantBuffer:
			action->kind = CommandListActionKind::SetConstantBuffer;
			break;
		case CommandListTargetKind::ShaderResource:
			action->kind = CommandListActionKind::SetShaderResource;
			break;
		case CommandListTargetKind::UnorderedAccessView:
			action->kind = CommandListActionKind::SetUnorderedAccessView;
			break;
		default:
			return false;
		}
		action->target = target;
		action->resource = value;
		return !action->resource.empty();
	}
	return false;
}

inline void MigotoCommandListParser::ParseLinksFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListLinks *links)
{
	if (!links)
		return;

	std::wstring lower = IniDocumentText::ToLower(IniDocumentText::Trim(key));
	if (lower == L"run") {
		MigotoCommandListParserText::AppendRunList(value, &links->main);
		return;
	}
	if (lower == L"pre" || lower == L"pre run" || lower == L"prerun") {
		MigotoCommandListParserText::AppendRunList(value, &links->pre);
		return;
	}
	if (lower == L"post" || lower == L"post run" || lower == L"postrun") {
		MigotoCommandListParserText::AppendRunList(value, &links->post);
		return;
	}
}

inline void MigotoCommandListParserText::ResolveCommandListVector(
	std::vector<std::wstring> *lists, const std::wstring &iniNamespace)
{
	if (!lists)
		return;
	for (std::wstring &list : *lists)
		list = MigotoCommandListParserText::ResolveCommandListReference(list, iniNamespace);
}

inline void MigotoCommandListParser::ResolveLinks(
	CommandListLinks *links, const std::wstring &iniNamespace)
{
	if (!links)
		return;
	MigotoCommandListParserText::ResolveCommandListVector(&links->pre, iniNamespace);
	MigotoCommandListParserText::ResolveCommandListVector(&links->main, iniNamespace);
	MigotoCommandListParserText::ResolveCommandListVector(&links->post, iniNamespace);
}

inline void MigotoCommandListParser::ResolveActionReferences(
	CommandListAction *action, const std::wstring &iniNamespace)
{
	if (!action)
		return;
	action->iniNamespace = iniNamespace;
	if (action->kind == CommandListActionKind::Run)
		action->commandList = MigotoCommandListParserText::ResolveCommandListReference(action->commandList, iniNamespace);
	else if (action->kind == CommandListActionKind::SetIndexBuffer ||
	         action->kind == CommandListActionKind::SetVertexBuffer ||
	         action->kind == CommandListActionKind::SetConstantBuffer ||
	         action->kind == CommandListActionKind::SetShaderResource ||
	         action->kind == CommandListActionKind::SetUnorderedAccessView)
		action->resource = MigotoCommandListParserText::ResolveResourceReference(action->resource, iniNamespace);
}

inline void MigotoCommandListParser::ParseSections(const IniDocument &ini, CommandListMap *commandLists)
{
	if (!commandLists)
		return;

	commandLists->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoCommandListParserText::IsExplicitCommandListSection(section.name))
			continue;

		CommandListConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals)
					continue;

				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
				std::wstring value = IniDocumentText::Trim(entry.value);
			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}
		}

		(*commandLists)[config.section] = config;
		if (section.iniNamespace.empty())
			(*commandLists)[config.originalSection] = config;
	}
}

}




namespace Bunny {

class ShaderOverrideConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	uint64_t hash = 0;
	bool handlingSkip = false;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
};

using ShaderOverrideMap = std::unordered_map<uint64_t, ShaderOverrideConfig>;

class MigotoShaderOverrideParser
{
public:
	static bool ParseHash(const std::wstring &text, uint64_t *value);
	static void ParseSections(
		const IniDocument &ini, ShaderOverrideMap *shaderOverrides);
};

}

namespace Bunny {

class MigotoShaderOverrideParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

};

inline bool MigotoShaderOverrideParser::ParseHash(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty())
		return false;

	uint64_t parsed = 0;
	for (wchar_t ch : trimmed) {
		unsigned digit = 0;
		if (ch >= L'0' && ch <= L'9')
			digit = ch - L'0';
		else if (ch >= L'a' && ch <= L'f')
			digit = ch - L'a' + 10;
		else if (ch >= L'A' && ch <= L'F')
			digit = ch - L'A' + 10;
		else
			return false;
		parsed = (parsed << 4) | digit;
	}

	*value = parsed;
	return true;
}

inline void MigotoShaderOverrideParser::ParseSections(
	const IniDocument &ini, ShaderOverrideMap *shaderOverrides)
{
	if (!shaderOverrides)
		return;

	shaderOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoShaderOverrideParserText::StartsWithI(section.name, L"ShaderOverride"))
			continue;

		ShaderOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals)
					continue;

				std::wstring key = IniDocumentText::ToLower(entry.key);

			if (key == L"hash") {
				uint64_t hash = 0;
				if (!MigotoShaderOverrideParser::ParseHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
		if (hasHash)
			(*shaderOverrides)[config.hash] = config;
	}
}

}




namespace Bunny {

enum class NumericMatchOp
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual
};

class NumericMatch
{
public:
	NumericMatchOp op = NumericMatchOp::Equal;
	uint32_t value = 0;
	bool enabled = false;
};

class TextureOverrideConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	uint32_t hash = 0;
	bool handlingSkip = false;
	std::wstring indexBufferResource;
	std::map<uint32_t, std::wstring> vertexBufferResources;
	bool hasMatchVertexCount = false;
	bool hasMatchIndexCount = false;
	bool hasMatchInstanceCount = false;
	bool hasMatchFirstVertex = false;
	bool hasMatchFirstIndex = false;
	bool hasMatchFirstInstance = false;
	bool hasMatchCs = false;
	bool hasMatchUavBytes = false;
	bool hasCsHash = false;
	std::map<uint32_t, uint32_t> preSkinMatchCsSrvHashes;
	std::map<uint32_t, std::wstring> preSkinCsSrvResources;
	// Draw-call texture slot replacement (ps-tN/vs-tN/this=).
	std::map<uint32_t, std::wstring> drawPsSrvResources;
	std::map<uint32_t, std::wstring> drawVsSrvResources;
	std::wstring drawReplaceThisResource;
	NumericMatch matchVertexCount;
	NumericMatch matchIndexCount;
	NumericMatch matchInstanceCount;
	NumericMatch matchFirstVertex;
	NumericMatch matchFirstIndex;
	NumericMatch matchFirstInstance;
	uint64_t matchCs = 0;
	uint64_t matchUavBytes = 0;
	uint64_t csHash = 0;
	bool hasMatchPriority = false;
	int matchPriority = 0;
	bool hasVertexLimitRaise = false;
	uint32_t overrideByteStride = 0;
	uint32_t overrideVertexCount = 0;
	uint64_t overrideByteWidth = 0;
	uint32_t uavByteStride = 0;
	uint64_t overrideNumElements = 0;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
};

using TextureOverrideList = std::vector<TextureOverrideConfig>;
using TextureOverrideMap = std::unordered_map<uint32_t, TextureOverrideList>;

class MigotoTextureOverrideParser
{
public:
	static bool ParseHash(const std::wstring &text, uint32_t *value);
	static void ParseSections(
		const IniDocument &ini, TextureOverrideMap *textureOverrides);
};

} // namespace Bunny



namespace Bunny {

class MigotoTextureOverrideParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseSrvIndexedKey(
	const std::wstring &key, const wchar_t *prefix, const wchar_t *suffix,
	uint32_t *index)
{
	if (!index || !StartsWithI(key, prefix))
		return false;

	const size_t prefixLength = wcslen(prefix);
	const size_t suffixLength = suffix ? wcslen(suffix) : 0;
	if (key.size() <= prefixLength + suffixLength)
		return false;
	if (suffixLength) {
		std::wstring keySuffix = key.substr(key.size() - suffixLength);
		if (IniDocumentText::ToLower(keySuffix) != IniDocumentText::ToLower(suffix))
			return false;
	}

	const std::wstring digits = key.substr(
		prefixLength, key.size() - prefixLength - suffixLength);
	if (digits.empty())
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : digits) {
		if (ch < L'0' || ch > L'9')
			return false;
		const uint32_t digit = static_cast<uint32_t>(ch - L'0');
		if (parsed > (UINT32_MAX - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}

	*index = parsed;
	return true;
}

static bool ParseInt(const std::wstring &text, int *value);
static bool ParseUInt(const std::wstring &text, uint32_t *value);
static bool ParseNumericMatch(const std::wstring &text, NumericMatch *match);
static bool ParseUInt64(const std::wstring &text, uint64_t *value);
static bool ParseVertexBufferKey(const std::wstring &key, uint32_t *slot);
static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace);

};

inline bool MigotoTextureOverrideParser::ParseHash(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty() || trimmed.size() > 8)
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : trimmed) {
		unsigned digit = 0;
		if (ch >= L'0' && ch <= L'9')
			digit = ch - L'0';
		else if (ch >= L'a' && ch <= L'f')
			digit = ch - L'a' + 10;
		else if (ch >= L'A' && ch <= L'F')
			digit = ch - L'A' + 10;
		else
			return false;
		parsed = (parsed << 4) | digit;
	}

	*value = parsed;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseInt(const std::wstring &text, int *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	long parsed = std::wcstol(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<int>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseNumericMatch(const std::wstring &text, NumericMatch *match)
{
	if (!match)
		return false;

	std::wstring value = IniDocumentText::Trim(text);
	if (value.empty())
		return false;

	NumericMatchOp op = NumericMatchOp::Equal;
	if (value.rfind(L"<=", 0) == 0) {
		op = NumericMatchOp::LessEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L">=", 0) == 0) {
		op = NumericMatchOp::GreaterEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L"!", 0) == 0) {
		op = NumericMatchOp::NotEqual;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"=", 0) == 0) {
		op = NumericMatchOp::Equal;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"<", 0) == 0) {
		op = NumericMatchOp::Less;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L">", 0) == 0) {
		op = NumericMatchOp::Greater;
		value = IniDocumentText::Trim(value.substr(1));
	}

	uint32_t parsed = 0;
	if (!MigotoTextureOverrideParserText::ParseUInt(value, &parsed))
		return false;

	match->op = op;
	match->value = parsed;
	match->enabled = true;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt64(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long long parsed = std::wcstoull(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint64_t>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseVertexBufferKey(const std::wstring &key, uint32_t *slot)
{
	if (!slot || key.size() < 3 || key[0] != L'v' || key[1] != L'b')
		return false;

	uint32_t parsed = 0;
	for (size_t i = 2; i < key.size(); ++i) {
		if (key[i] < L'0' || key[i] > L'9')
			return false;
		parsed = parsed * 10 + static_cast<uint32_t>(key[i] - L'0');
	}

	*slot = parsed;
	return true;
}

inline std::wstring MigotoTextureOverrideParserText::ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return IniDocumentText::ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

inline void MigotoTextureOverrideParser::ParseSections(
	const IniDocument &ini, TextureOverrideMap *textureOverrides)
{
	if (!textureOverrides)
		return;

	textureOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoTextureOverrideParserText::StartsWithI(section.name, L"TextureOverride"))
			continue;

		TextureOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals)
					continue;

				std::wstring key = IniDocumentText::ToLower(entry.key);

			if (key == L"hash") {
				uint32_t hash = 0;
				if (!MigotoTextureOverrideParser::ParseHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			if (key == L"ib") {
				config.indexBufferResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			uint32_t vbSlot = 0;
			if (MigotoTextureOverrideParserText::ParseVertexBufferKey(key, &vbSlot)) {
				config.vertexBufferResources[vbSlot] =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			if (key == L"match_vertex_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchVertexCount))
					config.hasMatchVertexCount = true;
				continue;
			}

			if (key == L"match_index_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchIndexCount))
					config.hasMatchIndexCount = true;
				continue;
			}

			if (key == L"match_instance_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchInstanceCount))
					config.hasMatchInstanceCount = true;
				continue;
			}

			if (key == L"match_first_vertex") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstVertex))
					config.hasMatchFirstVertex = true;
				continue;
			}

			if (key == L"match_first_index") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstIndex))
					config.hasMatchFirstIndex = true;
				continue;
			}

			if (key == L"match_first_instance") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstInstance))
					config.hasMatchFirstInstance = true;
				continue;
			}

			if (key == L"match_priority") {
				int value = 0;
				if (MigotoTextureOverrideParserText::ParseInt(entry.value, &value)) {
					config.matchPriority = value;
					config.hasMatchPriority = true;
				}
				continue;
			}

			if (key == L"cs_hash") {
				uint64_t value = 0;
				if (MigotoShaderOverrideParser::ParseHash(entry.value, &value)) {
					config.csHash = value;
					config.hasCsHash = true;
				}
				continue;
			}

			if (key == L"match_uav_bytes") {
				uint64_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt64(entry.value, &value)) {
					config.matchUavBytes = value;
					config.hasMatchUavBytes = true;
				}
				continue;
			}

			uint32_t srvIndex = 0;
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_cs_t", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_srv", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"cs-t", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"srv", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			// Draw-call texture slot replacement: ps-tN / vs-tN / this=
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"ps-t", L"", &srvIndex)) {
				config.drawPsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"vs-t", L"", &srvIndex)) {
				config.drawVsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"this") {
				config.drawReplaceThisResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"override_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"override_vertex_count") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideVertexCount = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"uav_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.uavByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
		if (config.overrideByteStride && config.overrideVertexCount)
			config.overrideByteWidth =
				static_cast<uint64_t>(config.overrideByteStride) * config.overrideVertexCount;
		if (config.uavByteStride && config.overrideByteWidth)
			config.overrideNumElements = config.overrideByteWidth / config.uavByteStride;
		const bool hasDrawContextMatch =
			config.hasMatchVertexCount || config.hasMatchIndexCount ||
			config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
			config.hasMatchFirstIndex || config.hasMatchFirstInstance;
		if (hasHash || config.hasVertexLimitRaise || config.hasMatchCs || config.hasCsHash || hasDrawContextMatch)
			(*textureOverrides)[config.hash].push_back(config);
	}

	for (auto &bucket : *textureOverrides) {
		std::sort(bucket.second.begin(), bucket.second.end(),
			[](const TextureOverrideConfig &lhs, const TextureOverrideConfig &rhs) {
				if (lhs.matchPriority != rhs.matchPriority)
					return lhs.matchPriority > rhs.matchPriority;
				return IniDocumentText::ToLower(lhs.section) < IniDocumentText::ToLower(rhs.section);
			});
	}
}

} // namespace Bunny



// ===================== Runtime/DX12ExecutionAnalysis.hpp =====================

#include "MigotoCommandVmUtils.hpp"

class DX12TextureOverrideMatch
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
	bool hasThisSrvTarget = false;
	bool compute = false;
	DX12CurrentShaderResourceBinding thisSrvBinding;
	uint32_t matchedHash = 0;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};




class DX12ExecuteIndirectRuntime
{
public:
	using Fn = DX12CommandListHookSignatures::ExecuteIndirect;

	class PreSkinProbe
	{
	public:
		std::vector<DX12CurrentComputeUavBinding> uavs;
		std::vector<DX12CurrentComputeUavBinding> srvs;
		std::vector<DX12CurrentComputeUavBinding> cbvs;
		std::vector<DX12CurrentRootConstants> rootConstants;
		bool bindingsFound = false;
		bool applied = false;
		uint64_t computeShaderHash = 0;
		UINT originalVertexCount = 0;
		UINT overrideVertexCount = 0;
		DX12PreSkinDispatchOverride dispatchOverride;
	};

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		Fn originalExecuteIndirect)
	{
		if (!DX12ModNeedsPreSkinningUavProbe()) {
			if (originalExecuteIndirect) {
				originalExecuteIndirect(commandList, commandSignature, maxCommandCount,
					argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
			}
			return;
		}

		PreSkinProbe &probe = ScratchProbe();
		PreparePreSkinProbe(commandList, &probe);

		if (originalExecuteIndirect) {
			originalExecuteIndirect(commandList, commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
		}

		FinishPreSkinProbe(commandList, probe);
	}

	static PreSkinProbe &ScratchProbe()
	{
		static thread_local PreSkinProbe probe;
		probe.uavs.clear();
		probe.srvs.clear();
		probe.cbvs.clear();
		probe.rootConstants.clear();
		probe.bindingsFound = false;
		probe.applied = false;
		probe.computeShaderHash = 0;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		PreSkinProbe *probe)
	{
		if (!probe || !DX12ModNeedsPreSkinningUavProbe())
			return;

		ID3D12PipelineState *pipelineState =
			DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no PSO from runtime state\n");
#endif
			return;
		}

		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: GetShaderInfo failed\n");
#endif
			return;
		}
		if (!shaderInfo.hasCS) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: PSO is not compute\n");
#endif
			return;
		}

		probe->computeShaderHash = shaderInfo.cs;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: compute PSO cs=%016llx\n",
			static_cast<unsigned long long>(probe->computeShaderHash));
#endif
		if (!DX12ModShouldTrackPreSkinBindingsForCs(probe->computeShaderHash)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: CS not in pre-skin list\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin CS match\n");
#endif
		probe->bindingsFound = DX12BindingTracker::GetCurrentComputeAll(
			commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		if (!probe->bindingsFound) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no compute bindings found\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Text(
			"DX12ExecuteIndirectRuntime: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
			probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());
#endif
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavs, probe->srvs, probe->cbvs, probe->rootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		if (probe->applied) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin applied origVtx=%u newVtx=%u\n",
				probe->originalVertexCount, probe->overrideVertexCount);
#endif
			return;
		}
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin matching failed\n");
#endif
	}

	static void FinishPreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		const PreSkinProbe &probe)
	{
		(void)probe;
		DX12ModRestorePreSkinningUavReplacement(commandList);
	}
};


class DX12FrameAnalysisManifestFormatUtils
{
public:
	static const char *ResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
	{
		switch (dimension) {
		case D3D12_RESOURCE_DIMENSION_BUFFER:
			return "BUFFER";
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			return "TEXTURE1D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			return "TEXTURE2D";
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			return "TEXTURE3D";
		default:
			return "UNKNOWN";
		}
	}

	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
			return "POINTLIST";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return "LINELIST";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
			return "LINESTRIP";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return "TRIANGLELIST";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			return "TRIANGLESTRIP";
		default:
			return "UNKNOWN";
		}
	}

	static const char *DxgiFormatName(UINT format)
	{
		switch (format) {
		case DXGI_FORMAT_UNKNOWN:
			return "DXGI_FORMAT_UNKNOWN";
		case DXGI_FORMAT_R16_UINT:
			return "DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R32_UINT:
			return "DXGI_FORMAT_R32_UINT";
		case DXGI_FORMAT_R32_FLOAT:
			return "DXGI_FORMAT_R32_FLOAT";
		case DXGI_FORMAT_R32G32_FLOAT:
			return "DXGI_FORMAT_R32G32_FLOAT";
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return "DXGI_FORMAT_R32G32B32_FLOAT";
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return "DXGI_FORMAT_R32G32B32A32_FLOAT";
		case DXGI_FORMAT_R16G16_FLOAT:
			return "DXGI_FORMAT_R16G16_FLOAT";
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return "DXGI_FORMAT_R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return "DXGI_FORMAT_R8G8B8A8_UNORM";
		case DXGI_FORMAT_BC1_UNORM:
			return "DXGI_FORMAT_BC1_UNORM";
		case DXGI_FORMAT_BC2_UNORM:
			return "DXGI_FORMAT_BC2_UNORM";
		case DXGI_FORMAT_BC3_UNORM:
			return "DXGI_FORMAT_BC3_UNORM";
		case DXGI_FORMAT_BC4_UNORM:
			return "DXGI_FORMAT_BC4_UNORM";
		case DXGI_FORMAT_BC5_UNORM:
			return "DXGI_FORMAT_BC5_UNORM";
		case DXGI_FORMAT_BC6H_UF16:
			return "DXGI_FORMAT_BC6H_UF16";
		case DXGI_FORMAT_BC7_UNORM:
			return "DXGI_FORMAT_BC7_UNORM";
		default:
			return "DXGI_FORMAT_OTHER";
		}
	}

	static void FormatShaderHash(UINT64 hash, bool hasHash, char *text, size_t textCount)
	{
		if (!text || textCount == 0)
			return;
		if (hasHash)
			sprintf_s(text, textCount, "%016llx", static_cast<unsigned long long>(hash));
		else
			sprintf_s(text, textCount, "-");
	}

	static void FormatTextPath(const wchar_t *filePath, wchar_t *textPath, size_t textPathCount)
	{
		if (!textPath || textPathCount == 0)
			return;
		textPath[0] = L'\0';
		if (!filePath || !filePath[0])
			return;
		wcsncpy_s(textPath, textPathCount, filePath, _TRUNCATE);
		wcscat_s(textPath, textPathCount, L".txt");
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

	static void FormatFileHash(const wchar_t *filePath, char *hash, size_t hashCount)
	{
		if (!hash || hashCount == 0)
			return;
		hash[0] = '\0';
		if (!filePath || !filePath[0])
			return;

		const wchar_t *name = wcsrchr(filePath, L'\\');
		name = name ? name + 1 : filePath;
		const wchar_t *hashMarker = nullptr;
		for (const wchar_t *cursor = wcsstr(name, L"-hash"); cursor;
				cursor = wcsstr(cursor + 1, L"-hash")) {
			const wchar_t *candidate = cursor + 5;
			if (IsHashText(candidate))
				hashMarker = candidate;
		}
		if (hashMarker) {
			CopyHashText(hashMarker, hash, hashCount);
			return;
		}

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

	static const wchar_t *DedupedFileName(const wchar_t *filePath)
	{
		if (!filePath)
			return L"";
		const wchar_t *name = wcsrchr(filePath, L'\\');
		return name ? name + 1 : filePath;
	}

	static void FormatDedupedTextName(
		const wchar_t *filePath, wchar_t *textPath, size_t textPathCount)
	{
		if (!textPath || textPathCount == 0)
			return;
		textPath[0] = L'\0';
		wcsncpy_s(textPath, textPathCount, DedupedFileName(filePath), _TRUNCATE);
		if (textPath[0])
			wcscat_s(textPath, textPathCount, L".txt");
	}

	static const char *ApiFunctionName(const char *kind)
	{
		if (!kind)
			return "Unknown";
		if (!strcmp(kind, "draw_indexed"))
			return "DrawIndexedInstanced";
		if (!strcmp(kind, "draw"))
			return "DrawInstanced";
		if (!strcmp(kind, "dispatch"))
			return "Dispatch";
		return kind;
	}

	static const char *RootDescriptorKindName(UINT rangeType)
	{
		if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
			return "CBV";
		if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
			return "SRV";
		if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
			return "UAV";
		return "";
	}
};



class DX12FrameAnalysisManifest
{
public:
	static void WriteCall(
		const char *functionName, UINT64 eventSerial, UINT64 drawId, UINT64 dispatchId,
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
		const DX12PsoShaderInfo &shaderInfo, D3D12_PRIMITIVE_TOPOLOGY topology,
		UINT vertexCountPerInstance, UINT indexCountPerInstance, UINT startVertexLocation,
		UINT startIndexLocation, INT baseVertexLocation, UINT instanceCount,
		UINT startInstanceLocation, UINT threadGroupCountX, UINT threadGroupCountY,
		UINT threadGroupCountZ, bool indexBufferValid,
		D3D12_GPU_VIRTUAL_ADDRESS indexBufferGpuVa, UINT indexBufferSize,
		DXGI_FORMAT indexBufferFormat)
	{
		char vs[32], ps[32], cs[32];
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.vs, shaderInfo.hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.ps, shaderInfo.hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.cs, shaderInfo.hasCS, cs, ARRAYSIZE(cs));

		char functionJson[64], vsJson[64], psJson[64], csJson[64], topologyJson[32];
		char cmdlistJson[32], psoJson[32];
		DX12JsonUtils::EscapeString(functionJson, sizeof(functionJson),
			DX12FrameAnalysisManifestFormatUtils::ApiFunctionName(functionName));
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(topologyJson, sizeof(topologyJson),
			DX12FrameAnalysisManifestFormatUtils::TopologyName(topology));
		sprintf_s(cmdlistJson, sizeof(cmdlistJson), "\"%p\"", commandList);
		sprintf_s(psoJson, sizeof(psoJson), "\"%p\"", pipelineState);

		char buffer[3072];
		if (dispatchId) {
			sprintf_s(buffer, sizeof(buffer),
				"\"func\":%s,"
				"\"type\":\"call.dispatch\","
				"\"call_index\":%llu,"
				"\"cmdlist\":%s,"
				"\"pipeline_state\":%s,"
				"\"pso\":%llu,"
				"\"cs\":%s,"
				"\"groups_x\":%u,"
				"\"groups_y\":%u,"
				"\"groups_z\":%u",
				functionJson,
				static_cast<unsigned long long>(eventSerial),
				cmdlistJson,
				psoJson,
				static_cast<unsigned long long>(shaderInfo.psoIndex),
				csJson,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ);
		} else {
			sprintf_s(buffer, sizeof(buffer),
				"\"func\":%s,"
				"\"type\":\"call.draw\","
				"\"call_index\":%llu,"
				"\"draw_id\":%llu,"
				"\"cmdlist\":%s,"
				"\"pipeline_state\":%s,"
				"\"pso\":%llu,"
				"\"cs\":%s,"
				"\"vs\":%s,"
				"\"ps\":%s,"
				"\"topology\":%s,"
				"\"vertex_count\":%u,"
				"\"index_count\":%u,"
				"\"start_vertex\":%u,"
				"\"start_index\":%u,"
				"\"base_vertex\":%d,"
				"\"instance_count\":%u,"
				"\"start_instance\":%u,"
				"\"ib_gpu\":\"0x%llx\","
				"\"ib_bytes\":%u,"
				"\"ib_fmt\":%u",
				functionJson,
				static_cast<unsigned long long>(eventSerial),
				static_cast<unsigned long long>(drawId),
				cmdlistJson,
				psoJson,
				static_cast<unsigned long long>(shaderInfo.psoIndex),
				csJson,
				vsJson,
				psJson,
				topologyJson,
				vertexCountPerInstance,
				indexCountPerInstance,
				startVertexLocation,
				startIndexLocation,
				baseVertexLocation,
				instanceCount,
				startInstanceLocation,
				static_cast<unsigned long long>(indexBufferValid ? indexBufferGpuVa : 0),
				indexBufferValid ? indexBufferSize : 0,
				indexBufferValid ? static_cast<UINT>(indexBufferFormat) : 0);
		}

		DX12FrameAnalysis::LogJsonFields(buffer);
	}

	static void WriteFileDump(
		const wchar_t *filePath, bool isTexture, UINT64 bytes, const char *status,
		const char *note)
	{
		char hash[16];
		wchar_t textPath[MAX_PATH];
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(filePath, hash, ARRAYSIZE(hash));
		textPath[0] = L'\0';
		if (!isTexture) {
			const wchar_t *name = DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath);
			const wchar_t *dot = name ? wcsrchr(name, L'.') : nullptr;
			if (dot) {
				const size_t prefixLength = static_cast<size_t>(dot - name);
				wcsncpy_s(textPath, ARRAYSIZE(textPath), name, prefixLength);
			} else if (name) {
				wcsncpy_s(textPath, ARRAYSIZE(textPath), name, _TRUNCATE);
			}
			wcscat_s(textPath, ARRAYSIZE(textPath), L".txt");
		}

		char statusJson[64], kindJson[16], hashJson[32], noteJson[128];
		char fileJson[512], textJson[512];
		DX12JsonUtils::EscapeString(statusJson, sizeof(statusJson), status);
		DX12JsonUtils::EscapeString(kindJson, sizeof(kindJson), isTexture ? "texture" : "buffer");
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(noteJson, sizeof(noteJson), note);
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		if (isTexture) {
			strcpy_s(textJson, sizeof(textJson), "null");
		} else {
			DX12JsonUtils::EscapeWString(textJson, sizeof(textJson), textPath);
		}

		char buffer[1024];
		sprintf_s(buffer, sizeof(buffer),
			"\"func\":\"FileDump\","
			"\"status\":%s,"
			"\"kind\":%s,"
			"\"file\":%s,"
			"\"text\":%s,"
			"\"hash\":%s,"
			"\"bytes\":%llu,"
			"\"note\":%s",
			statusJson,
			kindJson,
			fileJson,
			textJson,
			hashJson,
			static_cast<unsigned long long>(bytes),
			noteJson);

		DX12FrameAnalysis::LogJsonFields(buffer);
	}

	static void WriteIaBinding(
		const DX12FrameIaBufferBinding &buffer,
		const D3D12_RESOURCE_DESC &desc, UINT64 sourceOffset, UINT64 copyBytes,
		D3D12_RESOURCE_STATES sourceState, bool hasCurrentState,
		const wchar_t *filePath)
	{
		char vs[32], ps[32], cs[32], hash[16];
		char huntHash[16];
		char fileHash[16];
		char producerCs[32];
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.vs, buffer.shaderInfo.hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.ps, buffer.shaderInfo.hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.cs, buffer.shaderInfo.hasCS, cs, ARRAYSIZE(cs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.producerShaderInfo.cs, buffer.producerShaderInfo.hasCS,
			producerCs, ARRAYSIZE(producerCs));
		if (!buffer.producerShaderInfo.hasCS && buffer.producerPsoIndex) {
			DX12PsoShaderSummary producerSummary = {};
			if (DX12GetPsoShaderSummary(buffer.producerPsoIndex, &producerSummary)) {
				DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
					producerSummary.cs, producerSummary.hasCS,
					producerCs, ARRAYSIZE(producerCs));
			}
		}
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(
			filePath, fileHash, ARRAYSIZE(fileHash));
		sprintf_s(huntHash, sizeof(huntHash), "%08x", buffer.huntHash);
		sprintf_s(hash, sizeof(hash), "%08x", buffer.huntHash);

		char vsJson[64], psJson[64], csJson[64], producerCsJson[64];
		char roleJson[32], dimJson[32], fmtNameJson[64], skinSourceJson[64];
		char producerBindJson[64], hashJson[32], huntHashJson[32], fileHashJson[32];
		char fileJson[512], resourceJson[32];
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(producerCsJson, sizeof(producerCsJson), producerCs);
		DX12JsonUtils::EscapeString(roleJson, sizeof(roleJson), buffer.role.c_str());
		DX12JsonUtils::EscapeString(dimJson, sizeof(dimJson),
			DX12FrameAnalysisManifestFormatUtils::ResourceDimensionName(desc.Dimension));
		DX12JsonUtils::EscapeString(fmtNameJson, sizeof(fmtNameJson),
			DX12FrameAnalysisManifestFormatUtils::DxgiFormatName(buffer.format));
		DX12JsonUtils::EscapeString(skinSourceJson, sizeof(skinSourceJson),
			buffer.skinningClass.empty() ? "unknown" : buffer.skinningClass.c_str());
		DX12JsonUtils::EscapeString(producerBindJson, sizeof(producerBindJson),
			buffer.producerBindSpace.empty() ? "-" : buffer.producerBindSpace.c_str());
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(huntHashJson, sizeof(huntHashJson), huntHash);
		DX12JsonUtils::EscapeString(fileHashJson, sizeof(fileHashJson), fileHash);
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		sprintf_s(resourceJson, sizeof(resourceJson), "\"%p\"", buffer.resource.resource);

		char buffer2[3072];
		const bool ib = buffer.role == "IB";
		sprintf_s(buffer2, sizeof(buffer2),
			"\"func\":\"BindIA\","
			"\"call_index\":%llu,"
			"\"pso\":%llu,"
			"\"vs\":%s,"
			"\"ps\":%s,"
			"\"cs\":%s,"
			"\"role\":%s,"
			"\"slot\":%u,"
			"\"resource\":%s,"
			"\"dim\":%s,"
			"\"gpu\":\"0x%llx\","
			"\"offset\":%llu,"
			"\"bytes\":%llu,"
			"\"file_offset\":%llu,"
			"\"file_bytes\":%llu,"
			"\"view_gpu\":\"0x%llx\","
			"\"view_offset\":%llu,"
			"\"view_bytes\":%llu,"
			"\"draw_gpu\":\"0x%llx\","
			"\"draw_offset\":%llu,"
			"\"draw_bytes\":%llu,"
			"\"dump_scope\":\"%s\","
			"\"dump_is_draw_slice\":%s,"
			"\"stride\":%u,"
			"\"fmt\":%u,"
			"\"fmt_name\":%s,"
			"\"start_index\":%u,"
			"\"index_count\":%u,"
			"\"base_vertex\":%d,"
			"\"state\":\"0x%x\","
			"\"state_known\":%s,"
			"\"skin_source\":%s,"
			"\"producer_call_index\":%llu,"
			"\"producer_pso\":%llu,"
			"\"producer_cs\":%s,"
			"\"producer_bind\":%s,"
			"\"producer_root\":%u,"
			"\"producer_reg\":%u,"
			"\"file\":%s,"
			"\"hunt_hash\":%s,"
			"\"hash\":%s,"
			"\"file_hash\":%s",
			static_cast<unsigned long long>(buffer.eventSerial),
			static_cast<unsigned long long>(buffer.psoIndex),
			vsJson,
			psJson,
			csJson,
			roleJson,
			buffer.slot,
			resourceJson,
			dimJson,
			static_cast<unsigned long long>(buffer.gpuVa),
			static_cast<unsigned long long>(buffer.viewOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			static_cast<unsigned long long>(sourceOffset),
			static_cast<unsigned long long>(copyBytes),
			static_cast<unsigned long long>(buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.viewOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			static_cast<unsigned long long>(buffer.drawGpuVa ? buffer.drawGpuVa : buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.drawOffset),
			static_cast<unsigned long long>(buffer.drawSize ? buffer.drawSize : copyBytes),
			"full_view",
			"false",
			buffer.stride,
			buffer.format,
			fmtNameJson,
			buffer.drawStartIndex,
			buffer.drawIndexCount,
			buffer.drawBaseVertex,
			static_cast<UINT>(sourceState),
			hasCurrentState ? "true" : "false",
			skinSourceJson,
			static_cast<unsigned long long>(buffer.producerEventSerial),
			static_cast<unsigned long long>(buffer.producerPsoIndex),
			producerCsJson,
			producerBindJson,
			buffer.producerRootParameterIndex,
			buffer.producerShaderRegister,
			fileJson,
			huntHashJson,
			hashJson,
			fileHashJson);

		DX12FrameAnalysis::LogJsonFields(buffer2);
	}

	static void WriteResourceBinding(
		const DX12FrameResourceBinding &binding,
		const D3D12_RESOURCE_DESC &desc, UINT64 sourceOffset, UINT64 copyBytes,
		D3D12_RESOURCE_STATES sourceState, bool hasCurrentState,
		const wchar_t *filePath, const char *status = "", const char *note = "")
	{
		DX12PsoShaderSummary shaders;
		const bool hasShaders = DX12GetPsoShaderSummary(binding.psoIndex, &shaders);
		char vs[32], ps[32], cs[32], hash[16];
		char fileHash[16];
		const bool hasVS = binding.shaderInfo.hasVS || (hasShaders && shaders.hasVS);
		const bool hasPS = binding.shaderInfo.hasPS || (hasShaders && shaders.hasPS);
		const bool hasCS = binding.shaderInfo.hasCS || (hasShaders && shaders.hasCS);
		const UINT64 vsHash = binding.shaderInfo.hasVS ? binding.shaderInfo.vs : shaders.vs;
		const UINT64 psHash = binding.shaderInfo.hasPS ? binding.shaderInfo.ps : shaders.ps;
		const UINT64 csHash = binding.shaderInfo.hasCS ? binding.shaderInfo.cs : shaders.cs;
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(vsHash, hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(psHash, hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(csHash, hasCS, cs, ARRAYSIZE(cs));
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(
			filePath, fileHash, ARRAYSIZE(fileHash));

		const DX12DescriptorSummary &descriptor = binding.descriptor;
		const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
			descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
		const UINT64 fallbackSize = descriptor.viewSize ? descriptor.viewSize : copyBytes;
		const uint32_t descriptorHash = DX12HashDescriptorBufferView(
			binding.hasDescriptor ? &descriptor : nullptr, fallbackGpuVa, fallbackSize);
		uint32_t contentHash = 0;
		const bool hasContentHash =
			desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
			DX12TryGetTextureHash(descriptor.resource, &contentHash);
		const uint32_t viewHash = hasContentHash ? contentHash : descriptorHash;
		sprintf_s(hash, sizeof(hash), "%08x", viewHash);
		char huntHash[16];
		sprintf_s(huntHash, sizeof(huntHash), "%08x", viewHash);
		char descriptorHashText[16], contentHashText[16];
		sprintf_s(descriptorHashText, sizeof(descriptorHashText), "%08x", descriptorHash);
		sprintf_s(contentHashText, sizeof(contentHashText), "%08x", hasContentHash ? contentHash : 0);

		char vsJson[64], psJson[64], csJson[64];
		char bindJson[64], kindJson[32], dimJson[32], fmtNameJson[64], statusJson[64], noteJson[128];
		char hashJson[32], huntHashJson[32], fileHashJson[32], descriptorHashJson[32];
		char contentHashJson[32], hashSourceJson[32], fileJson[512], resourceJson[32];
		const char *kind = descriptor.kind.empty() && binding.rootDescriptor ?
			DX12FrameAnalysisManifestFormatUtils::RootDescriptorKindName(binding.rangeType) :
			descriptor.kind.c_str();
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(bindJson, sizeof(bindJson), binding.bindSpace.c_str());
		DX12JsonUtils::EscapeString(kindJson, sizeof(kindJson), kind);
		DX12JsonUtils::EscapeString(dimJson, sizeof(dimJson),
			DX12FrameAnalysisManifestFormatUtils::ResourceDimensionName(desc.Dimension));
		DX12JsonUtils::EscapeString(fmtNameJson, sizeof(fmtNameJson),
			DX12FrameAnalysisManifestFormatUtils::DxgiFormatName(static_cast<UINT>(desc.Format)));
		DX12JsonUtils::EscapeString(statusJson, sizeof(statusJson), status);
		DX12JsonUtils::EscapeString(noteJson, sizeof(noteJson), note);
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(huntHashJson, sizeof(huntHashJson), huntHash);
		DX12JsonUtils::EscapeString(fileHashJson, sizeof(fileHashJson), fileHash);
		DX12JsonUtils::EscapeString(descriptorHashJson, sizeof(descriptorHashJson), descriptorHashText);
		DX12JsonUtils::EscapeString(contentHashJson, sizeof(contentHashJson), contentHashText);
		DX12JsonUtils::EscapeString(hashSourceJson, sizeof(hashSourceJson),
			hasContentHash ? "content" : "descriptor_fallback");
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		sprintf_s(resourceJson, sizeof(resourceJson), "\"%p\"", descriptor.resource);

		char buffer[3072];
		sprintf_s(buffer, sizeof(buffer),
			"\"func\":\"BindResource\","
			"\"call_index\":%llu,"
			"\"draw_id\":%llu,"
			"\"dispatch_id\":%llu,"
			"\"pso\":%llu,"
			"\"vs\":%s,"
			"\"ps\":%s,"
			"\"cs\":%s,"
			"\"bind\":%s,"
			"\"root\":%u,"
			"\"range\":%u,"
			"\"reg\":%u,"
			"\"space\":%u,"
			"\"desc\":%llu,"
			"\"kind\":%s,"
			"\"resource\":%s,"
			"\"dim\":%s,"
			"\"width\":%llu,"
			"\"height\":%u,"
			"\"fmt\":%u,"
			"\"fmt_name\":%s,"
			"\"view_dimension\":%u,"
			"\"first_element\":%llu,"
			"\"num_elements\":%u,"
			"\"structure_byte_stride\":%u,"
			"\"buffer_view_offset\":%llu,"
			"\"buffer_view_bytes\":%llu,"
			"\"gpu\":\"0x%llx\","
			"\"offset\":%llu,"
			"\"bytes\":%llu,"
			"\"state\":\"0x%x\","
			"\"state_known\":%s,"
			"\"file\":%s,"
			"\"status\":%s,"
			"\"note\":%s,"
			"\"hash\":%s,"
			"\"hunt_hash\":%s,"
			"\"file_hash\":%s,"
			"\"hash_source\":%s,"
			"\"content_hash\":%s,"
			"\"descriptor_hash\":%s",
			static_cast<unsigned long long>(binding.eventSerial),
			static_cast<unsigned long long>(binding.drawId),
			static_cast<unsigned long long>(binding.dispatchId),
			static_cast<unsigned long long>(binding.psoIndex),
			vsJson,
			psJson,
			csJson,
			bindJson,
			binding.rootParameterIndex,
			binding.rangeIndex,
			binding.shaderRegister,
			binding.registerSpace,
			static_cast<unsigned long long>(binding.descriptorIndex),
			kindJson,
			resourceJson,
			dimJson,
			static_cast<unsigned long long>(desc.Width),
			desc.Height,
			static_cast<UINT>(desc.Format),
			fmtNameJson,
			descriptor.viewDimension,
			static_cast<unsigned long long>(descriptor.firstElement),
			descriptor.numElements,
			descriptor.structureByteStride,
			static_cast<unsigned long long>(descriptor.bufferViewOffset),
			static_cast<unsigned long long>(descriptor.bufferViewBytes),
			static_cast<unsigned long long>(descriptor.gpuVirtualAddress),
			static_cast<unsigned long long>(sourceOffset),
			static_cast<unsigned long long>(copyBytes),
			static_cast<UINT>(sourceState),
			hasCurrentState ? "true" : "false",
			fileJson,
			statusJson,
			noteJson,
			hashJson,
			huntHashJson,
			fileHashJson,
			hashSourceJson,
			contentHashJson,
			descriptorHashJson);

		DX12FrameAnalysis::LogJsonFields(buffer);
	}
};



class DX12VmFrameVariablesUtils
{
public:
	class FrameSnapshot
	{
	public:
		HWND hwnd = nullptr;
		double windowWidth = 0.0;
		double windowHeight = 0.0;
		double resWidth = 0.0;
		double resHeight = 0.0;
		double rtWidth = 0.0;
		double rtHeight = 0.0;
		double cursorScreenX = 0.0;
		double cursorScreenY = 0.0;
		double cursorWindowX = 0.0;
		double cursorWindowY = 0.0;
		double cursorX = 0.0;
		double cursorY = 0.0;
		double cursorShowing = 0.0;
		double cursorHotspotX = 0.0;
		double cursorHotspotY = 0.0;
		double effectiveDpi = 96.0;
	};

	static void UpdateForSwapChain(IDXGISwapChain *swapChain)
	{
		FrameSnapshot frame;
		const bool collected = CollectFrameSnapshot(swapChain, &frame);
		ApplyGlobals(frame);
		LogFrameVariables(frame, collected);
	}

	static bool CollectFrameSnapshot(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!frame)
			return false;
		*frame = FrameSnapshot();
		if (!swapChain)
			return false;

		DXGI_SWAP_CHAIN_DESC desc = {};
		if (FAILED(swapChain->GetDesc(&desc)))
			return false;

		frame->hwnd = desc.OutputWindow;
		frame->resWidth = PositiveDimension(desc.BufferDesc.Width);
		frame->resHeight = PositiveDimension(desc.BufferDesc.Height);

		CollectSwapChain1Size(swapChain, frame);
		CollectWindowSize(frame);

		frame->rtWidth = frame->resWidth > 0.0 ? frame->resWidth : frame->windowWidth;
		frame->rtHeight = frame->resHeight > 0.0 ? frame->resHeight : frame->windowHeight;
		frame->windowWidth = ResolveWindowWidth(*frame);
		frame->windowHeight = ResolveWindowHeight(*frame);

		if (frame->resWidth <= 0.0)
			frame->resWidth = frame->windowWidth;
		if (frame->resHeight <= 0.0)
			frame->resHeight = frame->windowHeight;
		if (frame->rtWidth <= 0.0)
			frame->rtWidth = frame->resWidth;
		if (frame->rtHeight <= 0.0)
			frame->rtHeight = frame->resHeight;

		CollectCursor(frame);
		frame->effectiveDpi = ResolveEffectiveDpi(frame->hwnd);
		return true;
	}

	static void ApplyGlobals(const FrameSnapshot &frame)
	{
		MigotoCommandVmUtils::SetGlobal(L"window_width", frame.windowWidth);
		MigotoCommandVmUtils::SetGlobal(L"window_height", frame.windowHeight);
		MigotoCommandVmUtils::SetGlobal(L"res_width", frame.resWidth);
		MigotoCommandVmUtils::SetGlobal(L"res_height", frame.resHeight);
		MigotoCommandVmUtils::SetGlobal(L"rt_width", frame.rtWidth);
		MigotoCommandVmUtils::SetGlobal(L"rt_height", frame.rtHeight);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_x", frame.cursorScreenX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_y", frame.cursorScreenY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_x", frame.cursorWindowX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_y", frame.cursorWindowY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_x", frame.cursorX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_y", frame.cursorY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_showing", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_visible", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_x", frame.cursorHotspotX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_y", frame.cursorHotspotY);
		MigotoCommandVmUtils::SetGlobal(L"effective_dpi", frame.effectiveDpi);
		MigotoCommandVmUtils::SetGlobal(L"hunting", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"frame_analysis", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"sli", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_active", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_available", 0.0);
	}

	static void CollectSwapChain1Size(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!swapChain || !frame)
			return;

		IDXGISwapChain1 *swapChain1 = nullptr;
		if (FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&swapChain1))) || !swapChain1)
			return;

		DXGI_SWAP_CHAIN_DESC1 desc1 = {};
		if (SUCCEEDED(swapChain1->GetDesc1(&desc1))) {
			if (desc1.Width > 0)
				frame->resWidth = PositiveDimension(desc1.Width);
			if (desc1.Height > 0)
				frame->resHeight = PositiveDimension(desc1.Height);
		}
		swapChain1->Release();
	}

	static void CollectWindowSize(FrameSnapshot *frame)
	{
		if (!frame || !frame->hwnd || !IsWindow(frame->hwnd))
			return;

		RECT client = {};
		if (!GetClientRect(frame->hwnd, &client))
			return;

		frame->windowWidth = PositiveSignedDimension(client.right - client.left);
		frame->windowHeight = PositiveSignedDimension(client.bottom - client.top);
	}

	static void CollectCursor(FrameSnapshot *frame)
	{
		if (!frame)
			return;

		CURSORINFO cursorInfo = {};
		cursorInfo.cbSize = sizeof(cursorInfo);
		POINT screenPoint = {};
		if (GetCursorInfo(&cursorInfo)) {
			screenPoint = cursorInfo.ptScreenPos;
			frame->cursorShowing = (cursorInfo.flags & CURSOR_SHOWING) ? 1.0 : 0.0;
			CollectCursorHotspot(cursorInfo.hCursor, frame);
		} else if (!GetCursorPos(&screenPoint)) {
			return;
		}

		frame->cursorScreenX = static_cast<double>(screenPoint.x);
		frame->cursorScreenY = static_cast<double>(screenPoint.y);

		POINT windowPoint = screenPoint;
		if (frame->hwnd && IsWindow(frame->hwnd) && ScreenToClient(frame->hwnd, &windowPoint)) {
			frame->cursorWindowX = static_cast<double>(windowPoint.x);
			frame->cursorWindowY = static_cast<double>(windowPoint.y);
		} else {
			frame->cursorWindowX = frame->cursorScreenX;
			frame->cursorWindowY = frame->cursorScreenY;
		}

		frame->cursorX = ResolveScaledCursor(
			frame->cursorWindowX, frame->cursorScreenX, frame->windowWidth);
		frame->cursorY = ResolveScaledCursor(
			frame->cursorWindowY, frame->cursorScreenY, frame->windowHeight);
	}

	static void CollectCursorHotspot(HCURSOR cursor, FrameSnapshot *frame)
	{
		if (!cursor || !frame)
			return;

		ICONINFO iconInfo = {};
		if (!GetIconInfo(cursor, &iconInfo))
			return;

		frame->cursorHotspotX = static_cast<double>(iconInfo.xHotspot);
		frame->cursorHotspotY = static_cast<double>(iconInfo.yHotspot);
		if (iconInfo.hbmMask)
			DeleteObject(iconInfo.hbmMask);
		if (iconInfo.hbmColor)
			DeleteObject(iconInfo.hbmColor);
	}

	static double ResolveWindowWidth(const FrameSnapshot &frame)
	{
		if (frame.windowWidth >= 640.0)
			return frame.windowWidth;
		if (frame.resWidth >= 640.0)
			return frame.resWidth;
		if (frame.rtWidth >= 640.0)
			return frame.rtWidth;
		return PositiveSignedDimension(GetSystemMetrics(SM_CXSCREEN));
	}

	static double ResolveWindowHeight(const FrameSnapshot &frame)
	{
		if (frame.windowHeight >= 480.0)
			return frame.windowHeight;
		if (frame.resHeight >= 480.0)
			return frame.resHeight;
		if (frame.rtHeight >= 480.0)
			return frame.rtHeight;
		return PositiveSignedDimension(GetSystemMetrics(SM_CYSCREEN));
	}

	static double ResolveScaledCursor(double windowCoord, double screenCoord, double dimension)
	{
		if (dimension <= 0.000001)
			return 0.0;

		double value = windowCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;

		value = screenCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;
		return 0.0;
	}

	static double ResolveEffectiveDpi(HWND hwnd)
	{
		typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
		static GetDpiForWindowFn getDpiForWindow = LoadGetDpiForWindow();

		UINT dpi = 0;
		if (getDpiForWindow && hwnd)
			dpi = getDpiForWindow(hwnd);
		if (dpi < 96)
			dpi = ResolveDeviceDpi(hwnd);
		return dpi >= 96 ? static_cast<double>(dpi) : 96.0;
	}

	static UINT(WINAPI *LoadGetDpiForWindow())(HWND)
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		return user32 ?
			reinterpret_cast<UINT(WINAPI *)(HWND)>(GetProcAddress(user32, "GetDpiForWindow")) :
			nullptr;
	}

	static UINT ResolveDeviceDpi(HWND hwnd)
	{
		HWND dcWindow = hwnd;
		HDC dc = GetDC(dcWindow);
		if (!dc)
		{
			dcWindow = nullptr;
			dc = GetDC(nullptr);
		}
		if (!dc)
			return 96;

		const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(dcWindow, dc);
		return dpi >= 96 ? static_cast<UINT>(dpi) : 96;
	}

	static double PositiveDimension(UINT value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double PositiveSignedDimension(LONG value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double Abs(double value)
	{
		return value < 0.0 ? -value : value;
	}

	static void LogFrameVariables(const FrameSnapshot &frame, bool collected)
	{
		DX12LogManager::Fields("DX12VmFrameVariables",
			"\"status\":\"%s\",\"hwnd\":\"%p\",\"windowWidth\":%.0f,\"windowHeight\":%.0f,"
			"\"resWidth\":%.0f,\"resHeight\":%.0f,\"rtWidth\":%.0f,\"rtHeight\":%.0f,"
			"\"cursorScreenX\":%.0f,\"cursorScreenY\":%.0f,\"cursorWindowX\":%.0f,"
			"\"cursorWindowY\":%.0f,\"cursorX\":%.6f,\"cursorY\":%.6f,"
			"\"cursorShowing\":%.0f,\"effectiveDpi\":%.0f",
			collected ? "updated" : "fallback",
			frame.hwnd,
			frame.windowWidth,
			frame.windowHeight,
			frame.resWidth,
			frame.resHeight,
			frame.rtWidth,
			frame.rtHeight,
			frame.cursorScreenX,
			frame.cursorScreenY,
			frame.cursorWindowX,
			frame.cursorWindowY,
			frame.cursorX,
			frame.cursorY,
			frame.cursorShowing,
			frame.effectiveDpi);
	}
};



// ===================== Presentation/DX12UiRuntime.hpp =====================


class DX12Overlay
{
public:
	static constexpr UINT ShowMessage = WM_APP + 0x512;
	static constexpr UINT HideMessage = WM_APP + 0x513;
	static constexpr UINT DestroyMessage = WM_APP + 0x514;
	static constexpr UINT PositionMessage = WM_APP + 0x515;
	static constexpr UINT RepaintMessage = WM_APP + 0x516;

	static DWORD WINAPI Thread(void*)
	{
		return 0;
	}

	static void EnsureWindow()
	{
	}

	static void Show()
	{
	}

	static void Hide()
	{
	}

	static void RequestRepaint()
	{
	}

	static bool UpdateWindowForSwapChain(IDXGISwapChain *swapChain)
	{
		(void)swapChain;
		return false;
	}

	static void Close()
	{
	}
};



class DX12OverlayStatusState
{
public:
	SRWLOCK lock;
	HWND overlayWindow = nullptr;
	wchar_t status[512] = L"3DMigoto DX12 hook alive";
	COLORREF statusColor = RGB(0, 255, 0);
	DWORD statusExpireTick = 0;

	DX12OverlayStatusState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12OverlayStatusState &Get()
	{
		static DX12OverlayStatusState state;
		return state;
	}

	static void SetWindow(HWND hwnd)
	{
		Get().overlayWindow = hwnd;
	}

	static HWND GetWindow()
	{
		return Get().overlayWindow;
	}

	static void SetStatusColorLocked(const wchar_t *status, COLORREF color)
	{
		DX12OverlayStatusState &state = Get();
		if (status && status[0])
			wcsncpy_s(state.status, status, _TRUNCATE);
		else
			wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
		state.statusColor = color;
		state.statusExpireTick = 0;
	}

	static void InvalidateStatus()
	{
		DX12Overlay::RequestRepaint();
	}

	static void SetStatus(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(0, 255, 0));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetWarning(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(255, 220, 0));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetError(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(255, 64, 64));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetTemporary(const wchar_t *status, DWORD durationMs)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		if (status && status[0])
			wcsncpy_s(state.status, status, _TRUNCATE);
		else
			wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
		state.statusColor = RGB(0, 255, 0);
		state.statusExpireTick = durationMs ? GetTickCount() + durationMs : 0;
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void ClearExpired()
	{
		bool changed = false;
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		if (state.statusExpireTick) {
			const DWORD now = GetTickCount();
			if (static_cast<LONG>(now - state.statusExpireTick) >= 0) {
				wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
				state.statusColor = RGB(0, 255, 0);
				state.statusExpireTick = 0;
				changed = true;
			}
		}
		ReleaseSRWLockExclusive(&state.lock);
		if (changed)
			InvalidateStatus();
	}

	static void GetStatus(wchar_t *status, size_t statusCount)
	{
		if (!status || statusCount == 0)
			return;

		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		wcsncpy_s(status, statusCount, state.status, _TRUNCATE);
		ReleaseSRWLockShared(&state.lock);
	}

	static COLORREF GetColor()
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		COLORREF color = state.statusColor;
		ReleaseSRWLockShared(&state.lock);
		return color;
	}
};



class DX12Input
{
public:
	inline static volatile LONG ReloadInProgress = 0;

	static void CompleteReload()
	{
		const DWORD start = GetTickCount();
		DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"begin_sync\"");
		const bool ok = DX12ModRuntimeReload();
		const DWORD elapsed = GetTickCount() - start;
		if (ok) {
			wchar_t status[128];
			swprintf_s(status, L"Reload Success TimeConsume: %.3f S",
				static_cast<double>(elapsed) / 1000.0);
			DX12OverlayStatusState::SetTemporary(status, 3000);
		}
		DX12LogManager::Fields("DX12ModRuntimeReload",
			"\"status\":\"done_sync\",\"result\":\"%s\",\"elapsedMs\":%lu",
			ok ? "ok" : "failed", elapsed);
		DX12ModRefreshRuntimeStateAfterReload();
		InterlockedExchange(&ReloadInProgress, 0);
	}

	static void RequestModReload()
	{
		if (InterlockedCompareExchange(&ReloadInProgress, 1, 0) != 0) {
			DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"already_running\"");
			DX12OverlayStatusState::SetWarning(L"Reload already running");
			return;
		}

		DX12OverlayStatusState::SetStatus(L"Reload running");
		// Keep DX11-style semantics: the reload and resident runtime refresh happen
		// as one operation instead of waiting for a later Present tick.
		CompleteReload();
	}
};



