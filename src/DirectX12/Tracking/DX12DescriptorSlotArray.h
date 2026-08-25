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

static void CopyDescriptorValue(
	DX12DescriptorValue *dest, const DescriptorRecord &src)
{
	if (!dest)
		return;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	if (src.kindId == DescriptorKind::CBV_ID) {
		dest->cbv = src.cbv;
		dest->extended.reset();
		return;
	}
	dest->cbv = {};
	dest->extended = std::make_unique<DX12ExtendedDescriptorValue>();
	DX12ExtendedDescriptorValue &extended = *dest->extended;
	static_cast<DX12DescriptorViewPayload &>(extended) = src;
	extended.staticReplacementGeneration = src.staticReplacementGeneration;
	extended.resource = src.resource;
	extended.counterResource = src.counterResource;
	extended.resourceDesc = src.resourceDesc;
	extended.hasResourceDesc = src.hasResourceDesc;
	extended.resourceHeapType = src.resourceHeapType;
	extended.hasResourceHeapType = src.hasResourceHeapType;
	extended.gpuVirtualAddress = src.gpuVirtualAddress;
	extended.resourceOffset = src.resourceOffset;
	extended.viewSize = src.viewSize;
	extended.currentState = src.currentState;
	extended.hasCurrentState = src.hasCurrentState;
	extended.stateTrackedByBarrier = src.stateTrackedByBarrier;
	extended.hasDesc = src.hasDesc;
}

static void CopyRecordValueForSlot(
	DescriptorRecord *dest,
	const DX12DescriptorValue &src,
	SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	const char *kind = "";
	switch (src.kindId) {
	case DescriptorKind::CBV_ID: kind = "CBV"; break;
	case DescriptorKind::SRV_ID: kind = "SRV"; break;
	case DescriptorKind::UAV_ID: kind = "UAV"; break;
	case DescriptorKind::RTV_ID: kind = "RTV"; break;
	case DescriptorKind::DSV_ID: kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: kind = "Sampler"; break;
	}
	if (dest->kind != kind)
		dest->kind = kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	dest->staticReplacementGeneration = 0;
	// Value-path records (CBV fast path) never carry a cached hunt hash.
	dest->descriptorHuntHash = 0;
	dest->cpuHandle = destCpuHandle;
	dest->resource = nullptr;
	dest->ownsResource = false;
	dest->counterResource = nullptr;
	dest->ownsCounterResource = false;
	dest->resourceDesc = {};
	dest->hasResourceDesc = false;
	dest->resourceHeapType = 0;
	dest->hasResourceHeapType = false;
	dest->gpuVirtualAddress = 0;
	dest->resourceOffset = 0;
	dest->viewSize = 0;
	dest->currentState = D3D12_RESOURCE_STATE_COMMON;
	dest->hasCurrentState = false;
	dest->stateTrackedByBarrier = false;
	dest->hasDesc = false;
	if (src.kindId == DescriptorKind::CBV_ID) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		dest->cbv = src.cbv;
		dest->gpuVirtualAddress = src.cbv.BufferLocation;
		dest->viewSize = src.cbv.SizeInBytes;
		dest->hasDesc = true;
		return;
	}
	if (!src.extended) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		return;
	}
	const DX12ExtendedDescriptorValue &extended = *src.extended;
	static_cast<DX12DescriptorViewPayload &>(*dest) = extended;
	dest->resource = extended.resource;
	dest->staticReplacementGeneration = extended.staticReplacementGeneration;
	dest->counterResource = extended.counterResource;
	dest->resourceDesc = extended.resourceDesc;
	dest->hasResourceDesc = extended.hasResourceDesc;
	dest->resourceHeapType = extended.resourceHeapType;
	dest->hasResourceHeapType = extended.hasResourceHeapType;
	dest->gpuVirtualAddress = extended.gpuVirtualAddress;
	dest->resourceOffset = extended.resourceOffset;
	dest->viewSize = extended.viewSize;
	dest->currentState = extended.currentState;
	dest->hasCurrentState = extended.hasCurrentState;
	dest->stateTrackedByBarrier = extended.stateTrackedByBarrier;
	dest->hasDesc = extended.hasDesc;
}

static DX12DescriptorValue MakeConstantBufferViewValue(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc,
	UINT64 valueId)
{
	DX12DescriptorValue value;
	value.kindId = DescriptorKind::CBV_ID;
	value.valueId = valueId;
	value.cbv = desc;
	return value;
}

static bool SameRecordValueAtSlot(const DescriptorRecord &dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	return src.valueId != 0 && dest.valueId == src.valueId &&
		dest.cpuHandle == destCpuHandle;
}

static const DescriptorSlotBinding *FindCurrentBinding(
	const DescriptorHeapRecord &heap, UINT slot)
{
	return heap.currentDescriptors.Find(slot);
}

static bool CopyCurrentRecord(
	const DescriptorHeapRecord &heap, UINT slot, DescriptorRecord *record)
{
	if (!record)
		return false;
	const DescriptorSlotBinding *binding = FindCurrentBinding(heap, slot);
	if (binding && binding->value) {
		CopyRecordValueForSlot(
			record, binding->value->value,
			heap.cpuStart + static_cast<SIZE_T>(slot) * heap.increment);
		return true;
	}
	if (slot < heap.occupied.size() && heap.occupied[slot] &&
			slot < heap.slots.size()) {
		CopyRecordValueForSlot(record, heap.slots[slot], heap.slots[slot].cpuHandle);
		return true;
	}
	return false;
}


static bool StoreCurrentRecordIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && SameRecordValueAtSlot(
				heap->slots[slot], source, destCpuHandle))
			return false;
		CopyRecordValueForSlot(&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value && source.valueId != 0 &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	CopyDescriptorValue(&newValue->value, source);
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

static bool StoreCurrentValueIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DX12DescriptorValue &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors || source.valueId == 0)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && heap->slots[slot].valueId == source.valueId &&
				heap->slots[slot].cpuHandle == destCpuHandle)
			return false;
		CopyRecordValueForSlot(
			&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	newValue->value = source;
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

enum class CopyCurrentSlotResult
{
	SourceMissingDestinationEmpty,
	SourceMissingDestinationErased,
	Changed,
	Unchanged
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

	const DescriptorSlotBinding *sourceBinding = FindCurrentBinding(srcHeap, srcSlot);
	if (sourceBinding && sourceBinding->value && destHeap->slots.empty()) {
		if (publishedValueId)
			*publishedValueId = sourceBinding->value->value.valueId;
		bool inserted = false;
		DescriptorSlotBinding *destBinding =
			destHeap->currentDescriptors.FindOrInsertDefault(destSlot, &inserted);
		if (!inserted && destBinding->value &&
				(destBinding->value == sourceBinding->value ||
				 destBinding->value->value.valueId ==
					sourceBinding->value->value.valueId))
			return CopyCurrentSlotResult::Unchanged;
		DX12DescriptorValuePool::Retain(sourceBinding->value);
		DX12DescriptorValueNode *oldValue = destBinding->value;
		destBinding->value = sourceBinding->value;
		DX12DescriptorValuePool::Release(oldValue);
		if (inserted)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	// Dense-to-dense direct copy: when both sides already have their dense
	// slot arrays materialized, copy the record straight from src slot to
	// dest slot in one pass. The previous flow went through a temporary
	// DescriptorRecord (CopyCurrentRecord -> StoreCurrentRecordIfChanged),
	// which paid the full field copy twice per descriptor. This mirrors
	// RenderDoc's single dst.CopyFrom(src) per descriptor.
	if (!srcHeap.slots.empty() && srcSlot < srcHeap.slots.size() &&
			srcSlot < srcHeap.occupied.size() && srcHeap.occupied[srcSlot] &&
			!destHeap->slots.empty() && destSlot < destHeap->slots.size() &&
			destSlot < destHeap->occupied.size()) {
		const DescriptorRecord &srcRecord = srcHeap.slots[srcSlot];
		DescriptorRecord &destRecord = destHeap->slots[destSlot];
		if (publishedValueId)
			*publishedValueId = srcRecord.valueId;
		const bool wasOccupied = destHeap->occupied[destSlot] != 0;
		if (wasOccupied &&
				SameRecordValueAtSlot(destRecord, srcRecord, destCpuHandle))
			return CopyCurrentSlotResult::Unchanged;
		CopyRecordValueForSlot(&destRecord, srcRecord, destCpuHandle);
		destHeap->occupied[destSlot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	DescriptorRecord source;
	if (!CopyCurrentRecord(srcHeap, srcSlot, &source))
		return SourceMissingResult(destHeap, destSlot);
	if (publishedValueId)
		*publishedValueId = source.valueId;
	return StoreCurrentRecordIfChanged(
		destHeap, destSlot, source, destCpuHandle) ?
		CopyCurrentSlotResult::Changed : CopyCurrentSlotResult::Unchanged;
}

static bool EraseCurrentRecord(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		if (!heap->occupied[slot])
			return false;
		heap->slots[slot] = DescriptorRecord();
		heap->occupied[slot] = 0;
		DecrementLiveSlots(heap);
		return true;
	}
	DescriptorSlotBinding *binding = heap->currentDescriptors.Find(slot);
	DX12DescriptorValueNode *oldValue = binding ? binding->value : nullptr;
	const bool erased = heap->currentDescriptors.Erase(slot);
	if (erased)
		DX12DescriptorValuePool::Release(oldValue);
	if (erased)
		DecrementLiveSlots(heap);
	return erased;
}

static void ClearSparseRecords(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	heap->currentDescriptors.ForEach(
		[](uint32_t, const DescriptorSlotBinding &binding) {
			DX12DescriptorValuePool::Release(binding.value);
		});
	heap->currentDescriptors.Clear();
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

