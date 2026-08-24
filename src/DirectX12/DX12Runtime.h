// ============================================================
// DX12Runtime.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Tracking.h"
#include "Config/DX12Config.h"
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
			DX12DescriptorShadowTelemetryUtils::RecordCopyBatch(
				rangeCalls, slots, singleRanges, multiRanges,
				resolvedRanges, sameHeapRanges, overlapRanges,
				sourceFound, sourceMissing, changed, unchanged,
				erased, alreadyEmpty, memoHits, memoMisses,
				repeatHits);
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
		CopyTelemetryBatch *telemetryBatch = nullptr,
		HeapVersionCommitSet *versionCommits = nullptr)
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
			const bool flatIdentitiesResolved =
				srcHeap.stateLocks && destHeap.stateLocks &&
				destHeap.stateLocks->ReadFlatValueId(
					destSlot, &flatDestValueId) &&
				srcHeap.stateLocks->ReadFlatValueId(
					srcSlot, &flatSrcValueId);
			if (flatIdentitiesResolved &&
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
			// When both flat identities are current and already differ, the
			// destination provably needs the copy: a RangeMemo entry can only
			// hit while both slots still hold the versions it stored (which
			// would make the identities equal), and the seqlock identity
			// compare would just re-observe the same difference. Skip both
			// for genuinely-changed slots - the common case when the game
			// republishes its descriptor ring every frame - and go straight
			// to the copy, which revalidates via SameRecordValueAtSlot
			// before storing.
			if (!flatIdentitiesResolved) {
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
			}
			if (telemetryBatch)
				telemetryBatch->RecordMemo(false);
			else
				DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(false);
			// Batch the heap-wide invalidation token bump: consumers only
			// need it to change once per completed API call (the multi-range
			// path below already provides exactly that), while single-slot
			// ranges would otherwise pay a contended fetch_add on the shared
			// per-heap token for every changed slot.
			auto publishDestSlot = [&](UINT64 valueId) {
				if (versionCommits) {
					DX12DescriptorHeapStateUtils::PublishRangeValue(
						&destHeap, destSlot, valueId);
					versionCommits->Add(&destHeap);
				} else {
					DX12DescriptorHeapStateUtils::PublishValue(
						&destHeap, destSlot, valueId);
				}
			};
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
					publishDestSlot(publishedValueId);
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
					sourceFound = 1;
					unchanged = 1;
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
					sourceMissing = 1;
					erased = 1;
					publishDestSlot(0);
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
				publishDestSlot(publishedValueId);
			} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
				sourceFound = 1;
				unchanged = 1;
			} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
				sourceMissing = 1;
				erased = 1;
				publishDestSlot(0);
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
		auto collectHeldLock = [](HeldShardLock *locks, size_t *count,
			size_t capacity, SRWLOCK *lock, bool exclusive) {
			if (!locks || !count || !lock)
				return;
			for (size_t i = 0; i < *count; ++i) {
				if (locks[i].lock != lock)
					continue;
				locks[i].exclusive = locks[i].exclusive || exclusive;
				return;
			}
			if (*count >= capacity)
				return;
			size_t insertAt = *count;
			for (size_t i = 0; i < *count; ++i) {
				if (reinterpret_cast<uintptr_t>(lock) <
						reinterpret_cast<uintptr_t>(locks[i].lock)) {
					insertAt = i;
					break;
				}
			}
			for (size_t i = *count; i > insertAt; --i)
				locks[i] = locks[i - 1];
			locks[insertAt].lock = lock;
			locks[insertAt].exclusive = exclusive;
			++*count;
		};
		auto acquireHeldLocks = [&](const HeldShardLock *locks, size_t count) {
			for (size_t i = 0; i < count; ++i)
				acquireHeldLock(locks[i]);
		};
		auto releaseHeldLocks = [&](const HeldShardLock *locks, size_t count) {
			for (size_t i = count; i > 0; --i)
				releaseHeldLock(locks[i - 1]);
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
			// Descriptor lock striping is fixed per heap
			// (DX12SparseDescriptorStore::ShardCount), so a dirty span can touch
			// at most 2x that many distinct shard locks (source + destination).
			// Collect them into a stack buffer instead of heap-allocating a
			// vector per dirty span: CopyDescriptors is a per-frame hot path and
			// every dirty span previously paid one allocation.
			constexpr size_t kMaxHeldShardLocks =
				2 * DX12SparseDescriptorStore<DescriptorSlotBinding>::ShardCount;
			HeldShardLock heldLocks[kMaxHeldShardLocks];
			size_t heldLockCount = 0;
			auto flushDirtySpan = [&](UINT dirtyEnd) {
				if (dirtyStart == UINT_MAX || dirtyStart >= dirtyEnd)
					return;
				heldLockCount = 0;
				for (UINT dirtyIndex = dirtyStart; dirtyIndex < dirtyEnd; ++dirtyIndex) {
					collectHeldLock(
						heldLocks, &heldLockCount, kMaxHeldShardLocks,
						HeapLock(&srcHeap, srcSlot + dirtyIndex),
						false);
					collectHeldLock(
						heldLocks, &heldLockCount, kMaxHeldShardLocks,
						HeapLock(&destHeap, destSlot + dirtyIndex),
						true);
				}
				acquireHeldLocks(heldLocks, heldLockCount);
				if (!srcHeap.stateLocks->IsActive() ||
						!destHeap.stateLocks->IsActive()) {
					releaseHeldLocks(heldLocks, heldLockCount);
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
				releaseHeldLocks(heldLocks, heldLockCount);
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

	// Minimal CopyDescriptors shadow mirror. Resolve each heap once, copy the
	// common non-overlapping case directly, and snapshot only overlapping ranges.
	static bool CopyDescriptorRangeSimple(
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count)
	{
		if (!count)
			return true;
		ResolvedHeapSlot destResolved;
		ResolvedHeapSlot srcResolved;
		if (!ResolveHeapSlotFast(destStart.ptr, type, &destResolved) ||
			!ResolveHeapSlotFast(srcStart.ptr, type, &srcResolved) ||
			!destResolved.heap || !srcResolved.heap)
			return false;

		DescriptorHeapRecord &destHeap = *destResolved.heap;
		DescriptorHeapRecord &srcHeap = *srcResolved.heap;
		if (destResolved.slot > destHeap.desc.NumDescriptors ||
			count > destHeap.desc.NumDescriptors - destResolved.slot ||
			srcResolved.slot > srcHeap.desc.NumDescriptors ||
			count > srcHeap.desc.NumDescriptors - srcResolved.slot)
			return false;
		if (destHeap.slots.empty())
			EnsureDescriptorHeapStorageLocked(&destHeap);

		if (count == 1) {
			DescriptorRecord source;
			const bool present = DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
				srcHeap, srcResolved.slot, &source);
			const SIZE_T handle = destStart.ptr;
			const bool changed = present ?
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&destHeap, destResolved.slot, source, handle) :
				DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
					&destHeap, destResolved.slot);
			if (changed) {
				DX12DescriptorHeapStateUtils::PublishRangeValue(
					&destHeap, destResolved.slot, present ? source.valueId : 0);
				DX12DescriptorHeapStateUtils::CommitPublishedRange(&destHeap);
			}
			return true;
		}

		const bool sameHeap = &srcHeap == &destHeap;
		const UINT srcEnd = srcResolved.slot + count;
		const UINT destEnd = destResolved.slot + count;
		const bool overlaps = sameHeap && srcResolved.slot < destEnd &&
			destResolved.slot < srcEnd;
		std::vector<DescriptorRecord> source;
		std::vector<uint8_t> present;
		if (overlaps) {
			// A same-heap overlapping copy has memmove semantics.
			source.resize(count);
			present.assign(count, 0);
			for (UINT i = 0; i < count; ++i)
				present[i] = DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
					srcHeap, srcResolved.slot + i, &source[i]) ? 1 : 0;
		}

		bool changed = false;
		for (UINT i = 0; i < count; ++i) {
			DescriptorRecord copied;
			const bool occupied = overlaps ? present[i] :
				DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
					srcHeap, srcResolved.slot + i, &copied);
			const DescriptorRecord *value = overlaps ?
				(occupied ? &source[i] : nullptr) :
				(occupied ? &copied : nullptr);
			const UINT slot = destResolved.slot + i;
			const SIZE_T handle = destStart.ptr +
				static_cast<SIZE_T>(i) * destHeap.increment;
			const bool slotChanged = value ?
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&destHeap, slot, *value, handle) :
				DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
					&destHeap, slot);
			if (slotChanged) {
				changed = true;
				DX12DescriptorHeapStateUtils::PublishRangeValue(
					&destHeap, slot, value ? value->valueId : 0);
			}
		}
		if (changed)
			DX12DescriptorHeapStateUtils::CommitPublishedRange(&destHeap);
		return true;
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no PSO from runtime state\n");
#endif
			return;
		}

		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: GetShaderInfo failed\n");
#endif
			return;
		}
		if (!shaderInfo.hasCS) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: PSO is not compute\n");
#endif
			return;
		}

		probe->computeShaderHash = shaderInfo.cs;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: compute PSO cs=%016llx\n",
			static_cast<unsigned long long>(probe->computeShaderHash));
#endif
		if (!DX12ModShouldTrackPreSkinBindingsForCs(probe->computeShaderHash)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: CS not in pre-skin list\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin CS match\n");
#endif
		probe->bindingsFound = DX12BindingTracker::GetCurrentComputeAll(
			commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		if (!probe->bindingsFound) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no compute bindings found\n");
#endif
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin applied origVtx=%u newVtx=%u\n",
				probe->originalVertexCount, probe->overrideVertexCount);
#endif
			return;
		}
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
		MigotoCommandVmUtils::SetGlobals({
			{L"window_width", frame.windowWidth},
			{L"window_height", frame.windowHeight},
			{L"res_width", frame.resWidth},
			{L"res_height", frame.resHeight},
			{L"rt_width", frame.rtWidth},
			{L"rt_height", frame.rtHeight},
			{L"cursor_screen_x", frame.cursorScreenX},
			{L"cursor_screen_y", frame.cursorScreenY},
			{L"cursor_window_x", frame.cursorWindowX},
			{L"cursor_window_y", frame.cursorWindowY},
			{L"cursor_x", frame.cursorX},
			{L"cursor_y", frame.cursorY},
			{L"cursor_showing", frame.cursorShowing},
			{L"cursor_visible", frame.cursorShowing},
			{L"cursor_hotspot_x", frame.cursorHotspotX},
			{L"cursor_hotspot_y", frame.cursorHotspotY},
			{L"effective_dpi", frame.effectiveDpi},
			{L"hunting", 0.0},
			{L"frame_analysis", 0.0},
			{L"sli", 0.0},
			{L"stereo_active", 0.0},
			{L"stereo_available", 0.0}});
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

		// The cursor handle and hotspot are stable for long periods. GetIconInfo
		// allocates two GDI bitmaps, so doing it on every Present is needlessly
		// expensive on the render thread.
		thread_local HCURSOR cachedCursor = nullptr;
		thread_local LONG cachedHotspotX = 0;
		thread_local LONG cachedHotspotY = 0;
		if (cursor == cachedCursor) {
			frame->cursorHotspotX = static_cast<double>(cachedHotspotX);
			frame->cursorHotspotY = static_cast<double>(cachedHotspotY);
			return;
		}

		ICONINFO iconInfo = {};
		if (!GetIconInfo(cursor, &iconInfo)) {
			cachedCursor = cursor;
			cachedHotspotX = 0;
			cachedHotspotY = 0;
			return;
		}
		cachedCursor = cursor;
		cachedHotspotX = static_cast<LONG>(iconInfo.xHotspot);
		cachedHotspotY = static_cast<LONG>(iconInfo.yHotspot);

		frame->cursorHotspotX = static_cast<double>(cachedHotspotX);
		frame->cursorHotspotY = static_cast<double>(cachedHotspotY);
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
		// This is diagnostic output, not runtime behavior. Logging one formatted
		// record per Present can serialize the render thread on the log mutex and
		// turn a normal frame into a multi-millisecond CPU stall.
		if (!kVerboseEnabled)
			return;
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


	static void Show()
	{
	}

	static void Hide()
	{
	}

	static void RequestRepaint()
	{
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


	static void GetStatus(wchar_t *status, size_t statusCount)
	{
		if (!status || statusCount == 0)
			return;

		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		wcsncpy_s(status, statusCount, state.status, _TRUNCATE);
		ReleaseSRWLockShared(&state.lock);
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
