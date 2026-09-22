// DX12ResourceContentHashing: GPU-VA resolve, buffer content cache, texture hash and IA hash.
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



class DX12BufferResolveRangeUtils
{
public:
// Cache entries retain both the canonical resource pointer and its monotonic
// identity generation.  That pair is sufficient to prove that a resolved
// buffer still names the same live D3D12 resource: RecordResource changes the
// generation whenever its GPU-VA range changes, while InvalidateResourceLocked
// removes every alias before its slot can be reused.  Do this O(1) validation
// instead of replaying the global range-invalidation journal for every cache
// hit.  The journal may contain thousands of unrelated transient resources in
// one frame, which turned a local IA cache hit into an O(history) operation.
static bool ApplyLatestResourceState(DX12BufferResourceSummary *summary)
{
	if (!summary || !summary->resource || !summary->resourceGeneration ||
		!summary->publishedState)
		return false;
	return summary->publishedState->TryRead(
		summary->resourceGeneration,
		&summary->currentState,
		&summary->hasCurrentState,
		&summary->stateTrackedByBarrier);
}

static bool FillSummaryFromCacheEntry(
	UINT64 gpuVirtualAddress, UINT64 size,
	const BufferResolveCacheEntry &entry,
	DX12BufferResourceSummary *summary)
{
	if (!summary || !entry.begin || gpuVirtualAddress < entry.begin || gpuVirtualAddress >= entry.end)
		return false;
	*summary = entry.summary;
	summary->resourceOffset = gpuVirtualAddress - entry.begin;
	const UINT64 available = entry.end - gpuVirtualAddress;
	summary->viewSize = size < available ? size : available;
	return DX12BufferResolveRangeUtils::ApplyLatestResourceState(summary);
}

static bool TryResolveSummaryFromResourceRecord(
	UINT64 gpuVirtualAddress, UINT64 size,
	const ResourceRecord &resource,
	DX12BufferResourceSummary *summary,
	UINT64 *resolvedBegin,
	UINT64 *resolvedEnd)
{
	if (!summary)
		return false;
	if (!resource.resource ||
		resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		resource.gpuVirtualAddress == 0 ||
		resource.size == 0)
		return false;

	const UINT64 begin = resource.gpuVirtualAddress;
	const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
		resource.gpuVirtualAddressRealEnd : begin + resource.size;
	if (gpuVirtualAddress < begin || gpuVirtualAddress >= realEnd)
		return false;

	summary->resource = resource.resource;
	summary->resourceGeneration = resource.identityGeneration;
	summary->publishedState = resource.publishedState;
	summary->resourceDesc = resource.desc;
	summary->hasResourceDesc = true;
	summary->gpuVirtualAddress = resource.gpuVirtualAddress;
	summary->resourceOffset = gpuVirtualAddress - begin;
	const UINT64 available = realEnd - gpuVirtualAddress;
	summary->viewSize = size < available ? size : available;
	if (resource.hasHeapType) {
		summary->resourceHeapType = static_cast<UINT>(resource.heapType);
		summary->hasResourceHeapType = true;
	}
	summary->currentState = static_cast<UINT>(resource.currentState);
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	if (resolvedBegin)
		*resolvedBegin = begin;
	if (resolvedEnd)
		*resolvedEnd = realEnd;
	return true;
}

template <typename IteratorT>
static bool SearchBackwardForCoveringBuffer(
	IteratorT current,
	IteratorT beginIt,
	UINT64 gpuVirtualAddress,
	UINT64 size,
	DX12BufferResourceSummary *summary,
	UINT64 *resolvedBegin,
	UINT64 *resolvedEnd,
	UINT *probes)
{
	if (!summary)
		return false;

	// The covering buffer of an interior VA is always the nearest predecessor
	// (D3D12 allocations are disjoint, so no other buffer's begin can lie
	// between it and the query). The walk therefore only runs long for VAs
	// that are inside NO tracked buffer (mod-owned resources created outside
	// the tracker, freed VAs, ...) - and for those it used to traverse the
	// entire map under the lock: thousands of pointer-chasing probes, 300-800
	// microseconds per pre-skin slot resolve in ZZZ. Bound the miss walk.
	constexpr UINT kMaxResolveBackwardProbes = 64;
	for (IteratorT it = current;;) {
		if (probes) {
			if (*probes >= kMaxResolveBackwardProbes)
				break;
			(*probes)++;
		}
		if (it->second < gResources.size()) {
			const ResourceRecord &resource = gResources[it->second];
			if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
					gpuVirtualAddress, size, resource, summary, resolvedBegin, resolvedEnd))
				return true;
		}

		if (it == beginIt)
			break;
		--it;
	}

	return false;
}
};

// Small TLS ring of recently resolved buffer ranges for
// DX12ResolveBufferResourceByGpuVa: streamer uploads walk VAs in bursts, so
// the last 32 resolutions cover most lookups without touching the maps.
struct DX12LocalBufferResolveCache
{
	size_t next = 0;
	std::array<BufferResolveCacheEntry, 32> entries = {};
};

bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	if (!summary || gpuVirtualAddress == 0)
		return false;

	*summary = DX12BufferResourceSummary();
	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return false;

	// Fast path: resolve through the sorted GPU VA 鈫?ResourceWrapper index.
	// The wrapper stores a cached ResourceRecord, avoiding both the
	// std::map red-black tree traversal and the gResources array lookup.
	if (DX12TryResolveFromGpuVaWrapper(gpuVirtualAddress, size, summary))
		return true;

	static thread_local DX12LocalBufferResolveCache localCache;
	for (size_t cacheIndex = 0;
			cacheIndex < localCache.entries.size();
			++cacheIndex) {
		const BufferResolveCacheEntry &entry = localCache.entries[cacheIndex];
		if (entry.begin && gpuVirtualAddress >= entry.begin && gpuVirtualAddress < entry.end) {
			const bool cacheResolved =
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, entry, summary);
			if (cacheResolved) {
				if (cacheIndex != 0)
					std::swap(
						localCache.entries[0],
						localCache.entries[cacheIndex]);
				return true;
			}
		}
	}

	bool resolved = false;
	UINT64 resolvedBegin = 0;
	UINT64 resolvedEnd = 0;
	UINT backwardProbes = 0;
	AcquireSRWLockShared(&gResourceLock);
	std::map<UINT64, BufferResolveCacheEntry>::iterator cached =
		gBufferResolveCache.upper_bound(gpuVirtualAddress);
	if (cached != gBufferResolveCache.begin()) {
		--cached;
		if (gpuVirtualAddress >= cached->second.begin &&
			gpuVirtualAddress < cached->second.end) {
			const bool cacheResolved =
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, cached->second, summary);
			if (cacheResolved) {
				localCache.entries[
					localCache.next++ % localCache.entries.size()] =
					cached->second;
				ReleaseSRWLockShared(&gResourceLock);
				return true;
			}
		}
	}

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer != gBufferResourceByGpuVa.begin()) {
		--foundBuffer;
		resolved = DX12BufferResolveRangeUtils::SearchBackwardForCoveringBuffer(
			foundBuffer,
			gBufferResourceByGpuVa.begin(),
			gpuVirtualAddress,
			size,
			summary,
			&resolvedBegin,
			&resolvedEnd,
			&backwardProbes);
	}
	ReleaseSRWLockShared(&gResourceLock);

	if (!resolved) {
		AcquireSRWLockShared(&gResourceLock);
		// Safety net for resources created before hook activation or temporarily
		// absent from the synchronous normal-Mod index. The scan is O(n) under
		// the lock; bound it because games with heavy per-frame buffer churn
		// (ring rotation) pay a full-array scan per untracked VA resolve -
		// measured at 50-500us per pre-skin slot in ZZZ with zero hits, since
		// the pre-hook resources are exactly the oldest entries (front).
		const size_t scanLimit =
			std::min<size_t>(gResources.size(), 512);
		for (size_t i = 0; i < scanLimit; ++i) {
			const ResourceRecord &resource = gResources[i];
			if (!resource.resource)
				continue;
			if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
				continue;
			if (resource.gpuVirtualAddress == 0 || resource.size == 0)
				continue;
			if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
					gpuVirtualAddress, size, resource, summary,
					&resolvedBegin, &resolvedEnd)) {
				resolved = true;
				DX12FrameAnalysis::LogJsonFunc("BufferResolveFallbackScanHit",
						"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"begin\":\"0x%llX\",\"end\":\"0x%llX\",\"resource\":\"%p\",\"resourceSize\":%llu,\"heapType\":%u,\"hasHeapType\":%u",
					static_cast<unsigned long long>(gpuVirtualAddress),
					static_cast<unsigned long long>(size),
					static_cast<unsigned long long>(resolvedBegin),
					static_cast<unsigned long long>(resolvedEnd),
					resource.resource,
					static_cast<unsigned long long>(resource.size),
					resource.hasHeapType ? static_cast<UINT>(resource.heapType) : UINT_MAX,
					resource.hasHeapType ? 1 : 0);
				break;
			}
		}
		ReleaseSRWLockShared(&gResourceLock);
	}
	if (resolved) {
		BufferResolveCacheEntry entry;
		entry.begin = resolvedBegin;
		entry.end = resolvedEnd;
		entry.summary = *summary;
		entry.summary.resourceOffset = 0;
		entry.summary.viewSize = entry.end - entry.begin;
		AcquireSRWLockExclusive(&gResourceLock);
		gBufferResolveCache[entry.begin] = entry;
		ReleaseSRWLockExclusive(&gResourceLock);
		localCache.entries[
			localCache.next++ % localCache.entries.size()] = entry;
		if (backwardProbes > 1) {
			DX12FrameAnalysis::LogJsonFunc("BufferResolveBackwardScanHit",
				"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"begin\":\"0x%llX\",\"end\":\"0x%llX\",\"probes\":%u,\"resource\":\"%p\"",
				static_cast<unsigned long long>(gpuVirtualAddress),
				static_cast<unsigned long long>(size),
				static_cast<unsigned long long>(resolvedBegin),
				static_cast<unsigned long long>(resolvedEnd),
				backwardProbes,
				summary->resource);
		}
	}
	return resolved;
}

static bool FillBufferSummaryFromRecordLocked(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	if (!summary || gpuVirtualAddress == 0 || size == 0)
		return false;

	*summary = DX12BufferResourceSummary();
	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return false;

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer == gBufferResourceByGpuVa.begin())
		return false;
	--foundBuffer;
	return DX12BufferResolveRangeUtils::SearchBackwardForCoveringBuffer(
		foundBuffer,
		gBufferResourceByGpuVa.begin(),
		gpuVirtualAddress,
		size,
		summary,
		nullptr,
		nullptr,
		nullptr);
}

class DX12BufferResolveDiagnosticsUtils
{
public:
static const char *ReasonLocked(UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource)
{
	if (nearestBegin)
		*nearestBegin = 0;
	if (nearestEnd)
		*nearestEnd = 0;
	if (nearestResource)
		*nearestResource = 0;
	if (gpuVirtualAddress == 0)
		return "zero_gpu_va";

	const UINT64 requestedEnd = gpuVirtualAddress + size;
	if (requestedEnd < gpuVirtualAddress)
		return "range_overflow";

	std::map<UINT64, size_t>::iterator foundBuffer =
		gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
	if (foundBuffer == gBufferResourceByGpuVa.begin())
		return "no_lower_buffer";
	for (--foundBuffer;; --foundBuffer) {
		if (foundBuffer->second >= gResources.size()) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "index_invalid";
			continue;
		}

		const ResourceRecord &resource = gResources[foundBuffer->second];
		const UINT64 begin = resource.gpuVirtualAddress;
		const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
			resource.gpuVirtualAddressRealEnd : begin + resource.size;
		const UINT64 end = resource.gpuVirtualAddressOobEnd ?
			resource.gpuVirtualAddressOobEnd : realEnd;
		if (nearestBegin)
			*nearestBegin = begin;
		if (nearestEnd)
			*nearestEnd = end;
		if (nearestResource)
			*nearestResource = reinterpret_cast<UINT64>(resource.resource);
		if (!resource.resource) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "resource_null";
			continue;
		}
		if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "not_buffer";
			continue;
		}
		if (begin == 0 || resource.size == 0) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "empty_range";
			continue;
		}
		if (gpuVirtualAddress < begin) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "before_range";
			continue;
		}
		if (gpuVirtualAddress < realEnd) {
			if (requestedEnd > realEnd && gpuVirtualAddress < end)
				return "after_real_range_oob_candidate";
			return "unknown_covering_buffer_found";
		}
		if (requestedEnd > end) {
			if (foundBuffer == gBufferResourceByGpuVa.begin())
				return "after_range";
			continue;
		}
		if (foundBuffer == gBufferResourceByGpuVa.begin())
			return "unknown";
	}
	return "unknown";
}
};

const char *DX12DiagnoseBufferResolveByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource)
{
	AcquireSRWLockShared(&gResourceLock);
	const char *reason = DX12BufferResolveDiagnosticsUtils::ReasonLocked(
		gpuVirtualAddress, size, nearestBegin, nearestEnd, nearestResource);
	ReleaseSRWLockShared(&gResourceLock);
	return reason;
}

bool DX12FillBufferSummaryFromRecordLocked(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary)
{
	return FillBufferSummaryFromRecordLocked(gpuVirtualAddress, size, summary);
}

const char *DX12DiagnoseBufferResolveByGpuVaLocked(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource)
{
	return DX12BufferResolveDiagnosticsUtils::ReasonLocked(
		gpuVirtualAddress, size, nearestBegin, nearestEnd, nearestResource);
}

uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize)
{
	uint32_t hash = 0;
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'B', 'V');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (summary && summary->hasResourceDesc) {
		D3D12_RESOURCE_DESC desc = summary->resourceDesc;
		desc.Alignment = 0;
		hash = HashBytes(hash, &desc, sizeof(desc));
		if (summary->hasResourceHeapType)
			hash = HashBytes(hash, &summary->resourceHeapType, sizeof(summary->resourceHeapType));
	} else {
		hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	}

	if (!hash)
		hash = HashBytes(0, &fallbackGpuVirtualAddress, sizeof(fallbackGpuVirtualAddress));
	return hash;
}

uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot)
{
	uint32_t hash = DX12HashBufferResourceView(summary, fallbackGpuVirtualAddress, fallbackSize);
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));
	hash = HashBytes(hash, &fallbackGpuVirtualAddress, sizeof(fallbackGpuVirtualAddress));
	hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));
	return hash;
}

// Buffer Content Cache
// Like DX11: cache the actual buffer bytes so IA hashes are based on
// CONTENT (CRC32 of data), not GPU virtual address.
// Keyed by GPU VA base of the resource so sub-regions resolve correctly.

struct CachedBufferContent
{
	struct Page
	{
		std::array<uint8_t, 4096> data = {};
		bool valid = false;
	};
	std::unordered_map<size_t, Page> pages;
	UINT64 gpuVaBase = 0;
	UINT64 dataSize = 0;
	UINT64 generation = 1;
};

static std::unordered_map<ID3D12Resource*, CachedBufferContent> gBufferContentByResource;
static SRWLOCK gBufferContentLock = SRWLOCK_INIT;
static std::atomic<UINT64> gBufferContentGeneration{1};

// Plan fix D: per-thread cache of per-resource content generations, valid
// while the global content generation is unchanged (every content change
// bumps the global generation, so an unchanged global implies every
// per-resource value is unchanged). Avoids a locked map lookup per validated
// IA-hash entry on every draw when the game rewrites dynamic buffers.
struct DX12BufferContentGenerationTlsEntry
{
	ID3D12Resource *resource = nullptr;
	UINT64 globalGeneration = 0;
	UINT64 generation = 0;
};
static constexpr UINT32 DX12BufferContentGenerationTlsEntryCount = 8;
static thread_local DX12BufferContentGenerationTlsEntry gBufferContentGenerationTls[DX12BufferContentGenerationTlsEntryCount];
static thread_local UINT32 gBufferContentGenerationTlsNext = 0;

class DX12BufferContentCacheUtils
{
public:
	static constexpr UINT64 PageSize = 4096;

	static UINT64 RequiredSize(
		ID3D12Resource *resource, UINT64 offset, UINT64 bytes)
	{
		UINT64 required = offset + bytes;
		if (!resource)
			return required;
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		if (desc.Width > required)
			required = desc.Width;
		return required;
	}

	static void EnsureEntryStorage(
		CachedBufferContent *entry,
		UINT64 requiredSize)
	{
		if (!entry)
			return;
		if (requiredSize > entry->dataSize)
			entry->dataSize = requiredSize;
	}

	static bool ReadRange(
		const CachedBufferContent &entry, UINT64 offset, UINT64 size, uint8_t *output)
	{
		if (!output || !ArePagesValid(entry, offset, size))
			return false;
		const UINT64 end = offset + size;
		for (UINT64 cursor = offset; cursor < end;) {
			const size_t pageIndex = static_cast<size_t>(cursor / PageSize);
			const UINT64 pageBegin = static_cast<UINT64>(pageIndex) * PageSize;
			const UINT64 copyEnd = (std::min)(pageBegin + PageSize, end);
			auto page = entry.pages.find(pageIndex);
			if (page == entry.pages.end() || !page->second.valid)
				return false;
			memcpy(output + static_cast<size_t>(cursor - offset),
				page->second.data.data() + static_cast<size_t>(cursor - pageBegin),
				static_cast<size_t>(copyEnd - cursor));
			cursor = copyEnd;
		}
		return true;
	}

	static void PageRange(
		UINT64 offset,
		UINT64 size,
		size_t *firstPage,
		size_t *lastPage)
	{
		if (!firstPage || !lastPage || !size) {
			if (firstPage)
				*firstPage = 0;
			if (lastPage)
				*lastPage = 0;
			return;
		}
		*firstPage = static_cast<size_t>(offset / PageSize);
		*lastPage = static_cast<size_t>((offset + size - 1) / PageSize);
	}

	static void TouchPages(
		CachedBufferContent *entry,
		UINT64 offset,
		UINT64 size,
		bool valid)
	{
		if (!entry || !size)
			return;
		EnsureEntryStorage(entry, offset + size);
		size_t firstPage = 0;
		size_t lastPage = 0;
		PageRange(offset, size, &firstPage, &lastPage);
		for (size_t page = firstPage; page <= lastPage; ++page) {
			auto found = entry->pages.find(page);
			if (found == entry->pages.end())
				continue;
			found->second.valid = valid;
		}
	}

	static bool ArePagesValid(
		const CachedBufferContent &entry,
		UINT64 offset,
		UINT64 size)
	{
		if (!size || offset + size > entry.dataSize)
			return false;
		size_t firstPage = 0;
		size_t lastPage = 0;
		PageRange(offset, size, &firstPage, &lastPage);
		for (size_t page = firstPage; page <= lastPage; ++page) {
			auto found = entry.pages.find(page);
			if (found == entry.pages.end() || !found->second.valid)
				return false;
		}
		return true;
	}
};

bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize)
{
	return DX12CacheBufferContentRange(resource, 0, data, dataSize);
}

// One modified page collected during the readback pass of
// DX12CacheBufferContentRange and committed under the exclusive lock.
struct DX12ChangedBufferPage
{
	size_t index = 0;
	UINT64 copyBegin = 0;
	UINT64 copyEnd = 0;
	bool wasValid = false;
	std::array<uint8_t, DX12BufferContentCacheUtils::PageSize> data = {};
};

bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize)
{
	if (!resource || !data || !dataSize)
		return false;
	// Bound the capture: a NULL written range on Unmap means the whole
	// subresource may have changed. The deadlock guard lives in the Unmap
	// hook (caller-module allowlist), so this bound is only a memory sanity
	// net for the content cache; large game-side buffers must remain
	// capturable, hence the deliberately loose limit.
	constexpr UINT64 kMaxCaptureBytes = 256ull * 1024 * 1024;
	if (dataSize > kMaxCaptureBytes || resourceOffset > kMaxCaptureBytes) {
		DX12InvalidateBufferContent(resource);
		return false;
	}
	const UINT64 gpuVaBase = resource->GetGPUVirtualAddress();
	const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceOffset > UINT64_MAX - static_cast<UINT64>(dataSize))
		return false;
	const UINT64 requiredSize = (std::max)(
		resourceDesc.Width,
		resourceOffset + static_cast<UINT64>(dataSize));
	const UINT64 writeEnd = resourceOffset + static_cast<UINT64>(dataSize);
	size_t firstPage = 0;
	size_t lastPage = 0;
	DX12BufferContentCacheUtils::PageRange(
		resourceOffset, static_cast<UINT64>(dataSize), &firstPage, &lastPage);

	std::vector<DX12ChangedBufferPage> changed;
	changed.reserve(64);
	size_t changedPages = 0;
	size_t changedBytes = 0;
	UINT64 storedGpuVaBase = gpuVaBase;
	bool committed = false;
	for (unsigned attempt = 0; attempt != 3; ++attempt) {
		changed.clear();
		UINT64 observedGeneration = 0;
		bool hadEntry = false;
		AcquireSRWLockShared(&gBufferContentLock);
		auto found = gBufferContentByResource.find(resource);
		if (found != gBufferContentByResource.end() &&
		    found->second.gpuVaBase == gpuVaBase) {
			hadEntry = true;
			observedGeneration = found->second.generation;
		}
		for (size_t page = firstPage; page <= lastPage; ++page) {
			const UINT64 pageBegin = static_cast<UINT64>(page) *
				DX12BufferContentCacheUtils::PageSize;
			const UINT64 pageEnd = (std::min)(
				pageBegin + DX12BufferContentCacheUtils::PageSize, requiredSize);
			const UINT64 copyBegin = (std::max)(pageBegin, resourceOffset);
			const UINT64 copyEnd = (std::min)(pageEnd, writeEnd);
			if (copyEnd <= copyBegin)
				continue;
			const size_t srcOffset = static_cast<size_t>(copyBegin - resourceOffset);
			const size_t copySize = static_cast<size_t>(copyEnd - copyBegin);
			const CachedBufferContent::Page *oldPage = nullptr;
			if (hadEntry) {
				auto oldPageIt = found->second.pages.find(page);
				if (oldPageIt != found->second.pages.end())
					oldPage = &oldPageIt->second;
			}
			const bool oldValid = oldPage && oldPage->valid;
			const size_t pageOffset = static_cast<size_t>(copyBegin - pageBegin);
			if (oldValid && memcmp(oldPage->data.data() + pageOffset,
				static_cast<const uint8_t*>(data) + srcOffset, copySize) == 0)
				continue;
			DX12ChangedBufferPage update;
			update.index = page;
			update.copyBegin = copyBegin;
			update.copyEnd = copyEnd;
			update.wasValid = oldValid;
			if (oldValid)
				update.data = oldPage->data;
			memcpy(update.data.data() + pageOffset,
				static_cast<const uint8_t*>(data) + srcOffset, copySize);
			changed.push_back(update);
		}
		ReleaseSRWLockShared(&gBufferContentLock);

		AcquireSRWLockExclusive(&gBufferContentLock);
		CachedBufferContent &entry = gBufferContentByResource[resource];
		if (hadEntry && (entry.gpuVaBase != gpuVaBase ||
			entry.generation != observedGeneration)) {
			ReleaseSRWLockExclusive(&gBufferContentLock);
			continue;
		}
		if (entry.gpuVaBase != gpuVaBase) {
			entry = CachedBufferContent();
			entry.gpuVaBase = gpuVaBase;
		}
		DX12BufferContentCacheUtils::EnsureEntryStorage(&entry, requiredSize);
		for (const DX12ChangedBufferPage &update : changed) {
			CachedBufferContent::Page &page = entry.pages[update.index];
			page.data = update.data;
			const UINT64 pageBegin = static_cast<UINT64>(update.index) *
				DX12BufferContentCacheUtils::PageSize;
			const UINT64 pageEnd = (std::min)(pageBegin +
				DX12BufferContentCacheUtils::PageSize, requiredSize);
			page.valid = update.wasValid ||
				(update.copyBegin == pageBegin && update.copyEnd == pageEnd);
		}
		changedPages = changed.size();
		changedBytes = 0;
		for (const DX12ChangedBufferPage &update : changed)
			changedBytes += static_cast<size_t>(update.copyEnd - update.copyBegin);
		if (changedPages != 0)
			entry.generation = gBufferContentGeneration.fetch_add(
				1, std::memory_order_release) + 1;
		storedGpuVaBase = entry.gpuVaBase;
		ReleaseSRWLockExclusive(&gBufferContentLock);
		committed = true;
		break;
	}
	if (!committed)
		return false;

	return true;
}

bool DX12CopyBufferContentRange(
	ID3D12Resource *dstResource,
	UINT64 dstOffset,
	ID3D12Resource *srcResource,
	UINT64 srcOffset,
	UINT64 numBytes)
{
	if (!dstResource || !srcResource || !numBytes)
		return false;

	std::vector<uint8_t> copiedData;
	AcquireSRWLockShared(&gBufferContentLock);
	auto srcFound = gBufferContentByResource.find(srcResource);
	if (srcFound == gBufferContentByResource.end() ||
	    !DX12BufferContentCacheUtils::ArePagesValid(
			srcFound->second,
			srcOffset,
			numBytes)) {
		ReleaseSRWLockShared(&gBufferContentLock);
		DX12InvalidateBufferContentRange(dstResource, dstOffset, numBytes);
		return false;
	}

	copiedData.resize(static_cast<size_t>(numBytes));
	const bool copied = DX12BufferContentCacheUtils::ReadRange(
		srcFound->second, srcOffset, numBytes, copiedData.data());
	ReleaseSRWLockShared(&gBufferContentLock);
	if (!copied) {
		DX12InvalidateBufferContentRange(dstResource, dstOffset, numBytes);
		return false;
	}
	return DX12CacheBufferContentRange(
		dstResource, dstOffset, copiedData.data(), copiedData.size());
}

bool DX12CopyBufferContent(
	ID3D12Resource *dstResource,
	ID3D12Resource *srcResource)
{
	if (!dstResource || !srcResource)
		return false;
	const D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
	const D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();
	if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
	    dstDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;
	const UINT64 bytes = (std::min)(srcDesc.Width, dstDesc.Width);
	return bytes != 0 &&
		DX12CopyBufferContentRange(
			dstResource,
			0,
			srcResource,
			0,
			bytes);
}

UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource)
{
	if (!resource)
		return gBufferContentGeneration.load(std::memory_order_relaxed);
	// Plan fix D: lock-free TLS fast path. The IA hash cache validates every
	// tracked entry against the global content generation; games that rewrite
	// dynamic buffers every frame bump the global generation constantly,
	// which previously forced a locked map lookup per validated entry on
	// every draw. The global generation monotonically increases on every
	// content change, so "global unchanged" implies "per-resource unchanged"
	// and the cached per-resource value stays valid.
	const UINT64 globalGeneration = gBufferContentGeneration.load(std::memory_order_acquire);
	for (UINT32 i = 0; i < DX12BufferContentGenerationTlsEntryCount; ++i) {
		const DX12BufferContentGenerationTlsEntry &entry =
			gBufferContentGenerationTls[i];
		if (entry.resource == resource &&
		    entry.globalGeneration == globalGeneration)
			return entry.generation;
	}
	AcquireSRWLockShared(&gBufferContentLock);
	auto found = gBufferContentByResource.find(resource);
	const UINT64 generation = found == gBufferContentByResource.end() ?
		1 : found->second.generation;
	ReleaseSRWLockShared(&gBufferContentLock);
	DX12BufferContentGenerationTlsEntry &slot =
		gBufferContentGenerationTls[gBufferContentGenerationTlsNext++ &
			(DX12BufferContentGenerationTlsEntryCount - 1)];
	slot.resource = resource;
	slot.globalGeneration = globalGeneration;
	slot.generation = generation;
	return generation;
}

void DX12InvalidateBufferContentRange(
	ID3D12Resource *resource, UINT64 offset, UINT64 size)
{
	if (!resource || !size)
		return;
	AcquireSRWLockExclusive(&gBufferContentLock);
	auto found = gBufferContentByResource.find(resource);
	if (found != gBufferContentByResource.end() &&
	    offset < found->second.dataSize) {
		const UINT64 boundedSize = (std::min)(
			size, found->second.dataSize - offset);
		DX12BufferContentCacheUtils::TouchPages(
			&found->second, offset, boundedSize, false);
		found->second.generation =
			gBufferContentGeneration.fetch_add(
				1, std::memory_order_release) + 1;
	}
	ReleaseSRWLockExclusive(&gBufferContentLock);
}

void DX12InvalidateBufferContent(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gBufferContentLock);
	gBufferContentByResource.erase(resource);
	ReleaseSRWLockExclusive(&gBufferContentLock);
	// Thread-local IA caches use this epoch as the cheap indication that a
	// per-resource generation must be re-read. Without it, a released or
	// GPU-written resource can retain a stale content hash indefinitely.
	// Bump unconditionally: GPU copy paths (CopyBufferRegion) may target a
	// resource that was never CPU-tracked, and unresolved-IA fallback keys
	// still need the global epoch to advance even when the erase above was a
	// no-op (otherwise a stale IA hash survives until the async readback lands
	// a frame later, causing a one-frame wrong IA replacement / model flicker).
	gBufferContentGeneration.fetch_add(1, std::memory_order_release);
}


bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size,
	const uint8_t **outData, size_t *outDataSize)
{
	if (!outData || !outDataSize || !gpuVirtualAddress || !size)
		return false;
	*outData = nullptr;
	*outDataSize = 0;
	static thread_local std::vector<uint8_t> s_contentSnapshot;

	// Resolve GPU VA to find the owning resource
	DX12BufferResourceSummary summary = {};
	if (!DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary))
		return false;
	if (!summary.resource)
		return false;

	AcquireSRWLockShared(&gBufferContentLock);
	std::unordered_map<ID3D12Resource*, CachedBufferContent>::iterator it =
		gBufferContentByResource.find(summary.resource);
	if (it == gBufferContentByResource.end()) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	const CachedBufferContent &cached = it->second;
	if (gpuVirtualAddress < cached.gpuVaBase) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}
	const UINT64 offset = gpuVirtualAddress - cached.gpuVaBase;
	if (offset + size > cached.dataSize ||
	    !DX12BufferContentCacheUtils::ArePagesValid(cached, offset, size)) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	s_contentSnapshot.resize(static_cast<size_t>(size));
	const bool copied = DX12BufferContentCacheUtils::ReadRange(
		cached, offset, size, s_contentSnapshot.data());
	ReleaseSRWLockShared(&gBufferContentLock);
	if (!copied)
		return false;
	*outData = s_contentSnapshot.data();
	*outDataSize = s_contentSnapshot.size();
	return true;
}

struct CachedTextureHash
{
	uint32_t hash = 0;
	D3D12_RESOURCE_DESC desc = {};
	UINT64 generation = 0;
};

static std::unordered_map<ID3D12Resource*, CachedTextureHash> gTextureHashByResource;
static SRWLOCK gTextureHashLock = SRWLOCK_INIT;
static std::atomic<UINT64> gTextureHashGeneration{1};

bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source)
{
	if (!resource || !hash)
		return false;

	CachedTextureHash entry;
	entry.hash = hash;
	if (desc)
		entry.desc = *desc;
	entry.generation = gTextureHashGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

	AcquireSRWLockExclusive(&gTextureHashLock);
	gTextureHashByResource[resource] = entry;
	ReleaseSRWLockExclusive(&gTextureHashLock);
	// Hash arrived: the wanted-texture requester (if any) is satisfied.
	DX12ReadbackForgetWantedTexture(resource, false);

	// TIMING-build scene-switch diagnostic: every hash publish, one line per
	// texture. After a scene change the modded texture's new resource must
	// appear here with the SAME hash the override targets; absence means the
	// upload never passed the copy-tracking gate.
	DX12_LOG("DX12TextureHashCache",
		"stage=store,resource=%p,hash=%08x,width=%llu,height=%u,mips=%u,"
		"format=%u,source=%s",
		resource, hash,
		static_cast<unsigned long long>(entry.desc.Width),
		entry.desc.Height, entry.desc.MipLevels,
		static_cast<UINT>(entry.desc.Format),
		source ? source : "");

	return true;
}

bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash)
{
	if (!hash)
		return false;
	*hash = 0;
	if (!resource)
		return false;

	AcquireSRWLockShared(&gTextureHashLock);
	std::unordered_map<ID3D12Resource*, CachedTextureHash>::iterator it =
		gTextureHashByResource.find(resource);
	if (it == gTextureHashByResource.end()) {
		ReleaseSRWLockShared(&gTextureHashLock);
		return false;
	}
	*hash = it->second.hash;
	ReleaseSRWLockShared(&gTextureHashLock);
	return *hash != 0;
}

UINT64 DX12GetTextureHashGeneration()
{
	return gTextureHashGeneration.load(std::memory_order_acquire);
}

void DX12InvalidateTextureHash(ID3D12Resource *resource)
{
	if (!resource)
		return;
	uint32_t erasedHash = 0;
	AcquireSRWLockExclusive(&gTextureHashLock);
	std::unordered_map<ID3D12Resource*, CachedTextureHash>::iterator found =
		gTextureHashByResource.find(resource);
	if (found != gTextureHashByResource.end()) {
		erasedHash = found->second.hash;
		gTextureHashByResource.erase(found);
	}
	ReleaseSRWLockExclusive(&gTextureHashLock);
	if (!erasedHash)
		return;
	gTextureHashGeneration.fetch_add(1, std::memory_order_relaxed);
	// TIMING-build scene-switch diagnostic: resource released with a live hash
	// (the scene teardown half of the hash lifecycle; pair with stage=store).
	DX12_LOG("DX12TextureHashCache",
		"stage=invalidate,resource=%p,hash=%08x", resource, erasedHash);
}
UINT64 DX12GetIaHashContentGeneration()
{
	return gBufferContentGeneration.load(std::memory_order_relaxed);
}

UINT64 DX12GetIaHashResolveGeneration()
{
	return gBufferResolveInvalidationEpochAtomic.load(std::memory_order_acquire);
}


// Flat, view-keyed memo in front of DX12HashIaBufferContentView's L1 map.
//
// The L1/L2/L3 layer below resolves a view hash correctly but is made of node
// containers, so one lookup costs a hash, a bucket walk and at least one node
// dereference - measured at 209 ns/call for a warm 512-view working set, and
// more once the map holds a frame's worth of views and the nodes are cold.
// The draw hook asks for the same view hashes over and over: a game rebinds its
// IA views every draw (measured ~1181 rebuilt view hashes per frame on the
// profiled title) and then replays the identical bind sequence next frame, while
// the caller-side slot memo cannot survive the rebind it exists to serve.
//
// This table answers that repeated question from a flat array. It is a pure
// accelerator: it duplicates the L1's own validity test instead of weakening
// it, and a miss (including "I cannot prove this is still current") falls
// through to the existing path rather than returning anything unproven.
//
// Validity, exactly as the L1 map decides it for an IA (metadata-only) hash:
//   - A resolved view carries the published identity it was hashed from. The
//     published record is immutable, so an unchanged identity generation proves
//     every hash input unchanged - including a resource recreated on the same
//     GPU virtual address with a different descriptor, which is the case a
//     metadata hash cannot see on its own.
//   - A view whose GPUVA is not resolved has no identity to compare, so it is
//     only reusable while the range-scoped invalidation state the L1 validates
//     against has not moved; a moved epoch falls through to the L1 instead of
//     being trusted here.
class IaViewHashMemo
{
public:
	// Sized from the profile, not guessed: the profiled title rebuilds ~1181
	// distinct view hashes per frame in its heaviest observed scene, and a
	// deterministic game replays the same bind sequence, so the reuse distance
	// of a resident view is one frame. 8192 entries cover several frames of the
	// heaviest observed scene and still leave room for a larger one; the table
	// is heap-owned per resolving thread and only allocated once it is used.
	static constexpr size_t kSetCount = 4096;
	static constexpr size_t kWayCount = 2;

	struct Entry
	{
		// First line: the key comparison and the answer.
		UINT64 gpuVa = 0;
		UINT64 size = 0;
		UINT64 resolveGeneration = 0;
		UINT32 stride = 0;
		UINT32 format = 0;
		UINT32 slot = 0;
		UINT32 hash = 0;
		// Second line: the identity that revalidates it. A zero hash marks an
		// unused way, which is safe because the hash function tags every value
		// it returns with MakeTrackerFourCC('D','X','I','A') and never returns
		// zero for a view it accepted.
		UINT64 resourceGeneration = 0;
		ID3D12Resource *resource = nullptr;
		DX12ResourcePublishedState *publishedState = nullptr;
	};

	static bool TryRead(
		UINT64 gpuVa, UINT64 size, UINT stride, UINT format, UINT slot,
		UINT64 resolveGeneration, UINT32 *hashOut)
	{
		Entry *set = SetFor(gpuVa, size, stride, format, slot);
		for (size_t way = 0; way < kWayCount; ++way) {
			Entry &entry = set[way];
			if (!entry.hash ||
			    entry.gpuVa != gpuVa || entry.size != size ||
			    entry.stride != stride || entry.format != format ||
			    entry.slot != slot)
				continue;
			const bool hasTrackedIdentity =
				entry.resource && entry.resourceGeneration && entry.publishedState;
			if (hasTrackedIdentity) {
				if (!entry.publishedState->TryRead(
						entry.resourceGeneration, nullptr, nullptr, nullptr))
					return false;
			} else if (entry.resolveGeneration != resolveGeneration) {
				// An unresolved GPUVA range: its hash is a pure function of the
				// view fields, so it can only change once a resource is published
				// over the range. That is precisely what the range-scoped
				// invalidation test answers, and it is the same test the L1 map
				// runs before it revalidates its own entry - so this is not a
				// weaker answer, it is the same answer reached without the map
				// lookup and its node chase. A moved epoch on its own proves
				// nothing: unrelated buffer churn moves it several times per
				// frame (measured 5..17 on the profiled title), and treating that
				// as evidence of change sent every IA view hash back through the
				// container path.
				if (RangeInvalidatedSince(gpuVa, size, entry.resolveGeneration))
					return false;
				entry.resolveGeneration = resolveGeneration;
			}
			if (hashOut)
				*hashOut = entry.hash;
			// TIMING-only: how much of the per-draw view-hash traffic this
			// accelerator actually answers, so "the container path is still
			// paying" can be told apart from "the keys genuinely differ each
			// frame" without guessing.
			DX12_PROF_COUNT("Rm.IaViewHashMemoHit");
			return true;
		}
		return false;
	}

	// Published after a hash was produced or revalidated, so the entry always
	// describes a value the authority below has just confirmed.
	static void Publish(
		UINT64 gpuVa, UINT64 size, UINT stride, UINT format, UINT slot,
		UINT32 hash, UINT64 resolveGeneration, UINT64 resourceGeneration,
		ID3D12Resource *resource, DX12ResourcePublishedState *publishedState)
	{
		if (!hash)
			return;
		Entry *set = SetFor(gpuVa, size, stride, format, slot);
		// A way already holding this key is refreshed in place; otherwise the
		// first way holding a different key is replaced. Two ways keep the
		// frame-to-frame working set of one recording thread resident without a
		// recency update on the hot path.
		Entry *target = &set[0];
		for (size_t way = 0; way < kWayCount; ++way) {
			const Entry &entry = set[way];
			if (!entry.hash || (entry.gpuVa == gpuVa && entry.size == size &&
					entry.stride == stride && entry.format == format &&
					entry.slot == slot)) {
				target = &set[way];
				break;
			}
		}
		target->gpuVa = gpuVa;
		target->size = size;
		target->stride = stride;
		target->format = format;
		target->slot = slot;
		target->resolveGeneration = resolveGeneration;
		target->resourceGeneration = resourceGeneration;
		target->resource = resource;
		target->publishedState = publishedState;
		target->hash = hash;
	}

private:
	static_assert((kSetCount & (kSetCount - 1)) == 0,
		"view hash memo set count must be a power of two");

	// Range-scoped revalidation of an unresolved view, under the same shared
	// lock and with the same predicate the L1 map uses for the same case. Kept
	// here rather than in the L1 so the accelerator can answer without first
	// walking the container that sent it the question.
	static bool RangeInvalidatedSince(UINT64 gpuVa, UINT64 size, UINT64 sinceEpoch)
	{
		AcquireSRWLockShared(&gResourceLock);
		const bool invalidated =
			DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
				gpuVa, gpuVa + size, sinceEpoch);
		ReleaseSRWLockShared(&gResourceLock);
		return invalidated;
	}

	// Buffer views are typically 256-byte aligned and large, so a whole view
	// key can differ only above bit 5. The set index is taken from the low bits
	// after a full avalanche (the same splitmix64 finalizer the IA signature
	// uses), because a partial mix left neighbouring views of one buffer sharing
	// a set - 512 views landed in 64 sets and thrashed two ways.
	static Entry *SetFor(UINT64 gpuVa, UINT64 size, UINT stride, UINT format, UINT slot)
	{
		UINT64 key = gpuVa ^ (size * 0x9e3779b97f4a7c15ull);
		key ^= static_cast<UINT64>(stride) << 32;
		key ^= static_cast<UINT64>(format) * 0x2545f4914f6cdd1dull;
		key ^= static_cast<UINT64>(slot) << 17;
		key ^= key >> 30;
		key *= 0xbf58476d1ce4e5b9ull;
		key ^= key >> 27;
		key *= 0x94d049bb133111ebull;
		key ^= key >> 31;
		const size_t set = static_cast<size_t>(key) & (kSetCount - 1);
		return &Table()[set * kWayCount];
	}

	// Heap-owned rather than a TLS array: this is a cache, and a thread that
	// never resolves a view hash should not carry its footprint.
	static Entry *Table()
	{
		static thread_local std::unique_ptr<Entry[]> table;
		if (!table)
			table = std::make_unique<Entry[]>(kSetCount * kWayCount);
		return table.get();
	}
};


uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	// L1 uses GPUVA to exploit temporal locality.
	// L2 uses resource identity, offset, and size for stable reuse.
	// This matches the DX11 resource, offset, and size key model.
	// Resource-generation invalidation:
	// - Resolved resources publish identity through a stable lock-free token.
	// - Unresolved GPUVA ranges fall back to gBufferResolveGeneration.
	// - CPU-upload content changes bump gBufferContentGeneration.
	// This keeps IA region hashes reusable across frames without keeping stale
	// content-derived hashes after upload buffers are rewritten.
	struct IaHashCacheValue {
		uint32_t hash = 0;
		UINT64 resolveGeneration = 0;
		UINT64 contentGeneration = 0;
		UINT64 observedGlobalContentGeneration = 0;
		UINT64 gpuVa = 0;
		UINT64 size = 0;
		ID3D12Resource *resource = nullptr;
		UINT64 resourceGeneration = 0;
		DX12ResourcePublishedState *publishedState = nullptr;
		bool tracksContent = false;
	};

	// -- L1 key (gpuVa-based) --
	struct IaHashKeyL1 {
		UINT64 gpuVa; UINT64 sz; UINT str; UINT fmt; UINT sl;
		bool operator==(const IaHashKeyL1 &o) const {
			return gpuVa == o.gpuVa && sz == o.sz && str == o.str && fmt == o.fmt && sl == o.sl;
		}
	};
	struct IaHashKeyL1Hasher {
		size_t operator()(const IaHashKeyL1 &k) const {
			return static_cast<size_t>(k.gpuVa ^ (k.gpuVa >> 32) ^ k.sz ^ (k.sz >> 32) ^
				(static_cast<UINT64>(k.str) << 20) ^ (static_cast<UINT64>(k.fmt) << 28) ^
				(static_cast<UINT64>(k.sl) << 36));
		}
	};

	// -- L2 key (resource-based, aligns with DX11) --
	struct IaHashKeyL2 {
		void *resource; UINT64 offset; UINT64 viewSz; UINT str; UINT fmt; UINT sl;
		bool operator==(const IaHashKeyL2 &o) const {
			return resource == o.resource && offset == o.offset && viewSz == o.viewSz &&
				str == o.str && fmt == o.fmt && sl == o.sl;
		}
	};
	struct IaHashKeyL2Hasher {
		size_t operator()(const IaHashKeyL2 &k) const {
			UINT64 h = reinterpret_cast<UINT64>(k.resource);
			return static_cast<size_t>(h ^ (h >> 32) ^ k.offset ^ (k.offset >> 32) ^
				k.viewSz ^ (k.viewSz >> 32) ^
				(static_cast<UINT64>(k.str) << 20) ^ (static_cast<UINT64>(k.fmt) << 28) ^
				(static_cast<UINT64>(k.sl) << 36));
		}
	};

	struct IaHashKeyL3Resolved {
		UINT dimension; UINT64 width; UINT height; UINT depthOrArraySize;
		UINT mipLevels; UINT resourceFormat; UINT sampleCount; UINT sampleQuality;
		UINT layout; UINT flags; UINT heapType;
		UINT64 resourceOffset; UINT64 viewSize;
		UINT str; UINT fmt; UINT sl;
		bool operator==(const IaHashKeyL3Resolved &o) const {
			return dimension == o.dimension &&
				width == o.width &&
				height == o.height &&
				depthOrArraySize == o.depthOrArraySize &&
				mipLevels == o.mipLevels &&
				resourceFormat == o.resourceFormat &&
				sampleCount == o.sampleCount &&
				sampleQuality == o.sampleQuality &&
				layout == o.layout &&
				flags == o.flags &&
				heapType == o.heapType &&
				resourceOffset == o.resourceOffset &&
				viewSize == o.viewSize &&
				str == o.str &&
				fmt == o.fmt &&
				sl == o.sl;
		}
	};
	struct IaHashKeyL3ResolvedHasher {
		size_t operator()(const IaHashKeyL3Resolved &k) const {
			size_t hash = 0x84222325a9582d75ull;
			auto mix = [&hash](UINT64 value) {
				hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
					(hash << 6) + (hash >> 2);
			};
			mix(k.dimension);
			mix(k.width);
			mix(k.height);
			mix(k.depthOrArraySize);
			mix(k.mipLevels);
			mix(k.resourceFormat);
			mix(k.sampleCount);
			mix(k.sampleQuality);
			mix(k.layout);
			mix(k.flags);
			mix(k.heapType);
			mix(k.resourceOffset);
			mix(k.viewSize);
			mix(k.str);
			mix(k.fmt);
			mix(k.sl);
			return hash;
		}
	};

	static thread_local std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher> s_cacheL1;
	static thread_local std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher> s_cacheL2;
	static thread_local std::unordered_map<
		IaHashKeyL3Resolved,
		uint32_t,
		IaHashKeyL3ResolvedHasher> s_cacheL3Resolved;
	static constexpr size_t MaxL1Entries = 32768;
	static constexpr size_t MaxL2Entries = 32768;
	static constexpr size_t MaxL3ResolvedEntries = 65536;
	const UINT64 resolveGeneration =
		gBufferResolveInvalidationEpochAtomic.load(std::memory_order_acquire);
	const UINT64 globalContentGeneration =
		gBufferContentGeneration.load(std::memory_order_relaxed);

	// -- flat view-keyed memo, ahead of every container lookup below --
	uint32_t memoHash = 0;
	if (IaViewHashMemo::TryRead(
			gpuVirtualAddress, size, stride, format, slot, resolveGeneration,
			&memoHash))
		return memoHash;

	// -- L1 lookup (gpuVa-based, fastest) --
	IaHashKeyL1 keyL1 = { gpuVirtualAddress, size, stride, format, slot };
	std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher>::iterator itL1 = s_cacheL1.find(keyL1);
	if (itL1 != s_cacheL1.end()) {
		IaHashCacheValue &cacheValue = itL1->second;
		bool invalidated = true;
		const bool hasTrackedResourceIdentity =
			cacheValue.resource != nullptr &&
			cacheValue.resourceGeneration != 0 &&
			cacheValue.publishedState != nullptr;
		if (!hasTrackedResourceIdentity ||
		    cacheValue.publishedState->TryRead(
			    cacheValue.resourceGeneration, nullptr, nullptr, nullptr)) {
			const UINT64 currentContentGeneration = cacheValue.tracksContent ?
				(cacheValue.observedGlobalContentGeneration == globalContentGeneration ?
					cacheValue.contentGeneration :
					(cacheValue.resource ?
						DX12GetBufferContentGenerationForResource(cacheValue.resource) :
						globalContentGeneration)) :
				cacheValue.contentGeneration;
			if (cacheValue.contentGeneration == currentContentGeneration) {
				if (cacheValue.tracksContent)
					cacheValue.observedGlobalContentGeneration = globalContentGeneration;
				if (hasTrackedResourceIdentity) {
					invalidated = false;
				} else if (cacheValue.resolveGeneration == resolveGeneration) {
					invalidated = false;
				} else {
					AcquireSRWLockShared(&gResourceLock);
					invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
						cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
					ReleaseSRWLockShared(&gResourceLock);
					if (!invalidated)
						cacheValue.resolveGeneration = resolveGeneration;
				}
			}
		}
		if (!invalidated) {
			IaViewHashMemo::Publish(
				gpuVirtualAddress, size, stride, format, slot, cacheValue.hash,
				cacheValue.resolveGeneration, cacheValue.resourceGeneration,
				cacheValue.resource, cacheValue.publishedState);
			return cacheValue.hash;
		}
		s_cacheL1.erase(itL1);
	}

	// -- L2 lookup (resource-based, stable across draws) --
	// DX11: (resource, offset, viewSize) gpuVa
	DX12BufferResourceSummary summary = {};
	const bool resolved = DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary);
	// DX11 (track_region_hashes off) matches buffers by the FULL RESOURCE
	// hash: the bound view range does not participate. Align DX12 with that
	// semantic - hash the full resource extent so a scene switch that changes
	// the bound view range of the same buffer (e.g. texcoords 9027 vs 15715
	// vertices) keeps matching the same mod hash.
	const UINT64 fullViewSize = resolved && summary.hasResourceDesc &&
			summary.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
			summary.resourceDesc.Width > summary.resourceOffset ?
		summary.resourceDesc.Width - summary.resourceOffset : size;
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, fullViewSize,
			stride, format, slot };
		std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher>::iterator itL2 = s_cacheL2.find(keyL2);
		if (itL2 != s_cacheL2.end()) {
			IaHashCacheValue &cacheValue = itL2->second;
			bool invalidated = true;
			const bool hasTrackedResourceIdentity =
				cacheValue.resource != nullptr &&
				cacheValue.resourceGeneration != 0 &&
				cacheValue.publishedState != nullptr;
			if (!hasTrackedResourceIdentity ||
			    cacheValue.publishedState->TryRead(
				    cacheValue.resourceGeneration, nullptr, nullptr, nullptr)) {
				const UINT64 currentContentGeneration = cacheValue.tracksContent ?
					(cacheValue.observedGlobalContentGeneration == globalContentGeneration ?
						cacheValue.contentGeneration :
						(cacheValue.resource ?
							DX12GetBufferContentGenerationForResource(cacheValue.resource) :
							globalContentGeneration)) :
					cacheValue.contentGeneration;
				if (cacheValue.contentGeneration == currentContentGeneration) {
					if (cacheValue.tracksContent)
						cacheValue.observedGlobalContentGeneration = globalContentGeneration;
					if (hasTrackedResourceIdentity) {
						invalidated = false;
					} else if (cacheValue.resolveGeneration == resolveGeneration) {
						invalidated = false;
					} else {
						AcquireSRWLockShared(&gResourceLock);
						invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
							cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
						ReleaseSRWLockShared(&gResourceLock);
						if (!invalidated)
							cacheValue.resolveGeneration = resolveGeneration;
					}
				}
			}
			if (!invalidated) {
				s_cacheL1[keyL1] = cacheValue;
				IaViewHashMemo::Publish(
					gpuVirtualAddress, size, stride, format, slot, cacheValue.hash,
					cacheValue.resolveGeneration, cacheValue.resourceGeneration,
					cacheValue.resource, cacheValue.publishedState);
				return cacheValue.hash;
			}
			s_cacheL2.erase(itL2);
		}
	}

	uint32_t hash = 0;
	const UINT64 contentGeneration = resolved && summary.resource ?
		DX12GetBufferContentGenerationForResource(summary.resource) :
		globalContentGeneration;

	// The IA hash is intentionally metadata-only (full-resource extent):
	// content-based hashing depends on the CPU content cache, which is empty
	// for buffers recreated through GPU-only paths (scene switches). A cache
	// miss then fell back to a different (metadata) value, so hashes recorded
	// while the cache was warm stopped matching after a scene switch - and
	// attempting to recover the content via asynchronous GPU readbacks flooded
	// the game queue with copy commands during scene-switch resource storms,
	// stalling the GPU. The metadata hash over the full resource extent is
	// deterministic (independent of cache state), so recording and matching
	// always agree.
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (resolved && summary.hasResourceDesc) {
		const D3D12_RESOURCE_DESC &desc = summary.resourceDesc;
		IaHashKeyL3Resolved keyL3 = {
			static_cast<UINT>(desc.Dimension),
			desc.Width,
			desc.Height,
			desc.DepthOrArraySize,
			desc.MipLevels,
			static_cast<UINT>(desc.Format),
			desc.SampleDesc.Count,
			desc.SampleDesc.Quality,
			static_cast<UINT>(desc.Layout),
			static_cast<UINT>(desc.Flags),
			summary.resourceHeapType,
			summary.resourceOffset,
			fullViewSize,
			stride,
			format,
			slot
		};
		std::unordered_map<
			IaHashKeyL3Resolved,
			uint32_t,
			IaHashKeyL3ResolvedHasher>::iterator itL3 =
			s_cacheL3Resolved.find(keyL3);
		if (itL3 != s_cacheL3Resolved.end()) {
			hash = itL3->second;
		} else {
			hash = HashBytes(hash, &desc.Dimension, sizeof(desc.Dimension));
			hash = HashBytes(hash, &desc.Width, sizeof(desc.Width));
			hash = HashBytes(hash, &desc.Height, sizeof(desc.Height));
			hash = HashBytes(hash, &desc.DepthOrArraySize, sizeof(desc.DepthOrArraySize));
			hash = HashBytes(hash, &desc.MipLevels, sizeof(desc.MipLevels));
			hash = HashBytes(hash, &desc.Format, sizeof(desc.Format));
			hash = HashBytes(hash, &desc.SampleDesc.Count, sizeof(desc.SampleDesc.Count));
			hash = HashBytes(hash, &desc.SampleDesc.Quality, sizeof(desc.SampleDesc.Quality));
			hash = HashBytes(hash, &desc.Layout, sizeof(desc.Layout));
			hash = HashBytes(hash, &desc.Flags, sizeof(desc.Flags));
			hash = HashBytes(hash, &summary.resourceHeapType, sizeof(summary.resourceHeapType));
			hash = HashBytes(hash, &summary.resourceOffset, sizeof(summary.resourceOffset));
			hash = HashBytes(hash, &fullViewSize, sizeof(fullViewSize));
			s_cacheL3Resolved[keyL3] = hash;
		}
	} else {
		hash = HashBytes(hash, &fullViewSize, sizeof(fullViewSize));
	}

	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));

	IaHashCacheValue cacheValue;
	cacheValue.hash = hash;
	cacheValue.resolveGeneration = resolveGeneration;
	cacheValue.contentGeneration = contentGeneration;
	cacheValue.observedGlobalContentGeneration = globalContentGeneration;
	cacheValue.gpuVa = gpuVirtualAddress;
	cacheValue.size = fullViewSize;
	cacheValue.resource = resolved ? summary.resource : nullptr;
	cacheValue.resourceGeneration =
		resolved ? summary.resourceGeneration : 0;
	cacheValue.publishedState = resolved ? summary.publishedState : nullptr;
	cacheValue.tracksContent = false;
	s_cacheL1[keyL1] = cacheValue;
	IaViewHashMemo::Publish(
		gpuVirtualAddress, size, stride, format, slot, cacheValue.hash,
		cacheValue.resolveGeneration, cacheValue.resourceGeneration,
		cacheValue.resource, cacheValue.publishedState);
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, fullViewSize,
			stride, format, slot };
		s_cacheL2[keyL2] = cacheValue;
	}
	if (s_cacheL1.size() > MaxL1Entries)
		s_cacheL1.erase(s_cacheL1.begin());
	if (s_cacheL2.size() > MaxL2Entries)
		s_cacheL2.erase(s_cacheL2.begin());
	if (s_cacheL3Resolved.size() > MaxL3ResolvedEntries)
		s_cacheL3Resolved.erase(s_cacheL3Resolved.begin());
	return hash;
}

uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize)
{
	(void)fallbackGpuVirtualAddress;
	uint32_t hash = 0;
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'D', 'V');
	hash = HashBytes(hash, &tag, sizeof(tag));
	if (descriptor) {
		hash = HashBytes(hash, descriptor->kind.data(), descriptor->kind.size());
		hash = HashBytes(hash, &descriptor->viewFormat, sizeof(descriptor->viewFormat));
		hash = HashBytes(hash, &descriptor->viewDimension, sizeof(descriptor->viewDimension));
		hash = HashBytes(hash, &descriptor->firstElement, sizeof(descriptor->firstElement));
		hash = HashBytes(hash, &descriptor->numElements, sizeof(descriptor->numElements));
		hash = HashBytes(hash, &descriptor->structureByteStride, sizeof(descriptor->structureByteStride));
		hash = HashBytes(hash, &descriptor->bufferViewOffset, sizeof(descriptor->bufferViewOffset));
		hash = HashBytes(hash, &descriptor->bufferViewBytes, sizeof(descriptor->bufferViewBytes));
		const UINT64 stableViewSize = descriptor->viewSize ?
			descriptor->viewSize : fallbackSize;
		hash = HashBytes(hash, &stableViewSize, sizeof(stableViewSize));
	} else {
		hash = HashBytes(hash, &fallbackSize, sizeof(fallbackSize));
	}
	return hash;
}
