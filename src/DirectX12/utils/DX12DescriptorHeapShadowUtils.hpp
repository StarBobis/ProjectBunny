#pragma once

// ---------------------------------------------------------------------------
// DX12DescriptorHeapShadowUtils
// ---------------------------------------------------------------------------
// Replaces the gRuntimeDescriptorByCpuHandle hash map with direct heap-slot
// array access. A shared structure lock keeps heap indexes stable while
// per-heap SRWLOCK instances serialize descriptor slot operations.
//
// Key insight: gDescriptorHeaps[].slots[] is already a flat array indexed by
// descriptor heap offset.  The runtime hash map was an unnecessary duplicate
// layer that serialised all descriptor copy traffic through a single lock.
//
// This utility class operates synchronously (no async queues).  All methods
// are static; all data members are public (per project convention).

#include <windows.h>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include "../resource/DX12ResourceTrackerPrivate.hpp"

class DX12DescriptorHeapShadowUtils
{
public:
	static SRWLOCK gHeapLocksGuard;
	static std::vector<std::unique_ptr<SRWLOCK>> gHeapLocks;

	// ---- Initialisation ----
	static void Initialize()
	{
		AcquireSRWLockExclusive(&gHeapLocksGuard);
		gHeapLocks.clear();
		ReleaseSRWLockExclusive(&gHeapLocksGuard);
	}

	static void EnsureHeapLocks(size_t requiredCount)
	{
		AcquireSRWLockShared(&gHeapLocksGuard);
		const bool ready = gHeapLocks.size() >= requiredCount;
		ReleaseSRWLockShared(&gHeapLocksGuard);
		if (ready)
			return;

		AcquireSRWLockExclusive(&gHeapLocksGuard);
		while (gHeapLocks.size() < requiredCount) {
			std::unique_ptr<SRWLOCK> lock = std::make_unique<SRWLOCK>();
			InitializeSRWLock(lock.get());
			gHeapLocks.push_back(std::move(lock));
		}
		ReleaseSRWLockExclusive(&gHeapLocksGuard);
	}

	static SRWLOCK *HeapLock(size_t heapIndex)
	{
		AcquireSRWLockShared(&gHeapLocksGuard);
		SRWLOCK *lock = heapIndex < gHeapLocks.size()
			? gHeapLocks[heapIndex].get() : nullptr;
		ReleaseSRWLockShared(&gHeapLocksGuard);
		return lock;
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

	// ---- Unified descriptor recording ----

	// Unified entry point for Create*View hooks.
	// Tries heap-slot fast path first; falls back to CPU ledger if the handle
	// is not in any known descriptor heap.
	static void RecordDescriptor(DescriptorRecord &&record)
	{
		if (!record.cpuHandle) {
			ReleaseDescriptorRecordResources(&record);
			return;
		}

		SIZE_T cpuHandle = record.cpuHandle;
		size_t heapIndex = 0;
		UINT slot = 0;

		AcquireSRWLockShared(&gDescriptorLock);
		const bool heapResolved = ResolveHeapSlot(
			cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &heapIndex, &slot);
		if (heapResolved) {
			EnsureHeapLocks(heapIndex + 1);
			LockExclusive(heapIndex);

			DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
			if (slot < heap.slots.size()) {
				DescriptorRecord &existing = heap.slots[slot];
				if (existing.ownsResource && existing.resource) {
					existing.resource->Release();
				}
				if (existing.ownsCounterResource && existing.counterResource) {
					existing.counterResource->Release();
				}
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&heap.slots[slot], record, cpuHandle);
				if (slot < heap.occupied.size()) {
					heap.occupied[slot] = 1;
				}
				DX12DescriptorHeapStateUtils::BumpVersion(&heap);
			}

			UnlockExclusive(heapIndex);
		}
		ReleaseSRWLockShared(&gDescriptorLock);

		if (heapResolved) {
			// Clone to properly AddRef resources for the CPU ledger copy
			DescriptorRecord ledgerCopy = CloneDescriptorRecord(record);
			ledgerCopy.cpuHandle = cpuHandle;
			WriteToCpuLedger(std::move(ledgerCopy));

			ReleaseDescriptorRecordResources(&record);
			BumpDescriptorQuerySerial();
		} else {
			WriteToCpuLedger(std::move(record));
		}
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

		EnsureHeapLocks(heapIndex + 1);
		LockExclusive(heapIndex);

		DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
		if (slot < heap.slots.size()) {
			DescriptorRecord &existing = heap.slots[slot];
			if (existing.ownsResource && existing.resource) {
				existing.resource->Release();
			}
			if (existing.ownsCounterResource && existing.counterResource) {
				existing.counterResource->Release();
			}
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&heap.slots[slot], record, cpuHandle);
			if (slot < heap.occupied.size()) {
				heap.occupied[slot] = 1;
			}
			DX12DescriptorHeapStateUtils::BumpVersion(&heap);
		} else {
			// Slot out of bounds — release the consumed record
			ReleaseDescriptorRecordResources(&record);
		}

		UnlockExclusive(heapIndex);
		ReleaseSRWLockShared(&gDescriptorLock);
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

		size_t heapIndex = 0;
		UINT slot = 0;
		AcquireSRWLockShared(&gDescriptorLock);
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot)) {
			ReleaseSRWLockShared(&gDescriptorLock);
			return false;
		}

		EnsureHeapLocks(heapIndex + 1);
		LockShared(heapIndex);

		bool found = false;
		if (heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[heapIndex];
			if (slot < heap.slots.size() &&
				slot < heap.occupied.size() &&
				heap.occupied[slot]) {
				*outRecord = heap.slots[slot];
				found = true;
			}
		}

		UnlockShared(heapIndex);
		ReleaseSRWLockShared(&gDescriptorLock);
		return found;
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

		EnsureHeapLocks((firstLock > secondLock ? firstLock : secondLock) + 1);

		if (destHeapIndex != srcHeapIndex) {
			LockExclusive(firstLock);
			LockExclusive(secondLock);
		} else {
			LockExclusive(destHeapIndex);
		}

		DescriptorHeapRecord &destHeap = gDescriptorHeaps[destHeapIndex];
		DescriptorHeapRecord &srcHeap = gDescriptorHeaps[srcHeapIndex];

		UINT copiedCount = 0;
		for (UINT i = 0; i < count; ++i) {
			const UINT sSlot = srcSlot + i;
			const UINT dSlot = destSlot + i;

			if (sSlot >= srcHeap.desc.NumDescriptors ||
				dSlot >= destHeap.desc.NumDescriptors) {
				continue;
			}

			if (dSlot >= destHeap.slots.size()) {
				destHeap.slots.resize(dSlot + 1);
			}
			if (dSlot >= destHeap.occupied.size()) {
				destHeap.occupied.resize(dSlot + 1, 0);
			}

			if (dSlot < destHeap.slots.size()) {
				DescriptorRecord &oldDest = destHeap.slots[dSlot];
				if (oldDest.ownsResource && oldDest.resource) {
					oldDest.resource->Release();
				}
				if (oldDest.ownsCounterResource && oldDest.counterResource) {
					oldDest.counterResource->Release();
				}
			}

			const SIZE_T destHandle = destStart.ptr +
				static_cast<SIZE_T>(i) * destHeap.increment;

			if (sSlot < srcHeap.occupied.size() && srcHeap.occupied[sSlot] &&
				sSlot < srcHeap.slots.size()) {
				// Fast: direct array copy
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&destHeap.slots[dSlot], srcHeap.slots[sSlot], destHandle);
			} else {
				// Source slot not occupied — try CPU ledger
				const SIZE_T srcHandle = srcStart.ptr +
					static_cast<SIZE_T>(i) * srcHeap.increment;
				DescriptorRecord cpuRecord;
				const std::unordered_map<SIZE_T, DescriptorRecord>::const_iterator it =
					gDescriptorByCpuHandle.find(srcHandle);
				const bool cpuFound = it != gDescriptorByCpuHandle.end();
				if (cpuFound)
					cpuRecord = it->second;
				if (cpuFound) {
					DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
						&destHeap.slots[dSlot], cpuRecord, destHandle);
				} else {
					continue;
				}
			}
			destHeap.occupied[dSlot] = 1;
			copiedCount++;
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
				srcRecord.cpuHandle = destHandle;
				WriteDescriptorToSlot(destHandle, std::move(srcRecord));
				// Clone to properly AddRef resources for the CPU ledger copy
				DescriptorRecord ledgerCopy = CloneDescriptorRecord(srcRecord);
				ledgerCopy.cpuHandle = destHandle;
				WriteToCpuLedger(std::move(ledgerCopy));
				copiedCount++;
			}
		}
		if (copiedCount > 0) {
			BumpDescriptorQuerySerial();
		}
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

// Static member definitions (C++17 inline variables)
inline SRWLOCK DX12DescriptorHeapShadowUtils::gHeapLocksGuard = SRWLOCK_INIT;
inline std::vector<std::unique_ptr<SRWLOCK>>
	DX12DescriptorHeapShadowUtils::gHeapLocks;
