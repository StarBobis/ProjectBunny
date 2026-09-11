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
	//
	// Only the union member the record's kind actually names is written. The
	// record is 264 bytes per slot while the descriptor it mirrors is 32, and a
	// CBV needs 16 of the 72 union bytes, so the rest of the union is dead weight
	// on this path. Every reader selects the member by kindId - HashRecord does
	// the same to derive the value id - so bytes left untouched are never read.
	switch (src.kindId)
	{
	case DescriptorKind::CBV_ID: dest->cbv = src.cbv; break;
	case DescriptorKind::SRV_ID: dest->srv = src.srv; break;
	case DescriptorKind::UAV_ID: dest->uav = src.uav; break;
	case DescriptorKind::RTV_ID: dest->rtv = src.rtv; break;
	case DescriptorKind::DSV_ID: dest->dsv = src.dsv; break;
	case DescriptorKind::Sampler_ID: dest->sampler = src.sampler; break;
	default: static_cast<DX12DescriptorViewPayload &>(*dest) = src; break;
	}
	// kindId and kind are only ever written together (SetDescriptorKind), so an
	// equal kind id proves an equal string: deciding on the integer avoids a
	// std::string comparison - and the two string buffers it reads - on every
	// copied descriptor.
	if (dest->kindId != src.kindId)
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

// One-load slot access for the copy hot path. Resolves the chunk pointer (or
// the dense-vector bounds) exactly once and reports storage presence and
// occupancy together; the individual helpers above would re-load the chunk
// pointer up to four times per slot.
struct CurrentSlotAccess
{
	DescriptorRecord *value = nullptr; // non-null when the storage exists
	bool occupied = false;
};

static CurrentSlotAccess LoadCurrentSlotAccess(DescriptorHeapRecord &heap, UINT slot)
{
	CurrentSlotAccess access;
	if (!heap.slots.empty()) {
		if (slot < heap.slots.size() && slot < heap.occupied.size()) {
			access.value = &heap.slots[slot];
			access.occupied = heap.occupied[slot] != 0;
		}
		return access;
	}
	const DX12ChunkedDescriptorStore<DescriptorRecord>::Chunk *chunk =
		heap.chunkedSlots.ChunkForSlot(slot);
	if (chunk) {
		const uint32_t offset =
			slot & (DX12ChunkedDescriptorStore<DescriptorRecord>::ChunkSlots - 1);
		access.value = const_cast<DescriptorRecord*>(&chunk->values[offset]);
		access.occupied = chunk->occupied[offset] != 0;
	}
	return access;
}

static CurrentSlotAccess LoadCurrentSlotAccess(const DescriptorHeapRecord &heap, UINT slot)
{
	CurrentSlotAccess access;
	if (!heap.slots.empty()) {
		if (slot < heap.slots.size() && slot < heap.occupied.size()) {
			access.value = const_cast<DescriptorRecord*>(&heap.slots[slot]);
			access.occupied = heap.occupied[slot] != 0;
		}
		return access;
	}
	const DX12ChunkedDescriptorStore<DescriptorRecord>::Chunk *chunk =
		heap.chunkedSlots.ChunkForSlot(slot);
	if (chunk) {
		const uint32_t offset =
			slot & (DX12ChunkedDescriptorStore<DescriptorRecord>::ChunkSlots - 1);
		access.value = const_cast<DescriptorRecord*>(&chunk->values[offset]);
		access.occupied = chunk->occupied[offset] != 0;
	}
	return access;
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
	// Resolve the slot storage exactly once: the individual accessors would
	// re-load the chunk pointer up to four times per slot (presence, pointer,
	// occupancy, then occupancy again on the write).
	const CurrentSlotAccess access = LoadCurrentSlotAccess(*heap, slot);
	if (!access.value)
		return StoreCurrentSlotResult::LockRequired;
	if (access.occupied &&
			SameRecordValueAtSlot(*access.value, source, destCpuHandle))
		return StoreCurrentSlotResult::Unchanged;
	CopyRecordValueForSlot(access.value, source, destCpuHandle);
	if (!access.occupied) {
		SetCurrentSlotOccupied(*heap, slot, true);
		IncrementLiveSlots(heap);
	}
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
	const CurrentSlotAccess src = LoadCurrentSlotAccess(srcHeap, srcSlot);
	if (!src.occupied)
		return SourceMissingResult(destHeap, destSlot);
	if (publishedValueId)
		*publishedValueId = src.value->valueId;

	// Flat-identity early-out: when the destination's post-publication
	// snapshot still carries the source value, the copy would be a no-op
	// (SameRecordValueAtSlot compares valueId and the canonical cpuHandle,
	// which every slot write uses). Skip the record copy and the identity
	// publication entirely - two relaxed loads instead of a full copy.
	UINT64 flatValueId = 0;
	if (destHeap->stateLocks &&
			destHeap->stateLocks->ReadFlatValueId(destSlot, &flatValueId) &&
			flatValueId != 0 &&
			flatValueId == src.value->valueId)
		return CopyCurrentSlotResult::Unchanged;

	const CurrentSlotAccess dest = LoadCurrentSlotAccess(*destHeap, destSlot);
	if (!dest.value)
		return CopyCurrentSlotResult::LockRequired;
	if (dest.occupied &&
			SameRecordValueAtSlot(*dest.value, *src.value, destCpuHandle))
		return CopyCurrentSlotResult::Unchanged;
	CopyRecordValueForSlot(dest.value, *src.value, destCpuHandle);
	SetCurrentSlotOccupied(*destHeap, destSlot, true);
	if (!dest.occupied)
		IncrementLiveSlots(destHeap);
	return CopyCurrentSlotResult::Changed;
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
