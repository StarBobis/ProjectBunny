#include "DX12ResourceTrackerPrivate.hpp"

#include "../utils/DX12DescriptorHeapShadowUtils.hpp"
#include "../utils/DX12TextureHashUtils.hpp"
#include "DX12ResourceCollectQueue.hpp"

#include <unordered_set>

bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary)
{
	if (!rootSignature || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<ID3D12RootSignature*, size_t>::iterator found =
		gRootSignatureByPtr.find(rootSignature);
	if (found == gRootSignatureByPtr.end() ||
		found->second >= gRootSignatures.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const RootSignatureRecord &record = gRootSignatures[found->second];
	summary->rootSignature = record.rootSignature;
	summary->hash = record.hash;
	summary->size = record.size;
	summary->nodeMask = record.nodeMask;
	summary->version = record.version;
	summary->flags = record.flags;
	summary->staticSamplerCount = record.staticSamplerCount;
	summary->parsed = record.parsed;
	summary->parameters = record.parameters;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}

bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature)
{
	if (!psoIndex || !rootSignature)
		return false;

	*rootSignature = nullptr;
	AcquireSRWLockShared(&gDescriptorLock);
	std::unordered_map<UINT64, size_t>::iterator found =
		gPsoRootByIndex.find(psoIndex);
	if (found == gPsoRootByIndex.end() ||
		found->second >= gPsoRoots.size() ||
		!gPsoRoots[found->second].rootSignature) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	*rootSignature = gPsoRoots[found->second].rootSignature;
	ReleaseSRWLockShared(&gDescriptorLock);
	return true;
}

class DX12BufferResolveRangeUtils
{
public:
static void ApplyLatestResourceStateLocked(DX12BufferResourceSummary *summary)
{
	if (!summary || !summary->resource)
		return;

	std::unordered_map<ID3D12Resource*, size_t>::iterator found = gResourceByPtr.find(summary->resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size())
		return;

	const ResourceRecord &resource = gResources[found->second];
	if (!resource.resource)
		return;

	summary->currentState = static_cast<UINT>(resource.currentState);
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
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
	DX12BufferResolveRangeUtils::ApplyLatestResourceStateLocked(summary);
	return true;
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

	struct LocalBufferResolveCache
	{
		size_t next = 0;
		BufferResolveCacheEntry entries[256] = {};
	};
	static thread_local LocalBufferResolveCache localCache;
	for (const BufferResolveCacheEntry &entry : localCache.entries) {
		if (entry.begin && gpuVirtualAddress >= entry.begin && gpuVirtualAddress < entry.end) {
			AcquireSRWLockShared(&gResourceLock);
			const bool entryInvalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
				entry.begin, entry.end, entry.rangeEpoch);
			const bool cacheResolved = !entryInvalidated &&
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, entry, summary);
			ReleaseSRWLockShared(&gResourceLock);
			if (cacheResolved) {
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
			const bool entryInvalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
				cached->second.begin, cached->second.end, cached->second.rangeEpoch);
			const bool cacheResolved = !entryInvalidated &&
				DX12BufferResolveRangeUtils::FillSummaryFromCacheEntry(
					gpuVirtualAddress, size, cached->second, summary);
			if (cacheResolved) {
				gBufferResolveGlobalHits++;
				localCache.entries[localCache.next++ % 8] = cached->second;
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
		// Fallback: first try gBufferResourceByGpuVaComplete 鈥?a secondary index
		// that mirrors all buffer resources with valid GPU VA that were missed by
		// the primary gBufferResourceByGpuVa index (e.g. reserved resources with
		// delayed VA assignment, or resources created before hook activation).
		// This replaces the O(n) linear scan with O(log n) map lookup.
		AcquireSRWLockShared(&gResourceLock);
		std::map<UINT64, size_t>::iterator completeIt =
			gBufferResourceByGpuVaComplete.upper_bound(gpuVirtualAddress);
		if (completeIt != gBufferResourceByGpuVaComplete.begin()) {
			--completeIt;
			const ResourceRecord &completeResource = gResources[completeIt->second];
			if (DX12BufferResolveRangeUtils::TryResolveSummaryFromResourceRecord(
					gpuVirtualAddress, size, completeResource, summary,
					&resolvedBegin, &resolvedEnd)) {
				resolved = true;
				gBufferResolveFallbackScanHits++;
			}
		}
		// Safety net: full linear scan for truly unindexed resources.
		if (!resolved) {
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
		localCache.entries[localCache.next++ % 8] = entry;
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

// 鈹€鈹€ Buffer Content Cache 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Like DX11: cache the actual buffer bytes so IA hashes are based on
// CONTENT (CRC32 of data), not GPU virtual address.
// Keyed by GPU VA base of the resource so sub-regions resolve correctly.

struct CachedBufferContent
{
	std::vector<uint8_t> data;
	UINT64 gpuVaBase = 0;
	UINT64 dataSize = 0;
};

static std::unordered_map<ID3D12Resource*, CachedBufferContent> gBufferContentByResource;
static SRWLOCK gBufferContentLock = SRWLOCK_INIT;
static std::atomic<UINT64> gBufferContentGeneration{1};

bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize)
{
	return DX12CacheBufferContentRange(resource, 0, data, dataSize);
}

bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize)
{
	if (!resource || !data || !dataSize)
		return false;

	CachedBufferContent entry;
	entry.data.assign(static_cast<const uint8_t*>(data),
		static_cast<const uint8_t*>(data) + dataSize);
	entry.dataSize = dataSize;

	// Get the GPU VA base directly (available immediately after creation,
	// unlike GPU VA resolution which may not be ready if tracking is async).
	entry.gpuVaBase = resource->GetGPUVirtualAddress() + resourceOffset;

	AcquireSRWLockExclusive(&gBufferContentLock);
	gBufferContentByResource[resource] = std::move(entry);
	ReleaseSRWLockExclusive(&gBufferContentLock);
	gBufferContentGeneration.fetch_add(1, std::memory_order_relaxed);

#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12BufferContentCache",
		"stage=stored,resource=0x%p,offset=%llu,bytes=%zu,gpuVaBase=0x%llx",
		resource,
		static_cast<unsigned long long>(resourceOffset),
		dataSize,
		static_cast<unsigned long long>(entry.gpuVaBase));
#endif
	return true;
}
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size,
	const uint8_t **outData, size_t *outDataSize)
{
	if (!outData || !outDataSize || !gpuVirtualAddress || !size)
		return false;
	*outData = nullptr;
	*outDataSize = 0;

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
	if (offset + size > cached.dataSize) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	*outData = cached.data.data() + offset;
	*outDataSize = static_cast<size_t>(size);
	ReleaseSRWLockShared(&gBufferContentLock);
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

	DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
		"\"stage\":\"stored\",\"source\":\"%s\",\"resource\":\"%p\",\"hash\":\"%08x\",\"generation\":%llu,\"width\":%llu,\"height\":%u,\"format\":%u",
		source ? source : "",
		resource,
		hash,
		static_cast<unsigned long long>(entry.generation),
		desc ? static_cast<unsigned long long>(desc->Width) : 0ull,
		desc ? desc->Height : 0u,
		desc ? static_cast<UINT>(desc->Format) : 0u);
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

void DX12InvalidateTextureHash(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gTextureHashLock);
	const size_t erased = gTextureHashByResource.erase(resource);
	ReleaseSRWLockExclusive(&gTextureHashLock);
	if (erased) {
		DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
			"\"stage\":\"invalidated\",\"resource\":\"%p\",\"erased\":%zu",
			resource,
			erased);
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

	const UINT rowCount = DX12TextureHashUtils::Texture2DTopMipRowCount(
		desc.Format, desc.Height);
	const UINT64 rowBytes = DX12TextureHashUtils::Texture2DTopMipRowBytes(
		desc.Format, static_cast<UINT>(desc.Width));
	if (!rowCount || !rowBytes || footprint.Footprint.RowPitch < rowBytes)
		return false;

	const UINT64 requiredBytes =
		static_cast<UINT64>(footprint.Footprint.RowPitch) * (rowCount - 1) + rowBytes;
	const UINT64 sourceGpuVa = src->pResource->GetGPUVirtualAddress() + footprint.Offset;
	const uint8_t *content = nullptr;
	size_t contentSize = 0;
	if (!DX12TryGetBufferContent(sourceGpuVa, requiredBytes, &content, &contentSize)) {
		DX12LogManager::DebugJsonFunc("DX12TextureHashCache",
			"\"stage\":\"copy_upload_miss\",\"dst\":\"%p\",\"src\":\"%p\",\"gpu\":\"0x%llx\",\"bytes\":%llu,\"width\":%llu,\"height\":%u,\"format\":%u",
			dst->pResource,
			src->pResource,
			static_cast<unsigned long long>(sourceGpuVa),
			static_cast<unsigned long long>(requiredBytes),
			static_cast<unsigned long long>(desc.Width),
			desc.Height,
			static_cast<UINT>(desc.Format));
		return false;
	}

	uint32_t hash = 0;
	if (!DX12TextureHashUtils::Texture2DTopMipHashFromRows(
			desc,
			content,
			footprint.Footprint.RowPitch,
			rowBytes,
			rowCount,
			&hash)) {
		return false;
	}
	return DX12CacheTextureHash(dst->pResource, hash, &desc, "copy_texture_region");
}
UINT64 DX12GetIaHashContentGeneration()
{
	return gBufferContentGeneration.load(std::memory_order_relaxed);
}

UINT64 DX12GetIaHashResolveGeneration()
{
	return gBufferResolveInvalidationEpochAtomic.load(std::memory_order_relaxed);
}

uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	// 鈹€鈹€ DX11 瀵归綈: 鍙岀骇 TLS 缂撳瓨 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	// L1: gpuVa-based key 鈥?蹇€熸崟鎹夋椂闂村眬閮ㄦ€э紙鍚屼竴 gpuVa 閲嶅鍑虹幇锛?
	// L2: resource-based key 鈥?绋冲畾鎹曟崏 buffer 绾у鐢紙鍚屼竴 buffer 涓嶅悓 offset锛?
	//     瀵归綈 DX11 鐨?(ID3D11Buffer*, offset, size) 妯″紡銆?
	// Resource-generation invalidation:
	// - GPUVA index/resource lifetime changes bump gBufferResolveGeneration.
	// - CPU-upload content changes bump gBufferContentGeneration.
	// This keeps IA region hashes reusable across frames without keeping stale
	// content-derived hashes after upload buffers are rewritten.
	struct IaHashCacheValue {
		uint32_t hash = 0;
		UINT64 resolveGeneration = 0;
		UINT64 contentGeneration = 0;
		UINT64 gpuVa = 0;
		UINT64 size = 0;
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

	static thread_local std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher> s_cacheL1;
	static thread_local std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher> s_cacheL2;
	const UINT64 resolveGeneration = gBufferResolveInvalidationEpochAtomic.load(std::memory_order_relaxed);
	const UINT64 contentGeneration = gBufferContentGeneration.load(std::memory_order_relaxed);

	// -- L1 lookup (gpuVa-based, fastest) --
	IaHashKeyL1 keyL1 = { gpuVirtualAddress, size, stride, format, slot };
	std::unordered_map<IaHashKeyL1, IaHashCacheValue, IaHashKeyL1Hasher>::iterator itL1 = s_cacheL1.find(keyL1);
	if (itL1 != s_cacheL1.end()) {
		const IaHashCacheValue &cacheValue = itL1->second;
		bool invalidated = true;
		if (cacheValue.contentGeneration == contentGeneration) {
			if (cacheValue.resolveGeneration == resolveGeneration) {
				invalidated = false;
			} else {
				AcquireSRWLockShared(&gResourceLock);
				invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
					cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
				ReleaseSRWLockShared(&gResourceLock);
			}
		}
		if (!invalidated)
			return cacheValue.hash;
		s_cacheL1.erase(itL1);
	}

	// -- L2 lookup (resource-based, stable across draws) --
	// 瀵归綈 DX11: 鐢?(resource, offset, viewSize) 鏇夸唬 gpuVa
	DX12BufferResourceSummary summary = {};
	const bool resolved = DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary);
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, summary.viewSize,
			stride, format, slot };
		std::unordered_map<IaHashKeyL2, IaHashCacheValue, IaHashKeyL2Hasher>::iterator itL2 = s_cacheL2.find(keyL2);
		if (itL2 != s_cacheL2.end()) {
			// L2 鍛戒腑 鈫?鎻愬崌鍒?L1锛堜笅娆＄浉鍚?gpuVa 鐩存帴鍛戒腑锛?
			const IaHashCacheValue &cacheValue = itL2->second;
			bool invalidated = true;
			if (cacheValue.contentGeneration == contentGeneration) {
				if (cacheValue.resolveGeneration == resolveGeneration) {
					invalidated = false;
				} else {
					AcquireSRWLockShared(&gResourceLock);
					invalidated = DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked(
						cacheValue.gpuVa, cacheValue.gpuVa + cacheValue.size, cacheValue.resolveGeneration);
					ReleaseSRWLockShared(&gResourceLock);
				}
			}
			if (!invalidated) {
				s_cacheL1[keyL1] = cacheValue;
				return cacheValue.hash;
			}
			s_cacheL2.erase(itL2);
		}
	}

	// -- 涓ょ骇缂撳瓨閮?miss 鈫?璁＄畻 hash --
	uint32_t hash = 0;

	// 鈹€鈹€ Tier 1: Content-based CRC32 (matches DX11 behavior) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	const uint8_t *contentData = nullptr;
	size_t contentSize = 0;
	if (DX12TryGetBufferContent(gpuVirtualAddress, size, &contentData, &contentSize))
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
		contentCacheValue.gpuVa = gpuVirtualAddress;
		contentCacheValue.size = size;
		s_cacheL1[keyL1] = contentCacheValue;
		if (resolved && summary.resource) {
			IaHashKeyL2 keyL2 = { summary.resource,
				summary.resourceOffset, summary.viewSize,
				stride, format, slot };
			s_cacheL2[keyL2] = contentCacheValue;
		}
		if (s_cacheL1.size() > 8192 || s_cacheL2.size() > 8192) {
			s_cacheL1.clear();
			s_cacheL2.clear();
		}
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=content_crc32,gpuVa=0x%llx,size=%llu,"
			"stride=%u,format=%u,slot=%u,hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
		return hash;
	}

	// 鈹€鈹€ Tier 2/3: Metadata-based hash 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (resolved && summary.hasResourceDesc) {
		const D3D12_RESOURCE_DESC &desc = summary.resourceDesc;
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
		hash = HashBytes(hash, &summary.viewSize, sizeof(summary.viewSize));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=resolved,gpuVa=0x%llx,size=%llu,"
			"offset=%llu,viewSize=%llu,stride=%u,format=%u,"
			"slot=%u,hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			static_cast<unsigned long long>(summary.resourceOffset),
			static_cast<unsigned long long>(summary.viewSize),
			stride, format, slot, hash);
#endif
	} else {
		hash = HashBytes(hash, &size, sizeof(size));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"stage=fallback_no_gpuva,gpuVa=0x%llx,"
			"size=%llu,stride=%u,format=%u,slot=%u,"
			"hash=%08x",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
	}

	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));

	// 鍐欏叆鍙岀骇缂撳瓨
	IaHashCacheValue cacheValue;
	cacheValue.hash = hash;
	cacheValue.resolveGeneration = resolveGeneration;
	cacheValue.contentGeneration = contentGeneration;
	cacheValue.gpuVa = gpuVirtualAddress;
	cacheValue.size = size;
	s_cacheL1[keyL1] = cacheValue;
	if (resolved && summary.resource) {
		IaHashKeyL2 keyL2 = { summary.resource,
			summary.resourceOffset, summary.viewSize,
			stride, format, slot };
		s_cacheL2[keyL2] = cacheValue;
	}
	if (s_cacheL1.size() > 8192 || s_cacheL2.size() > 8192) {
		s_cacheL1.clear();
		s_cacheL2.clear();
	}
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

static void CompleteDescriptorSummaryResourceInfo(
	DX12DescriptorSummary *summary, bool resourceLockHeld)
{
	if (!summary || !summary->resource)
		return;

	if (!resourceLockHeld)
		AcquireSRWLockShared(&gResourceLock);
	std::unordered_map<ID3D12Resource*, size_t>::iterator found =
		gResourceByPtr.find(summary->resource);
	if (found == gResourceByPtr.end() || found->second >= gResources.size()) {
		if (!resourceLockHeld)
			ReleaseSRWLockShared(&gResourceLock);
		return;
	}
	const ResourceRecord &resource = gResources[found->second];
	if (!summary->hasResourceDesc) {
		summary->resourceDesc = resource.desc;
		summary->hasResourceDesc = true;
		if (summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
			summary->gpuVirtualAddress == 0)
			summary->gpuVirtualAddress = resource.gpuVirtualAddress;
	}
	if (!summary->hasResourceHeapType && resource.hasHeapType) {
		summary->resourceHeapType = static_cast<UINT>(resource.heapType);
		summary->hasResourceHeapType = true;
	}
	if (!summary->hasCurrentState) {
		summary->currentState = static_cast<UINT>(resource.currentState);
		summary->hasCurrentState = resource.hasCurrentState;
		summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	} else if (!summary->stateTrackedByBarrier && resource.stateTrackedByBarrier) {
		summary->currentState = static_cast<UINT>(resource.currentState);
		summary->hasCurrentState = resource.hasCurrentState;
		summary->stateTrackedByBarrier = true;
	}
	if (!resourceLockHeld)
		ReleaseSRWLockShared(&gResourceLock);
}

static bool CompleteDescriptorSummaryBufferGpuVaInfo(
	DX12DescriptorSummary *summary, UINT64 gpuVirtualAddress, UINT64 size,
	bool resourceLockHeld)
{
	if (!summary || summary->resource || gpuVirtualAddress == 0 || size == 0)
		return false;

	DX12BufferResourceSummary resource;
	const bool resolved = resourceLockHeld ?
		FillBufferSummaryFromRecordLocked(gpuVirtualAddress, size, &resource) :
		DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &resource);
	if (!resolved || !resource.resource || !resource.hasResourceDesc) {
		// CBV sizes are often aligned or stale relative to the currently tracked
		// resource extent. Resolve by address first, then clamp the view to the
		// known buffer range so hunting metadata can still dump the bytes that
		// actually exist.
		const bool addressResolved = resourceLockHeld ?
			FillBufferSummaryFromRecordLocked(gpuVirtualAddress, 1, &resource) :
			DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, 1, &resource);
		if (!addressResolved || !resource.resource || !resource.hasResourceDesc)
			return false;
		if (resource.resourceDesc.Width > resource.resourceOffset) {
			const UINT64 available = resource.resourceDesc.Width - resource.resourceOffset;
			resource.viewSize = size < available ? size : available;
		}
	}

	summary->resource = resource.resource;
	summary->resourceDesc = resource.resourceDesc;
	summary->hasResourceDesc = resource.hasResourceDesc;
	summary->resourceHeapType = resource.resourceHeapType;
	summary->hasResourceHeapType = resource.hasResourceHeapType;
	summary->gpuVirtualAddress = gpuVirtualAddress;
	summary->resourceOffset = resource.resourceOffset;
	summary->viewSize = resource.viewSize ? resource.viewSize : size;
	summary->currentState = resource.currentState;
	summary->hasCurrentState = resource.hasCurrentState;
	summary->stateTrackedByBarrier = resource.stateTrackedByBarrier;
	return true;
}

static void FillDescriptorSummary(
	DX12DescriptorSummary *summary, const DescriptorRecord &record,
	bool completeResourceInfo = true, bool resourceLockHeld = false,
	bool completeBufferGpuVaInfo = true)
{
	if (!summary)
		return;

	summary->kind = record.kind;
	summary->cpuHandle = record.cpuHandle;
	summary->resource = record.resource;
	summary->counterResource = record.counterResource;
	summary->resourceDesc = record.resourceDesc;
	summary->hasResourceDesc = record.hasResourceDesc;
	summary->resourceHeapType = record.resourceHeapType;
	summary->hasResourceHeapType = record.hasResourceHeapType;
	summary->gpuVirtualAddress = record.gpuVirtualAddress;
	summary->resourceOffset = record.resourceOffset;
	summary->viewSize = record.viewSize;
	summary->currentState = static_cast<UINT>(record.currentState);
	summary->hasCurrentState = record.hasCurrentState;
	summary->stateTrackedByBarrier = record.stateTrackedByBarrier;
	summary->hasDesc = record.hasDesc;
	if (completeResourceInfo)
		CompleteDescriptorSummaryResourceInfo(summary, resourceLockHeld);

	if (record.kindId == DescriptorKind::CBV_ID) {
		summary->cbvSize = record.cbv.SizeInBytes;
		summary->bufferViewOffset = summary->resourceOffset;
		summary->bufferViewBytes = summary->viewSize ? summary->viewSize : record.cbv.SizeInBytes;
		const UINT64 cbvGpuVa = record.cbv.BufferLocation ?
			record.cbv.BufferLocation : record.gpuVirtualAddress;
		const UINT64 cbvBytes = record.cbv.SizeInBytes ?
			record.cbv.SizeInBytes : record.viewSize;
		if (completeBufferGpuVaInfo && !summary->resource && cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(
				summary, cbvGpuVa, cbvBytes, resourceLockHeld);
			summary->bufferViewOffset = summary->resourceOffset;
			summary->bufferViewBytes = summary->viewSize;
		}
	} else if (record.kindId == DescriptorKind::SRV_ID && record.hasDesc) {
		summary->srvDesc = record.srv;
		summary->hasSrvDesc = true;
		summary->viewFormat = static_cast<UINT>(record.srv.Format);
		summary->viewDimension = static_cast<UINT>(record.srv.ViewDimension);
		if (record.srv.ViewDimension == D3D12_SRV_DIMENSION_BUFFER) {
			summary->firstElement = record.srv.Buffer.FirstElement;
			summary->numElements = record.srv.Buffer.NumElements;
			summary->structureByteStride = record.srv.Buffer.StructureByteStride;
			summary->bufferViewOffset = record.resourceOffset;
			summary->bufferViewBytes = record.viewSize;
			if (summary->bufferViewBytes == 0 &&
					summary->hasResourceDesc &&
					summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					summary->resourceDesc.Width > summary->bufferViewOffset) {
				summary->bufferViewBytes =
					summary->resourceDesc.Width - summary->bufferViewOffset;
				if (summary->viewSize == 0)
					summary->viewSize = summary->bufferViewBytes;
			}
		}
	} else if (record.kindId == DescriptorKind::UAV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.uav.Format);
		summary->viewDimension = static_cast<UINT>(record.uav.ViewDimension);
		if (record.uav.ViewDimension == D3D12_UAV_DIMENSION_BUFFER) {
			summary->firstElement = record.uav.Buffer.FirstElement;
			summary->numElements = record.uav.Buffer.NumElements;
			summary->structureByteStride = record.uav.Buffer.StructureByteStride;
			summary->bufferViewOffset = record.resourceOffset;
			summary->bufferViewBytes = record.viewSize;
			if (summary->bufferViewBytes == 0 &&
					summary->hasResourceDesc &&
					summary->resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					summary->resourceDesc.Width > summary->bufferViewOffset) {
				summary->bufferViewBytes =
					summary->resourceDesc.Width - summary->bufferViewOffset;
				if (summary->viewSize == 0)
					summary->viewSize = summary->bufferViewBytes;
			}
		}
	} else if (record.kindId == DescriptorKind::RTV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.rtv.Format);
		summary->viewDimension = static_cast<UINT>(record.rtv.ViewDimension);
	} else if (record.kindId == DescriptorKind::DSV_ID && record.hasDesc) {
		summary->viewFormat = static_cast<UINT>(record.dsv.Format);
		summary->viewDimension = static_cast<UINT>(record.dsv.ViewDimension);
	}
}

struct MetadataSnapshotCache
{
	bool enabled = false;
	bool populated = false;
	bool building = false;
	std::vector<DX12RootSignatureSummary> rootSignatures;
	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12PsoRootSummary> psoRoots;
	std::vector<DX12DescriptorHeapSummary> descriptorHeaps;
};

static thread_local MetadataSnapshotCache tMetadataSnapshotCache;

static void CopyCachedMetadataSnapshot(
	const MetadataSnapshotCache &cache,
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	if (rootSignatures)
		*rootSignatures = cache.rootSignatures;
	if (descriptors)
		*descriptors = cache.descriptors;
	if (psoRoots)
		*psoRoots = cache.psoRoots;
	if (descriptorHeaps)
		*descriptorHeaps = cache.descriptorHeaps;
}

static void BuildResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps);

void DX12BeginFrameAnalysisMetadataCache()
{
	tMetadataSnapshotCache = MetadataSnapshotCache();
	tMetadataSnapshotCache.enabled = true;
}

void DX12EndFrameAnalysisMetadataCache()
{
	tMetadataSnapshotCache = MetadataSnapshotCache();
}

void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots)
{
	DX12GetResourceMetadataSnapshot(rootSignatures, descriptors, psoRoots, nullptr);
}

void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	// DX12DescriptorCollectPipeline::DrainForSnapshot removed 鈥?was dead code
	MetadataSnapshotCache &cache = tMetadataSnapshotCache;
	if (cache.enabled && !cache.building) {
		if (!cache.populated) {
			cache.building = true;
			// FrameAnalysis has several exporters that all need the same
			// descriptor inventory. Build it once per dump thread; per-export
			// million-slot rescans were the dominant F8 stall.
			BuildResourceMetadataSnapshot(
				&cache.rootSignatures, &cache.descriptors,
				&cache.psoRoots, &cache.descriptorHeaps);
			cache.building = false;
			cache.populated = true;
		}
		CopyCachedMetadataSnapshot(
			cache, rootSignatures, descriptors, psoRoots, descriptorHeaps);
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCacheHit",
			"\"roots\":%zu,\"descriptors\":%zu,\"psoRoots\":%zu,\"heaps\":%zu,\"wantRoots\":%u,\"wantDescriptors\":%u,\"wantPsoRoots\":%u,\"wantHeaps\":%u",
			cache.rootSignatures.size(), cache.descriptors.size(),
			cache.psoRoots.size(), cache.descriptorHeaps.size(),
			rootSignatures ? 1 : 0,
			descriptors ? 1 : 0,
			psoRoots ? 1 : 0,
			descriptorHeaps ? 1 : 0);
		return;
	}

	BuildResourceMetadataSnapshot(rootSignatures, descriptors, psoRoots, descriptorHeaps);
}

static void BuildResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps)
{
	AcquireSRWLockShared(&gResourceLock);
	// Snapshot pointers must remain stable while sparse heap maps can rehash.
	// This path is frame-analysis only, so take a coherent exclusive snapshot
	// instead of adding synchronization cost to normal Mod descriptor writes.
	AcquireSRWLockExclusive(&gDescriptorLock);
	std::vector<const DescriptorRecord*> liveDescriptors;
	std::vector<DescriptorRecord> heapDescriptors;
	std::unordered_set<SIZE_T> heapDescriptorHandles;
	size_t liveDescriptorCount = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		liveDescriptorCount += static_cast<size_t>(
			DX12DescriptorSlotArrayUtils::ReadLiveSlots(&heap));
	}
	liveDescriptors.reserve(liveDescriptorCount + gDescriptorByCpuHandle.size());
	heapDescriptors.reserve(liveDescriptorCount);
	heapDescriptorHandles.reserve(liveDescriptorCount);
	for (DescriptorHeapRecord &heap : gDescriptorHeaps) {
		DescriptorHeapRecord::StateLocks *stateLocks = heap.stateLocks.get();
		if (stateLocks)
			stateLocks->LockAllExclusive();
		if (stateLocks && !stateLocks->IsActive()) {
			stateLocks->UnlockAllExclusive();
			continue;
		}
		heap.currentDescriptors.ForEach(
			[&heapDescriptors, &heapDescriptorHandles, &heap](
				uint32_t slot, const DescriptorSlotBinding &binding) {
				if (!binding.value)
					return;
				const SIZE_T cpuHandle = heap.cpuStart +
					static_cast<SIZE_T>(slot) * heap.increment;
				heapDescriptors.push_back(DescriptorRecord());
				DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
					&heapDescriptors.back(), binding.value->value,
					cpuHandle);
				heapDescriptorHandles.insert(cpuHandle);
			});
		for (UINT slot = 0; slot < heap.occupied.size() && slot < heap.slots.size(); ++slot) {
			if (!heap.occupied[slot])
				continue;
			heapDescriptors.push_back(DescriptorRecord());
			DX12DescriptorSlotArrayUtils::CopyRecordValueForSlot(
				&heapDescriptors.back(), heap.slots[slot],
				heap.slots[slot].cpuHandle);
			heapDescriptorHandles.insert(heap.slots[slot].cpuHandle);
		}
		if (stateLocks)
			stateLocks->UnlockAllExclusive();
	}
	for (const DescriptorRecord &record : heapDescriptors)
		liveDescriptors.push_back(&record);
	for (const std::pair<const SIZE_T, DescriptorRecord> &entry : gDescriptorByCpuHandle) {
		if (heapDescriptorHandles.find(entry.first) != heapDescriptorHandles.end())
			continue;
		liveDescriptors.push_back(&entry.second);
	}

	size_t descriptorStateKnown = 0;
	size_t cbvDescriptors = 0;
	size_t srvDescriptors = 0;
	size_t uavDescriptors = 0;
	size_t rtvDescriptors = 0;
	size_t dsvDescriptors = 0;
	size_t textureDescriptors = 0;
	for (const DescriptorRecord *recordPtr : liveDescriptors) {
		const DescriptorRecord &record = *recordPtr;
		bool hasCurrentState = record.hasCurrentState;
		if (!hasCurrentState && record.resource) {
			std::unordered_map<ID3D12Resource*, size_t>::iterator foundResource =
				gResourceByPtr.find(record.resource);
			if (foundResource != gResourceByPtr.end() &&
				foundResource->second < gResources.size()) {
				hasCurrentState = gResources[foundResource->second].hasCurrentState;
			}
		}
		if (hasCurrentState)
			descriptorStateKnown++;
		D3D12_RESOURCE_DESC resourceDesc = record.resourceDesc;
		bool hasResourceDesc = record.hasResourceDesc;
		if (!hasResourceDesc && record.resource) {
			std::unordered_map<ID3D12Resource*, size_t>::iterator foundResource =
				gResourceByPtr.find(record.resource);
			if (foundResource != gResourceByPtr.end() &&
				foundResource->second < gResources.size()) {
				resourceDesc = gResources[foundResource->second].desc;
				hasResourceDesc = true;
			}
		}
		if (record.kindId == DescriptorKind::CBV_ID)
			cbvDescriptors++;
		else if (record.kindId == DescriptorKind::SRV_ID)
			srvDescriptors++;
		else if (record.kindId == DescriptorKind::UAV_ID)
			uavDescriptors++;
		else if (record.kindId == DescriptorKind::RTV_ID)
			rtvDescriptors++;
		else if (record.kindId == DescriptorKind::DSV_ID)
			dsvDescriptors++;
		if (hasResourceDesc &&
			resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			textureDescriptors++;
	}
	DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotBegin",
		"\"roots\":%zu,\"descriptors\":%zu,\"descriptorStates\":%zu,"
		"\"resources\":%zu,\"resourceSlots\":%zu,\"resourcesFromCreate\":%llu,"
		"\"psoRoots\":%zu,\"heaps\":%zu,\"cbv\":%zu,\"srv\":%zu,\"uav\":%zu,\"rtv\":%zu,\"dsv\":%zu,\"texture\":%zu,\"wantRoots\":%u,\"wantDescriptors\":%u,\"wantPsoRoots\":%u,\"wantHeaps\":%u",
		gRootSignatures.size(), liveDescriptors.size(), descriptorStateKnown,
		gLiveResourceCount, gResources.size(),
		static_cast<unsigned long long>(gResourcesRecordedFromCreate),
		gPsoRoots.size(), gDescriptorHeaps.size(),
		cbvDescriptors, srvDescriptors, uavDescriptors, rtvDescriptors, dsvDescriptors,
		textureDescriptors,
		rootSignatures ? 1 : 0,
		descriptors ? 1 : 0,
		psoRoots ? 1 : 0,
		descriptorHeaps ? 1 : 0);
	DX12LogManager::JsonFunc("DX12MetadataSnapshot",
		"descriptors=%zu,cbv=%zu,srv=%zu,uav=%zu,rtv=%zu,dsv=%zu,texture=%zu,resources=%zu,resourceSlots=%zu,heaps=%zu",
		liveDescriptors.size(), cbvDescriptors, srvDescriptors, uavDescriptors,
		rtvDescriptors, dsvDescriptors, textureDescriptors,
		gLiveResourceCount, gResources.size(), gDescriptorHeaps.size());

	if (rootSignatures) {
		rootSignatures->clear();
		rootSignatures->reserve(gRootSignatures.size());
		for (const RootSignatureRecord &record : gRootSignatures) {
			DX12RootSignatureSummary summary;
			summary.rootSignature = record.rootSignature;
			summary.hash = record.hash;
			summary.size = record.size;
			summary.nodeMask = record.nodeMask;
			summary.version = record.version;
			summary.flags = record.flags;
			summary.staticSamplerCount = record.staticSamplerCount;
			summary.parsed = record.parsed;
			summary.parameters = record.parameters;
			rootSignatures->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"roots\",\"count\":%zu", rootSignatures->size());
	}

	if (descriptors) {
		descriptors->clear();
		descriptors->reserve(liveDescriptors.size());
		size_t copiedBufferDescriptors = 0;
		size_t copiedCbvBufferDescriptors = 0;
		size_t copiedSrvBufferDescriptors = 0;
		size_t copiedUavBufferDescriptors = 0;
		size_t copiedCbvUnresolvedGpuVaDescriptors = 0;
		size_t copiedCbvMissingGpuVaDescriptors = 0;
		size_t copiedCbvMissingSizeDescriptors = 0;
		size_t cbvResolvedByAddressOnly = 0;
		size_t cbvAfterRangeMisses = 0;
		size_t cbvNoLowerBufferMisses = 0;
		size_t cbvOtherResolveMisses = 0;
		size_t cbvResolveSamples = 0;
		for (const DescriptorRecord *recordPtr : liveDescriptors) {
			DX12DescriptorSummary summary;
			FillDescriptorSummary(&summary, *recordPtr, true, true);
			if (summary.hasResourceDesc &&
				summary.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				copiedBufferDescriptors++;
				if (summary.kind == DescriptorKind::CBV) {
					copiedCbvBufferDescriptors++;
					if (recordPtr->cbv.SizeInBytes && summary.viewSize &&
						summary.viewSize < recordPtr->cbv.SizeInBytes)
						cbvResolvedByAddressOnly++;
				}
				else if (summary.kind == DescriptorKind::SRV)
					copiedSrvBufferDescriptors++;
				else if (summary.kind == DescriptorKind::UAV)
					copiedUavBufferDescriptors++;
			} else if (summary.kind == DescriptorKind::CBV &&
					summary.gpuVirtualAddress && !summary.resource) {
				copiedCbvUnresolvedGpuVaDescriptors++;
				UINT64 nearestBegin = 0;
				UINT64 nearestEnd = 0;
				UINT64 nearestResource = 0;
				const UINT64 cbvSize = summary.cbvSize ? summary.cbvSize : summary.viewSize;
				const char *reason = DX12BufferResolveDiagnosticsUtils::ReasonLocked(
					summary.gpuVirtualAddress, cbvSize ? cbvSize : 1,
					&nearestBegin, &nearestEnd, &nearestResource);
				if (!strcmp(reason, "after_range"))
					cbvAfterRangeMisses++;
				else if (!strcmp(reason, "no_lower_buffer"))
					cbvNoLowerBufferMisses++;
				else
					cbvOtherResolveMisses++;
				if (cbvResolveSamples < 16) {
					DX12FrameAnalysis::LogJsonFunc("CBVResolveMissSample",
						"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"reason\":\"%s\",\"nearestBegin\":\"0x%llX\",\"nearestEnd\":\"0x%llX\",\"nearestResource\":\"0x%llX\",\"bufferIndexSize\":%zu,\"resourceSlots\":%zu",
						static_cast<unsigned long long>(summary.gpuVirtualAddress),
						static_cast<unsigned long long>(cbvSize),
						reason,
						static_cast<unsigned long long>(nearestBegin),
						static_cast<unsigned long long>(nearestEnd),
						static_cast<unsigned long long>(nearestResource),
						gBufferResourceByGpuVa.size(), gResources.size());
					cbvResolveSamples++;
				}
			} else if (summary.kind == DescriptorKind::CBV && !summary.gpuVirtualAddress) {
				copiedCbvMissingGpuVaDescriptors++;
			} else if (summary.kind == DescriptorKind::CBV && !summary.viewSize && !summary.cbvSize) {
				copiedCbvMissingSizeDescriptors++;
			}
			descriptors->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"descriptors\",\"count\":%zu", descriptors->size());
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotDescriptorResources",
			"\"descriptors\":%zu,\"buffer\":%zu,\"cbvBuffer\":%zu,\"srvBuffer\":%zu,\"uavBuffer\":%zu,\"cbvUnresolvedGpuVa\":%zu,\"cbvMissingGpuVa\":%zu,\"cbvMissingSize\":%zu,\"cbvResolvedByAddressOnly\":%zu,\"cbvAfterRangeMisses\":%zu,\"cbvNoLowerBufferMisses\":%zu,\"cbvOtherResolveMisses\":%zu,\"bufferIndexSize\":%zu",
			descriptors->size(), copiedBufferDescriptors,
			copiedCbvBufferDescriptors, copiedSrvBufferDescriptors,
			copiedUavBufferDescriptors, copiedCbvUnresolvedGpuVaDescriptors,
			copiedCbvMissingGpuVaDescriptors, copiedCbvMissingSizeDescriptors,
			cbvResolvedByAddressOnly, cbvAfterRangeMisses, cbvNoLowerBufferMisses,
			cbvOtherResolveMisses, gBufferResourceByGpuVa.size());
	}

	if (psoRoots) {
		psoRoots->clear();
		psoRoots->reserve(gPsoRoots.size());
		for (const PsoRootRecord &record : gPsoRoots) {
			DX12PsoRootSummary summary;
			summary.psoIndex = record.psoIndex;
			summary.kind = record.kind;
			summary.pipelineState = record.pipelineState;
			summary.rootSignature = record.rootSignature;
			psoRoots->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"psoRoots\",\"count\":%zu", psoRoots->size());
	}

	if (descriptorHeaps) {
		descriptorHeaps->clear();
		descriptorHeaps->reserve(gDescriptorHeaps.size());
		for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
			DX12DescriptorHeapSummary summary;
			summary.heap = record.heap;
			summary.type = static_cast<UINT>(record.desc.Type);
			summary.numDescriptors = record.desc.NumDescriptors;
			summary.flags = static_cast<UINT>(record.desc.Flags);
			summary.nodeMask = record.desc.NodeMask;
			summary.cpuStart = record.cpuStart;
			summary.gpuStart = record.gpuStart;
			summary.increment = record.increment;
			descriptorHeaps->push_back(summary);
		}
		DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotCopied",
			"\"kind\":\"heaps\",\"count\":%zu", descriptorHeaps->size());
	}

	ReleaseSRWLockExclusive(&gDescriptorLock);
	ReleaseSRWLockShared(&gResourceLock);
	DX12FrameAnalysis::LogJsonFunc("MetadataSnapshotComplete", nullptr);
}

bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, summary))
		return false;
	CompleteDescriptorSummaryResourceInfo(summary, false);
	if (summary->kind == DescriptorKind::CBV && !summary->resource) {
		const UINT64 cbvGpuVa = summary->gpuVirtualAddress;
		const UINT64 cbvBytes = summary->cbvSize ? summary->cbvSize : summary->viewSize;
		if (cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(summary, cbvGpuVa, cbvBytes, false);
			summary->bufferViewOffset = summary->resourceOffset;
			summary->bufferViewBytes = summary->viewSize;
		}
	}
	return true;
}

bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	// Try heap slot via ShadowUtils first (per-heap lock, no global contention)
	DescriptorRecord shadowRecord;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlot(cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &shadowRecord)) {
		FillDescriptorSummary(summary, shadowRecord, false, false, false);
		return true;
	}
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr)) {
		if (location.heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
			if (location.slot < heap.occupied.size() &&
					location.slot < heap.slots.size() &&
					heap.occupied[location.slot]) {
				FillDescriptorSummary(summary, heap.slots[location.slot], false, false, false);
				ReleaseSRWLockShared(&gDescriptorLock);
				return true;
			}
		}
	}

	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(cpuHandle);
	if (found != gDescriptorByCpuHandle.end()) {
		FillDescriptorSummary(summary, found->second, false, false, false);
		ReleaseSRWLockShared(&gDescriptorLock);
		return true;
	}

	ReleaseSRWLockShared(&gDescriptorLock);
	return false;
}

class DX12DescriptorQueryBatchUtils
{
public:
static bool FillLightSummaryLocked(SIZE_T cpuHandle, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !summary)
		return false;

	DescriptorRecord shadowRecord;
	if (DX12DescriptorHeapShadowUtils::ReadDescriptorFromSlotLocked(
			cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES,
			&shadowRecord)) {
		FillDescriptorSummary(summary, shadowRecord, false, false, false);
		return true;
	}

	DescriptorSlotLocation location = {};
	if (ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr)) {
		if (location.heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
			if (location.slot < heap.occupied.size() &&
					location.slot < heap.slots.size() &&
					heap.occupied[location.slot]) {
				FillDescriptorSummary(summary, heap.slots[location.slot], false, false, false);
				return true;
			}
		}
	}

	std::unordered_map<SIZE_T, DescriptorRecord>::iterator found =
		gDescriptorByCpuHandle.find(cpuHandle);
	if (found != gDescriptorByCpuHandle.end()) {
		FillDescriptorSummary(summary, found->second, false, false, false);
		return true;
	}

	return false;
}

static void CompleteResourceInfoForFoundSummaries(
	DX12DescriptorSummary *summaries, const uint8_t *found, size_t count)
{
	if (!summaries || !found || count == 0)
		return;

	AcquireSRWLockShared(&gResourceLock);
	for (size_t i = 0; i < count; ++i) {
		if (!found[i])
			continue;
		CompleteDescriptorSummaryResourceInfo(&summaries[i], true);
		const bool isCbv = summaries[i].kind == DescriptorKind::CBV;
		const UINT64 cbvGpuVa = summaries[i].gpuVirtualAddress;
		const UINT64 cbvBytes = summaries[i].cbvSize ? summaries[i].cbvSize : summaries[i].viewSize;
		if (isCbv && !summaries[i].resource && cbvGpuVa && cbvBytes) {
			CompleteDescriptorSummaryBufferGpuVaInfo(&summaries[i], cbvGpuVa, cbvBytes, true);
			summaries[i].bufferViewOffset = summaries[i].resourceOffset;
			summaries[i].bufferViewBytes = summaries[i].viewSize;
		}
	}
	ReleaseSRWLockShared(&gResourceLock);
}
};

class DX12DescriptorHeapQueryUtils
{
public:
static bool TryFillByPtrLocked(
	ID3D12DescriptorHeap *heap,
	DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;
	std::unordered_map<ID3D12DescriptorHeap*, size_t>::const_iterator found =
		gDescriptorHeapByPtr.find(heap);
	if (found == gDescriptorHeapByPtr.end() ||
		found->second >= gDescriptorHeaps.size()) {
		return false;
	}
	const DescriptorHeapRecord &record = gDescriptorHeaps[found->second];
	if (record.heap != heap)
		return false;
	DX12DescriptorHeapStateUtils::FillSummary(record, summary);
	return true;
}

static bool TryFillByGpuHandleLocked(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!gpuHandle.ptr || !summary)
		return false;
	for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
		if (record.desc.Type != type || record.gpuStart == 0 || record.increment == 0)
			continue;
		const UINT64 begin = record.gpuStart;
		const UINT64 end = begin +
			static_cast<UINT64>(record.desc.NumDescriptors) * record.increment;
		if (gpuHandle.ptr < begin || gpuHandle.ptr >= end)
			continue;
		DX12DescriptorHeapStateUtils::FillSummary(record, summary);
		return true;
	}
	return false;
}

static bool TryFillByCpuHandleLocked(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!cpuHandle.ptr || !summary)
		return false;
	for (const DescriptorHeapRecord &record : gDescriptorHeaps) {
		if (record.desc.Type != type || record.cpuStart == 0 || record.increment == 0)
			continue;
		const SIZE_T begin = record.cpuStart;
		const SIZE_T end = begin +
			static_cast<SIZE_T>(record.desc.NumDescriptors) * record.increment;
		if (cpuHandle.ptr < begin || cpuHandle.ptr >= end)
			continue;
		DX12DescriptorHeapStateUtils::FillSummary(record, summary);
		return true;
	}
	return false;
}
};

void DX12FindDescriptorSummariesByCpuHandle(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	bool completeResourceInfo)
{
	if (!cpuHandles || !summaries || !found || count == 0)
		return;

	memset(found, 0, count * sizeof(uint8_t));
	DX12DescriptorHeapShadowUtils::EnsureVisible(
		cpuHandles,
		count);
	AcquireSRWLockShared(&gDescriptorLock);
	for (size_t i = 0; i < count; ++i) {
		summaries[i] = DX12DescriptorSummary();
		if (DX12DescriptorQueryBatchUtils::FillLightSummaryLocked(cpuHandles[i], &summaries[i]))
			found[i] = 1;
	}
	ReleaseSRWLockShared(&gDescriptorLock);

	if (completeResourceInfo)
		DX12DescriptorQueryBatchUtils::CompleteResourceInfoForFoundSummaries(
			summaries, found, count);
}

void DX12FindDescriptorSummariesByCpuHandleNoWait(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	uint8_t *pending,
	bool completeResourceInfo)
{
	if (!cpuHandles || !summaries || !found || count == 0)
		return;

	memset(found, 0, count * sizeof(uint8_t));
	std::vector<DescriptorRecord> pendingRecords;
	std::vector<uint8_t> pendingDirect;
	std::vector<uint8_t> pendingRaw;
	if (pending) {
		pendingRaw.resize(count);
		// DX12DescriptorCollectPipeline::FindPendingDescriptorHandles removed 鈥?was dead code
		memset(pending, 0, count * sizeof(uint8_t));
	}
	if (pending) {
		pendingRecords.resize(count);
		pendingDirect.resize(count);
		// DX12DescriptorCollectPipeline::CopyPendingDescriptorWrites removed 鈥?was dead code
	}

	AcquireSRWLockShared(&gDescriptorLock);
	for (size_t i = 0; i < count; ++i) {
		summaries[i] = DX12DescriptorSummary();
		if (pending && pendingDirect.size() == count && pendingDirect[i]) {
			FillDescriptorSummary(
				&summaries[i],
				pendingRecords[i],
				false,
				false,
				false);
			found[i] = 1;
			pending[i] = 0;
			continue;
		}
		if (DX12DescriptorQueryBatchUtils::FillLightSummaryLocked(cpuHandles[i], &summaries[i])) {
			found[i] = 1;
			if (pending)
				pending[i] = 0;
		} else if (pending && pendingRaw.size() == count && pendingRaw[i]) {
			pending[i] = 1;
		}
	}
	ReleaseSRWLockShared(&gDescriptorLock);

	if (completeResourceInfo)
		DX12DescriptorQueryBatchUtils::CompleteResourceInfoForFoundSummaries(
			summaries, found, count);
	for (size_t i = 0; i < pendingRecords.size(); ++i)
		ReleaseDescriptorRecordResources(&pendingRecords[i]);
}

bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !epoch || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (!ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr) ||
			location.heapIndex >= gDescriptorHeaps.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (location.slot >= heap.slotHistory.size() ||
			location.slot >= heap.slotHistoryEpochs.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const std::vector<DescriptorRecord> &history = heap.slotHistory[location.slot];
	const std::vector<UINT64> &epochs = heap.slotHistoryEpochs[location.slot];
	if (history.empty() || epochs.empty()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	size_t best = SIZE_MAX;
	size_t lo = 0;
	size_t hi = epochs.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (epochs[mid] <= epoch) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (best == SIZE_MAX || best >= history.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	FillDescriptorSummary(summary, history[best], false);
	ReleaseSRWLockShared(&gDescriptorLock);
	CompleteDescriptorSummaryResourceInfo(summary, false);
	return true;
}

bool FindDescriptorSummaryByCpuHandleAtEpochAndKind(
	SIZE_T cpuHandle, UINT64 epoch, const char *kind, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !epoch || !kind || !kind[0] || !summary)
		return false;

	DX12DescriptorHeapShadowUtils::EnsureVisible(
		&cpuHandle,
		1);
	AcquireSRWLockShared(&gDescriptorLock);
	DescriptorSlotLocation location = {};
	if (!ResolveDescriptorSlotForCpuHandleLocked(cpuHandle, nullptr, &location, nullptr) ||
			location.heapIndex >= gDescriptorHeaps.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const DescriptorHeapRecord &heap = gDescriptorHeaps[location.heapIndex];
	if (location.slot >= heap.slotHistory.size() ||
			location.slot >= heap.slotHistoryEpochs.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}
	const std::vector<DescriptorRecord> &history = heap.slotHistory[location.slot];
	const std::vector<UINT64> &epochs = heap.slotHistoryEpochs[location.slot];
	if (history.empty() || epochs.empty()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}

	size_t best = SIZE_MAX;
	size_t lo = 0;
	size_t hi = epochs.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (epochs[mid] <= epoch) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (best == SIZE_MAX || best >= history.size()) {
		ReleaseSRWLockShared(&gDescriptorLock);
		return false;
	}

	for (size_t i = best + 1; i > 0; --i) {
		const DescriptorRecord &record = history[i - 1];
		if (record.kind != kind)
			continue;
		// Root-signature ranges declare the descriptor type used by the shader.
		// Walk the slot history to the latest matching type instead of treating
		// a later different-kind write as loss of the SRV/UAV binding evidence.
		FillDescriptorSummary(summary, record, false);
		ReleaseSRWLockShared(&gDescriptorLock);
		CompleteDescriptorSummaryResourceInfo(summary, false);
		return true;
	}

	ReleaseSRWLockShared(&gDescriptorLock);
	return false;
}

bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!gpuHandle.ptr || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByGpuHandleLocked(
		gpuHandle, type, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

bool DX12FindDescriptorHeapByPtr(
	ID3D12DescriptorHeap *heap, DX12DescriptorHeapSummary *summary)
{
	if (!heap || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByPtrLocked(
		heap, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary)
{
	if (!cpuHandle.ptr || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	const bool found = DX12DescriptorHeapQueryUtils::TryFillByCpuHandleLocked(
		cpuHandle, type, summary);
	ReleaseSRWLockShared(&gDescriptorLock);
	return found;
}

void DX12GetTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;

	resources->clear();
	AcquireSRWLockShared(&gResourceLock);
	resources->reserve(gLiveResourceCount);
	for (const ResourceRecord &record : gResources) {
		if (!record.resource)
			continue;
		DX12ResourceSummary summary;
		summary.resource = record.resource;
		summary.resourceDesc = record.desc;
		summary.hasResourceDesc = true;
		if (record.hasHeapType) {
			summary.resourceHeapType = static_cast<UINT>(record.heapType);
			summary.hasResourceHeapType = true;
		}
		summary.gpuVirtualAddress = record.gpuVirtualAddress;
		summary.size = record.size;
		summary.currentState = static_cast<UINT>(record.currentState);
		summary.hasCurrentState = record.hasCurrentState;
		summary.stateTrackedByBarrier = record.stateTrackedByBarrier;
		resources->push_back(summary);
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void DX12GetRetainedTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;

	resources->clear();
	AcquireSRWLockShared(&gResourceLock);
	resources->reserve(gLiveResourceCount);
	for (const ResourceRecord &record : gResources) {
		if (!record.resource)
			continue;
		if (!DX12ResourceSnapshotUtils::TryAddRef(record.resource))
			continue;
		DX12ResourceSummary summary;
		summary.resource = record.resource;
		summary.resourceDesc = record.desc;
		summary.hasResourceDesc = true;
		if (record.hasHeapType) {
			summary.resourceHeapType = static_cast<UINT>(record.heapType);
			summary.hasResourceHeapType = true;
		}
		summary.gpuVirtualAddress = record.gpuVirtualAddress;
		summary.size = record.size;
		summary.currentState = static_cast<UINT>(record.currentState);
		summary.hasCurrentState = record.hasCurrentState;
		summary.stateTrackedByBarrier = record.stateTrackedByBarrier;
		summary.retained = true;
		resources->push_back(summary);
	}
	ReleaseSRWLockShared(&gResourceLock);
}

void DX12ReleaseTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources)
{
	if (!resources)
		return;
	for (DX12ResourceSummary &summary : *resources) {
		if (summary.retained && summary.resource)
			DX12ResourceSnapshotUtils::Release(summary.resource);
		summary.resource = nullptr;
		summary.retained = false;
	}
	resources->clear();
}

bool DX12RetainResource(ID3D12Resource *resource)
{
	return DX12ResourceSnapshotUtils::TryAddRef(resource);
}

void DX12ReleaseRetainedResource(ID3D12Resource *resource)
{
	DX12ResourceSnapshotUtils::Release(resource);
}
