// DX12ResourceTrackerHooks: hook-runtime recording of created/copied resources and descriptors.
// Split from DX12ResourceTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <intrin.h>



const D3D12_UNORDERED_ACCESS_VIEW_DESC *DX12ResourceTrackerHookRuntime::PrepareUavDesc(
	ID3D12Resource *resource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
	D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustUavDesc(resource, storage, source) ? storage : desc;
}

const D3D12_RESOURCE_DESC *DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
	const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_DESC *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustBufferResourceDesc(storage, source) ? storage : desc;
}

const D3D12_RESOURCE_DESC1 *DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
	const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_DESC1 *storage,
	const char *source)
{
	if (!desc || !storage)
		return desc;
	*storage = *desc;
	return DX12ModAdjustBufferResourceDesc1(storage, source) ? storage : desc;
}

bool DX12ResourceTrackerHookRuntime::ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc)
{
	// Buffers must always stay tracked for IA/buffer replacement, GPU-VA lookup,
	// and upload/readback content capture. Textures only need global resource
	// records when an active feature consumes texture identity through the
	// descriptor/runtime tracking path.
	if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
		return true;
	if (!desc)
		return true;
	if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		return true;
	if (IsTextureResourceDimension(desc->Dimension))
		return DX12ResourceTrackingPolicyUtils::NeedsTextureResourceTracking();
	return false;
}

void DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(
	HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap)
{
	if (FAILED(hr) || !device || !desc || !heap || !*heap)
		return;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	if (FAILED(static_cast<IUnknown*>(*heap)->QueryInterface(IID_PPV_ARGS(&descriptorHeap))))
		return;

	DescriptorHeapRecord record;
	record.heap = descriptorHeap;
	record.desc = *desc;
	record.cpuStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
	record.gpuStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	record.increment = device->GetDescriptorHandleIncrementSize(desc->Type);
	DX12DescriptorHeapShadowUtils::CacheIncrementForType(
		desc->Type, record.increment);
	InitializeDescriptorHeapRecord(&record);
	DX12ObjectLifetimeTrackerUtils::InstallDescriptorHeapReleaseTracker(descriptorHeap);
	descriptorHeap->Release();

	AcquireSRWLockExclusive(&gDescriptorLock);
	auto found = gDescriptorHeapByPtr.find(record.heap);
	if (found != gDescriptorHeapByPtr.end()) {
		DescriptorHeapRecord &old = gDescriptorHeaps[found->second];
		InvalidateDescriptorHeapLocked(old.heap);
	}
	const size_t heapIndex = gDescriptorHeaps.size();
	gDescriptorHeapByPtr[record.heap] = heapIndex;
	if (record.cpuStart)
		gDescriptorHeapByCpuStart[record.cpuStart] = heapIndex;
	gDescriptorHeaps.push_back(std::move(record));
	BumpDescriptorQuerySerial();
	ReleaseSRWLockExclusive(&gDescriptorLock);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
	HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature)
{
	if (FAILED(hr) || !blob || !blobLength || !rootSignature || !*rootSignature)
		return;

	// DX11 keeps shader metadata resident immediately after creation. Mirror that
	// behavior for DX12 root signatures so pre-skin dispatches never fall back to
	// descriptor-heap scans while waiting for an async parser to catch up.
	DX12RecordRootSignatureImmediate(
		static_cast<ID3D12RootSignature*>(*rootSignature),
		nodeMask,
		blob,
		blobLength);
}

static void ApplyStaticReplacementToRecordedSrv(
	ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);

void DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(
	ID3D12Device *device,
	ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::SRV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.srv = *desc;
		record.hasDesc = true;
		FillSrvBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	// Cache the draw-time hunt hash at write time (see
	// DX12ComputeDescriptorHuntHash); 0 disables the cache for this record.
	record.descriptorHuntHash = DX12ComputeDescriptorHuntHash(device, record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
	ApplyStaticReplacementToRecordedSrv(device, destDescriptor);
}

static UINT64 StaticReplacementDescriptorValueId(
	UINT64 logicalValueId, UINT64 replacementGeneration)
{
	UINT64 value = logicalValueId ^ 0x9e3779b97f4a7c15ull;
	value ^= replacementGeneration + 0x517cc1b727220a95ull + (value << 6) +
		(value >> 2);
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ull;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebull;
	value ^= value >> 31;
	return value ? value : 1;
}

static void ApplyStaticReplacementToRecordedSrv(
	ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	if (!device || !destDescriptor.ptr || !DX12HasStaticTextureReplacements())
		return;

	DescriptorRecord record;
	if (!DX12DescriptorHeapShadowUtils::FindLatestDescriptor(
			destDescriptor.ptr, &record) ||
		record.kindId != DescriptorKind::SRV_ID || !record.resource)
		return;

	uint32_t textureHash = 0;
	if (!DX12TryGetTextureHash(record.resource, &textureHash))
		return;
	D3D12_CPU_DESCRIPTOR_HANDLE replacementSrv = {};
	UINT64 replacementGeneration = 0;
	if (!DX12ResolveStaticTextureReplacementSrv(
			device, textureHash, &record, &replacementSrv, nullptr,
			&replacementGeneration) ||
		!replacementSrv.ptr || replacementSrv.ptr == destDescriptor.ptr ||
		!replacementGeneration)
		return;

	auto originalSimple = DX12ResourceTrackerHookRegistry::GetDeviceOriginal<
		DX12ResourceTrackerHookSignatures::CopyDescriptorsSimple>(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple,
		nullptr, "ID3D12Device::CopyDescriptorsSimple");
	if (!originalSimple)
		return;
	originalSimple(device, 1, destDescriptor, replacementSrv,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Keep the original resource in shadow state, but publish that its physical
	// descriptor bytes already match this static-rule generation. Future copies
	// can then propagate both states without resolving the texture again.
	record.staticReplacementGeneration = replacementGeneration;
	record.valueId = StaticReplacementDescriptorValueId(
		record.valueId, replacementGeneration);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(
		std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
	ID3D12Device *device,
	ID3D12Resource *resource,
	ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::UAV_ID);
	record.cpuHandle = destDescriptor.ptr;
	record.counterResource = counterResource;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.uav = *desc;
		record.hasDesc = true;
		FillUavBufferView(&record);
	}
	PrepareDescriptorValueId(&record);
	// Cache the draw-time hunt hash at write time (see
	// DX12ComputeDescriptorHuntHash); 0 disables the cache for this record.
	record.descriptorHuntHash = DX12ComputeDescriptorHuntHash(device, record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(
	ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	// RTV/DSV/Sampler values have no mod consumer: PreSkin and descriptor
	// replacement only read CBV/SRV/UAV slots, and the heap slot versions are
	// per-heap-type, so skipping these cannot stale any CBV/SRV/UAV query
	// cache. Only capture/hunting needs them (DX11 tracks nothing here).
	if (!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::RTV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.rtv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(
	ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	if (!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::DSV_ID);
	record.cpuHandle = destDescriptor.ptr;
	CaptureDescriptorResourcePointer(&record, resource);
	if (desc) {
		record.dsv = *desc;
		record.hasDesc = true;
	}
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedSampler(
	const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	if (!desc || !DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;
	DescriptorRecord record;
	SetDescriptorKind(&record, DescriptorKind::Sampler_ID);
	record.cpuHandle = destDescriptor.ptr;
	record.sampler = *desc;
	record.hasDesc = true;
	PrepareDescriptorValueId(&record);
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

// One CopyDescriptors call expands into several blocks (the measured average is
// ~8 single-slot blocks per call), and every block used to re-run the whole
// per-call setup: the tracking-mode gate (two out-of-line policy calls), the
// copied-descriptor tally, and a fresh heap-version commit set. Only work that
// is genuinely call-scoped lives here.
//
// Heap-slot resolution deliberately does NOT move here: each block carries
// different CPU handles and the answer (heap record, slot) is only valid for
// the handle it was asked about, so every block still resolves its own source
// and destination.
struct DescriptorCopyCallScope
{
	D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	UINT increment = 0;
	// NeedsDumpOrHuntingTracking() is two out-of-line policy calls; it is
	// sampled once per API call, exactly like the entry gate that decides
	// whether the call is recorded at all.
	bool dumpOrHunt = false;
	// Descriptors copied by this call, accumulated over its blocks.
	LONG64 copiedDescriptors = 0;
	// Per-descriptor outcome counters, accumulated here and published once per
	// API call. They used to be incremented per descriptor, which cost a bucket
	// lookup per slot - on a path that is now called once per copied descriptor
	// (measured 27k descriptors per frame in 27k one-descriptor blocks) the
	// counter was a measurable part of the cost it was reporting. The values are
	// the same; only the number of lookups changed.
	LONG64 directPathCalls = 0;
	LONG64 changedCalls = 0;
	LONG64 unchangedCalls = 0;
	LONG64 lockedPathCalls = 0;
	LONG64 ledgerFallbackCalls = 0;
	// Changed slots publish their own seqlock identities immediately (readers
	// rely on them), while the heap-wide invalidation token - which consumers
	// only require to change once per completed API call - is bumped once per
	// touched destination heap after the last block (see
	// HeapVersionCommitSet and PublishSlotIdentity).
	DX12DescriptorHeapShadowUtils::HeapVersionCommitSet commitSet;
};

// Descriptors per API call is the divisor every per-descriptor cost needs: the
// shadow moves a 264-byte DescriptorRecord per slot while the API it mirrors
// moves 32 bytes, so whether the measured cost is per-call fixed work or
// per-descriptor payload traffic decides where to shrink next. The tally covers
// every expanded block of the call and is published once, together with the
// call's single heap-version commit.
static void FinishCopiedDescriptorCall(DescriptorCopyCallScope &scope)
{
	DX12_PROF_ADD_ITEMS("Desc.CopySlots", scope.copiedDescriptors);
	DX12_PROF_ADD_CALLS("Rm.DescCopyDirectPath", scope.directPathCalls);
	DX12_PROF_ADD_CALLS("Rm.DescCopyChanged", scope.changedCalls);
	DX12_PROF_ADD_CALLS("Rm.DescCopyUnchanged", scope.unchangedCalls);
	DX12_PROF_ADD_CALLS("Rm.DescCopyLockedPath", scope.lockedPathCalls);
	DX12_PROF_ADD_CALLS("Rm.DescCopyLedgerFallback", scope.ledgerFallbackCalls);
	scope.commitSet.CommitAll();
}

// Records one already-expanded block of a descriptor copy: `numDescriptors`
// descriptors from `srcDescriptorRangeStart` to `destDescriptorRangeStart`.
// Everything per-block lives here by construction - the two heap resolutions,
// the activity/materialization verdicts derived from them (a heap can retire or
// materialize while the call is in flight), the chunk cursors, the reverse
// direction (a pointer comparison of this block's own handles) and the per-slot
// work.
static void RecordCopiedDescriptorBlock(
	DescriptorCopyCallScope &scope,
	UINT numDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart)
{
	const UINT increment = scope.increment;
	if (!increment)
		return;
	scope.copiedDescriptors += numDescriptors;

	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot source;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot destination;
	const bool resolvedBoth =
		DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
			srcDescriptorRangeStart.ptr, scope.heapType, &source) &&
		DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
			destDescriptorRangeStart.ptr, scope.heapType, &destination);
	// A failed resolve leaves that side at the default nullptr heap, so every
	// heap access below is guarded: the stateLocks test precedes its own
	// dereference, and the materialization flags are only read through a present
	// heap.
	const bool heapsActive = resolvedBoth &&
		source.heap->stateLocks && destination.heap->stateLocks &&
		source.heap->stateLocks->IsActive() &&
		destination.heap->stateLocks->IsActive();
	const bool sourceMaterialized =
		source.heap && source.heap->IsMaterialized();
	const bool destinationMaterialized =
		destination.heap && destination.heap->IsMaterialized();
	const bool bothMaterialized = heapsActive &&
		sourceMaterialized && destinationMaterialized;
	// Chunk-mode direct copies are lock-free per slot and only enabled
	// outside Dump/Hunt tracking, where whole-heap materialization never runs
	// concurrently with the chunked store (see
	// DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged).
	// Both materialization atomics are read once per block, and the mode gate
	// once per call.
	const bool chunkFast = heapsActive &&
		!sourceMaterialized && !destinationMaterialized && !scope.dumpOrHunt;
	const bool reverse = destDescriptorRangeStart.ptr > srcDescriptorRangeStart.ptr;
	// Chunk presence is cached per 4096-slot chunk boundary, so a contiguous
	// range pays two chunk-pointer loads per chunk instead of per slot.
	constexpr UINT kChunkSlots = DX12ChunkedDescriptorStore<DescriptorRecord>::ChunkSlots;
	UINT curSrcChunk = UINT_MAX;
	UINT curDstChunk = UINT_MAX;
	bool curSrcChunkOk = false;
	bool curDstChunkOk = false;
	// Seeded with this block's own sample above: re-reading it at the first
	// chunk boundary would only repeat a load taken a few instructions earlier,
	// and only the chunk fast path - which already proved both heaps resolved -
	// consults the flag at all.
	bool curDstMaterialized = destinationMaterialized;
	// Block-level split of the locked fallback: Rm.DescCopyLockedPath counts
	// slots, while the per-block setup above only repeats once per block, so the
	// number that decides whether hoisting it pays off is how many blocks
	// needed the locked path at all.
	bool blockTookLockedPath = false;
	for (UINT n = 0; n < numDescriptors; ++n) {
		const UINT i = reverse ? numDescriptors - n - 1 : n;
		const SIZE_T srcHandle = srcDescriptorRangeStart.ptr +
			static_cast<SIZE_T>(i) * increment;
		const SIZE_T destHandle = destDescriptorRangeStart.ptr +
			static_cast<SIZE_T>(i) * increment;
		const UINT srcSlot = source.slot + i;
		const UINT dstSlot = destination.slot + i;
		const UINT srcChunk = srcSlot / kChunkSlots;
		const UINT dstChunk = dstSlot / kChunkSlots;
		if (srcChunk != curSrcChunk) {
			curSrcChunk = srcChunk;
			curSrcChunkOk = chunkFast &&
				source.heap->chunkedSlots.ChunkExists(srcSlot);
		}
		if (dstChunk != curDstChunk) {
			// Past the first boundary this must be a fresh read: a hunt/dump
			// materialization can land on the destination while this call is in
			// flight, and the dense adapter is then authoritative, so the
			// remaining slots must take the locked path. The chunkFast gate also
			// keeps the failed-resolve case (nullptr heap) out of the
			// dereference.
			if (curDstChunk != UINT_MAX)
				curDstMaterialized = chunkFast &&
					destination.heap->IsMaterialized();
			curDstChunk = dstChunk;
			curDstChunkOk = chunkFast &&
				destination.heap->chunkedSlots.ChunkExists(dstSlot);
		}
		const bool directCopy = bothMaterialized ||
			(chunkFast && curSrcChunkOk && curDstChunkOk &&
				!curDstMaterialized);
		if (directCopy) {
			++scope.directPathCalls;
			UINT64 publishedValueId = 0;
			const DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult result =
				DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
					destination.heap, dstSlot,
					*source.heap, srcSlot, destHandle, &publishedValueId);
			// LockRequired is unreachable here (chunks never disappear while
			// the heap is active); fall through to the locked path if it ever
			// happens.
			if (result != DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::LockRequired) {
				if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed ||
					result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased) {
					++scope.changedCalls;
					DX12DescriptorHeapStateUtils::PublishRangeValue(
						destination.heap, dstSlot, publishedValueId);
					scope.commitSet.Add(destination.heap);
				} else {
					// The slot already held this value: the 264-byte record copy
					// and the republication were both skipped inside the slot
					// array. Counted because the shadow's cost is dominated by
					// that copy, so the changed/unchanged split decides whether
					// shrinking the per-slot payload is worth it.
					++scope.unchangedCalls;
				}
				continue;
			}
		}
		DescriptorRecord record;
		++scope.lockedPathCalls;
		blockTookLockedPath = true;
		if (!DX12DescriptorHeapShadowUtils::FindLatestDescriptor(srcHandle, &record)) {
			DX12DescriptorHeapShadowUtils::EraseDescriptor(destHandle);
			continue;
		}
		record.cpuHandle = destHandle;
		if (!DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(
				destHandle, std::move(record))) {
			// Global exclusive lock + map insert + a global CAS loop: counted
			// separately because a single fallback here costs more than the rest
			// of the call together.
			++scope.ledgerFallbackCalls;
			DX12DescriptorHeapShadowUtils::WriteToCpuLedger(std::move(record));
		}
	}
	if (blockTookLockedPath)
		DX12_PROF_ADD_ITEMS("Desc.CopyBlocksLocked", 1);
	// The commit set spans the whole API call, not this block: the caller
	// commits after its last block.
}

// One expanded run of descriptor copies: consecutive API range pairs whose
// destination and source both continue the previous pair, so the whole run is
// recorded as a single block. The game hands CopyDescriptors about eight
// single-descriptor ranges per call (measured: 3547 calls and 27961 blocks per
// frame, one descriptor each), which turned the per-block setup - two heap
// resolutions, four atomic reads, the commit set - into a per-descriptor cost.
struct DescriptorCopyRun
{
	UINT count = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE dest = {};
	D3D12_CPU_DESCRIPTOR_HANDLE src = {};
};

// Records the pending run as one block and clears it. Desc.CopyBlocks counts
// these merged blocks, so the coalescing is self-measuring: the counter is the
// divisor of the per-block cost above.
static void FlushDescriptorCopyRun(
	DescriptorCopyCallScope &scope, DescriptorCopyRun &run)
{
	if (!run.count)
		return;
	DX12_PROF_ADD_ITEMS("Desc.CopyBlocks", 1);
	RecordCopiedDescriptorBlock(scope, run.count, run.dest, run.src);
	run.count = 0;
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
	ID3D12Device *device,
	UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes,
	UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	if (!destDescriptorRangeStarts || !srcDescriptorRangeStarts)
		return;

	// Everything the expanded blocks share is prepared once here. Sampling the
	// mode gate per call matches this function's own entry gate below and the
	// contract every other gate in the shadow relies on: the mode can only turn
	// the lock-free chunk path off, and the per-block materialization reads
	// still gate that path, so a mid-call flip cannot be missed by more than the
	// blocks already in flight when it happens.
	DescriptorCopyCallScope scope;
	scope.heapType = descriptorHeapsType;
	scope.dumpOrHunt =
		DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking();
	if (descriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			!scope.dumpOrHunt)
		return;

	// Keep only the required flattened range mapping. Each mapped block uses the
	// minimal shadow copy path; no memoization or per-call telemetry is needed.
	UINT destRange = 0, srcRange = 0, destOffset = 0, srcOffset = 0;
	DescriptorCopyRun run;
	UINT increment = DX12DescriptorHeapShadowUtils::CachedIncrementForType(
		descriptorHeapsType);
	if (!increment && device)
		increment = device->GetDescriptorHandleIncrementSize(descriptorHeapsType);
	if (!increment)
		return;
	DX12DescriptorHeapShadowUtils::CacheIncrementForType(
		descriptorHeapsType, increment);
	scope.increment = increment;
	while (destRange < numDestDescriptorRanges && srcRange < numSrcDescriptorRanges) {
		const UINT destRangeSize = destDescriptorRangeSizes ? destDescriptorRangeSizes[destRange] : 1;
		const UINT srcRangeSize = srcDescriptorRangeSizes ? srcDescriptorRangeSizes[srcRange] : 1;
		if (!destRangeSize) {
			++destRange;
			continue;
		}
		if (!srcRangeSize) {
			++srcRange;
			continue;
		}
		const UINT count = min(destRangeSize - destOffset, srcRangeSize - srcOffset);
		D3D12_CPU_DESCRIPTOR_HANDLE dest = destDescriptorRangeStarts[destRange];
		D3D12_CPU_DESCRIPTOR_HANDLE src = srcDescriptorRangeStarts[srcRange];
		dest.ptr += static_cast<SIZE_T>(destOffset) * increment;
		src.ptr += static_cast<SIZE_T>(srcOffset) * increment;
		// Ranges that continue each other in both directions become one block, so
		// the per-block setup is paid per run rather than per range. The run has
		// to be flushed before any range that does not continue it, and a
		// self-overlapping pair (destination == source) never joins one.
		const bool continuesRun = run.count &&
			dest.ptr == run.dest.ptr + static_cast<SIZE_T>(run.count) * increment &&
			src.ptr == run.src.ptr + static_cast<SIZE_T>(run.count) * increment;
		if (continuesRun) {
			run.count += count;
		} else {
			FlushDescriptorCopyRun(scope, run);
			if (dest.ptr != src.ptr) {
				run.count = count;
				run.dest = dest;
				run.src = src;
			} else {
				// Self-overlapping ranges carry no work at all; counted separately
				// so Desc.CopySlots / Desc.CopyBlocks stays an average block size.
				DX12_PROF_ADD_ITEMS("Desc.CopyBlocksSkipped", 1);
			}
		}
		destOffset += count;
		srcOffset += count;
		if (destOffset == destRangeSize) {
			++destRange;
			destOffset = 0;
		}
		if (srcOffset == srcRangeSize) {
			++srcRange;
			srcOffset = 0;
		}
	}
	FlushDescriptorCopyRun(scope, run);
	// One tally and one heap-version commit for the whole API call, after its
	// last block.
	FinishCopiedDescriptorCall(scope);
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
	UINT numDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType,
	UINT increment)
{
	if (!increment)
		return;
	// Single-block API call: the same per-call preparation, block recording and
	// single heap-version commit the multi-range path uses, with the range walk
	// reduced to this one block.
	DescriptorCopyCallScope scope;
	scope.heapType = descriptorHeapsType;
	scope.increment = increment;
	scope.dumpOrHunt =
		DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking();
	RecordCopiedDescriptorBlock(
		scope, numDescriptors, destDescriptorRangeStart,
		srcDescriptorRangeStart);
	FinishCopiedDescriptorCall(scope);
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
	ID3D12Device *device,
	UINT numDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	UINT increment =
		DX12DescriptorHeapShadowUtils::CachedIncrementForType(descriptorHeapsType);
	if (!increment && device) {
		increment = device->GetDescriptorHandleIncrementSize(descriptorHeapsType);
		if (increment) {
			DX12DescriptorHeapShadowUtils::CacheIncrementForType(
				descriptorHeapsType, increment);
		}
	}
	RecordCopiedDescriptorsSimple(
		numDescriptors,
		destDescriptorRangeStart,
		srcDescriptorRangeStart,
		descriptorHeapsType,
		increment);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	const bool shouldTrack = !FAILED(hr) && resource && *resource && ShouldRecordCreatedResource(desc);
	if (!shouldTrack)
	{
		return;
	}
	ID3D12Resource *d3dResource = static_cast<ID3D12Resource*>(*resource);
	// DX11 keeps newly created mod-relevant resources visible to replacement
	// lookups immediately. Keep DX12 aligned in normal Mod mode and reserve the
	// async queue for the heavier dump/hunt metadata path.
	const bool useAsyncTracking =
		DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata();
	if (!useAsyncTracking || !DX12ResourceCollectQueue::Enqueue(
			d3dResource,
			desc,
			heapProperties,
			placedInfo,
			initialState)) {
		RecordResource(d3dResource, desc, heapProperties, placedInfo, initialState);
	}
	// Install Map/Unmap hooks for buffer content capture
	// Covers ALL creation paths (CreateCommittedResource, CreatePlacedResource,
	// CreateReservedResource, and all _1/_2/_3 variants).
	InstallMapUnmapHooks(d3dResource);
}

PlacedResourceInfo DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(
	ID3D12Heap *heap, UINT64 heapOffset)
{
	PlacedResourceInfo info;
	if (!heap)
		return info;
	info.hasPlacedHeap = true;
	info.heapOffset = heapOffset;
	info.heapSize = 0;
	return info;
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	RecordCreatedResource(hr, resource, desc ? &desc0 : nullptr, heapProperties, placedInfo, initialState);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_BARRIER_LAYOUT initialLayout)
{
	RecordCreatedResource(hr, resource, desc, heapProperties, placedInfo,
		DX12ResourceStateTracker::FromBarrierLayout(initialLayout));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
	HRESULT hr,
	void **resource,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_BARRIER_LAYOUT initialLayout)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	RecordCreatedResourceWithLayout(hr, resource, desc ? &desc0 : nullptr, heapProperties, placedInfo, initialLayout);
}


void DX12ResourceTrackerHooks::Install(ID3D12Device *device)
{
	(void)device;

	if (!gCleanupRegistered) {
		atexit(CleanupTrackedResources);
		gCleanupRegistered = true;
		DX12BindingCollectQueue::Start();
		DX12ResourceCollectQueue::Start();
		DX12_LOG_TEXT("DX12 persistent resource and descriptor tracking enabled (wrapper mode)\n");
	}
}
