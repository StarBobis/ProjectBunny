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
#include "DX12DescriptorShadowTelemetryUtils.hpp"
#include "descriptor/DX12DescriptorCopyMemoUtils.hpp"

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
		if (ResolveAndLockHeapSlot(
				destDescriptor.ptr,
				D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
				true,
				&resolved)) {
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
		if (!sourceHeap.stateLocks || !destinationHeap.stateLocks ||
				!sourceHeap.stateLocks->IsActive() ||
				!destinationHeap.stateLocks->IsActive())
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
			const CopyIdentityResult identityResult =
				ComparePublishedSlotIdentities(
					srcHeap, srcSlot, destHeap, destSlot);
			if (identityResult != CopyIdentityResult::Unknown) {
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
			DescriptorHeapRecord::StateLocks *sourceState =
				srcHeap.stateLocks.get();
			DescriptorHeapRecord::StateLocks *destinationState =
				destHeap.stateLocks.get();
			const DX12DescriptorCopyMemo::Key memoKey = {
				&srcHeap, &destHeap, srcSlot, destSlot
			};
			DX12DescriptorCopyMemo::Result memoResult;
			const bool canMemo = sourceState && destinationState &&
				sourceState->IsActive() && destinationState->IsActive() &&
				destSlot < destHeap.desc.NumDescriptors;
			if (canMemo && CopyMemo().TryLoad(
					memoKey,
					sourceState->ReadSlotVersion(srcSlot),
					destinationState->ReadSlotVersion(destSlot),
					&memoResult)) {
				if (telemetryBatch)
					telemetryBatch->RecordMemo(true);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(true);
				if (memoResult == DX12DescriptorCopyMemo::Result::Unchanged) {
					if (telemetryBatch)
						telemetryBatch->RecordResults(1, 0, 0, 1, 0, 0);
					else
						DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
							1, 0, 0, 1, 0, 0);
				} else {
					if (telemetryBatch)
						telemetryBatch->RecordResults(0, 1, 0, 0, 0, 1);
					else
						DX12DescriptorShadowTelemetryUtils::RecordCopyResults(
							0, 1, 0, 0, 0, 1);
				}
				return;
			}
			if (canMemo) {
				if (telemetryBatch)
					telemetryBatch->RecordMemo(false);
				else
					DX12DescriptorShadowTelemetryUtils::RecordCopyMemo(false);
			}
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
			bool completedCopy = false;
			if (srcHeap.stateLocks->IsActive() && destHeap.stateLocks->IsActive() &&
					destSlot < destHeap.desc.NumDescriptors) {
				copyResult =
					DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
						&destHeap, destSlot, srcHeap, srcSlot, destStart.ptr);
				completedCopy = true;
				if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed) {
					sourceFound = 1;
					changed = 1;
					DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, destSlot);
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
					sourceFound = 1;
					unchanged = 1;
				} else if (copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
					sourceMissing = 1;
					erased = 1;
					DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, destSlot);
				} else {
					sourceMissing = 1;
					alreadyEmpty = 1;
				}
			}
			if (completedCopy) {
				const DX12DescriptorCopyMemo::Result nextResult =
					(copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed ||
					 copyResult == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) ?
					DX12DescriptorCopyMemo::Result::Unchanged :
					DX12DescriptorCopyMemo::Result::SourceMissingDestinationEmpty;
				CopyMemo().Store(
					memoKey,
					sourceState->ReadSlotVersion(srcSlot),
					destinationState->ReadSlotVersion(destSlot),
					nextResult);
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
		auto accountCopyResult = [&](
			DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult result,
			UINT dSlot) {
			if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed) {
				sourceFoundCount++;
				changedCount++;
				DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
			} else if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Unchanged) {
				sourceFoundCount++;
				unchangedCount++;
			} else if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
				sourceMissingCount++;
				erasedCount++;
				DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
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
					DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
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
				DX12DescriptorHeapStateUtils::BumpVersion(&destHeap, dSlot);
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
				accountCopyResult(
					DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
						&destHeap, currentDestSlot, srcHeap, srcSlot + i,
						destHandle),
					currentDestSlot);
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
