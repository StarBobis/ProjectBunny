#include "DX12ResourceTrackerPrivate.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

static LONG ReferenceCount(DX12DescriptorValueNode *node)
{
	return node ? InterlockedCompareExchange(&node->references, 0, 0) : 0;
}

static void InitializeSparseHeap(
	DescriptorHeapRecord *heap,
	UINT descriptorCount,
	SIZE_T cpuStart)
{
	Require(heap != nullptr);
	heap->desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap->desc.NumDescriptors = descriptorCount;
	heap->cpuStart = cpuStart;
	heap->increment = 32;
}

static SIZE_T SlotHandle(const DescriptorHeapRecord &heap, UINT slot)
{
	return heap.cpuStart + static_cast<SIZE_T>(slot) * heap.increment;
}

static DescriptorRecord MakeRecord(UINT64 valueId, UINT token)
{
	DescriptorRecord record;
	record.kind = "SRV";
	record.kindId = 2;
	record.valueId = valueId;
	record.cpuHandle = 0xabc000 + token * 32;
	record.resource = reinterpret_cast<ID3D12Resource*>(
		static_cast<uintptr_t>(0x100000 + token * 0x100));
	record.ownsResource = true;
	record.counterResource = reinterpret_cast<ID3D12Resource*>(
		static_cast<uintptr_t>(0x200000 + token * 0x100));
	record.ownsCounterResource = true;
	record.resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	record.resourceDesc.Alignment = 65536;
	record.resourceDesc.Width = 256 + token;
	record.resourceDesc.Height = 128 + token;
	record.resourceDesc.DepthOrArraySize = 1;
	record.resourceDesc.MipLevels = static_cast<UINT16>(1 + token % 4);
	record.resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	record.resourceDesc.SampleDesc.Count = 1;
	record.resourceDesc.SampleDesc.Quality = token;
	record.resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	record.resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	record.hasResourceDesc = true;
	record.resourceHeapType = D3D12_HEAP_TYPE_DEFAULT;
	record.hasResourceHeapType = true;
	record.gpuVirtualAddress = 0x300000 + token * 0x1000;
	record.resourceOffset = token * 64;
	record.viewSize = 4096 + token;
	record.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	record.hasCurrentState = true;
	record.stateTrackedByBarrier = true;
	record.hasDesc = true;
	record.srv = {};
	record.srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	record.srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	record.srv.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	record.srv.Texture2D.MostDetailedMip = token % 3;
	record.srv.Texture2D.MipLevels = 1 + token % 4;
	record.srv.Texture2D.PlaneSlice = token % 2;
	record.srv.Texture2D.ResourceMinLODClamp = static_cast<float>(token) / 8.0f;
	return record;
}

static void RequireSameDescriptorValue(
	const DescriptorRecord &actual,
	const DescriptorRecord &expected,
	SIZE_T expectedCpuHandle)
{
	Require(std::memcmp(
		static_cast<const DX12DescriptorViewPayload*>(&actual),
		static_cast<const DX12DescriptorViewPayload*>(&expected),
		sizeof(DX12DescriptorViewPayload)) == 0);
	Require(actual.kind == expected.kind);
	Require(actual.kindId == expected.kindId);
	Require(actual.valueId == expected.valueId);
	Require(actual.cpuHandle == expectedCpuHandle);
	Require(actual.resource == expected.resource);
	Require(!actual.ownsResource);
	Require(actual.counterResource == expected.counterResource);
	Require(!actual.ownsCounterResource);
	Require(std::memcmp(
		&actual.resourceDesc, &expected.resourceDesc,
		sizeof(D3D12_RESOURCE_DESC)) == 0);
	Require(actual.hasResourceDesc == expected.hasResourceDesc);
	Require(actual.resourceHeapType == expected.resourceHeapType);
	Require(actual.hasResourceHeapType == expected.hasResourceHeapType);
	Require(actual.gpuVirtualAddress == expected.gpuVirtualAddress);
	Require(actual.resourceOffset == expected.resourceOffset);
	Require(actual.viewSize == expected.viewSize);
	Require(actual.currentState == expected.currentState);
	Require(actual.hasCurrentState == expected.hasCurrentState);
	Require(actual.stateTrackedByBarrier == expected.stateTrackedByBarrier);
	Require(actual.hasDesc == expected.hasDesc);
}

static void RequireValueAt(
	const DescriptorHeapRecord &heap,
	UINT slot,
	const DescriptorRecord &expected)
{
	DescriptorRecord materialized;
	Require(DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
		heap, slot, &materialized));
	RequireSameDescriptorValue(materialized, expected, SlotHandle(heap, slot));
}

static std::vector<DX12DescriptorValueNode*> CurrentValueNodes(
	const DescriptorHeapRecord &heap)
{
	std::vector<DX12DescriptorValueNode*> nodes;
	heap.currentDescriptors.ForEach(
		[&nodes](uint32_t, const DescriptorSlotBinding &binding) {
			if (!binding.value ||
					std::find(nodes.begin(), nodes.end(), binding.value) != nodes.end())
				return;
			nodes.push_back(binding.value);
		});
	return nodes;
}

static void ClearAndRequireReleased(DescriptorHeapRecord *heap)
{
	const std::vector<DX12DescriptorValueNode*> nodes = CurrentValueNodes(*heap);
	DX12DescriptorSlotArrayUtils::ClearSparseRecords(heap);
	Require(heap->currentDescriptors.Empty());
	for (DX12DescriptorValueNode *node : nodes)
		Require(ReferenceCount(node) == 0);
}

static void TestStoreMaterializeAndUnchanged()
{
	DescriptorHeapRecord heap;
	InitializeSparseHeap(&heap, 16, 0x10000);
	const DescriptorRecord original = MakeRecord(101, 1);
	const SIZE_T handle = SlotHandle(heap, 2);

	Require(DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
		&heap, 2, original, handle));
	Require(DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap) == 1);
	Require(heap.currentDescriptors.Size() == 1);
	const DescriptorSlotBinding *binding =
		DX12DescriptorSlotArrayUtils::FindCurrentBinding(heap, 2);
	Require(binding && binding->value);
	DX12DescriptorValueNode *firstValue = binding->value;
	Require(ReferenceCount(firstValue) == 1);
	RequireSameDescriptorValue(firstValue->record, original, 0);
	RequireValueAt(heap, 2, original);

	DescriptorRecord sameIdentity = MakeRecord(101, 99);
	Require(!DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
		&heap, 2, sameIdentity, handle));
	binding = DX12DescriptorSlotArrayUtils::FindCurrentBinding(heap, 2);
	Require(binding && binding->value == firstValue);
	Require(ReferenceCount(firstValue) == 1);
	RequireValueAt(heap, 2, original);

	ClearAndRequireReleased(&heap);
}

static void TestCrossHeapCopyOverwriteAndErase()
{
	DescriptorHeapRecord sourceHeap;
	DescriptorHeapRecord destinationHeap;
	InitializeSparseHeap(&sourceHeap, 16, 0x20000);
	InitializeSparseHeap(&destinationHeap, 16, 0x30000);
	const DescriptorRecord first = MakeRecord(201, 2);
	const DescriptorRecord second = MakeRecord(202, 3);
	DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
		&sourceHeap, 1, first, SlotHandle(sourceHeap, 1));
	const DescriptorSlotBinding *sourceBinding =
		DX12DescriptorSlotArrayUtils::FindCurrentBinding(sourceHeap, 1);
	Require(sourceBinding && sourceBinding->value);
	DX12DescriptorValueNode *sharedValue = sourceBinding->value;

	using CopyResult = DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult;
	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&destinationHeap, 4, sourceHeap, 1,
		SlotHandle(destinationHeap, 4)) == CopyResult::Changed);
	const DescriptorSlotBinding *destinationBinding =
		DX12DescriptorSlotArrayUtils::FindCurrentBinding(destinationHeap, 4);
	Require(destinationBinding && destinationBinding->value == sharedValue);
	Require(ReferenceCount(sharedValue) == 2);
	RequireValueAt(destinationHeap, 4, first);

	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&destinationHeap, 4, sourceHeap, 1,
		SlotHandle(destinationHeap, 4)) == CopyResult::Unchanged);
	Require(DX12DescriptorSlotArrayUtils::FindCurrentBinding(
		destinationHeap, 4)->value == sharedValue);
	Require(ReferenceCount(sharedValue) == 2);

	Require(DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
		&sourceHeap, 1, second, SlotHandle(sourceHeap, 1)));
	DX12DescriptorValueNode *replacementValue =
		DX12DescriptorSlotArrayUtils::FindCurrentBinding(sourceHeap, 1)->value;
	Require(replacementValue != sharedValue);
	Require(ReferenceCount(replacementValue) == 1);
	Require(ReferenceCount(sharedValue) == 1);
	RequireValueAt(sourceHeap, 1, second);
	RequireValueAt(destinationHeap, 4, first);

	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&destinationHeap, 4, sourceHeap, 8,
		SlotHandle(destinationHeap, 4)) ==
		CopyResult::SourceMissingDestinationErased);
	Require(!DX12DescriptorSlotArrayUtils::FindCurrentBinding(
		destinationHeap, 4));
	Require(ReferenceCount(sharedValue) == 0);
	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&destinationHeap, 4, sourceHeap, 8,
		SlotHandle(destinationHeap, 4)) ==
		CopyResult::SourceMissingDestinationEmpty);

	ClearAndRequireReleased(&destinationHeap);
	ClearAndRequireReleased(&sourceHeap);
	Require(ReferenceCount(replacementValue) == 0);
}

static void TestSparseAndDenseDifferentialPaths()
{
	DescriptorHeapRecord sparseHeap;
	DescriptorHeapRecord denseHeap;
	DescriptorHeapRecord sparseDestination;
	InitializeSparseHeap(&sparseHeap, 8, 0x40000);
	InitializeSparseHeap(&denseHeap, 8, 0x50000);
	InitializeSparseHeap(&sparseDestination, 8, 0x60000);
	denseHeap.slots.resize(denseHeap.desc.NumDescriptors);
	denseHeap.occupied.assign(denseHeap.desc.NumDescriptors, 0);
	const DescriptorRecord value = MakeRecord(301, 4);

	DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
		&sparseHeap, 2, value, SlotHandle(sparseHeap, 2));
	using CopyResult = DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult;
	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&denseHeap, 3, sparseHeap, 2, SlotHandle(denseHeap, 3)) ==
		CopyResult::Changed);
	Require(denseHeap.currentDescriptors.Empty());
	Require(denseHeap.occupied[3] == 1);
	RequireValueAt(denseHeap, 3, value);

	Require(DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
		&sparseDestination, 5, denseHeap, 3,
		SlotHandle(sparseDestination, 5)) == CopyResult::Changed);
	RequireValueAt(sparseDestination, 5, value);
	Require(DX12DescriptorSlotArrayUtils::EraseCurrentRecord(&denseHeap, 3));
	Require(!DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
		denseHeap, 3, &denseHeap.slots[0]));

	ClearAndRequireReleased(&sparseDestination);
	ClearAndRequireReleased(&sparseHeap);
}

struct DescriptorSnapshotEntry
{
	bool occupied = false;
	DescriptorRecord value;
};

// Mirrors the production overlap contract: materialize every source value
// before changing any destination. This is deliberately a full DescriptorRecord
// snapshot rather than a borrowed DescriptorSlotBinding pointer.
static void SnapshotCopyRange(
	DescriptorHeapRecord *heap,
	UINT sourceSlot,
	UINT destinationSlot,
	UINT count)
{
	std::vector<DescriptorSnapshotEntry> snapshot(count);
	for (UINT i = 0; i < count; ++i) {
		snapshot[i].occupied =
			DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
				*heap, sourceSlot + i, &snapshot[i].value);
	}
	for (UINT i = 0; i < count; ++i) {
		const UINT slot = destinationSlot + i;
		if (snapshot[i].occupied) {
			DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
				heap, slot, snapshot[i].value, SlotHandle(*heap, slot));
		} else {
			DX12DescriptorSlotArrayUtils::EraseCurrentRecord(heap, slot);
		}
	}
}

static void PopulateSequentialValues(
	DescriptorHeapRecord *heap,
	UINT64 firstValueId,
	UINT tokenBase)
{
	for (UINT slot = 0; slot < 5; ++slot) {
		const DescriptorRecord value =
			MakeRecord(firstValueId + slot, tokenBase + slot);
		DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
			heap, slot, value, SlotHandle(*heap, slot));
	}
}

static void RequireValueIdAt(
	const DescriptorHeapRecord &heap,
	UINT slot,
	UINT64 expectedValueId)
{
	DescriptorRecord value;
	Require(DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
		heap, slot, &value));
	Require(value.valueId == expectedValueId);
	Require(value.cpuHandle == SlotHandle(heap, slot));
}

static void TestOverlappingRangeSnapshotSemantics()
{
	DescriptorHeapRecord forward;
	InitializeSparseHeap(&forward, 8, 0x70000);
	PopulateSequentialValues(&forward, 1000, 10);
	SnapshotCopyRange(&forward, 0, 1, 4);
	const UINT64 forwardExpected[] = { 1000, 1000, 1001, 1002, 1003 };
	for (UINT slot = 0; slot < 5; ++slot)
		RequireValueIdAt(forward, slot, forwardExpected[slot]);
	ClearAndRequireReleased(&forward);

	DescriptorHeapRecord backward;
	InitializeSparseHeap(&backward, 8, 0x80000);
	PopulateSequentialValues(&backward, 2000, 20);
	SnapshotCopyRange(&backward, 1, 0, 4);
	const UINT64 backwardExpected[] = { 2001, 2002, 2003, 2004, 2004 };
	for (UINT slot = 0; slot < 5; ++slot)
		RequireValueIdAt(backward, slot, backwardExpected[slot]);
	ClearAndRequireReleased(&backward);

	DescriptorHeapRecord missing;
	InitializeSparseHeap(&missing, 8, 0x90000);
	const DescriptorRecord first = MakeRecord(3000, 30);
	const DescriptorRecord third = MakeRecord(3002, 32);
	const DescriptorRecord staleDestination = MakeRecord(3999, 39);
	DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
		&missing, 0, first, SlotHandle(missing, 0));
	DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
		&missing, 2, third, SlotHandle(missing, 2));
	DX12DescriptorSlotArrayUtils::StoreCurrentRecord(
		&missing, 3, staleDestination, SlotHandle(missing, 3));
	SnapshotCopyRange(&missing, 0, 1, 3);
	RequireValueIdAt(missing, 0, 3000);
	RequireValueIdAt(missing, 1, 3000);
	DescriptorRecord absent;
	Require(!DX12DescriptorSlotArrayUtils::CopyCurrentRecord(
		missing, 2, &absent));
	RequireValueIdAt(missing, 3, 3002);
	ClearAndRequireReleased(&missing);
}

static void TestValuePoolRefcountAndReuse()
{
	DX12DescriptorValueNode *node = DX12DescriptorValuePool::Acquire();
	Require(node != nullptr);
	Require(ReferenceCount(node) == 1);
	node->record = MakeRecord(9001, 90);
	DX12DescriptorValuePool::Retain(node);
	Require(ReferenceCount(node) == 2);
	DX12DescriptorValuePool::Release(node);
	Require(ReferenceCount(node) == 1);
	DX12DescriptorValuePool::Release(node);
	Require(ReferenceCount(node) == 0);

	DX12DescriptorValueNode *reused = DX12DescriptorValuePool::Acquire();
	Require(reused == node);
	Require(ReferenceCount(reused) == 1);
	Require(reused->record.kind.empty());
	Require(reused->record.valueId == 0);
	Require(reused->record.resource == nullptr);
	Require(reused->record.counterResource == nullptr);
	Require(!reused->record.ownsResource);
	Require(!reused->record.ownsCounterResource);
	DX12DescriptorValuePool::Release(reused);
	Require(ReferenceCount(reused) == 0);
}

int main()
{
	TestStoreMaterializeAndUnchanged();
	TestCrossHeapCopyOverwriteAndErase();
	TestSparseAndDenseDifferentialPaths();
	TestOverlappingRangeSnapshotSemantics();
	TestValuePoolRefcountAndReuse();
	return 0;
}
