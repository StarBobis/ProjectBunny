#include "DX12ResourceTrackerPrivate.hpp"
#include "DX12PreSkinBindingCacheUtils.hpp"
#include "DX12DescriptorCopyMemoUtils.hpp"
#include "DX12DescriptorArenaUtils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
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
	heap->currentDescriptors.Initialize(descriptorCount);
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

static DescriptorRecord MakeConstantBufferViewRecord(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc)
{
	DescriptorRecord record;
	record.kind = "CBV";
	record.kindId = DescriptorKind::CBV_ID;
	record.cbv = desc;
	record.hasDesc = true;
	record.gpuVirtualAddress = desc.BufferLocation;
	record.viewSize = desc.SizeInBytes;
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
	DescriptorRecord firstMaterialized;
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&firstMaterialized, firstValue->value, 0);
	RequireSameDescriptorValue(firstMaterialized, original, 0);
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
	DX12DescriptorSlotArrayUtils::CopyDescriptorValue(
		&node->value, MakeRecord(9001, 90));
	DX12DescriptorValuePool::Retain(node);
	Require(ReferenceCount(node) == 2);
	DX12DescriptorValuePool::Release(node);
	Require(ReferenceCount(node) == 1);
	DX12DescriptorValuePool::Release(node);
	Require(ReferenceCount(node) == 0);

	DX12DescriptorValueNode *reused = DX12DescriptorValuePool::Acquire();
	Require(reused == node);
	Require(ReferenceCount(reused) == 1);
	Require(reused->value.valueId == 0);
	Require(!reused->value.extended);
	DX12DescriptorValuePool::Release(reused);
	Require(ReferenceCount(reused) == 0);
}

static void TestCompactValueMaterializationAndCbvHashEquivalence()
{
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
	desc.BufferLocation = 0x1234567800ull;
	desc.SizeInBytes = 4096;
	DescriptorRecord record = MakeConstantBufferViewRecord(desc);
	const UINT64 genericHash =
		DX12DescriptorValueHashUtils::HashRecord(record);
	const UINT64 fastHash =
		DX12DescriptorValueHashUtils::HashConstantBufferView(desc);
	Require(genericHash != 0);
	Require(genericHash == fastHash);
	record.valueId = genericHash;

	DX12DescriptorValue compact;
	DX12DescriptorSlotArrayUtils::CopyDescriptorValue(&compact, record);
	Require(sizeof(DX12DescriptorValue) < sizeof(DescriptorRecord));
	Require(!compact.extended);
	DescriptorRecord materialized;
	DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
		&materialized, compact, 0xfeed0000);
	RequireSameDescriptorValue(materialized, record, 0xfeed0000);
}

static DX12DirectIaProbeCacheKey MakeDirectProbeKey(
	UINT64 reloadGeneration, UINT64 preSkinGeneration,
	uint32_t vertexHash, UINT firstIndex)
{
	DX12IaHashState state;
	DX12IaBufferHash buffer;
	buffer.slot = 0;
	buffer.hash = vertexHash;
	state.vertexBuffers.push_back(buffer);
	DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
		100, 2, firstIndex, -3, 7);
	return DX12DirectIaProbeCacheUtils::MakeKey(
		reloadGeneration, 123, state, draw);
}

static void TestDirectIaProbeCacheRetainsIndependentPreSkinGenerations()
{
	DX12DirectIaProbeCache cache;
	bool evicted = false;
	DX12DirectIaProbeCacheUtils::Store(
		&cache, MakeDirectProbeKey(9, 7, 0x11111111, 1), false, &evicted);
	Require(!evicted);
	DX12DirectIaProbeCacheUtils::Store(
		&cache, MakeDirectProbeKey(9, 7, 0x22222222, 2), true, &evicted);
	Require(cache.entries.size() == 2);

	// Different IA content hashes are entries in the same scope. The old cache
	// incorrectly treated each resource's generation as a global scope and
	// discarded the whole working set whenever bindings changed.
	DX12DirectIaProbeCacheUtils::Store(
		&cache, MakeDirectProbeKey(9, 7, 0x33333333, 3), false, &evicted);
	Require(cache.entries.size() == 3);

	DX12DirectIaProbeCacheUtils::Store(
		&cache, MakeDirectProbeKey(9, 8, 0x44444444, 4), false, &evicted);
	Require(!evicted);
	Require(cache.entries.size() == 4);
	Require(cache.lruKeys.size() == 4);
	bool oldGenerationCandidate = true;
	Require(DX12DirectIaProbeCacheUtils::TryLoad(
		&cache, MakeDirectProbeKey(9, 7, 0x11111111, 1),
		&oldGenerationCandidate));
	Require(!oldGenerationCandidate);

	DX12DirectIaProbeCacheKey drawVariant =
		MakeDirectProbeKey(9, 8, 0x44444444, 4);
	drawVariant.instanceCount++;
	Require(!(drawVariant == MakeDirectProbeKey(9, 8, 0x44444444, 4)));
}

class PreSkinSlotIdentityTestResolver
{
public:
	inline static DX12DescriptorHeapSummary heap = {};
	inline static std::array<UINT64, 64> versions = {};
	inline static std::array<UINT64, 64> valueIds = {};

	static bool ResolveTableHeap(
		const CommandListBindingState &,
		const RootTableState &,
		DX12DescriptorHeapSummary *out)
	{
		if (!out)
			return false;
		*out = heap;
		return true;
	}

	static bool ReadDescriptorSlotIdentities(
		ID3D12DescriptorHeap *, const UINT *slots, size_t count,
		UINT64 *outVersions, UINT64 *outValueIds)
	{
		for (size_t index = 0; index < count; ++index) {
			if (slots[index] >= versions.size())
				return false;
			outVersions[index] = versions[slots[index]];
			outValueIds[index] = valueIds[slots[index]];
		}
		return true;
	}
};

static void TestPreSkinBindingCacheObservesOnlyReferencedSlots()
{
	CommandListBindingState snapshot;
	snapshot.computeRootSignature = reinterpret_cast<ID3D12RootSignature*>(0x1000);
	snapshot.cbvSrvUavHeap = reinterpret_cast<ID3D12DescriptorHeap*>(0x2000);
	snapshot.computeTables[0].valid = true;
	snapshot.computeTables[0].baseDescriptor.ptr = 1100;

	PreSkinSlotIdentityTestResolver::heap = DX12DescriptorHeapSummary();
	PreSkinSlotIdentityTestResolver::heap.heap = snapshot.cbvSrvUavHeap;
	PreSkinSlotIdentityTestResolver::heap.gpuStart = 1000;
	PreSkinSlotIdentityTestResolver::heap.cpuStart = 3000;
	PreSkinSlotIdentityTestResolver::heap.increment = 10;
	PreSkinSlotIdentityTestResolver::heap.numDescriptors = 64;
	PreSkinSlotIdentityTestResolver::heap.version = 100;
	PreSkinSlotIdentityTestResolver::versions.fill(1);
	PreSkinSlotIdentityTestResolver::valueIds.fill(1000);

	DX12RootSignatureSummary rootSignature;
	DX12RootParameterSummary parameter;
	parameter.rootParameterIndex = 0;
	parameter.parameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	DX12RootDescriptorRangeSummary range;
	range.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.numDescriptors = 3;
	range.effectiveOffset = 2;
	parameter.ranges.push_back(range);
	rootSignature.parameters.push_back(parameter);

	DX12PreSkinBindingCacheKey first;
	DX12PreSkinBindingCacheUtils::BuildKey<PreSkinSlotIdentityTestResolver>(
		snapshot, 100, 1, &rootSignature, &first);
	DX12PreSkinBindingCacheKey unrelatedHeapWrite;
	DX12PreSkinBindingCacheUtils::BuildKey<PreSkinSlotIdentityTestResolver>(
		snapshot, 101, 1, &rootSignature, &unrelatedHeapWrite);
	Require(DX12PreSkinBindingCacheUtils::KeysEqual(first, unrelatedHeapWrite));

	PreSkinSlotIdentityTestResolver::versions[12]++;
	DX12PreSkinBindingCacheKey repeatedCopy;
	DX12PreSkinBindingCacheUtils::BuildKey<PreSkinSlotIdentityTestResolver>(
		snapshot, 102, 1, &rootSignature, &repeatedCopy);
	Require(DX12PreSkinBindingCacheUtils::KeysEqual(first, repeatedCopy));

	PreSkinSlotIdentityTestResolver::valueIds[12]++;
	DX12PreSkinBindingCacheKey referencedSlotWrite;
	DX12PreSkinBindingCacheUtils::BuildKey<PreSkinSlotIdentityTestResolver>(
		snapshot, 102, 1, &rootSignature, &referencedSlotWrite);
	Require(!DX12PreSkinBindingCacheUtils::KeysEqual(first, referencedSlotWrite));
}

static bool gPreSkinRootDescriptorRangeValid = true;

static bool ValidatePreSkinRootDescriptorRange(
	UINT64 observedEpoch, UINT64 gpuVirtualAddress, UINT64 size)
{
	Require(observedEpoch != 0);
	Require(gpuVirtualAddress == 0x4000);
	Require(size == 1);
	return gPreSkinRootDescriptorRangeValid;
}

static void TestPreSkinBindingCacheValidatesRootDescriptorRanges()
{
	DX12PreSkinBindingTlsCache cache;
	DX12PreSkinBindingCacheKey storedKey;
	storedKey.fingerprint = 7;
	storedKey.bufferResolveEpoch = 10;
	DX12PreSkinBindingRootDescriptorCacheKey rootDescriptor;
	rootDescriptor.rootParameterIndex = 2;
	rootDescriptor.type = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootDescriptor.address = 0x4000;
	storedKey.rootDescriptors.push_back(rootDescriptor);

	std::vector<DX12CurrentComputeUavBinding> bindings;
	std::vector<DX12CurrentRootConstants> constants;
	DX12PreSkinBindingCacheUtils::Store(
		&cache, storedKey, bindings, bindings, bindings, constants, false);

	DX12PreSkinBindingCacheKey currentKey = storedKey;
	currentKey.bufferResolveEpoch = 11;
	bool foundAny = true;
	gPreSkinRootDescriptorRangeValid = true;
	Require(DX12PreSkinBindingCacheUtils::TryLoad(
		&cache, currentKey, &bindings, &bindings, &bindings, &constants,
		&foundAny, ValidatePreSkinRootDescriptorRange));
	Require(!foundAny);

	currentKey.bufferResolveEpoch = 12;
	gPreSkinRootDescriptorRangeValid = false;
	Require(!DX12PreSkinBindingCacheUtils::TryLoad(
		&cache, currentKey, &bindings, &bindings, &bindings, &constants,
		&foundAny, ValidatePreSkinRootDescriptorRange));
}

static void TestDescriptorCopyMemoInvalidatesByObservedShard()
{
	DescriptorHeapRecord::StateLocks sourceState(2048);
	DescriptorHeapRecord::StateLocks destinationState(2048);
	DX12DescriptorCopyMemo memo;
	const DX12DescriptorCopyMemo::Key key = {
		reinterpret_cast<void*>(0x1000),
		reinterpret_cast<void*>(0x2000),
		7,
		11
	};
	memo.Store(
		key,
		sourceState.ReadSlotVersion(key.sourceSlot),
		destinationState.ReadSlotVersion(key.destinationSlot),
		DX12DescriptorCopyMemo::Result::Unchanged);

	DX12DescriptorCopyMemo::Result result;
	Require(memo.TryLoad(
		key,
		sourceState.ReadSlotVersion(key.sourceSlot),
		destinationState.ReadSlotVersion(key.destinationSlot),
		&result));
	Require(result == DX12DescriptorCopyMemo::Result::Unchanged);

	// Even another slot sharing the same lock shard does not invalidate the
	// exact source/destination proof.
	sourceState.BumpVersion(
		key.sourceSlot + DescriptorHeapRecord::StateLocks::ShardCount);
	Require(memo.TryLoad(
		key,
		sourceState.ReadSlotVersion(key.sourceSlot),
		destinationState.ReadSlotVersion(key.destinationSlot),
		&result));

	sourceState.BumpVersion(key.sourceSlot);
	Require(!memo.TryLoad(
		key,
		sourceState.ReadSlotVersion(key.sourceSlot),
		destinationState.ReadSlotVersion(key.destinationSlot),
		&result));
}

static void TestIaTargetPlanReuseRequiresIdenticalIaInputs()
{
	DX12IaHashTargetPlan direct;
	direct.ResetKnown();
	direct.RequireVertexBuffer(3);
	DX12IaHashTargetPlan full = direct;
	full.RequireDescriptorTarget();
	Require(full.HasSameIaHashTargets(direct));
	full.RequireVertexBuffer(4);
	Require(!full.HasSameIaHashTargets(direct));
}

static void TestIaHashKeyPrefixCacheInvalidation()
{
	DX12IaHashKeyPrefixCacheEntry entry;
	DX12IaHashStateCacheKey prefix;
	prefix.targetPlanFingerprint = 17;
	prefix.iaViewFingerprint = 23;
	prefix.bufferContentGeneration = 31;
	prefix.bufferResolveGeneration = 37;
	DX12IaHashKeyPrefixCacheUtils::Store(
		&entry, 5, 17, true, 101, 103, prefix);

	DX12IaHashStateCacheKey loaded;
	Require(DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 5, 17, true, 101, 103, &loaded));
	Require(loaded == prefix);
	Require(!DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 6, 17, true, 101, 103, &loaded));
	Require(!DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 5, 19, true, 101, 103, &loaded));
	Require(!DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 5, 17, false, 101, 103, &loaded));
	Require(!DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 5, 17, true, 102, 103, &loaded));
	Require(!DX12IaHashKeyPrefixCacheUtils::TryLoad(
		entry, 5, 17, true, 101, 104, &loaded));
}

static void TestIaHashThreadCacheSharesGenerationSafeEntries()
{
	DX12IaHashStateCache &cache = DX12IaHashStateCacheUtils::ThreadCache();
	DX12IaHashStateCacheUtils::Clear(&cache);
	DX12IaHashStateCacheKey key;
	key.targetPlanFingerprint = 11;
	key.iaViewFingerprint = 13;
	key.bufferContentGeneration = 17;
	key.bufferResolveGeneration = 19;
	key.vertexCount = 23;
	DX12IaHashState state;
	state.hasIndexBuffer = true;
	state.indexHash = 29;
	bool evicted = false;
	Require(DX12IaHashStateCacheUtils::Store(
		&cache, key, true, std::move(state), &evicted) != nullptr);
	Require(!evicted);

	const DX12IaHashState *loaded = nullptr;
	bool foundState = false;
	Require(DX12IaHashStateCacheUtils::TryLoad(
		&DX12IaHashStateCacheUtils::ThreadCache(), key, &loaded, &foundState));
	Require(foundState && loaded && loaded->indexHash == 29);
	DX12IaHashStateCacheKey changedGeneration = key;
	changedGeneration.bufferContentGeneration++;
	Require(!DX12IaHashStateCacheUtils::TryLoad(
		&cache, changedGeneration, &loaded, &foundState));
	DX12IaHashStateCacheUtils::Clear(&cache);
}

static void TestPreSkinBindingCacheFingerprintIndexHandlesCollisionsAndEviction()
{
	DX12PreSkinBindingTlsCache cache;
	std::vector<DX12CurrentComputeUavBinding> bindings;
	std::vector<DX12CurrentRootConstants> constants;
	DX12PreSkinBindingCacheKey first;
	first.fingerprint = 101;
	first.computeRootSignature = reinterpret_cast<ID3D12RootSignature*>(1);
	DX12PreSkinBindingCacheKey collision = first;
	collision.computeRootSignature = reinterpret_cast<ID3D12RootSignature*>(2);
	DX12PreSkinBindingCacheUtils::Store(
		&cache, first, bindings, bindings, bindings, constants, false);
	DX12PreSkinBindingCacheUtils::Store(
		&cache, collision, bindings, bindings, bindings, constants, true);
	bool foundAny = false;
	Require(DX12PreSkinBindingCacheUtils::TryLoad(
		&cache, first, nullptr, nullptr, nullptr, nullptr, &foundAny));
	Require(!foundAny);
	Require(DX12PreSkinBindingCacheUtils::TryLoad(
		&cache, collision, nullptr, nullptr, nullptr, nullptr, &foundAny));
	Require(foundAny);

	for (size_t index = cache.entries.size();
			index <= DX12PreSkinBindingCacheUtils::CacheCapacity; ++index) {
		DX12PreSkinBindingCacheKey key;
		key.fingerprint = 1000 + index;
		DX12PreSkinBindingCacheUtils::Store(
			&cache, key, bindings, bindings, bindings, constants, false);
	}
	Require(cache.entries.size() == DX12PreSkinBindingCacheUtils::CacheCapacity);
	Require(cache.fingerprintIndex.size() == cache.entries.size());
}

class DescriptorArenaTestRef
{
public:
	LONG references = 1;

	ULONG Release()
	{
		return static_cast<ULONG>(InterlockedDecrement(&references));
	}
};

static void TestDescriptorArenaGrowthAndUniqueOwnership()
{
	Require(DX12DescriptorArenaUtils::NextCapacity(
		0, 0, 18, 4096, 1000000) == 4096);
	Require(DX12DescriptorArenaUtils::NextCapacity(
		4096, 4090, 18, 4096, 1000000) == 8192);
	Require(DX12DescriptorArenaUtils::NextCapacity(
		8192, 8180, 32, 4096, 1000000) == 16384);

	DescriptorArenaTestRef object;
	std::vector<DescriptorArenaTestRef*> owned;
	Require(DX12DescriptorArenaUtils::AppendUniqueOwned(&owned, &object));
	InterlockedIncrement(&object.references);
	Require(!DX12DescriptorArenaUtils::AppendUniqueOwned(&owned, &object));
	Require(object.references == 1);
	Require(owned.size() == 1);
}

static void RunConstantBufferViewBenchmark()
{
	constexpr UINT SlotCount = 8192;
	constexpr UINT Iterations = 25000;
	auto run = [=](bool compact) {
		DescriptorHeapRecord heap;
		InitializeSparseHeap(&heap, SlotCount, 0x100000);
		const auto begin = std::chrono::steady_clock::now();
		for (UINT i = 0; i < Iterations; ++i) {
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = 0x40000000ull +
				static_cast<UINT64>(i) * 256;
			desc.SizeInBytes = 4096;
			const UINT64 valueId =
				DX12DescriptorValueHashUtils::HashConstantBufferView(desc);
			const UINT slot = i % SlotCount;
			if (compact) {
				const DX12DescriptorValue value =
					DX12DescriptorSlotArrayUtils::MakeConstantBufferViewValue(
						desc, valueId);
				DX12DescriptorSlotArrayUtils::StoreCurrentValueIfChanged(
					&heap, slot, value, SlotHandle(heap, slot));
			} else {
				DescriptorRecord record = MakeConstantBufferViewRecord(desc);
				record.valueId = valueId;
				DX12DescriptorSlotArrayUtils::StoreCurrentRecordIfChanged(
					&heap, slot, record, SlotHandle(heap, slot));
			}
		}
		const auto elapsed = std::chrono::duration_cast<
			std::chrono::microseconds>(
				std::chrono::steady_clock::now() - begin).count();
		ClearAndRequireReleased(&heap);
		return elapsed;
	};
	const long long genericUs = run(false);
	const long long compactUs = run(true);
	std::printf(
		"cbv_generic_us=%lld cbv_compact_us=%lld value_size=%zu record_size=%zu node_size=%zu\n",
		genericUs, compactUs, sizeof(DX12DescriptorValue),
		sizeof(DescriptorRecord), sizeof(DX12DescriptorValueNode));

	constexpr UINT CopyPairCount = 65000;
	DX12DescriptorCopyMemo memo;
	DescriptorHeapRecord::StateLocks sourceState(CopyPairCount);
	DescriptorHeapRecord::StateLocks destinationState(CopyPairCount);
	const void *sourceHeap = reinterpret_cast<void*>(0x100000);
	const void *destinationHeap = reinterpret_cast<void*>(0x200000);
	const auto memoStoreBegin = std::chrono::steady_clock::now();
	for (UINT slot = 0; slot < CopyPairCount; ++slot) {
		const DX12DescriptorCopyMemo::Key key = {
			sourceHeap, destinationHeap, slot, slot
		};
		memo.Store(
			key,
			sourceState.ReadSlotVersion(slot),
			destinationState.ReadSlotVersion(slot),
			DX12DescriptorCopyMemo::Result::Unchanged);
	}
	const auto memoStoreEnd = std::chrono::steady_clock::now();
	UINT memoHits = 0;
	const auto memoLookupBegin = std::chrono::steady_clock::now();
	for (UINT slot = 0; slot < CopyPairCount; ++slot) {
		const DX12DescriptorCopyMemo::Key key = {
			sourceHeap, destinationHeap, slot, slot
		};
		DX12DescriptorCopyMemo::Result result;
		if (memo.TryLoad(
				key,
				sourceState.ReadSlotVersion(slot),
				destinationState.ReadSlotVersion(slot),
				&result))
			++memoHits;
	}
	const auto memoLookupEnd = std::chrono::steady_clock::now();
	std::printf(
		"copy_memo_pairs=%u copy_memo_hits=%u copy_memo_store_us=%lld copy_memo_lookup_us=%lld\n",
		CopyPairCount,
		memoHits,
		static_cast<long long>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				memoStoreEnd - memoStoreBegin).count()),
		static_cast<long long>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				memoLookupEnd - memoLookupBegin).count()));
	std::fflush(stdout);
	// A frame-sized descriptor ring must survive in the memo.  Falling back for
	// conflict-evicted entries turns tens of thousands of semantically unchanged
	// copies back into slot locking and sparse-store traffic every frame.
	Require(memoHits > CopyPairCount * 99 / 100);
}

int main(int argc, char **argv)
{
	TestStoreMaterializeAndUnchanged();
	TestCrossHeapCopyOverwriteAndErase();
	TestSparseAndDenseDifferentialPaths();
	TestOverlappingRangeSnapshotSemantics();
	TestValuePoolRefcountAndReuse();
	TestCompactValueMaterializationAndCbvHashEquivalence();
	TestDirectIaProbeCacheRetainsIndependentPreSkinGenerations();
	TestPreSkinBindingCacheObservesOnlyReferencedSlots();
	TestPreSkinBindingCacheValidatesRootDescriptorRanges();
	TestDescriptorCopyMemoInvalidatesByObservedShard();
	TestIaTargetPlanReuseRequiresIdenticalIaInputs();
	TestIaHashKeyPrefixCacheInvalidation();
	TestIaHashThreadCacheSharesGenerationSafeEntries();
	TestPreSkinBindingCacheFingerprintIndexHandlesCollisionsAndEviction();
	TestDescriptorArenaGrowthAndUniqueOwnership();
	if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0)
		RunConstantBufferViewBenchmark();
	return 0;
}
