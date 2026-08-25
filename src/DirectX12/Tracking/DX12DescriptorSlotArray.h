// Descriptor slot array storage and copy helpers.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12DescriptorRecords.h"

struct DescriptorSlotLocation
{
	size_t heapIndex = 0;
	UINT slot = 0;
};

class DX12DescriptorSlotArrayUtils
{
public:
static LONG64 ReadLiveSlots(const DescriptorHeapRecord *heap)
{
	return heap ? InterlockedCompareExchange64(
		const_cast<volatile LONG64*>(&heap->liveSlots), 0, 0) : 0;
}

static void SetLiveSlots(DescriptorHeapRecord *heap, LONG64 value)
{
	if (heap)
		InterlockedExchange64(&heap->liveSlots, value);
}

static void IncrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (heap)
		InterlockedIncrement64(&heap->liveSlots);
}

static void DecrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	LONG64 current = ReadLiveSlots(heap);
	while (current > 0) {
		const LONG64 observed = InterlockedCompareExchange64(
			&heap->liveSlots, current - 1, current);
		if (observed == current)
			return;
		current = observed;
	}
}

static void CopyRecordValueForSlot(DescriptorRecord *dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	// Descriptor copies dominate some DX12 games. DescriptorRecord's generated
	// assignment unconditionally assigns std::string even though a slot almost
	// always keeps the same descriptor kind (especially CBV). Copy the POD-like
	// payload explicitly and only touch the string when its value really changes.
	static_cast<DX12DescriptorViewPayload &>(*dest) = src;
	if (dest->kind != src.kind)
		dest->kind = src.kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	dest->staticReplacementGeneration = src.staticReplacementGeneration;
	dest->descriptorHuntHash = src.descriptorHuntHash;
	dest->cpuHandle = destCpuHandle;
	dest->resource = src.resource;
	// Descriptor heap slots mirror descriptor values. Ownership stays with the
	// CPU descriptor ledger/resource tracker, so slot copies do not multiply
	// COM references.
	dest->ownsResource = false;
	dest->counterResource = src.counterResource;
	dest->ownsCounterResource = false;
	dest->resourceDesc = src.resourceDesc;
	dest->hasResourceDesc = src.hasResourceDesc;
	dest->resourceHeapType = src.resourceHeapType;
	dest->hasResourceHeapType = src.hasResourceHeapType;
	dest->gpuVirtualAddress = src.gpuVirtualAddress;
	dest->resourceOffset = src.resourceOffset;
	dest->viewSize = src.viewSize;
	dest->currentState = src.currentState;
	dest->hasCurrentState = src.hasCurrentState;
	dest->stateTrackedByBarrier = src.stateTrackedByBarrier;
	dest->hasDesc = src.hasDesc;
}

static bool SameRecordValueAtSlot(const DescriptorRecord &dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	return src.valueId != 0 && dest.valueId == src.valueId &&
		dest.cpuHandle == destCpuHandle;
}

// ---- Unified current-value storage access ----
// Every heap keeps either the materialized dense adapter (Dump/Hunt modes;
// slots/occupied vectors) or the chunked dense store (normal Mod mode) as the
// authoritative current value. These helpers dispatch on which one exists.

static bool CurrentSlotStoragePresent(const DescriptorHeapRecord &heap, UINT slot)
{
	if (!heap.slots.empty())
		return slot < heap.slots.size() && slot < heap.occupied.size();
	return heap.chunkedSlots.ChunkExists(slot);
}

static const DescriptorRecord *CurrentSlotPtr(
	const DescriptorHeapRecord &heap, UINT slot)
{
	if (!heap.slots.empty()) {
		if (slot >= heap.slots.size() || slot >= heap.occupied.size())
			return nullptr;
		return &heap.slots[slot];
	}
	return heap.chunkedSlots.SlotPtr(slot);
}

static DescriptorRecord *CurrentSlotPtr(DescriptorHeapRecord &heap, UINT slot)
{
	if (!heap.slots.empty()) {
		if (slot >= heap.slots.size() || slot >= heap.occupied.size())
			return nullptr;
		return &heap.slots[slot];
	}
	return heap.chunkedSlots.SlotPtr(slot);
}

static bool CurrentSlotOccupied(const DescriptorHeapRecord &heap, UINT slot)
{
	if (!heap.slots.empty()) {
		return slot < heap.slots.size() && slot < heap.occupied.size() &&
			heap.occupied[slot] != 0;
	}
	return heap.chunkedSlots.IsOccupied(slot);
}

static void SetCurrentSlotOccupied(DescriptorHeapRecord &heap, UINT slot, bool occupied)
{
	if (!heap.slots.empty()) {
		if (slot < heap.occupied.size())
			heap.occupied[slot] = occupied ? 1 : 0;
		return;
	}
	heap.chunkedSlots.SetOccupied(slot, occupied);
}

static bool CopyCurrentRecord(
	const DescriptorHeapRecord &heap, UINT slot, DescriptorRecord *record)
{
	if (!record)
		return false;
	const DescriptorRecord *value = CurrentSlotOccupied(heap, slot) ?
		CurrentSlotPtr(heap, slot) : nullptr;
	if (!value)
		return false;
	CopyRecordValueForSlot(record, *value, value->cpuHandle);
	return true;
}

// Store outcome: Stored/Unchanged when the storage was present and the record
// was handled; LockRequired when the destination chunk is not allocated yet
// and the caller must allocate it under the owning shard lock, then retry.
enum class StoreCurrentSlotResult
{
	Stored,
	Unchanged,
	LockRequired,
	Invalid
};

static StoreCurrentSlotResult StoreCurrentRecordIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return StoreCurrentSlotResult::Invalid;
	if (!CurrentSlotStoragePresent(*heap, slot))
		return StoreCurrentSlotResult::LockRequired;
	DescriptorRecord *dest = CurrentSlotPtr(*heap, slot);
	const bool wasOccupied = CurrentSlotOccupied(*heap, slot);
	if (wasOccupied && SameRecordValueAtSlot(*dest, source, destCpuHandle))
		return StoreCurrentSlotResult::Unchanged;
	CopyRecordValueForSlot(dest, source, destCpuHandle);
	SetCurrentSlotOccupied(*heap, slot, true);
	if (!wasOccupied)
		IncrementLiveSlots(heap);
	return StoreCurrentSlotResult::Stored;
}

enum class CopyCurrentSlotResult
{
	SourceMissingDestinationEmpty,
	SourceMissingDestinationErased,
	Changed,
	Unchanged,
	// Destination chunk is not allocated; the caller must take the
	// destination shard lock, allocate the chunk and retry the copy.
	LockRequired
};

static CopyCurrentSlotResult CopyCurrentSlotIfChanged(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	const DescriptorHeapRecord &srcHeap,
	UINT srcSlot,
	SIZE_T destCpuHandle,
	UINT64 *publishedValueId = nullptr)
{
	if (publishedValueId)
		*publishedValueId = 0;
	if (!destHeap || destSlot >= destHeap->desc.NumDescriptors ||
			srcSlot >= srcHeap.desc.NumDescriptors) {
		return SourceMissingResult(destHeap, destSlot);
	}

	// Dense-to-dense direct copy: when both sides already have their slot
	// storage (materialized adapter or allocated chunk), copy the record
	// straight from the source slot to the destination slot in one pass. The
	// previous flow went through a temporary DescriptorRecord
	// (CopyCurrentRecord -> StoreCurrentRecordIfChanged), which paid the full
	// field copy twice per descriptor. This mirrors RenderDoc's single
	// dst.CopyFrom(src) per descriptor.
	const DescriptorRecord *srcRecord =
		CurrentSlotOccupied(srcHeap, srcSlot) ?
			CurrentSlotPtr(srcHeap, srcSlot) : nullptr;
	if (!srcRecord)
		return SourceMissingResult(destHeap, destSlot);
	if (CurrentSlotStoragePresent(*destHeap, destSlot)) {
		DescriptorRecord *destRecord = CurrentSlotPtr(*destHeap, destSlot);
		if (publishedValueId)
			*publishedValueId = srcRecord->valueId;
		const bool wasOccupied = CurrentSlotOccupied(*destHeap, destSlot);
		if (wasOccupied && SameRecordValueAtSlot(*destRecord, *srcRecord, destCpuHandle))
			return CopyCurrentSlotResult::Unchanged;
		CopyRecordValueForSlot(destRecord, *srcRecord, destCpuHandle);
		SetCurrentSlotOccupied(*destHeap, destSlot, true);
		if (!wasOccupied)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	// Destination storage is missing: the caller must allocate the chunk
	// under the destination shard lock and retry.
	return CopyCurrentSlotResult::LockRequired;
}

static bool EraseCurrentRecord(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap)
		return false;
	if (!CurrentSlotStoragePresent(*heap, slot))
		return false;
	if (!CurrentSlotOccupied(*heap, slot))
		return false;
	*CurrentSlotPtr(*heap, slot) = DescriptorRecord();
	SetCurrentSlotOccupied(*heap, slot, false);
	DecrementLiveSlots(heap);
	return true;
}

private:
static CopyCurrentSlotResult SourceMissingResult(
	DescriptorHeapRecord *destHeap, UINT destSlot)
{
	return EraseCurrentRecord(destHeap, destSlot) ?
		CopyCurrentSlotResult::SourceMissingDestinationErased :
		CopyCurrentSlotResult::SourceMissingDestinationEmpty;
}
};
