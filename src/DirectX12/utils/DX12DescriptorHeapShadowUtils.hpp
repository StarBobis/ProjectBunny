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
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include "../resource/DX12ResourceTrackerPrivate.hpp"

class DX12DescriptorHeapShadowUtils
{
public:
	// gDescriptorLock must be held while resolving this pointer. The lock object
	// itself is heap allocated, so vector growth cannot move it under a waiter.
	static SRWLOCK *HeapLock(size_t heapIndex)
	{
		return heapIndex < gDescriptorHeaps.size()
			? gDescriptorHeaps[heapIndex].stateLock.get() : nullptr;
	}

	// ---- Lock helpers (per-heap) ----
	static void LockExclusive(size_t heapIndex)
	{
		SRWLOCK *lock = HeapLock(heapIndex);
		if (lock)
			AcquireSRWLockExclusive(lock);
	}

	static void UnlockExclusive(size_t heapIndex)
	{
		SRWLOCK *lock = HeapLock(heapIndex);
		if (lock)
			ReleaseSRWLockExclusive(lock);
	}

	static void LockShared(size_t heapIndex)
	{
		SRWLOCK *lock = HeapLock(heapIndex);
		if (lock)
			AcquireSRWLockShared(lock);
	}

	static void UnlockShared(size_t heapIndex)
	{
		SRWLOCK *lock = HeapLock(heapIndex);
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
		AcquireSRWLockShared(&gDescriptorLock);
		size_t heapIndex = 0;
		UINT slot = 0;
		const bool heapResolved = ResolveHeapSlot(
			cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &heapIndex, &slot);
		if (!heapResolved) {
			ReleaseSRWLockShared(&gDescriptorLock);
			WriteToCpuLedger(std::move(record));
			return true;
		}

		LockExclusive(heapIndex);
		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		const DescriptorRecord *existing =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(heap, slot);
		const bool changed = !existing ||
			!DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
				*existing, record, cpuHandle);
		if (changed) {
			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				&heap, slot, record, cpuHandle);
			DX12DescriptorHeapStateUtils::BumpVersion(&heap);
		}
		UnlockExclusive(heapIndex);
		ReleaseSRWLockShared(&gDescriptorLock);

		ReleaseDescriptorRecordResources(&record);
		if (changed)
			BumpDescriptorQuerySerial();
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
		size_t heapIndex = 0;
		UINT slot = 0;
		AcquireSRWLockShared(&gDescriptorLock);
		if (!ResolveHeapSlot(cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&heapIndex, &slot)) {
			ReleaseSRWLockShared(&gDescriptorLock);
			return false;
		}

		LockExclusive(heapIndex);

		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		const DescriptorRecord *existing =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(heap, slot);
		const bool changed = !existing ||
			!DX12DescriptorSlotArrayUtils::SameRecordValueAtSlot(
				*existing, record, cpuHandle);
		if (changed) {
			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				&heap, slot, record, cpuHandle);
			DX12DescriptorHeapStateUtils::BumpVersion(&heap);
		}

		UnlockExclusive(heapIndex);
		ReleaseSRWLockShared(&gDescriptorLock);
		ReleaseDescriptorRecordResources(&record);
		if (changed)
			BumpDescriptorQuerySerial();
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

		AcquireSRWLockShared(&gDescriptorLock);
		const bool found = ReadDescriptorFromSlotLocked(
			cpuHandle, requiredType, outRecord);
		ReleaseSRWLockShared(&gDescriptorLock);
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
		LockShared(heapIndex);
		const DescriptorRecord *current =
			DX12DescriptorSlotArrayUtils::FindCurrentRecord(
				gDescriptorHeaps[heapIndex], slot);
		if (current)
			*outRecord = *current;
		UnlockShared(heapIndex);
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

		AcquireSRWLockShared(&gDescriptorLock);
		size_t destHeapIndex = 0;
		UINT destSlot = 0;
		const bool destResolved = ResolveHeapSlot(
			destStart.ptr, type, &destHeapIndex, &destSlot);

		size_t srcHeapIndex = 0;
		UINT srcSlot = 0;
		const bool srcResolved = ResolveHeapSlot(
			srcStart.ptr, type, &srcHeapIndex, &srcSlot);

		// If either range endpoint cannot be resolved to a known heap slot,
		// fall back to per-descriptor lookup via FindLatestDescriptor which
		// checks both heap slots and the CPU ledger.
		if (!destResolved || !srcResolved) {
			ReleaseSRWLockShared(&gDescriptorLock);
			CopyDescriptorRangeWithFallback(type, destStart, srcStart, count);
			return;
		}

		size_t firstLock = destHeapIndex;
		size_t secondLock = srcHeapIndex;
		if (srcHeapIndex < destHeapIndex) {
			firstLock = srcHeapIndex;
			secondLock = destHeapIndex;
		}

		if (destHeapIndex != srcHeapIndex) {
			LockExclusive(firstLock);
			LockExclusive(secondLock);
		} else {
			LockExclusive(destHeapIndex);
		}

		DescriptorHeapRecord &destHeap = gDescriptorHeaps[destHeapIndex];
		DescriptorHeapRecord &srcHeap = gDescriptorHeaps[srcHeapIndex];

		auto findSource = [&](UINT i) -> const DescriptorRecord* {
			const UINT sSlot = srcSlot + i;
			if (sSlot >= srcHeap.desc.NumDescriptors)
				return nullptr;

			const SIZE_T srcHandle = srcStart.ptr +
				static_cast<SIZE_T>(i) * srcHeap.increment;
			const DescriptorRecord *source =
				DX12DescriptorSlotArrayUtils::FindCurrentRecord(srcHeap, sSlot);
			if (!source) {
				const auto found = gDescriptorByCpuHandle.find(srcHandle);
				if (found != gDescriptorByCpuHandle.end())
					source = &found->second;
			}
			return source;
		};

		UINT copiedCount = 0;
		auto applyDestination = [&](UINT i, const DescriptorRecord *source) {
			const UINT dSlot = destSlot + i;
			if (dSlot >= destHeap.desc.NumDescriptors)
				return;
			if (!source) {
				if (DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
						&destHeap, dSlot))
					copiedCount++;
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
			copiedCount++;
		};

		const UINT64 srcEnd = static_cast<UINT64>(srcSlot) + count;
		const UINT64 destEnd = static_cast<UINT64>(destSlot) + count;
		const bool overlaps = destHeapIndex == srcHeapIndex &&
			static_cast<UINT64>(destSlot) < srcEnd &&
			static_cast<UINT64>(srcSlot) < destEnd;
		if (!overlaps) {
			// The overwhelmingly common path, especially CopyDescriptorsSimple:
			// no temporary vector, DescriptorRecord construction, or allocation.
			for (UINT i = 0; i < count; ++i)
				applyDestination(i, findSource(i));
		} else {
			// Snapshot first so an overlapping same-heap copy cannot overwrite a
			// later source. Unknown sources deliberately clear stale destinations.
			using SnapshotEntry =
				DX12SparseDescriptorStore<DescriptorRecord>::SnapshotEntry;
			std::vector<SnapshotEntry> snapshot(count);
			for (UINT i = 0; i < count; ++i) {
				const DescriptorRecord *source = findSource(i);
				if (!source)
					continue;
				snapshot[i].occupied = true;
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&snapshot[i].value, *source, source->cpuHandle);
			}
			for (UINT i = 0; i < count; ++i) {
				applyDestination(i, snapshot[i].occupied ?
					&snapshot[i].value : nullptr);
			}
		}

		if (copiedCount > 0) {
			DX12DescriptorHeapStateUtils::BumpVersion(&destHeap);
		}

		if (destHeapIndex != srcHeapIndex) {
			UnlockExclusive(secondLock);
			UnlockExclusive(firstLock);
		} else {
			UnlockExclusive(destHeapIndex);
		}
		ReleaseSRWLockShared(&gDescriptorLock);

		if (copiedCount > 0) {
			BumpDescriptorQuerySerial();
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
		AcquireSRWLockShared(&gDescriptorLock);
		size_t heapIndex = 0;
		UINT slot = 0;
		if (ResolveHeapSlot(cpuHandle,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &heapIndex, &slot)) {
			LockExclusive(heapIndex);
			const bool erased = DX12DescriptorSlotArrayUtils::EraseCurrentRecord(
				&gDescriptorHeaps[heapIndex], slot);
			if (erased)
				DX12DescriptorHeapStateUtils::BumpVersion(&gDescriptorHeaps[heapIndex]);
			UnlockExclusive(heapIndex);
			ReleaseSRWLockShared(&gDescriptorLock);
			if (erased)
				BumpDescriptorQuerySerial();
			return erased;
		}
		ReleaseSRWLockShared(&gDescriptorLock);

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
