// DX12ResourceMapCapture: CPU-mappable registry and Map/Unmap write capture.
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


static SRWLOCK gCpuMappableResourceLock = SRWLOCK_INIT;
static std::unordered_set<ID3D12Resource*> gCpuMappableResources;
static std::atomic<UINT64> gCpuMappableResourceGeneration{1};

void RegisterCpuMappableResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gCpuMappableResourceLock);
	if (gCpuMappableResources.insert(resource).second)
		gCpuMappableResourceGeneration.fetch_add(1, std::memory_order_release);
	ReleaseSRWLockExclusive(&gCpuMappableResourceLock);
	{
		static volatile LONG registerLogCounter = 0;
		if ((InterlockedIncrement(&registerLogCounter) & 0xFFF) == 1) {
			DX12LogManager::Fields("DX12BufferCaptureRegister",
				"stage=registered,resource=%p",
				resource);
		}
	}
}

void UnregisterCpuMappableResource(ID3D12Resource *resource, ID3D12Resource *canonical)
{
	AcquireSRWLockExclusive(&gCpuMappableResourceLock);
	bool removed = resource && gCpuMappableResources.erase(resource) != 0;
	if (canonical && canonical != resource)
		removed = gCpuMappableResources.erase(canonical) != 0 || removed;
	if (removed)
		gCpuMappableResourceGeneration.fetch_add(1, std::memory_order_release);
	ReleaseSRWLockExclusive(&gCpuMappableResourceLock);
}


// RecordDescriptorMerged removed - dead code.  Hooks now call
// DX12DescriptorHeapShadowUtils::RecordDescriptor directly.

// Buffer Content Capture via Map/Unmap hooks
// Like DX11: capture UPLOAD buffer data at Unmap time so Tier 1
// (content CRC32) hashing works.  Maps are tracked per-thread.
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

struct DX12CpuMappableLookupCache
{
	ID3D12Resource *resource = nullptr;
	UINT64 generation = 0;
	bool eligible = false;
	bool isUpload = false;
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
	bool isUpload = false;
	if (eligible) {
		D3D12_HEAP_PROPERTIES heapProps = {};
		D3D12_HEAP_FLAGS heapFlags = {};
		if (SUCCEEDED(resource->GetHeapProperties(&heapProps, &heapFlags)))
			isUpload = heapProps.Type == D3D12_HEAP_TYPE_UPLOAD;
	}
	gCpuMappableLookupCache.isUpload = isUpload;
	return eligible;
}

// Upload-heap check for the async capture path. Reuses the TLS lookup cache
// populated by IsCpuMappableTrackedResource so the capture path pays at most
// one cached load beyond the eligibility check.
static bool IsCpuMappableUploadResource(ID3D12Resource *resource)
{
	if (!IsCpuMappableTrackedResource(resource))
		return false;
	return gCpuMappableLookupCache.isUpload;
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
	// Async capture: enqueue a copy-queue readback instead of memcpy'ing the
	// written range into the content cache on the recording thread. Only
	// UPLOAD heaps are GPU-readable by the copy engine without barriers;
	// READBACK heaps are CPU-read-only and never written by the game this way.
	if (!IsCpuMappableUploadResource(resource)) {
		static volatile LONG captureSkipLogCounter = 0;
		if ((InterlockedIncrement(&captureSkipLogCounter) & 0xFF) == 1) {
			DX12LogManager::Fields("DX12BufferCaptureSkip",
				"stage=not_upload_or_untracked,resource=%p,offset=%llu,bytes=%llu",
				resource,
				static_cast<unsigned long long>(offset),
				static_cast<unsigned long long>(bytes));
		}
		return;
	}
	DX12AsyncReadbackQueueBufferCapture(resource, offset, bytes);
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
