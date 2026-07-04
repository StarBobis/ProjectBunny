#include "DX12ResourceTrackerPrivate.hpp"

#include "DX12DescriptorCollectPipeline.hpp"
#include "DX12ResourceCollectQueue.hpp"

bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary)
{
	if (!rootSignature || !summary)
		return false;

	AcquireSRWLockShared(&gDescriptorLock);
	auto found = gRootSignatureByPtr.find(rootSignature);
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
	auto found = gPsoRootByIndex.find(psoIndex);
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
		UINT64 generation = 0;
		size_t next = 0;
		BufferResolveCacheEntry entries[256] = {};
	};
	static thread_local LocalBufferResolveCache localCache;
	const UINT64 generation = gBufferResolveGeneration.load(std::memory_order_relaxed);
	if (localCache.generation != generation) {
		localCache = LocalBufferResolveCache();
		localCache.generation = generation;
		gBufferResolveGenerationResets++;
	}
	for (const BufferResolveCacheEntry &entry : localCache.entries) {
		if (entry.begin && gpuVirtualAddress >= entry.begin && gpuVirtualAddress < entry.end) {
			gBufferResolveTlsHits++;
			*summary = entry.summary;
			summary->resourceOffset = gpuVirtualAddress - entry.begin;
			const UINT64 available = entry.end - gpuVirtualAddress;
			summary->viewSize = size < available ? size : available;
			return true;
		}
	}

	bool resolved = false;
	UINT64 resolvedBegin = 0;
	UINT64 resolvedEnd = 0;
	UINT backwardProbes = 0;
	AcquireSRWLockShared(&gResourceLock);
	auto cached = gBufferResolveCache.upper_bound(gpuVirtualAddress);
	if (cached != gBufferResolveCache.begin()) {
		--cached;
		if (gpuVirtualAddress >= cached->second.begin &&
		    gpuVirtualAddress < cached->second.end) {
			gBufferResolveGlobalHits++;
			*summary = cached->second.summary;
			summary->resourceOffset = gpuVirtualAddress - cached->second.begin;
			const UINT64 available = cached->second.end - gpuVirtualAddress;
			summary->viewSize = size < available ? size : available;
			localCache.entries[localCache.next++ % 8] = cached->second;
			ReleaseSRWLockShared(&gResourceLock);
			return true;
		}
	}

	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
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
		// Fallback: linear scan all gResources for buffer entries whose
		// GPU VA range covers the requested address.  This catches
		// resources that were tracked but whose GPU VA never made it
		// into gBufferResourceByGpuVa (e.g. reserved resources that
		// returned zero VA at creation time, or resources created
		// before the hook was active).
		AcquireSRWLockShared(&gResourceLock);
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
		localCache.entries[localCache.next++ % 8] = entry;
		AcquireSRWLockExclusive(&gResourceLock);
		if (gBufferResolveGeneration.load(std::memory_order_relaxed) == generation)
			gBufferResolveCache[entry.begin] = entry;
		ReleaseSRWLockExclusive(&gResourceLock);
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

	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
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

	auto foundBuffer = gBufferResourceByGpuVa.upper_bound(gpuVirtualAddress);
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

// ── Buffer Content Cache ──────────────────────────────────────────────
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

bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize)
{
	if (!resource || !data || !dataSize)
		return false;

	CachedBufferContent entry;
	entry.data.assign(static_cast<const uint8_t*>(data),
		static_cast<const uint8_t*>(data) + dataSize);
	entry.dataSize = dataSize;

	// Get the GPU VA base directly (available immediately after creation,
	// unlike GPU VA resolution which may not be ready if tracking is async).
	entry.gpuVaBase = resource->GetGPUVirtualAddress();

	AcquireSRWLockExclusive(&gBufferContentLock);
	gBufferContentByResource[resource] = std::move(entry);
	ReleaseSRWLockExclusive(&gBufferContentLock);

#if defined(_DEBUG)
	DX12LogManager::DebugJsonFunc("DX12BufferContentCache",
		"\"stage\":\"stored\",\"resource\":\"0x%p\",\"bytes\":%zu,\"gpuVaBase\":\"0x%llx\"",
		resource, dataSize, static_cast<unsigned long long>(entry.gpuVaBase));
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
	auto it = gBufferContentByResource.find(summary.resource);
	if (it == gBufferContentByResource.end()) {
		ReleaseSRWLockShared(&gBufferContentLock);
		return false;
	}

	const CachedBufferContent &cached = it->second;
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

// ── IA Buffer Hash (Content-Based, like DX11) ──────────────────────────
// Priority:
//   1. Content cache hit �?CRC32 of actual buffer bytes (stable, like DX11)
//   2. Resource resolved  �?hash on RESOURCE_DESC + offset + viewSize (stable)
//   3. Fallback           �?hash on size + stride + format + slot (NO GPU VA!)

uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	// ── TLS 缓存: 同一 IA buffer 只 hash 一次 ─────────────────────
	// DX11 风格: Key = (gpuVa, size, stride, format, slot)
	// 4096 条目 (原 256) 避免大场景下缓存 thrashing。
	// 每帧 buffer resolve generation 变化时整体失效。
	struct IaHashCacheEntry { UINT64 gpuVa; UINT64 sz; UINT str; UINT fmt; UINT sl; uint32_t hash; };
	static thread_local IaHashCacheEntry s_cache[4096] = {};
	static thread_local UINT64 s_cacheGen = 0;
	static constexpr UINT kCacheMask = 4095;
	UINT64 gen = gBufferResolveGeneration.load(std::memory_order_relaxed);
	if (s_cacheGen != gen) { memset(s_cache, 0, sizeof(s_cache)); s_cacheGen = gen; }
	UINT bucket = static_cast<UINT>(((gpuVirtualAddress >> 6) ^ (gpuVirtualAddress >> 18) ^ size) & kCacheMask);
	for (UINT i = 0; i < 8; ++i) {
		IaHashCacheEntry &e = s_cache[(bucket + i) & kCacheMask];
		if (e.gpuVa == gpuVirtualAddress && e.sz == size && e.str == stride && e.fmt == format && e.sl == slot)
			return e.hash;
	}

	// ── Tier 1: Content-based CRC32 (matches DX11 behavior) ──────────
	const uint8_t *contentData = nullptr;
	size_t contentSize = 0;
	if (DX12TryGetBufferContent(gpuVirtualAddress, size, &contentData, &contentSize))
	{
		uint32_t hash = 0;
		const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'C');
		hash = HashBytes(hash, &tag, sizeof(tag));
		hash = crc32c_append(hash, contentData, contentSize);
		hash = HashBytes(hash, &stride, sizeof(stride));
		hash = HashBytes(hash, &format, sizeof(format));
		hash = HashBytes(hash, &slot, sizeof(slot));
		for (UINT j = 0; j < 8; ++j) {
			IaHashCacheEntry &ce = s_cache[(bucket + j) & kCacheMask];
			if (!ce.gpuVa) { ce.gpuVa = gpuVirtualAddress; ce.sz = size; ce.str = stride; ce.fmt = format; ce.sl = slot; ce.hash = hash; break; }
		}
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"\"stage\":\"content_crc32\",\"gpuVa\":\"0x%llx\",\"size\":%llu,"
			"\"stride\":%u,\"format\":%u,\"slot\":%u,\"hash\":\"%08x\"",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
		return hash;
	}

	// ── Tier 2/3: Metadata-based hash (NO GPU VA) ──────────────────
	DX12BufferResourceSummary summary = {};
	const bool resolved = DX12ResolveBufferResourceByGpuVa(
		gpuVirtualAddress, size, &summary);

	uint32_t hash = 0;
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));

	if (resolved && summary.hasResourceDesc) {
		// ── Stable: hash each RESOURCE_DESC field individually ────────
		// Hashing the raw struct (HashBytes(&desc, sizeof(desc))) is
		// unstable because D3D12_RESOURCE_DESC contains uninitialized
		// padding bytes (offset 4-7 between Dimension and Alignment,
		// and tail padding at offset 52-55).  Field-by-field hashing
		// eliminates this source of cross-session hash drift.
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
		// Always include heap type (even 0) �?conditional inclusion
		// (hasResourceHeapType) would make the hash differ between
		// sessions where tracking happened vs. didn't.
		hash = HashBytes(hash, &summary.resourceHeapType,
			sizeof(summary.resourceHeapType));
		hash = HashBytes(hash, &summary.resourceOffset,
			sizeof(summary.resourceOffset));
		hash = HashBytes(hash, &summary.viewSize,
			sizeof(summary.viewSize));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"\"stage\":\"resolved\",\"gpuVa\":\"0x%llx\",\"size\":%llu,"
			"\"offset\":%llu,\"viewSize\":%llu,\"stride\":%u,\"format\":%u,"
			"\"slot\":%u,\"hash\":\"%08x\"",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			static_cast<unsigned long long>(summary.resourceOffset),
			static_cast<unsigned long long>(summary.viewSize),
			stride, format, slot, hash);
#endif
	} else {
		// Fallback: buffer properties only, NO GPU VA
		// Two different buffers with same size+stride+format+slot
		// would collide, but in practice each mesh has unique sizes.
		hash = HashBytes(hash, &size, sizeof(size));
#if defined(_DEBUG)
		DX12LogManager::DebugJsonFunc("DX12IaHashResolve",
			"\"stage\":\"fallback_no_gpuva\",\"gpuVa\":\"0x%llx\","
			"\"size\":%llu,\"stride\":%u,\"format\":%u,\"slot\":%u,"
			"\"hash\":\"%08x\"",
			static_cast<unsigned long long>(gpuVirtualAddress),
			static_cast<unsigned long long>(size),
			stride, format, slot, hash);
#endif
	}

	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));
	for (UINT j = 0; j < 8; ++j) {
		IaHashCacheEntry &ce2 = s_cache[(bucket + j) & kCacheMask];
		if (!ce2.gpuVa) { ce2.gpuVa = gpuVirtualAddress; ce2.sz = size; ce2.str = stride; ce2.fmt = format; ce2.sl = slot; ce2.hash = hash; break; }
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
	auto found = gResourceByPtr.find(summary->resource);
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
	DX12ResourceCollectQueue::DrainForSnapshot("MetadataSnapshot");
	DX12DescriptorCollectPipeline::DrainForSnapshot("MetadataSnapshot");
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
	AcquireSRWLockShared(&gDescriptorLock);
	std::vector<const DescriptorRecord*> liveDescriptors;
	size_t liveDescriptorCount = 0;
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps)
		liveDescriptorCount += heap.liveSlots;
	liveDescriptors.reserve(liveDescriptorCount + gDescriptorByCpuHandle.size());
	for (const DescriptorHeapRecord &heap : gDescriptorHeaps) {
		for (UINT slot = 0; slot < heap.occupied.size() && slot < heap.slots.size(); ++slot) {
			if (heap.occupied[slot])
				liveDescriptors.push_back(&heap.slots[slot]);
		}
	}
	for (const auto &entry : gDescriptorByCpuHandle) {
		DescriptorSlotLocation ledgerLocation = {};
		if (ResolveDescriptorSlotForCpuHandleLocked(
				entry.first, nullptr, &ledgerLocation, nullptr) &&
				ledgerLocation.heapIndex < gDescriptorHeaps.size()) {
			const DescriptorHeapRecord &heap = gDescriptorHeaps[ledgerLocation.heapIndex];
			if (ledgerLocation.slot < heap.occupied.size() &&
					ledgerLocation.slot < heap.slots.size() &&
					heap.occupied[ledgerLocation.slot])
				continue;
		}
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
			auto foundResource = gResourceByPtr.find(record.resource);
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
			auto foundResource = gResourceByPtr.find(record.resource);
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
		"\"descriptors\":%zu,\"cbv\":%zu,\"srv\":%zu,\"uav\":%zu,\"rtv\":%zu,\"dsv\":%zu,\"texture\":%zu,\"resources\":%zu,\"resourceSlots\":%zu,\"heaps\":%zu",
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

	ReleaseSRWLockShared(&gDescriptorLock);
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

	DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
		&cpuHandle,
		1,
		"FindDescriptorSummaryLight");
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

	auto found = gDescriptorByCpuHandle.find(cpuHandle);
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

	auto found = gDescriptorByCpuHandle.find(cpuHandle);
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
	DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
		cpuHandles,
		count,
		"FindDescriptorSummariesBatch");
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

bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary)
{
	if (!cpuHandle || !epoch || !summary)
		return false;

	DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
		&cpuHandle,
		1,
		"FindDescriptorSummaryAtEpoch");
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

	DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(
		&cpuHandle,
		1,
		"FindDescriptorSummaryAtEpochAndKind");
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

	DX12ResourceCollectQueue::DrainForSnapshot("TrackedResourceSnapshot");
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

	DX12ResourceCollectQueue::DrainForSnapshot("RetainedTrackedResourceSnapshot");
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
