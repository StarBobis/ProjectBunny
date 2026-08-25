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



class DX12ResourceCreateLogUtils
{
public:
	inline static volatile LONG64 StageCalls = 0;
	inline static volatile LONG64 RecordCalls = 0;
	inline static volatile LONG64 RecordTracked = 0;
	inline static volatile LONG64 RecordSkipped = 0;
	static constexpr LONG64 StatsInterval = 4096;

	static bool ShouldLogSample(LONG64 value)
	{
		return value <= 8 || (value % StatsInterval) == 0;
	}

	static void LogStage(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource)
	{
		const LONG64 calls = InterlockedIncrement64(&StageCalls);
		if (SUCCEEDED(hr) && !ShouldLogSample(calls))
			return;
		DX12LogManager::Fields("DX12ResourceCreateStage",
			"api=%s,stage=%s,stageCalls=%lld,present=%ld,threadId=%lu,"
			"device=%p,hr=0x%lx,resourcePtr=%p,resource=%p,"
			"hasDesc=%u,dimension=%u,width=%llu,height=%u,format=%u,flags=0x%llx,"
			"hasHeap=%u,heapType=%u,initialState=%u,skipAll=%ld",
			api ? api : "",
			stage ? stage : "",
			static_cast<long long>(calls),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			device,
			hr,
			resource,
			(resource && *resource) ? *resource : nullptr,
			desc ? 1 : 0,
			desc ? static_cast<UINT>(desc->Dimension) : 0,
			desc ? static_cast<unsigned long long>(desc->Width) : 0,
			desc ? desc->Height : 0,
			desc ? static_cast<UINT>(desc->Format) : 0,
			desc ? static_cast<unsigned long long>(desc->Flags) : 0,
			heapProperties ? 1 : 0,
			heapProperties ? static_cast<UINT>(heapProperties->Type) : 0,
			static_cast<UINT>(initialState),
			DX12HotPathState::skipAll.load());
	}

	static void LogRecordStats(const char *stage, HRESULT hr, void **resource, bool shouldTrack)
	{
		const LONG64 calls = InterlockedIncrement64(&RecordCalls);
		if (shouldTrack)
			InterlockedIncrement64(&RecordTracked);
		else
			InterlockedIncrement64(&RecordSkipped);
		if (SUCCEEDED(hr) && shouldTrack && !ShouldLogSample(calls))
			return;
		DX12LogManager::Fields("DX12ResourceCreateRecord",
			"stage=%s,recordCalls=%lld,tracked=%lld,skipped=%lld,present=%ld,threadId=%lu,hr=0x%lx,resourcePtr=%p,resource=%p,shouldTrack=%u",
			stage ? stage : "",
			static_cast<long long>(calls),
			static_cast<long long>(InterlockedCompareExchange64(&RecordTracked, 0, 0)),
			static_cast<long long>(InterlockedCompareExchange64(&RecordSkipped, 0, 0)),
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			hr,
			resource,
			(resource && *resource) ? *resource : nullptr,
			shouldTrack ? 1 : 0);
	}

	static void LogFallback(ID3D12Resource *resource)
	{
		DX12LogManager::Fields("DX12ResourceCollectQueue",
			"event=FallbackInlineRecord,resource=%p",
			resource);
	}
};

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
	++gDescriptorHeapRecordsSeen;
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
	LogResourceTrackerStatsLocked();
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

	auto originalSimple = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple,
		DX12ResourceTrackerHookState::OrigCopyDescriptorsSimple,
		"ID3D12Device::CopyDescriptorsSimple");
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
	if (descriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;

	// Keep only the required flattened range mapping. Each mapped block uses the
	// minimal shadow copy path; no memoization or per-call telemetry is needed.
	UINT destRange = 0, srcRange = 0, destOffset = 0, srcOffset = 0;
	UINT increment = DX12DescriptorHeapShadowUtils::CachedIncrementForType(
		descriptorHeapsType);
	if (!increment && device)
		increment = device->GetDescriptorHandleIncrementSize(descriptorHeapsType);
	if (!increment)
		return;
	DX12DescriptorHeapShadowUtils::CacheIncrementForType(
		descriptorHeapsType, increment);
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
		if (dest.ptr != src.ptr)
			RecordCopiedDescriptorsSimple(
				count, dest, src, descriptorHeapsType, increment);
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

	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot source;
	DX12DescriptorHeapShadowUtils::ResolvedHeapSlot destination;
	const bool resolvedBoth =
		DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
			srcDescriptorRangeStart.ptr, descriptorHeapsType, &source) &&
		DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
			destDescriptorRangeStart.ptr, descriptorHeapsType, &destination);
	const bool heapsActive = resolvedBoth &&
		source.heap->stateLocks && destination.heap->stateLocks &&
		source.heap->stateLocks->IsActive() &&
		destination.heap->stateLocks->IsActive();
	const bool bothMaterialized = heapsActive &&
		source.heap->IsMaterialized() && destination.heap->IsMaterialized();
	// Chunk-mode direct copies are lock-free per slot. They are only enabled
	// outside Dump/Hunt tracking, where whole-heap materialization never runs
	// concurrently with the chunked store (see
	// DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged).
	const bool chunkFast = heapsActive && !bothMaterialized &&
		!source.heap->IsMaterialized() && !destination.heap->IsMaterialized() &&
		!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking();
	const bool reverse = destDescriptorRangeStart.ptr > srcDescriptorRangeStart.ptr;
	for (UINT n = 0; n < numDescriptors; ++n) {
		const UINT i = reverse ? numDescriptors - n - 1 : n;
		const SIZE_T srcHandle = srcDescriptorRangeStart.ptr +
			static_cast<SIZE_T>(i) * increment;
		const SIZE_T destHandle = destDescriptorRangeStart.ptr +
			static_cast<SIZE_T>(i) * increment;
		const bool directCopy = bothMaterialized ||
			(chunkFast &&
				source.heap->chunkedSlots.ChunkExists(source.slot + i) &&
				destination.heap->chunkedSlots.ChunkExists(destination.slot + i));
		if (directCopy) {
			const DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult result =
				DX12DescriptorSlotArrayUtils::CopyCurrentSlotIfChanged(
					destination.heap,
					destination.slot + i,
					*source.heap, source.slot + i, destHandle, nullptr);
			// LockRequired is unreachable here (chunks never disappear while
			// the heap is active). In chunk mode a hunt/dump materialization
			// can land on the destination mid-copy; the dense adapter is then
			// authoritative, so redo this slot through the locked path.
			const bool racedMaterialization =
				!bothMaterialized && destination.heap->IsMaterialized();
			if (result != DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::LockRequired &&
					!racedMaterialization) {
				if (result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::Changed ||
					result == DX12DescriptorSlotArrayUtils::CopyCurrentSlotResult::SourceMissingDestinationErased)
					DX12DescriptorHeapStateUtils::BumpVersion(
						destination.heap, destination.slot + i);
				continue;
			}
		}
		DescriptorRecord record;
		if (!DX12DescriptorHeapShadowUtils::FindLatestDescriptor(srcHandle, &record)) {
			DX12DescriptorHeapShadowUtils::EraseDescriptor(destHandle);
			continue;
		}
		record.cpuHandle = destHandle;
		if (!DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot(
				destHandle, std::move(record)))
			DX12DescriptorHeapShadowUtils::WriteToCpuLedger(std::move(record));
	}
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
	DX12ResourceCreateLogUtils::LogRecordStats("Record", hr, resource, shouldTrack);
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
		DX12LogManager::Text("DX12 persistent resource and descriptor tracking enabled (wrapper mode)\n");
	}
}
