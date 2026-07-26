#pragma once

#include "Tracking/DX12ResourceInfrastructure.hpp"

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
				erased, alreadyEmpty, memoHits, memoMisses);
		}
	};

	static SRWLOCK *HeapLock(DescriptorHeapRecord *heap, UINT slot)
	{
		return heap && heap->stateLocks ?
			heap->stateLocks->LockForSlot(slot) : nullptr;
	}

	static DX12DescriptorCopyMemo &CopyMemo()
	{
		static thread_local DX12DescriptorCopyMemo memo;
		return memo;
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
		const UINT64 valueId =
			DX12DescriptorValueHashUtils::HashConstantBufferView(*desc);
		const DX12DescriptorValue value =
			DX12DescriptorSlotArrayUtils::MakeConstantBufferViewValue(
				*desc, valueId);

		ResolvedHeapSlot resolved;
		const bool resolvedKnownHeap = ResolveHeapSlotFast(
				destDescriptor.ptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&resolved);
		// ResolveHeapSlotFast already validates the heap activity immediately
		// before returning. PublishedSlotValueEquals performs the authoritative
		// seqlock read, so another activity load here only duplicated an atomic on
		// every CreateCBV call.
		if (resolvedKnownHeap &&
				resolved.heap && resolved.heap->stateLocks &&
				resolved.heap->stateLocks->PublishedSlotValueEquals(
					resolved.slot, valueId)) {
			return false;
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
		if (!ResolveAndLockHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, true, &resolved)) {
			return false;
		}

		DescriptorHeapRecord &heap = *resolved.heap;
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
		LockShared(heapIndex, slot);
		const bool found =
			DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
				gDescriptorHeaps[heapIndex], slot, outRecord);
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
		UINT destinationSlot)
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
			const DX12DescriptorCopyMemo::Key memoKey = {
				&srcHeap,
				&destHeap,
				srcSlot,
				destSlot
			};
			const UINT64 sourceVersion =
				srcHeap.stateLocks ? srcHeap.stateLocks->ReadSlotVersion(srcSlot) : 0;
			const UINT64 destinationVersion =
				destHeap.stateLocks ? destHeap.stateLocks->ReadSlotVersion(destSlot) : 0;
			DX12DescriptorCopyMemo::Result memoResult =
				DX12DescriptorCopyMemo::Result::SourceMissingDestinationEmpty;
			if ((sourceVersion & 1ull) == 0 &&
				(destinationVersion & 1ull) == 0 &&
				CopyMemo().TryLoad(
					memoKey,
					sourceVersion,
					destinationVersion,
					&memoResult)) {
				if (telemetryBatch)
					telemetryBatch->RecordMemo(true);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
				if (telemetryBatch)
					telemetryBatch->RecordResults(
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 1 : 0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 0 : 1,
						0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 1 : 0,
						0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 0 : 1);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 1 : 0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 0 : 1,
						0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 1 : 0,
						0,
						memoResult == DX12DescriptorCopyMemo::Result::Unchanged ? 0 : 1);
				return;
			}
			// A completed copy makes this exact source/destination pair a proven
			// no-op until either slot is published again.  Consult the slot-version
			// memo before loading both published value IDs or taking shard locks.
			// This is the normal steady-state path for descriptor rings: the game
			// repeatedly copies stable staging descriptors into stable visible slots.
			// Most pairs in the captured workload mutate every frame. The old
			// 65K x 4-way TLS memo missed 87% of probes and made a cache miss more
			// expensive than reading the authoritative slot identities directly.
			// Source descriptor rings commonly republish a slot with a new version
			// even when its value already matches the visible destination. Version-
			// keyed memoization therefore misses and performs a large random TLS cache
			// lookup precisely where the published value IDs can prove the copy is a
			// no-op directly. The seqlock comparison is the authoritative proof.
			const CopyIdentityResult identityResult =
				ComparePublishedSlotIdentities(
					srcHeap, srcSlot, destHeap, destSlot);
			if (identityResult != CopyIdentityResult::Unknown) {
				if ((sourceVersion & 1ull) == 0 &&
					(destinationVersion & 1ull) == 0) {
					CopyMemo().Store(
						memoKey,
						sourceVersion,
						destinationVersion,
						identityResult == CopyIdentityResult::Unchanged ?
							DX12DescriptorCopyMemo::Result::Unchanged :
							DX12DescriptorCopyMemo::Result::SourceMissingDestinationEmpty);
				}
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
			// The overwhelmingly common path, especially CopyDescriptorsSimple:
			// no temporary vector or allocation. The source is shared/read-only;
			// only the destination is exclusive, so copies can fan out from one
			// stable CPU heap without serialising readers.
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
				HeldShardLock first;
				HeldShardLock second;
				lockSlotPair(srcSlot + i, destSlot + i, &first, &second);
				if (!srcHeap.stateLocks->IsActive() ||
						!destHeap.stateLocks->IsActive()) {
					unlockSlotPair(first, second);
					return;
				}
				const UINT currentDestSlot = destSlot + i;
				const SIZE_T destHandle = destStart.ptr +
					static_cast<SIZE_T>(i) * destHeap.increment;
				UINT64 publishedValueId = 0;
				accountCopyResult(
					DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
						&destHeap, currentDestSlot, srcHeap, srcSlot + i,
						destHandle, &publishedValueId),
					currentDestSlot, publishedValueId);
				unlockSlotPair(first, second);
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

		UINT increment = 0;
		DX12DescriptorShadowTelemetryUtils::AcquireGlobalShared(&gDescriptorLock);
		for (size_t hi = 0; hi < gDescriptorHeaps.size(); ++hi) {
			if (gDescriptorHeaps[hi].desc.Type == type &&
				gDescriptorHeaps[hi].increment > 0) {
				increment = gDescriptorHeaps[hi].increment;
				break;
			}
		}
		ReleaseSRWLockShared(&gDescriptorLock);

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
