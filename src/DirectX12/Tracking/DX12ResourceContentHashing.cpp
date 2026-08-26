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

	for (IteratorT it = current;;) {
		if (probes)
			(*probes)++;
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

	struct LocalBufferResolveCache
	{
		size_t next = 0;
		std::array<BufferResolveCacheEntry, 32> entries = {};
	};
	static thread_local LocalBufferResolveCache localCache;
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
				gBufferResolveTlsHits++;
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
				gBufferResolveGlobalHits++;
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
		// absent from the synchronous normal-Mod index.
		for (size_t i = 0; i < gResources.size(); ++i) {
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
				gBufferResolveFallbackScanHits++;
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
		gBufferResolveBackwardScanHits++;
		BufferResolveCacheEntry entry;
		entry.begin = resolvedBegin;
		entry.end = resolvedEnd;
		entry.summary = *summary;
		entry.summary.resourceOffset = 0;
		entry.summary.viewSize = entry.end - entry.begin;
		AcquireSRWLockExclusive(&gResourceLock);
		entry.rangeEpoch = DX12BufferResolveCacheUtils::CurrentInvalidationEpochLocked();
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
	else {
		gBufferResolveMisses++;
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
		uint32_t version = 0;
		bool valid = false;
	};
	std::unordered_map<size_t, Page> pages;
	UINT64 gpuVaBase = 0;
	UINT64 dataSize = 0;
	UINT64 generation = 1;
};

struct BufferRegionHashKey
{
	UINT64 offset = 0;
	UINT64 size = 0;
	UINT stride = 0;
	UINT format = 0;
	UINT slot = 0;

	bool operator==(const BufferRegionHashKey &other) const
	{
		return offset == other.offset &&
			size == other.size &&
			stride == other.stride &&
			format == other.format &&
			slot == other.slot;
	}
};

struct BufferRegionHashKeyHasher
{
	size_t operator()(const BufferRegionHashKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		auto mix = [&hash](UINT64 value) {
			hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
				(hash << 6) + (hash >> 2);
		};
		mix(key.offset);
		mix(key.size);
		mix(key.stride);
		mix(key.format);
		mix(key.slot);
		return hash;
	}
};

struct CachedBufferRegionHash
{
	uint32_t hash = 0;
	UINT64 pageStamp = 0;
};

static std::unordered_map<ID3D12Resource*, CachedBufferContent> gBufferContentByResource;
static std::unordered_map<
	ID3D12Resource*,
	std::unordered_map<BufferRegionHashKey, CachedBufferRegionHash, BufferRegionHashKeyHasher>>
	gBufferRegionHashesByResource;
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
	static constexpr size_t MaxRegionHashesPerResource = 8192;

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

	static size_t PageCount(UINT64 size)
	{
		return static_cast<size_t>((size + PageSize - 1) / PageSize);
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
			if (found->second.version == UINT32_MAX)
				found->second.version = 1;
			else
				++found->second.version;
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

	static UINT64 ComputePageStamp(
		const CachedBufferContent &entry,
		UINT64 offset,
		UINT64 size)
	{
		size_t firstPage = 0;
		size_t lastPage = 0;
		PageRange(offset, size, &firstPage, &lastPage);
		UINT64 stamp = 0x84222325a9582d75ull;
		for (size_t page = firstPage; page <= lastPage; ++page) {
			auto found = entry.pages.find(page);
			const uint32_t version = found == entry.pages.end() ? 0 : found->second.version;
			stamp ^= static_cast<UINT64>(version) +
				0x9e3779b97f4a7c15ull + (stamp << 6) + (stamp >> 2);
		}
		return stamp;
	}

	static bool TryLoadRegionHash(
		const CachedBufferContent &entry,
		const std::unordered_map<
			BufferRegionHashKey,
			CachedBufferRegionHash,
			BufferRegionHashKeyHasher> &regionHashes,
		UINT64 offset,
		UINT64 size,
		UINT stride,
		UINT format,
		UINT slot,
		uint32_t *hash)
	{
		if (!hash || !ArePagesValid(entry, offset, size))
			return false;
		const BufferRegionHashKey key = {offset, size, stride, format, slot};
		auto found = regionHashes.find(key);
		if (found == regionHashes.end())
			return false;
		const UINT64 currentStamp = ComputePageStamp(entry, offset, size);
		if (found->second.pageStamp != currentStamp)
			return false;
		*hash = found->second.hash;
		return *hash != 0;
	}

	static void StoreRegionHash(
		const CachedBufferContent &entry,
		std::unordered_map<
			BufferRegionHashKey,
			CachedBufferRegionHash,
			BufferRegionHashKeyHasher> *regionHashes,
		UINT64 offset,
		UINT64 size,
		UINT stride,
		UINT format,
		UINT slot,
		uint32_t hash)
	{
		if (!regionHashes || !hash || !ArePagesValid(entry, offset, size))
			return;
		if (regionHashes->size() >= MaxRegionHashesPerResource)
			regionHashes->clear();
		const BufferRegionHashKey key = {offset, size, stride, format, slot};
		CachedBufferRegionHash value;
		value.hash = hash;
		value.pageStamp = ComputePageStamp(entry, offset, size);
		(*regionHashes)[key] = value;
	}

	static uint32_t ComputeIaContentHash(
		const uint8_t *data,
		size_t dataSize,
		UINT stride,
		UINT format,
		UINT slot)
	{
		if (!data || !dataSize)
			return 0;
		uint32_t hash = 0;
		const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'C');
		hash = HashBytes(hash, &tag, sizeof(tag));
		hash = crc32c_append(hash, data, dataSize);
		hash = HashBytes(hash, &stride, sizeof(stride));
		hash = HashBytes(hash, &format, sizeof(format));
		hash = HashBytes(hash, &slot, sizeof(slot));
		return hash;
	}
};

bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize)
{
	return DX12CacheBufferContentRange(resource, 0, data, dataSize);
}

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

	struct ChangedPage
	{
		size_t index = 0;
		UINT64 copyBegin = 0;
		UINT64 copyEnd = 0;
		bool wasValid = false;
		std::array<uint8_t, DX12BufferContentCacheUtils::PageSize> data = {};
	};
	std::vector<ChangedPage> changed;
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
			ChangedPage update;
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
			gBufferRegionHashesByResource[resource].clear();
		}
		DX12BufferContentCacheUtils::EnsureEntryStorage(&entry, requiredSize);
		for (const ChangedPage &update : changed) {
			CachedBufferContent::Page &page = entry.pages[update.index];
			page.data = update.data;
			if (page.version == UINT32_MAX)
				page.version = 1;
			else
				++page.version;
			const UINT64 pageBegin = static_cast<UINT64>(update.index) *
				DX12BufferContentCacheUtils::PageSize;
			const UINT64 pageEnd = (std::min)(pageBegin +
				DX12BufferContentCacheUtils::PageSize, requiredSize);
			page.valid = update.wasValid ||
				(update.copyBegin == pageBegin && update.copyEnd == pageEnd);
		}
		changedPages = changed.size();
		changedBytes = 0;
		for (const ChangedPage &update : changed)
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
	gBufferRegionHashesByResource.erase(resource);
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


// -- Readback-on-miss for buffer content hashing ----------------------------
//
// Scene switches recreate mesh buffers through GPU-only paths (streaming,
// pool copies) that never touch the CPU content cache. IA content hashing
// then falls back to metadata and mod replacements (keyed on content hashes
// like 1be79644) silently stop matching after switching scenes back. Request
// an asynchronous GPU readback so the content cache recovers; the request is
// throttled to at most one per resource per 120 presents so a permanently
// uncapturable buffer cannot enqueue a readback every frame.
static SRWLOCK gBufferReadbackRequestLock = SRWLOCK_INIT;
static std::unordered_map<ID3D12Resource*, LONG> gBufferReadbackRequestPresent;

void DX12RequestBufferContentReadback(
	ID3D12Resource *resource, UINT64 offset, UINT64 size)
{
	if (!resource || !size)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	AcquireSRWLockExclusive(&gBufferReadbackRequestLock);
	auto it = gBufferReadbackRequestPresent.find(resource);
	const bool allow = it == gBufferReadbackRequestPresent.end() ||
		present - it->second >= 120;
	if (allow)
		gBufferReadbackRequestPresent[resource] = present;
	ReleaseSRWLockExclusive(&gBufferReadbackRequestLock);
	if (!allow)
		return;
	if (DX12AsyncReadbackQueueBufferCapture(resource, offset, size)) {
		DX12LogManager::Fields("DX12IaHashAudit",
			"stage=readback_request,resource=%p,offset=%llu,bytes=%llu",
			resource,
			static_cast<unsigned long long>(offset),
			static_cast<unsigned long long>(size));
	}
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

static bool DX12TryHashResolvedBufferContentRegion(
	const DX12BufferResourceSummary &summary,
	UINT64 size,
	UINT stride,
	UINT format,
	UINT slot,
	uint32_t *hash)
{
	if (!hash || !summary.resource || !size)
		return false;
	*hash = 0;

	AcquireSRWLockShared(&gBufferContentLock);
	auto contentIt = gBufferContentByResource.find(summary.resource);
	if (contentIt == gBufferContentByResource.end()) {
		ReleaseSRWLockShared(&gBufferContentLock);
		// Scene switches recreate buffers through GPU-only paths (no Map /
		// no tracked copy), leaving the CPU content cache empty. Without the
		// content the IA hash falls back to metadata, which never matches the
		// content hashes recorded in mod configs - the draw then silently
		// loses its texcoord/IB replacement after switching scenes back.
		// Request an asynchronous GPU readback so the content cache (and the
		// content-based IA hash) recovers one frame later.
		DX12RequestBufferContentReadback(
			summary.resource, summary.resourceOffset, size);
		return false;
	}
	const CachedBufferContent &content = contentIt->second;
	auto regionIt = gBufferRegionHashesByResource.find(summary.resource);
	if (regionIt != gBufferRegionHashesByResource.end() &&
	    DX12BufferContentCacheUtils::TryLoadRegionHash(
			content,
			regionIt->second,
			summary.resourceOffset,
			size,
			stride,
			format,
			slot,
			hash)) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return *hash != 0;
	}
	if (!DX12BufferContentCacheUtils::ArePagesValid(
			content,
			summary.resourceOffset,
			size)) {
		ReleaseSRWLockShared(&gBufferContentLock);
		// Content exists but the requested pages are invalid (GPU writes
		// since capture, or a partial capture). Re-request a readback.
		DX12RequestBufferContentReadback(
			summary.resource, summary.resourceOffset, size);
		return false;
	}
	const UINT64 capturedPageStamp =
		DX12BufferContentCacheUtils::ComputePageStamp(
			content, summary.resourceOffset, size);
	// Copy only the requested IA region while holding the shared lock. CRC work
	// is deliberately performed after releasing it so writers are not blocked
	// for the duration of a potentially large hash calculation.
	std::vector<uint8_t> region(static_cast<size_t>(size));
	const bool copied = DX12BufferContentCacheUtils::ReadRange(
		content, summary.resourceOffset, size, region.data());
	ReleaseSRWLockShared(&gBufferContentLock);
	if (!copied)
		return false;
	*hash = DX12BufferContentCacheUtils::ComputeIaContentHash(
		region.data(), region.size(), stride, format, slot);
	if (!*hash)
		return false;

	bool storedForSnapshot = false;
	AcquireSRWLockExclusive(&gBufferContentLock);
	auto storeContentIt = gBufferContentByResource.find(summary.resource);
	if (storeContentIt != gBufferContentByResource.end() &&
	    DX12BufferContentCacheUtils::ArePagesValid(
		    storeContentIt->second, summary.resourceOffset, size) &&
	    DX12BufferContentCacheUtils::ComputePageStamp(
		    storeContentIt->second, summary.resourceOffset, size) ==
		    capturedPageStamp) {
		DX12BufferContentCacheUtils::StoreRegionHash(
			storeContentIt->second,
			&gBufferRegionHashesByResource[summary.resource],
			summary.resourceOffset,
			size,
			stride,
			format,
			slot,
			*hash);
		storedForSnapshot = true;
	}
	ReleaseSRWLockExclusive(&gBufferContentLock);
	if (!storedForSnapshot)
		*hash = 0;
	return storedForSnapshot;
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
	AcquireSRWLockExclusive(&gTextureHashLock);
	const size_t erased = gTextureHashByResource.erase(resource);
	ReleaseSRWLockExclusive(&gTextureHashLock);
	if (erased) {
		gTextureHashGeneration.fetch_add(1, std::memory_order_relaxed);
	}
}

static bool DX12CopyRegionCoversTopMip(
	const D3D12_RESOURCE_DESC &desc,
	UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
	const D3D12_BOX *srcBox)
{
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		return false;
	if (desc.Width > UINT_MAX || desc.Height == 0)
		return false;
	if (dstX != 0 || dstY != 0 || dstZ != 0)
		return false;
	if (footprint.Footprint.Width < desc.Width ||
		footprint.Footprint.Height < desc.Height)
		return false;
	if (!srcBox)
		return true;
	if (srcBox->left != 0 || srcBox->top != 0 || srcBox->front != 0)
		return false;
	if (srcBox->right < desc.Width || srcBox->bottom < desc.Height || srcBox->back < 1)
		return false;
	return true;
}

bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox)
{
	if (!dst || !src || !dst->pResource || !src->pResource)
		return false;
	if (dst->Type != D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX ||
		dst->SubresourceIndex != 0 ||
		src->Type != D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
		return false;

	const D3D12_RESOURCE_DESC desc = dst->pResource->GetDesc();
	const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint = src->PlacedFootprint;
	if (!DX12CopyRegionCoversTopMip(desc, dstX, dstY, dstZ, footprint, srcBox))
		return false;

	// Async texture hashing: queue a GPU readback of the texture's top mip
	// instead of hashing the upload source synchronously on the recording
	// thread. Reading the texture (rather than the upload buffer) is exact
	// even when the game reuses one upload arena for several textures in the
	// same frame: the readback executes after the game's submit and observes
	// the post-copy bytes of this texture. The worker publishes the hash one
	// frame later; static textures (the normal override target) hash
	// identically, so override matching is unaffected.
	return DX12AsyncReadbackQueueTextureCapture(dst->pResource, &desc);
}
UINT64 DX12GetIaHashContentGeneration()
{
	return gBufferContentGeneration.load(std::memory_order_relaxed);
}

UINT64 DX12GetIaHashResolveGeneration()
{
	return gBufferResolveInvalidationEpochAtomic.load(std::memory_order_acquire);
}


uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	// TLS-throttled audit: one line per (va,size,stride,format,slot) per
	// stage/hash change. Makes scene-switch hash degradation visible
	// (content -> content_miss -> metadata) without logging every bind.
	struct IaHashAuditEntry
	{
		UINT64 key = 0;
		const char *stage = nullptr;
		uint32_t hash = 0;
		bool valid = false;
	};
	static thread_local IaHashAuditEntry tlsIaHashAudit[64];
	auto iaHashAudit = [&](const char *stage, uint32_t hashValue) {
		const UINT64 auditKey = gpuVirtualAddress ^
			(static_cast<UINT64>(size) << 8) ^
			(static_cast<UINT64>(stride) << 20) ^
			(static_cast<UINT64>(format) << 28) ^
			(static_cast<UINT64>(slot) << 36);
		IaHashAuditEntry &entry = tlsIaHashAudit[auditKey & 63];
		if (entry.valid && entry.key == auditKey && entry.stage == stage &&
		    entry.hash == hashValue)
			return;
		entry.valid = true;
		entry.key = auditKey;
		entry.stage = stage;
		entry.hash = hashValue;
		DX12LogManager::Fields("DX12IaHashAudit",
			"stage=%s,va=0x%llx,size=%u,stride=%u,format=%u,slot=%u,hash=%08x",
			stage,
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<UINT>(size),
			stride, format, slot, hashValue);
	};

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
			iaHashAudit("cache", cacheValue.hash);
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
				iaHashAudit("cache", cacheValue.hash);
				return cacheValue.hash;
			}
			s_cacheL2.erase(itL2);
		}
	}

	uint32_t hash = 0;
	const UINT64 contentGeneration = resolved && summary.resource ?
		DX12GetBufferContentGenerationForResource(summary.resource) :
		globalContentGeneration;

	if (resolved && summary.resource &&
	    DX12TryHashResolvedBufferContentRegion(
			summary,
			fullViewSize,
			stride,
			format,
			slot,
			&hash)) {
		IaHashCacheValue contentCacheValue;
		contentCacheValue.hash = hash;
		contentCacheValue.resolveGeneration = resolveGeneration;
		contentCacheValue.contentGeneration = contentGeneration;
		contentCacheValue.observedGlobalContentGeneration = globalContentGeneration;
		contentCacheValue.gpuVa = gpuVirtualAddress;
		contentCacheValue.size = fullViewSize;
		contentCacheValue.resource = summary.resource;
		contentCacheValue.resourceGeneration = summary.resourceGeneration;
		contentCacheValue.publishedState = summary.publishedState;
		contentCacheValue.tracksContent = true;
		s_cacheL1[keyL1] = contentCacheValue;
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, fullViewSize,
			stride, format, slot };
		s_cacheL2[keyL2] = contentCacheValue;
		if (s_cacheL1.size() > MaxL1Entries)
			s_cacheL1.erase(s_cacheL1.begin());
		if (s_cacheL2.size() > MaxL2Entries)
			s_cacheL2.erase(s_cacheL2.begin());
		if (s_cacheL3Resolved.size() > MaxL3ResolvedEntries)
			s_cacheL3Resolved.erase(s_cacheL3Resolved.begin());
		iaHashAudit("content", hash);
		return hash;
	}

	// Tier 1 uses content-based CRC32. This path is now reserved for exact-hash
	// IA candidates; the default steady-state IA path uses DX12HashIaBufferView.
	const uint8_t *contentData = nullptr;
	size_t contentSize = 0;
	if (DX12TryGetBufferContent(gpuVirtualAddress, fullViewSize, &contentData, &contentSize))
	{
		const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'C');
		hash = HashBytes(hash, &tag, sizeof(tag));
		hash = crc32c_append(hash, contentData, contentSize);
		hash = HashBytes(hash, &stride, sizeof(stride));
		hash = HashBytes(hash, &format, sizeof(format));
		hash = HashBytes(hash, &slot, sizeof(slot));
		IaHashCacheValue contentCacheValue;
		contentCacheValue.hash = hash;
		contentCacheValue.resolveGeneration = resolveGeneration;
		contentCacheValue.contentGeneration = contentGeneration;
		contentCacheValue.observedGlobalContentGeneration = globalContentGeneration;
		contentCacheValue.gpuVa = gpuVirtualAddress;
		contentCacheValue.size = fullViewSize;
		contentCacheValue.resource = resolved ? summary.resource : nullptr;
		contentCacheValue.resourceGeneration =
			resolved ? summary.resourceGeneration : 0;
		contentCacheValue.publishedState =
			resolved ? summary.publishedState : nullptr;
		contentCacheValue.tracksContent = true;
		s_cacheL1[keyL1] = contentCacheValue;
		if (resolved && summary.resource) {
			IaHashKeyL2 keyL2 = { summary.resource,
				summary.resourceOffset, fullViewSize,
				stride, format, slot };
			s_cacheL2[keyL2] = contentCacheValue;
		}
		if (s_cacheL1.size() > MaxL1Entries)
			s_cacheL1.erase(s_cacheL1.begin());
		if (s_cacheL2.size() > MaxL2Entries)
			s_cacheL2.erase(s_cacheL2.begin());
		if (s_cacheL3Resolved.size() > MaxL3ResolvedEntries)
			s_cacheL3Resolved.erase(s_cacheL3Resolved.begin());
		iaHashAudit("content", hash);
		return hash;
	}
	iaHashAudit("content_miss", 0);

	// Tiers 2 and 3 use metadata-based hashing.
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
	iaHashAudit("metadata", hash);
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
