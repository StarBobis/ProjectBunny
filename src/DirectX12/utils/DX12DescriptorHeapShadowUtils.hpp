#pragma once

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

#include <windows.h>
#include <d3d12.h>
#include <array>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include "../resource/DX12ResourceTrackerPrivate.hpp"

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
		static constexpr size_t EntryCount = 8;
		std::array<HeapResolveCacheEntry, EntryCount> entries;
		size_t next = 0;
	};

	static SRWLOCK *HeapLock(DescriptorHeapRecord *heap, UINT slot)
	{
		return heap && heap->stateLocks ?
			heap->stateLocks->LockForSlot(slot) : nullptr;
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
			AcquireSRWLockExclusive(lock);
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
			AcquireSRWLockShared(lock);
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
		for (const HeapResolveCacheEntry &entry : cache.entries) {
			if (!entry.heap || !entry.heap->stateLocks ||
				!entry.heap->stateLocks->IsActive() ||
				entry.cpuStart == 0 || entry.increment == 0 ||
				cpuHandle < entry.cpuStart ||
				(requiredType != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES &&
					entry.type != requiredType))
				continue;
			const SIZE_T offset = cpuHandle - entry.cpuStart;
			const UINT64 slot = offset / entry.increment;
			if (slot >= entry.numDescriptors)
				continue;
			out->heap = entry.heap;
			out->heapIndex = entry.heapIndex;
			out->slot = static_cast<UINT>(slot);
			return true;
		}

		AcquireSRWLockShared(&gDescriptorLock);
		size_t heapIndex = 0;
		UINT slot = 0;
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot)) {
			ReleaseSRWLockShared(&gDescriptorLock);
			return false;
		}
		DescriptorHeapRecord *heap = &gDescriptorHeaps[heapIndex];
		HeapResolveCacheEntry &entry =
			cache.entries[cache.next++ % HeapResolveTlsCache::EntryCount];
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
				AcquireSRWLockExclusive(lock);
			else
				AcquireSRWLockShared(lock);
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
		if (!ResolveAndLockHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, true, &resolved)) {
			WriteToCpuLedger(std::move(record));
			return true;
		}

		DescriptorHeapRecord &heap = *resolved.heap;
		const DescriptorRecord *existing =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(heap, resolved.slot);
		const bool changed = !existing ||
			!DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
				*existing, record, cpuHandle);
		if (changed) {
			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				&heap, resolved.slot, record, cpuHandle);
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
		}
		UnlockHeapSlot(resolved, true);

		ReleaseDescriptorRecordResources(&record);
		return changed;
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
		const DescriptorRecord *existing =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(heap, resolved.slot);
		const bool changed = !existing ||
			!DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
				*existing, record, cpuHandle);
		if (changed) {
			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				&heap, resolved.slot, record, cpuHandle);
			DX12DescriptorHeapStateUtils::BumpVersion(&heap, resolved.slot);
		}

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
		const DescriptorRecord *current =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(
				*resolved.heap, resolved.slot);
		if (current)
			*outRecord = *current;
		UnlockHeapSlot(resolved, false);
		const bool found = current != nullptr;
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
		const DescriptorRecord *current =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(
				gDescriptorHeaps[heapIndex], slot);
		if (current)
			*outRecord = *current;
		UnlockShared(heapIndex, slot);
		return current != nullptr;
	}

	// ---- Copy range (heap-slot fast path + CPU ledger fallback) ----

	static void CopyDescriptorRange(
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count)
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
			CopyDescriptorRangeWithFallback(type, destStart, srcStart, count);
			return;
		}

		DescriptorHeapRecord &destHeap = *destResolved.heap;
		DescriptorHeapRecord &srcHeap = *srcResolved.heap;
		const UINT destSlot = destResolved.slot;
		const UINT srcSlot = srcResolved.slot;

		auto findSource = [&](UINT i) -> const DescriptorRecord* {
			const UINT sSlot = srcSlot + i;
			if (sSlot >= srcHeap.desc.NumDescriptors)
				return nullptr;

			const DescriptorRecord *source =
				DX12DescriptorSlotArrayUtils::FindCurrentRecord(srcHeap, sSlot);
			return source;
		};

		auto applyDestination = [&](UINT i, const DescriptorRecord *source) {
			const UINT dSlot = destSlot + i;
			if (dSlot >= destHeap.desc.NumDescriptors)
				return;
			if (!source) {
				if (DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
						&destHeap, dSlot)) {
					DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
				}
				return;
			}

			const SIZE_T destHandle = destStart.ptr +
				static_cast<SIZE_T>(i) * destHeap.increment;
			const DescriptorRecord *existing =
				DX12DescriptorSlotArrayUtils::FindCurrentRecord(destHeap, dSlot);
			if (existing && DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
					*existing, *source, destHandle))
				return;

			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				&destHeap, dSlot, *source, destHandle);
			DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
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
				AcquireSRWLockExclusive(held.lock);
			else
				AcquireSRWLockShared(held.lock);
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

		const UINT64 srcEnd = static_cast<UINT64>(srcSlot) + count;
		const UINT64 destEnd = static_cast<UINT64>(destSlot) + count;
		const bool overlaps = &destHeap == &srcHeap &&
			static_cast<UINT64>(destSlot) < srcEnd &&
			static_cast<UINT64>(srcSlot) < destEnd;
		if (!overlaps) {
			// The overwhelmingly common path, especially CopyDescriptorsSimple:
			// no temporary vector or allocation. The source is shared/read-only;
			// only the destination is exclusive, so copies can fan out from one
			// stable CPU heap without serialising readers.
			for (UINT i = 0; i < count; ++i) {
				HeldShardLock first;
				HeldShardLock second;
				lockSlotPair(srcSlot + i, destSlot + i, &first, &second);
				if (!srcHeap.stateLocks->IsActive() ||
						!destHeap.stateLocks->IsActive()) {
					unlockSlotPair(first, second);
					return;
				}
				applyDestination(i, findSource(i));
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
					AcquireSRWLockShared(sourceLock);
				if (!srcHeap.stateLocks->IsActive()) {
					if (sourceLock)
						ReleaseSRWLockShared(sourceLock);
					return;
				}
				const DescriptorRecord *source = findSource(i);
				if (!source) {
					if (sourceLock)
						ReleaseSRWLockShared(sourceLock);
					continue;
				}
				snapshot[i].occupied = true;
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&snapshot[i].value, *source, source->cpuHandle);
				if (sourceLock)
					ReleaseSRWLockShared(sourceLock);
			}
			for (UINT i = 0; i < count; ++i) {
				SRWLOCK *destLock = HeapLock(&destHeap, destSlot + i);
				if (destLock)
					AcquireSRWLockExclusive(destLock);
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
		AcquireSRWLockShared(&gDescriptorLock);
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
		AcquireSRWLockShared(&gDescriptorLock);
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

		AcquireSRWLockExclusive(&gDescriptorLock);
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

		AcquireSRWLockExclusive(&gDescriptorLock);
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
