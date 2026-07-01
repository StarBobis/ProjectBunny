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
		BufferResolveCacheEntry entries[8] = {};
	};
	static thread_local LocalBufferResolveCache localCache;
	const UINT64 generation = gBufferResolveGeneration.load(std::memory_order_relaxed);
	if (localCache.generation != generation) {
		localCache = LocalBufferResolveCache();
		localCache.generation = generation;
	}
	for (const BufferResolveCacheEntry &entry : localCache.entries) {
		if (entry.begin && gpuVirtualAddress >= entry.begin && gpuVirtualAddress < entry.end) {
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
	AcquireSRWLockShared(&gResourceLock);
	auto cached = gBufferResolveCache.upper_bound(gpuVirtualAddress);
	if (cached != gBufferResolveCache.begin()) {
		--cached;
		if (gpuVirtualAddress >= cached->second.begin &&
		    gpuVirtualAddress < cached->second.end) {
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
		if (foundBuffer->second < gResources.size()) {
			const ResourceRecord &resource = gResources[foundBuffer->second];
			const UINT64 begin = resource.gpuVirtualAddress;
			const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
				resource.gpuVirtualAddressRealEnd : begin + resource.size;
			const UINT64 oobEnd = resource.gpuVirtualAddressOobEnd ?
				resource.gpuVirtualAddressOobEnd : realEnd;
			if (resource.resource &&
			    resource.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
			    begin != 0 && resource.size != 0 &&
			    gpuVirtualAddress >= begin && gpuVirtualAddress < realEnd) {
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
				resolvedBegin = begin;
				resolvedEnd = realEnd;
				resolved = true;
				if (requestedEnd > realEnd || gpuVirtualAddress >= realEnd) {
					DX12FrameAnalysis::LogJsonFunc("BufferResolveClampedToRealRange",
						"\"gpuVa\":\"0x%llX\",\"size\":%llu,\"resource\":\"%p\",\"begin\":\"0x%llX\",\"realEnd\":\"0x%llX\",\"oobEnd\":\"0x%llX\",\"viewSize\":%llu",
						static_cast<unsigned long long>(gpuVirtualAddress),
						static_cast<unsigned long long>(size),
						resource.resource,
						static_cast<unsigned long long>(begin),
						static_cast<unsigned long long>(realEnd),
						static_cast<unsigned long long>(oobEnd),
						static_cast<unsigned long long>(summary->viewSize));
				}
			}
		}
	}
	ReleaseSRWLockShared(&gResourceLock);

	if (resolved) {
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
	if (foundBuffer->second >= gResources.size())
		return false;

	const ResourceRecord &resource = gResources[foundBuffer->second];
	const UINT64 begin = resource.gpuVirtualAddress;
	const UINT64 realEnd = resource.gpuVirtualAddressRealEnd ?
		resource.gpuVirtualAddressRealEnd : begin + resource.size;
	const UINT64 oobEnd = resource.gpuVirtualAddressOobEnd ?
		resource.gpuVirtualAddressOobEnd : realEnd;
	if (!resource.resource ||
		resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		begin == 0 || resource.size == 0 ||
		gpuVirtualAddress < begin || gpuVirtualAddress >= realEnd)
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
	return true;
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
	--foundBuffer;
	if (foundBuffer->second >= gResources.size())
		return "index_invalid";

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
	if (!resource.resource)
		return "resource_null";
	if (resource.desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return "not_buffer";
	if (begin == 0 || resource.size == 0)
		return "empty_range";
	if (gpuVirtualAddress < begin)
		return "before_range";
	if (requestedEnd > realEnd && gpuVirtualAddress < end)
		return "after_real_range_oob_candidate";
	if (requestedEnd > end)
		return "after_range";
	return "unknown";
}
};

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

uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot)
{
	if (!gpuVirtualAddress || !size)
		return 0;

	DX12BufferResourceSummary summary = {};
	const bool resolved = DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, &summary);
	if (!resolved)
		return DX12HashBufferView(nullptr, gpuVirtualAddress, size, stride, format, slot);

	uint32_t hash = DX12HashBufferResourceView(&summary, gpuVirtualAddress, size);
	const uint32_t tag = MakeTrackerFourCC('D', 'X', 'I', 'A');
	hash = HashBytes(hash, &tag, sizeof(tag));
	hash = HashBytes(hash, &summary.resourceOffset, sizeof(summary.resourceOffset));
	hash = HashBytes(hash, &summary.viewSize, sizeof(summary.viewSize));
	hash = HashBytes(hash, &stride, sizeof(stride));
	hash = HashBytes(hash, &format, sizeof(format));
	hash = HashBytes(hash, &slot, sizeof(slot));
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
