// Buffer Content Capture via Map/Unmap hooks
// Like DX11: capture UPLOAD buffer data at Unmap time so Tier 1
// (content CRC32) hashing works.  Maps are tracked per-thread.
#include <intrin.h>
struct DX12ResourceMapTracking
{
	ID3D12Resource *resource = nullptr;
	void *data = nullptr;
	UINT subresource = 0;
	UINT mapDepth = 1;
};

static thread_local std::vector<DX12ResourceMapTracking> gActiveMaps;

static void TrackResourceMap(
	ID3D12Resource *resource, UINT subresource, void *data)
{
	for (DX12ResourceMapTracking &active : gActiveMaps) {
		if (active.resource == resource && active.subresource == subresource) {
			active.data = data;
			if (active.mapDepth != UINT_MAX)
				++active.mapDepth;
			return;
		}
	}
	DX12ResourceMapTracking active;
	active.resource = resource;
	active.data = data;
	active.subresource = subresource;
	gActiveMaps.push_back(active);
}

static bool TakeResourceMap(
	ID3D12Resource *resource, UINT subresource, DX12ResourceMapTracking *mapping)
{
	if (!mapping)
		return false;
	for (auto it = gActiveMaps.begin(); it != gActiveMaps.end(); ++it) {
		if (it->resource != resource || it->subresource != subresource)
			continue;
		*mapping = *it;
		if (it->mapDepth > 1)
			--it->mapDepth;
		else
			gActiveMaps.erase(it);
		return mapping->data != nullptr;
	}
	return false;
}

// Content capture must not read driver-synchronized (CUDA/DLSS-shared)
// mapped memory: the read blocks until the owning CUDA context is free, but
// that context is held by the middleware call that is waiting for our Unmap
// to return (observed hung 8+ s in the page memcmp loop; no TDR). Restrict
// the capture to Unmaps issued by the game's own modules; middleware
// (nvngx_dlss, sl_*, ...) mappings are never read. The check is cached per
// module so the hot path stays cheap.
static bool CaptureAllowedForCaller(const void *caller)
{
	if (!caller)
		return false;
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(caller), &module) ||
	    !module)
		return false;

	struct Entry { HMODULE module = nullptr; bool allowed = false; };
	static thread_local Entry sCache[8];
	static thread_local UINT sCacheNext = 0;
	for (const Entry &entry : sCache) {
		if (entry.module == module)
			return entry.allowed;
	}

	wchar_t name[MAX_PATH] = {};
	bool allowed = false;
	if (GetModuleFileNameW(module, name, MAX_PATH)) {
		const wchar_t *base = wcsrchr(name, L'\\');
		base = base ? base + 1 : name;
		// Allowlist of the game's own modules; anything else (NVIDIA
		// Streamline/DLSS, overlays, capture tools) is skipped.
		allowed =
			_wcsicmp(base, L"UnityPlayer.dll") == 0 ||
			_wcsicmp(base, L"GameAssembly.dll") == 0 ||
			_wcsicmp(base, L"ZenlessZoneZero.exe") == 0;
		if (!allowed) {
			// Rate-limited diagnostic so the guard's effect is visible in the
			// log (one line per skipped module).
			static volatile LONG skipLogCounter = 0;
			if ((InterlockedIncrement(&skipLogCounter) & 0x3FF) == 1) {
				DX12LogManager::Fields("DX12BufferCaptureSkip",
					"\"module\":\"%S\",\"allowed\":%s",
					name, allowed ? "true" : "false");
			}
		}
	}
	Entry &entry = sCache[sCacheNext++ % 8];
	entry.module = module;
	entry.allowed = allowed;
	return allowed;
}

static void CaptureResourceMapWrites(
	ID3D12Resource *resource, UINT subresource,
	const D3D12_RANGE *writtenRange, const void *caller)
{
	DX12ResourceMapTracking mapping;
	if (!TakeResourceMap(resource, subresource, &mapping))
		return;
	// A null range means the whole subresource may have changed. An empty
	// range explicitly means no CPU writes and must not update generations.
	if (writtenRange && writtenRange->End <= writtenRange->Begin)
		return;
	if (!CaptureAllowedForCaller(caller)) {
		// Never read driver-synchronized mappings from middleware; keep the
		// cache coherent by dropping the entry instead of serving stale data.
		DX12InvalidateBufferContent(resource);
		return;
	}
	UINT64 offset = 0;
	UINT64 bytes = 0;
	if (writtenRange) {
		offset = writtenRange->Begin;
		bytes = writtenRange->End - writtenRange->Begin;
	} else {
		bytes = resource->GetDesc().Width;
	}
	if (!bytes || bytes > SIZE_MAX)
		return;
	// Size bound is only a memory sanity net for the content cache; the
	// deadlock guard is the caller-module allowlist above. Large game-side
	// buffers (upload arenas, big meshes) must still be capturable, so the
	// bound is deliberately loose. Oversized ranges are invalidated instead.
	constexpr UINT64 kMaxCaptureBytes = 256ull * 1024 * 1024;
	if (bytes > kMaxCaptureBytes || offset > kMaxCaptureBytes) {
		DX12InvalidateBufferContent(resource);
		return;
	}
	DX12CacheBufferContentRange(resource, offset,
		static_cast<const uint8_t*>(mapping.data) + static_cast<size_t>(offset),
		static_cast<size_t>(bytes));
}

struct DX12CpuMappableLookupCache
{
	ID3D12Resource *resource = nullptr;
	UINT64 generation = 0;
	bool eligible = false;
};

static thread_local DX12CpuMappableLookupCache gCpuMappableLookupCache;

static bool IsCpuMappableTrackedResource(ID3D12Resource *resource)
{
	const UINT64 generation =
		gCpuMappableResourceGeneration.load(std::memory_order_acquire);
	if (gCpuMappableLookupCache.resource == resource &&
		gCpuMappableLookupCache.generation == generation) {
		return gCpuMappableLookupCache.eligible;
	}

	AcquireSRWLockShared(&gCpuMappableResourceLock);
	const bool eligible = gCpuMappableResources.find(resource) != gCpuMappableResources.end();
	const UINT64 resolvedGeneration =
		gCpuMappableResourceGeneration.load(std::memory_order_relaxed);
	ReleaseSRWLockShared(&gCpuMappableResourceLock);
	gCpuMappableLookupCache.resource = resource;
	gCpuMappableLookupCache.generation = resolvedGeneration;
	gCpuMappableLookupCache.eligible = eligible;
	return eligible;
}

// Hooked Map ??records the mapped pointer for later content capture
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
		if (SUCCEEDED(hr) && ppData && *ppData && IsCpuMappableTrackedResource(resource)) {
			TrackResourceMap(resource, Subresource, *ppData);
		}
	}
	return hr;
}

// Hooked Unmap ??captures buffer content before unmapping
static void STDMETHODCALLTYPE HookedResourceUnmap(
	ID3D12Resource *resource,
	UINT Subresource,
	const D3D12_RANGE *pWrittenRange)
{
	{
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Record);
		CaptureResourceMapWrites(resource, Subresource, pWrittenRange, _ReturnAddress());
	}

	// Call original Unmap
	using UnmapFn = void(STDMETHODCALLTYPE *)(ID3D12Resource*, UINT, const D3D12_RANGE*);
	UnmapFn original = reinterpret_cast<UnmapFn>(DX12ResourceTrackerHookState::ResourceOriginals.Unmap);
	if (original) {
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Original);
		original(resource, Subresource, pWrittenRange);
	}
}

// Wrapper-mode Map interception. ResourceWrapper::Map routes here so buffer
// content capture works without any code patching (RenderDoc-style).
HRESULT DX12ResourceTrackerHookRuntime::WrappedResourceMap(
	ID3D12Resource *resource, UINT subresource, const D3D12_RANGE *readRange, void **data)
{
	if (!resource)
		return E_POINTER;

	HRESULT hr = E_FAIL;
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Original);
		hr = resource->Map(subresource, readRange, data);
	}
	{
		DX12_CREATE_TIMING_SCOPE(ResourceMapHook, Record);
		if (SUCCEEDED(hr) && data && *data && IsCpuMappableTrackedResource(resource)) {
			TrackResourceMap(resource, subresource, *data);
		}
	}
	return hr;
}

// Wrapper-mode Unmap interception. ResourceWrapper::Unmap routes here.
void DX12ResourceTrackerHookRuntime::WrappedResourceUnmap(
	ID3D12Resource *resource, UINT subresource,
	const D3D12_RANGE *writtenRange, const void *caller)
{
	if (!resource)
		return;

	{
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Record);
		CaptureResourceMapWrites(resource, subresource, writtenRange, caller);
	}

	{
		DX12_CREATE_TIMING_SCOPE(ResourceUnmapHook, Original);
		resource->Unmap(subresource, writtenRange);
	}
}

// Resource VTable Hook Installation
void DX12ResourceTrackerHookRuntime::InstallMapUnmapHooks(ID3D12Resource *resource)
{
	if (!resource)
		return;

	// DX11 keeps CPU-side buffer content for region hashing; DX12 needs the same
	// data when IA texture overrides are active, not only during dump/hunt.
	if (!DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking() &&
	    !DX12ResourceTrackingPolicyUtils::NeedsIaContentTracking())
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
	RegisterCpuMappableResource(resource);

	// Wrapper-only mode: ResourceWrapper::Map/Unmap perform the same capture via
	// DX12ResourceTrackerHookRuntime::WrappedResourceMap/WrappedResourceUnmap;
	// vtable patching was removed (DLSS FG compat).
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

void DX12ResourceTrackerHookRuntime::RecordCreatedConstantBufferView(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor)
{
	DX12DescriptorHeapShadowUtils::RecordCreatedConstantBufferViewIfChanged(
		desc, destDescriptor);
}

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
	if (!DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking() ||
			!destDescriptorRangeStarts || !srcDescriptorRangeStarts)
		return;

	UINT destRange = 0;
	UINT srcRange = 0;
	UINT destOffset = 0;
	UINT srcOffset = 0;
	DX12DescriptorHeapShadowUtils::CopyTelemetryBatch copyTelemetry;
	// The increment is a device constant per heap type; query the device only
	// once per type instead of paying a COM vtable call on every
	// CopyDescriptors.
	UINT increment = DX12DescriptorHeapShadowUtils::CachedIncrementForType(
		descriptorHeapsType);
	if (!increment && device)
		increment = device->GetDescriptorHandleIncrementSize(descriptorHeapsType);
	if (!increment)
		return;
	DX12DescriptorHeapShadowUtils::CacheIncrementForType(
		descriptorHeapsType, increment);
	D3D12_CPU_DESCRIPTOR_HANDLE pendingDest = {};
	D3D12_CPU_DESCRIPTOR_HANDLE pendingSrc = {};
	UINT pendingCount = 0;
	auto flushPendingRange = [&]() {
		if (!pendingCount)
			return;
		if (pendingDest.ptr == pendingSrc.ptr) {
			copyTelemetry.RecordRange(pendingCount, false, false, false);
			copyTelemetry.RecordResults(
				pendingCount,
				0,
				0,
				pendingCount,
				0,
				0);
			pendingCount = 0;
			return;
		}
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
	if (DX12ResourceTrackingPolicyUtils::NeedsDescriptorTracking()) {
		if (numDescriptors != 0 &&
				destDescriptorRangeStart.ptr == srcDescriptorRangeStart.ptr)
			return;
		DX12DescriptorHeapShadowUtils::CopyDescriptorRange(
			descriptorHeapsType, destDescriptorRangeStart,
			srcDescriptorRangeStart, numDescriptors);
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
		DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(device, resource, desc, destDescriptor);
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
			device, resource, counterResource, descToUse, destDescriptor);
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
		DX12BindingCollectQueue::Start();
		DX12ResourceCollectQueue::Start();
		DX12LogManager::Text("DX12 persistent resource and descriptor tracking enabled (wrapper mode)\n");
	}
}
