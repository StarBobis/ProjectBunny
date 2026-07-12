#include "DX12ResourceTrackerPrivate.hpp"

#include "../utils/DX12DescriptorHeapShadowUtils.hpp"
#include "DX12HookManager.hpp"
#include "DX12ResourceCollectQueue.hpp"
#include "DX12ObjectLifetimeTrackerUtils.hpp"
#include "DX12ResourceTrackerHookCallbacks.hpp"
#include "DX12ResourceTrackerHookDiagnostics.hpp"
#include "DX12ResourceTrackerHookRegistry.hpp"
#include "DX12ResourceTrackerHookRuntime.hpp"
#include "DX12ResourceTrackerHooks.hpp"
#include "proxy/DX12Wrapper.hpp"
#include "DX12ResourceTrackerHookState.hpp"
#include "DX12ResourceStateTracker.hpp"
#include "types/DX12ResourceTrackerPrivateDataGuids.hpp"
#include "types/DX12ResourceTrackerHookSlotTypes.hpp"
#include "types/DX12ResourceHookSlotTypes.hpp"
#include "utils/DX12CreateTimingTelemetryUtils.hpp"

// ── Buffer Content Capture via Map/Unmap hooks ───────────────────────
// Like DX11: capture UPLOAD buffer data at Unmap time so Tier 1
// (content CRC32) hashing works.  Maps are tracked per-thread.
struct DX12ResourceMapTracking
{
	ID3D12Resource *resource = nullptr;
	void *data = nullptr;
	UINT subresource = 0;
};

static thread_local DX12ResourceMapTracking gActiveMap;

// Hooked Map �?records the mapped pointer for later content capture
static HRESULT STDMETHODCALLTYPE HookedResourceMap(
	ID3D12Resource *resource,
	UINT Subresource,
	const D3D12_RANGE *pReadRange,
	void **ppData)
{
	// Call original first to get the mapped pointer
	using MapFn = HRESULT(STDMETHODCALLTYPE *)(ID3D12Resource*, UINT, const D3D12_RANGE*, void**);
	MapFn original = reinterpret_cast<MapFn>(DX12ResourceTrackerHookState::ResourceOriginals.Map);
	if (!original)
		return E_FAIL;

	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Original);
		hr = original(resource, Subresource, pReadRange, ppData);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Record);
		if (SUCCEEDED(hr) && ppData && *ppData) {
		// Safety: only track UPLOAD/READBACK buffers (our hook may fire
		// on other resource types if the driver shares vtables).
			D3D12_HEAP_PROPERTIES heapProps = {};
			D3D12_HEAP_FLAGS heapFlags = {};
			if (SUCCEEDED(resource->GetHeapProperties(&heapProps, &heapFlags)) &&
			    (heapProps.Type == D3D12_HEAP_TYPE_UPLOAD ||
			     heapProps.Type == D3D12_HEAP_TYPE_READBACK)) {
				gActiveMap.resource = resource;
				gActiveMap.data = *ppData;
				gActiveMap.subresource = Subresource;
			}
		}
	}
	return hr;
}

// Hooked Unmap �?captures buffer content before unmapping
static void STDMETHODCALLTYPE HookedResourceUnmap(
	ID3D12Resource *resource,
	UINT Subresource,
	const D3D12_RANGE *pWrittenRange)
{
	{
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Record);
		// Capture content if this resource was previously mapped AND tracked
		if (gActiveMap.resource == resource &&
		    gActiveMap.subresource == Subresource &&
		    gActiveMap.data) {
		// Determine written size:
		// D3D12_RANGE: Begin=0, End=0 means entire subresource
		// D3D12_RANGE: nullptr means nothing written (unlikely for uploads)
		size_t dataSize = 0;
		if (pWrittenRange) {
			dataSize = static_cast<size_t>(pWrittenRange->End - pWrittenRange->Begin);
		}
		if (dataSize == 0) {
			// Game wrote entire resource �?resolve size from resource desc
			D3D12_RESOURCE_DESC desc = resource->GetDesc();
			dataSize = static_cast<size_t>(desc.Width);
		}
		if (dataSize > 0) {
			const uint8_t *capturePtr =
				static_cast<const uint8_t*>(gActiveMap.data);
			if (pWrittenRange && pWrittenRange->Begin > 0)
				capturePtr += pWrittenRange->Begin;
			DX12CacheBufferContentRange(resource, pWrittenRange ? pWrittenRange->Begin : 0, capturePtr, dataSize);
		}
			gActiveMap = DX12ResourceMapTracking();
		}
	}

	// Call original Unmap
	using UnmapFn = void(STDMETHODCALLTYPE *)(ID3D12Resource*, UINT, const D3D12_RANGE*);
	UnmapFn original = reinterpret_cast<UnmapFn>(DX12ResourceTrackerHookState::ResourceOriginals.Unmap);
	if (original) {
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Original);
		original(resource, Subresource, pWrittenRange);
	}
}

// ── Resource VTable Hook Installation ─────────────────────────────────
void DX12ResourceTrackerHookRuntime::InstallMapUnmapHooks(ID3D12Resource *resource)
{
	if (!resource)
		return;

	// Map/Unmap hooks are only needed for buffer content capture (Dump feature).
	// Without an active Dump or Hunt there is nothing to capture, so skip the
	// GetHeapProperties driver call and VTable patching entirely.
	if (!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())
		return;

	D3D12_RESOURCE_DESC desc = resource->GetDesc();
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;

	// Only hook UPLOAD/READBACK buffers (CPU-accessible)
	D3D12_HEAP_PROPERTIES heapProps = {};
	D3D12_HEAP_FLAGS heapFlags = {};
	HRESULT hr = resource->GetHeapProperties(&heapProps, &heapFlags);
	if (FAILED(hr))
		return;
	if (heapProps.Type != D3D12_HEAP_TYPE_UPLOAD &&
	    heapProps.Type != D3D12_HEAP_TYPE_READBACK)
		return;

	// Install Map/Unmap hooks on this resource (one-shot)
	DX12VTableHook hooks[] = {
		{DX12ResourceHookSlotTypes::ID3D12Resource::Map,
			reinterpret_cast<void**>(&DX12ResourceTrackerHookState::ResourceOriginals.Map),
			reinterpret_cast<void*>(HookedResourceMap), "ID3D12Resource::Map"},
		{DX12ResourceHookSlotTypes::ID3D12Resource::Unmap,
			reinterpret_cast<void**>(&DX12ResourceTrackerHookState::ResourceOriginals.Unmap),
			reinterpret_cast<void*>(HookedResourceUnmap), "ID3D12Resource::Unmap"},
	};
	DX12HookManager::InstallVTableHooks(resource, hooks, ARRAYSIZE(hooks));

	DX12LogManager::DebugJsonFunc("DX12BufferContentHook",
		"stage=installed,resource=0x%p,bytes=%llu,heapType=%u",
		resource, static_cast<unsigned long long>(desc.Width),
		static_cast<UINT>(heapProps.Type));
}

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
		DX12LogManager::JsonFunc("DX12ResourceCreateStage",
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
			DX12HotPathState::skipAll);
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
		DX12LogManager::JsonFunc("DX12ResourceCreateRecord",
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
		DX12LogManager::JsonFunc("DX12ResourceCollectQueue",
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
	// Resource creation is always a baseline fact. Only the heavier full-metadata
	// path is conditional; otherwise keep the always-needed dumpable resource set.
	if (DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
		return true;
	if (!desc)
		return true;
	return desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
		IsTextureResourceDimension(desc->Dimension);
}

void DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
	const char *api,
	const char *stage,
	ID3D12Device *device,
	HRESULT hr,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState,
	void **resource)
{
	DX12ResourceCreateLogUtils::LogStage(
		api, stage, device, hr, desc, heapProperties, initialState, resource);
}

void DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
	const char *api,
	const char *stage,
	ID3D12Device *device,
	HRESULT hr,
	const D3D12_RESOURCE_DESC1 *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_RESOURCE_STATES initialState,
	void **resource)
{
	D3D12_RESOURCE_DESC desc0 = ResourceDescFromDesc1(desc);
	LogResourceCreateStage(api, stage, device, hr, desc ? &desc0 : nullptr,
		heapProperties, initialState, resource);
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

	DX12RecordRootSignatureImmediate(
		static_cast<ID3D12RootSignature*>(*rootSignature),
		nodeMask,
		blob,
		blobLength);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12DescriptorHeapShadowUtils::RecordCreatedConstantBufferViewIfChanged(
		desc, destDescriptor);
}

void DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(
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
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
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
	DX12DescriptorHeapShadowUtils::RecordCreatedDescriptorIfChanged(std::move(record));
}

void DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(
	ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
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
	if (!desc)
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
	if (!DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() ||
			!destDescriptorRangeStarts || !srcDescriptorRangeStarts)
		return;

	UINT destRange = 0;
	UINT srcRange = 0;
	UINT destOffset = 0;
	UINT srcOffset = 0;
	DX12DescriptorHeapShadowUtils::CopyTelemetryBatch copyTelemetry;
	const UINT increment = device ?
		device->GetDescriptorHandleIncrementSize(descriptorHeapsType) : 0;
	if (!increment)
		return;
	D3D12_CPU_DESCRIPTOR_HANDLE pendingDest = {};
	D3D12_CPU_DESCRIPTOR_HANDLE pendingSrc = {};
	UINT pendingCount = 0;
	auto flushPendingRange = [&]() {
		if (!pendingCount)
			return;
		DX12DescriptorHeapShadowUtils::CopyDescriptorRange(
			descriptorHeapsType,
			pendingDest,
			pendingSrc,
			pendingCount,
			&copyTelemetry);
		pendingCount = 0;
	};
	while (destRange < numDestDescriptorRanges && srcRange < numSrcDescriptorRanges) {
		const UINT destRangeSize = destDescriptorRangeSizes ? destDescriptorRangeSizes[destRange] : 1;
		const UINT srcRangeSize = srcDescriptorRangeSizes ? srcDescriptorRangeSizes[srcRange] : 1;
		if (destOffset >= destRangeSize) {
			destRange++;
			destOffset = 0;
			continue;
		}
		if (srcOffset >= srcRangeSize) {
			srcRange++;
			srcOffset = 0;
			continue;
		}

		const UINT count = min(destRangeSize - destOffset, srcRangeSize - srcOffset);
		D3D12_CPU_DESCRIPTOR_HANDLE dest = destDescriptorRangeStarts[destRange];
		D3D12_CPU_DESCRIPTOR_HANDLE src = srcDescriptorRangeStarts[srcRange];
		dest.ptr += static_cast<SIZE_T>(destOffset) * increment;
		src.ptr += static_cast<SIZE_T>(srcOffset) * increment;
		const SIZE_T pendingBytes =
			static_cast<SIZE_T>(pendingCount) * increment;
		const bool contiguous = pendingCount != 0 &&
			dest.ptr == pendingDest.ptr + pendingBytes &&
			src.ptr == pendingSrc.ptr + pendingBytes &&
			count <= UINT_MAX - pendingCount;
		if (contiguous) {
			pendingCount += count;
		} else {
			flushPendingRange();
			pendingDest = dest;
			pendingSrc = src;
			pendingCount = count;
		}
		destOffset += count;
		srcOffset += count;
	}
	flushPendingRange();
	copyTelemetry.Flush();
}

void DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
	ID3D12Device *device,
	UINT numDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
	D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	if (DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking())
		DX12DescriptorHeapShadowUtils::CopyDescriptorRange(
			descriptorHeapsType, destDescriptorRangeStart,
			srcDescriptorRangeStart, numDescriptors);
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
	RecordResource(d3dResource, desc, heapProperties, placedInfo, initialState);
	// ── Install Map/Unmap hooks for buffer content capture ──────────
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

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDescriptorHeap(
	ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDescriptorHeap", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDescriptorHeap, DX12ResourceTrackerHookState::OrigCreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap");
	if (DX12InternalReplayState::IsActive())
		return original ? original(device, desc, riid, heap) : E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Original);
		hr = original ? original(device, desc, riid, heap) : E_FAIL;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(hr, device, desc, heap);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorHeapHook, Wrap);
		if (SUCCEEDED(hr) && heap && *heap)
			DX12Wrapper::WrapD3D12DescriptorHeapResult(riid, heap);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRootSignature(
	ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
	REFIID riid, void **rootSignature)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRootSignature", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRootSignature, DX12ResourceTrackerHookState::OrigCreateRootSignature, "ID3D12Device::CreateRootSignature");
	if (DX12InternalReplayState::IsActive())
		return original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(RootSignatureHook, Original);
		hr = original ? original(device, nodeMask, blob, blobLength, riid, rootSignature) : E_FAIL;
	}
	{
		DX12_CREATE_TIMING_SCOPE(RootSignatureHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(hr, nodeMask, blob, blobLength, rootSignature);
	}
	return hr;
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateConstantBufferView(
	ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateConstantBufferView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateConstantBufferView, DX12ResourceTrackerHookState::OrigCreateConstantBufferView, "ID3D12Device::CreateConstantBufferView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorCbvHook, Original);
		original(device, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorCbvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateShaderResourceView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateShaderResourceView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateShaderResourceView, DX12ResourceTrackerHookState::OrigCreateShaderResourceView, "ID3D12Device::CreateShaderResourceView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorSrvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorSrvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateUnorderedAccessView(
	ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateUnorderedAccessView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateUnorderedAccessView, DX12ResourceTrackerHookState::OrigCreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, counterResource, desc, destDescriptor);
		return;
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC adjustedDesc = {};
	const D3D12_UNORDERED_ACCESS_VIEW_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareUavDesc(
			resource, desc, &adjustedDesc, "CreateUnorderedAccessView");
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Original);
		original(device, resource, counterResource, descToUse, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorUavHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
			resource, counterResource, descToUse, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateRenderTargetView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateRenderTargetView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateRenderTargetView, DX12ResourceTrackerHookState::OrigCreateRenderTargetView, "ID3D12Device::CreateRenderTargetView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorRtvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorRtvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateDepthStencilView(
	ID3D12Device *device, ID3D12Resource *resource,
	const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateDepthStencilView", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateDepthStencilView, DX12ResourceTrackerHookState::OrigCreateDepthStencilView, "ID3D12Device::CreateDepthStencilView");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, resource, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorDsvHook, Original);
		original(device, resource, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(DescriptorDsvHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(resource, desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateSampler(
	ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateSampler", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateSampler, DX12ResourceTrackerHookState::OrigCreateSampler, "ID3D12Device::CreateSampler");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, desc, destDescriptor);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(SamplerHook, Original);
		original(device, desc, destDescriptor);
	}
	{
		DX12_CREATE_TIMING_SCOPE(SamplerHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedSampler(desc, destDescriptor);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptors(
	ID3D12Device *device, UINT numDestDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
	const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
	const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
	const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptors", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptors, DX12ResourceTrackerHookState::OrigCopyDescriptors, "ID3D12Device::CopyDescriptors");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
			destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
			srcDescriptorRangeSizes, descriptorHeapsType);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsHook, Original);
		original(device, numDestDescriptorRanges, destDescriptorRangeStarts,
			destDescriptorRangeSizes, numSrcDescriptorRanges, srcDescriptorRangeStarts,
			srcDescriptorRangeSizes, descriptorHeapsType);
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
			device, numDestDescriptorRanges, destDescriptorRangeStarts, destDescriptorRangeSizes,
			numSrcDescriptorRanges, srcDescriptorRangeStarts, srcDescriptorRangeSizes, descriptorHeapsType);
	}
}

void STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCopyDescriptorsSimple(
	ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CopyDescriptorsSimple", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CopyDescriptorsSimple, DX12ResourceTrackerHookState::OrigCopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple");
	if (!original)
		return;
	if (DX12InternalReplayState::IsActive()) {
		original(device, numDescriptors, destDescriptorRangeStart,
			srcDescriptorRangeStart, descriptorHeapsType);
		return;
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleHook, Original);
		original(device, numDescriptors, destDescriptorRangeStart,
			srcDescriptorRangeStart, descriptorHeapsType);
	}
	{
		DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
			device, numDescriptors, destDescriptorRangeStart, srcDescriptorRangeStart, descriptorHeapsType);
	}
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource(
	ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateCommittedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateCommittedResource, DX12ResourceTrackerHookState::OrigCreateCommittedResource, "ID3D12Device::CreateCommittedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
			desc, &adjustedDesc, "CreateCommittedResource");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateCommittedResource", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource(
	ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreatePlacedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreatePlacedResource, DX12ResourceTrackerHookState::OrigCreatePlacedResource, "ID3D12Device::CreatePlacedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, &placedInfo, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreatePlacedResource", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource(
	ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateReservedResource", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateReservedResource, DX12ResourceTrackerHookState::OrigCreateReservedResource, "ID3D12Device::CreateReservedResource");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialState,
			optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device::CreateReservedResource", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateFence(
	ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
	REFIID riid, void **fence)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device::CreateFence", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device::CreateFence,
		DX12ResourceTrackerHookState::OrigCreateFence, "ID3D12Device::CreateFence");
	if (!original)
		return E_FAIL;
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(FenceHook, Original);
		hr = original(device, initialValue, flags, riid, fence);
	}
	{
		DX12_CREATE_TIMING_SCOPE(FenceHook, Wrap);
		if (SUCCEEDED(hr) && fence && *fence)
			DX12Wrapper::WrapD3D12FenceResult(riid, fence);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource1(
	ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateCommittedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateCommittedResource1, DX12ResourceTrackerHookState::OrigCreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC adjustedDesc = {};
	const D3D12_RESOURCE_DESC *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
			desc, &adjustedDesc, "CreateCommittedResource1");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateCommittedResource1", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource1(
	ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device4::CreateReservedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device4::CreateReservedResource1, DX12ResourceTrackerHookState::OrigCreateReservedResource1, "ID3D12Device4::CreateReservedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialState,
			optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource(
			hr, resource, desc, nullptr, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage(
		"ID3D12Device4::CreateReservedResource1", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource2(
	ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreateCommittedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreateCommittedResource2, DX12ResourceTrackerHookState::OrigCreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
			desc, &adjustedDesc, "CreateCommittedResource2");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialState, optimizedClearValue, protectedSession, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "OriginalEnd", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
			hr, resource, descToUse, heapProperties, nullptr, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreateCommittedResource2", "HookReturn", device, hr,
		descToUse, heapProperties, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource1(
	ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
	const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device8::CreatePlacedResource1", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device8::CreatePlacedResource1, DX12ResourceTrackerHookState::OrigCreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "OriginalBegin", device, S_OK,
		desc, nullptr, initialState, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialState, optimizedClearValue, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "OriginalEnd", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
			hr, resource, desc, nullptr, &placedInfo, initialState);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device8::CreatePlacedResource1", "HookReturn", device, hr,
		desc, nullptr, initialState, resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateCommittedResource3(
	ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
	D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateCommittedResource3", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateCommittedResource3, DX12ResourceTrackerHookState::OrigCreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heapProperties, heapFlags, desc,
			initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
			castableFormats, riid, resource);
	D3D12_RESOURCE_DESC1 adjustedDesc = {};
	const D3D12_RESOURCE_DESC1 *descToUse = desc;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Prepare);
		descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
			desc, &adjustedDesc, "CreateCommittedResource3");
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "OriginalBegin", device, S_OK,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Original);
		hr = original(device, heapProperties, heapFlags, descToUse,
			initialLayout, optimizedClearValue, protectedSession, numCastableFormats,
			castableFormats, riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "OriginalEnd", device, hr,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
			hr, resource, descToUse, heapProperties, nullptr, initialLayout);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreateCommittedResource3", "HookReturn", device, hr,
		descToUse, heapProperties, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourceCommittedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreatePlacedResource2(
	ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
	const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
	const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreatePlacedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreatePlacedResource2, DX12ResourceTrackerHookState::OrigCreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "OriginalBegin", device, S_OK,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Original);
		hr = original(device, heap, heapOffset, desc,
			initialLayout, optimizedClearValue, numCastableFormats, castableFormats,
			riid, resource);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "OriginalEnd", device, hr,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Record);
		const PlacedResourceInfo placedInfo =
			DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, heapOffset);
		DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
			hr, resource, desc, nullptr, &placedInfo, initialLayout);
	}
	DX12ResourceTrackerHookRuntime::LogResourceCreateStage1(
		"ID3D12Device10::CreatePlacedResource2", "HookReturn", device, hr,
		desc, nullptr, DX12ResourceStateTracker::FromBarrierLayout(initialLayout), resource);
	{
		DX12_CREATE_TIMING_SCOPE(ResourcePlacedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DX12ResourceTrackerHookCallbacks::HookedCreateReservedResource2(
	ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
	D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
	ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
	const DXGI_FORMAT *castableFormats, REFIID riid, void **resource)
{
	DX12ResourceTrackerHookDiagnostics::LogResourceHookCallWhenTracking("ID3D12Device10::CreateReservedResource2", device);
	auto original = DX12ResourceTrackerHookRegistry::GetDeviceOriginal(
		device, DX12ResourceTrackerHookSlotTypes::ID3D12Device10::CreateReservedResource2, DX12ResourceTrackerHookState::OrigCreateReservedResource2, "ID3D12Device10::CreateReservedResource2");
	if (!original)
		return E_FAIL;
	if (DX12InternalReplayState::IsActive())
		return original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Original);
		hr = original(device, desc, initialLayout,
			optimizedClearValue, protectedSession, numCastableFormats, castableFormats,
			riid, resource);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Record);
		DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
			hr, resource, desc, nullptr, nullptr, initialLayout);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceReservedHook, Wrap);
		if (SUCCEEDED(hr) && resource && *resource)
			DX12Wrapper::WrapD3D12ResourceResult(riid, resource);
	}
	return hr;
}

void DX12ResourceTrackerHooks::Install(ID3D12Device *device)
{
	(void)device;

	if (!gCleanupRegistered) {
		atexit(CleanupTrackedResources);
		gCleanupRegistered = true;
		DX12ResourceCollectQueue::Start();
		DX12LogManager::Text("DX12 persistent resource and descriptor tracking enabled (wrapper mode)\n");
	}
}
