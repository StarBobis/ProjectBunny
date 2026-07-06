#pragma once

// ---------------------------------------------------------------------------
// DX12DescriptorHeapShadowUtils
// ---------------------------------------------------------------------------
// Replaces the gRuntimeDescriptorByCpuHandle hash map with direct heap-slot
// array access, and replaces the global gDescriptorLock with per-heap SRWLOCK
// for descriptor slot operations.
//
// Key insight: gDescriptorHeaps[].slots[] is already a flat array indexed by
// descriptor heap offset.  The runtime hash map was an unnecessary duplicate
// layer that serialised all descriptor copy traffic through a single lock.
//
// This utility class operates synchronously (no async queues).  All methods
// are static; all data members are public (per project convention).

#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include "../resource/DX12ResourceTrackerPrivate.hpp"

class DX12DescriptorHeapShadowUtils
{
public:
	// ---- Per-heap lock array ----
	static constexpr UINT MAX_HEAP_LOCKS = 128;
	static SRWLOCK gHeapLocks[MAX_HEAP_LOCKS];
	static std::atomic<UINT> gHeapLockCount;

	// ---- Initialisation ----
	static void Initialize()
	{
		gHeapLockCount.store(0, std::memory_order_relaxed);
		for (UINT i = 0; i < MAX_HEAP_LOCKS; ++i) {
			gHeapLocks[i] = SRWLOCK_INIT;
		}
	}

	static void EnsureHeapLocks(size_t requiredCount)
	{
		if (requiredCount > MAX_HEAP_LOCKS)
			return;
		UINT current = gHeapLockCount.load(std::memory_order_relaxed);
		if (current < static_cast<UINT>(requiredCount)) {
			gHeapLockCount.store(static_cast<UINT>(requiredCount),
				std::memory_order_relaxed);
		}
	}

	// ---- Lock helpers (per-heap) ----
	static void LockExclusive(size_t heapIndex)
	{
		if (heapIndex < MAX_HEAP_LOCKS) {
			AcquireSRWLockExclusive(&gHeapLocks[heapIndex]);
		}
	}

	static void UnlockExclusive(size_t heapIndex)
	{
		if (heapIndex < MAX_HEAP_LOCKS) {
			ReleaseSRWLockExclusive(&gHeapLocks[heapIndex]);
		}
	}

	static void LockShared(size_t heapIndex)
	{
		if (heapIndex < MAX_HEAP_LOCKS) {
			AcquireSRWLockShared(&gHeapLocks[heapIndex]);
		}
	}

	static void UnlockShared(size_t heapIndex)
	{
		if (heapIndex < MAX_HEAP_LOCKS) {
			ReleaseSRWLockShared(&gHeapLocks[heapIndex]);
		}
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

		if (ResolveHeapSlot(cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&heapIndex, &slot)) {
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
		if (!ResolveHeapSlot(cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				&heapIndex, &slot)) {
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
		if (!ResolveHeapSlot(cpuHandle, requiredType, &heapIndex, &slot)) {
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
				bool cpuFound = false;
				{
					AcquireSRWLockShared(&gDescriptorLock);
					std::unordered_map<SIZE_T, DescriptorRecord>::const_iterator it =
						gDescriptorByCpuHandle.find(srcHandle);
					if (it != gDescriptorByCpuHandle.end()) {
						cpuRecord = it->second;
						cpuFound = true;
					}
					ReleaseSRWLockShared(&gDescriptorLock);
				}
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
		for (size_t hi = 0; hi < gDescriptorHeaps.size(); ++hi) {
			if (gDescriptorHeaps[hi].desc.Type == type &&
				gDescriptorHeaps[hi].increment > 0) {
				increment = gDescriptorHeaps[hi].increment;
				break;
			}
		}

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
inline SRWLOCK DX12DescriptorHeapShadowUtils::gHeapLocks[
	DX12DescriptorHeapShadowUtils::MAX_HEAP_LOCKS] = {};
inline std::atomic<UINT> DX12DescriptorHeapShadowUtils::gHeapLockCount{0};
