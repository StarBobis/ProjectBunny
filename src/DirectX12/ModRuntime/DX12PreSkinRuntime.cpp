// DX12PreSkinRuntime.cpp: pre-skinning state, descriptor patches, caches, retirement and output resize tracking.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"



UINT64 MakePreSkinUavMatchCacheKey(
	ID3D12GraphicsCommandList *commandList, uint64_t csHash, UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint)
{
	(void)commandList;
	UINT64 key = 0x4f1bbcdc9cb7a2c5ull;
	key = HashCombine64(key, gReloadGeneration);
	key = HashCombine64(key, csHash);
	key = HashCombine64(key, bindingHash);
	key = HashCombine64(key, bindingDependencyFingerprint);
	key = HashCombine64(key, DX12GetIaHashContentGeneration());
	return key;
}


UINT64 CurrentPreSkinActiveGeneration()
{
	// Plain volatile load: the generation is only ever bumped with interlocked
	// exchanges, and this runs on every draw (tens of thousands per frame), so
	// the LOCK CMPXCHG read-modify-write fence buys nothing.
	return static_cast<UINT64>(gPreSkinActiveGeneration);
}


UINT64 DX12ModGetPreSkinActiveGenerationFast()
{
	return CurrentPreSkinActiveGeneration();
}


const std::unordered_set<std::wstring> &GetActivePreSkinSectionsSnapshot(
	UINT64 *generationOut)
{
	static thread_local DX12ActivePreSkinSectionSnapshotTlsCache cache;
	const UINT64 generation = CurrentPreSkinActiveGeneration();
	if (generationOut)
		*generationOut = generation;
	if (cache.generation == generation)
		return cache.sections;

	cache.generation = generation;
	cache.sections.clear();
	if (gHasActivePreSkinTextureOverridesFast == 0 &&
	    gHasConfirmedPreSkinSectionsFast == 0)
		return cache.sections;

	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinLock, "Lock.preSkinShared");
	cache.sections.reserve(
		gActivePreSkinTextureOverrides.size() + gConfirmedPreSkinSections.size());
	for (const auto &item : gActivePreSkinTextureOverrides)
		cache.sections.insert(item.first);
	cache.sections.insert(
		gConfirmedPreSkinSections.begin(), gConfirmedPreSkinSections.end());
	ReleaseSRWLockShared(&gPreSkinLock);
	return cache.sections;
}



void BumpPreSkinActiveGeneration()
{
	InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&gPreSkinActiveGeneration));
}


// Resource facts are immutable for a committed resource, so they are cached
// once when the output identity changes instead of being queried per draw.
static void CachePreSkinOutputResourceFacts(DX12ActivePreSkinTextureOverride *active)
{
	if (!active)
		return;
	active->outputGpuVirtualAddress = 0;
	active->outputResourceWidth = 0;
	active->outputResourceFactsValid = false;
	if (!active->outputResource)
		return;
	const D3D12_RESOURCE_DESC desc = active->outputResource->GetDesc();
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;
	const UINT64 gpuVirtualAddress =
		active->outputResource->GetGPUVirtualAddress();
	if (!gpuVirtualAddress)
		return;
	active->outputGpuVirtualAddress = gpuVirtualAddress;
	active->outputResourceWidth = desc.Width;
	active->outputResourceFactsValid = true;
}

static bool IsExpandedPreSkinOutput(const DX12ActivePreSkinTextureOverride &active)
{
	return active.outputResource && active.outputByteWidth &&
		active.outputResourceFactsValid &&
		active.outputByteWidth > active.outputResourceWidth;
}

static void PublishPreSkinExpandedOutputRangeLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active)
{
	if (!IsExpandedPreSkinOutput(active)) {
		auto found = gPreSkinExpandedOutputRangesBySection.find(section);
		if (found != gPreSkinExpandedOutputRangesBySection.end()) {
			gPreSkinExpandedOutputRangesBySection.erase(found);
			if (gPreSkinExpandedOutputRangesBySection.empty())
				InterlockedExchange(&gHasPreSkinExpandedOutputRangesFast, 0);
		}
		return;
	}
	DX12PreSkinExpandedOutputRange &range =
		gPreSkinExpandedOutputRangesBySection[section];
	range.gpuVirtualAddress = active.outputGpuVirtualAddress;
	range.width = active.outputResourceWidth;
	InterlockedExchange(&gHasPreSkinExpandedOutputRangesFast, 1);
}

static void RemovePreSkinExpandedOutputRangeLocked(const std::wstring &section)
{
	auto found = gPreSkinExpandedOutputRangesBySection.find(section);
	if (found == gPreSkinExpandedOutputRangesBySection.end())
		return;
	gPreSkinExpandedOutputRangesBySection.erase(found);
	if (gPreSkinExpandedOutputRangesBySection.empty())
		InterlockedExchange(&gHasPreSkinExpandedOutputRangesFast, 0);
}


void MoveActivePreSkinTextureOverrideResourceLocked(
	const std::wstring &section, DX12ActivePreSkinTextureOverride *active,
	std::vector<ID3D12Resource*> *releaseResources)
{
	if (!active)
		return;
	RemovePreSkinExpandedOutputRangeLocked(section);
	// Plan fix C: keep the output-resource reverse index in sync.
	if (active->outputResource) {
		auto indexIt = gPreSkinProducerSectionsByOutputResource.find(active->outputResource);
		if (indexIt != gPreSkinProducerSectionsByOutputResource.end()) {
			std::vector<std::wstring> &sections = indexIt->second;
			sections.erase(std::remove(sections.begin(), sections.end(), section), sections.end());
			if (sections.empty())
				gPreSkinProducerSectionsByOutputResource.erase(indexIt);
		}
		if (releaseResources)
			releaseResources->push_back(active->outputResource);
		active->outputResource = nullptr;
	}
	active->outputByteWidth = 0;
	active->outputStride = 0;
	active->outputGpuVirtualAddress = 0;
	active->outputResourceWidth = 0;
	active->outputResourceFactsValid = false;
}


void ReleaseMovedPreSkinResources(std::vector<ID3D12Resource*> *resources)
{
	if (!resources)
		return;
	for (ID3D12Resource *resource : *resources) {
		if (resource)
			resource->Release();
	}
	resources->clear();
}


void ClearActivePreSkinTextureOverridesLocked(
	std::vector<ID3D12Resource*> *releaseResources)
{
	const bool changed =
		!gActivePreSkinTextureOverrides.empty();
	for (auto &item : gActivePreSkinTextureOverrides)
		MoveActivePreSkinTextureOverrideResourceLocked(item.first, &item.second, releaseResources);
	gActivePreSkinTextureOverrides.clear();
	gPreSkinProducerSectionsByOutputResource.clear();
	InterlockedExchange(&gHasActivePreSkinTextureOverridesFast, 0);
	// This clears only the transient producer publication. Never clear
	// gPreSkinOutputResizeCache here: Present calls this function every frame,
	// while DX12 may record the corresponding consumer draw after that Present.
	// DLSS FG and NVIDIA capture widen this window. The consumer must fall back
	// to the fence-owned cache in DX12ModResolvePreSkinOutputResizeCacheByGpuVa.
	DX12ModClearPreSkinOutputResizeMap();
	if (changed)
		BumpPreSkinActiveGeneration();
}


void StoreActivePreSkinTextureOverrideLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active,
	std::vector<ID3D12Resource*> *releaseResources)
{
	// Do not use frame-local recording order as match_cs proof. Once the actual
	// PreSkin producer has matched, retain the section until configuration reload
	// so draws recorded across a Present or on another command list remain valid.
	if (gConfirmedPreSkinSections.insert(section).second)
		InterlockedExchange(&gHasConfirmedPreSkinSectionsFast, 1);
	auto existing = gActivePreSkinTextureOverrides.find(section);
	const DX12ActivePreSkinTextureOverride *existingPtr =
		existing != gActivePreSkinTextureOverrides.end() ? &existing->second : nullptr;
	// Plan fix G: the pre-skin dispatch publishes the same output buffer
	// identity every frame (the GPU rewrites its contents, but the active
	// entry - buffer, byte width, stride, producer slot layout - is stable).
	// Only bump the active generation when that identity actually changed;
	// bumping every frame invalidated the per-draw replay gate and the
	// per-slot pre-skin redirect negative caches, forcing the full GPUVA
	// lookup chain on every matching draw every frame.
	const bool sameContent = existingPtr &&
		existingPtr->outputResource == active.outputResource &&
		existingPtr->outputByteWidth == active.outputByteWidth &&
		existingPtr->outputStride == active.outputStride &&
		existingPtr->producer.rootParameterIndex == active.producer.rootParameterIndex &&
		existingPtr->producer.rangeIndex == active.producer.rangeIndex &&
		existingPtr->producer.shaderRegister == active.producer.shaderRegister &&
		existingPtr->producer.descriptorOffset == active.producer.descriptorOffset;
	if (sameContent) {
		// Keep the existing identity and generation, while releasing the old
		// reference after replacing the published metadata.
		ID3D12Resource *oldResource = existingPtr->outputResource;
		// The output identity is unchanged, so the cached resource facts and
		// the expanded-range snapshot entry carry over as-is.
		DX12ActivePreSkinTextureOverride stored = active;
		stored.outputGpuVirtualAddress = existingPtr->outputGpuVirtualAddress;
		stored.outputResourceWidth = existingPtr->outputResourceWidth;
		stored.outputResourceFactsValid = existingPtr->outputResourceFactsValid;
		gActivePreSkinTextureOverrides[section] = stored;
		if (releaseResources && oldResource)
			releaseResources->push_back(oldResource);
		return;
	}
	if (existing != gActivePreSkinTextureOverrides.end())
		MoveActivePreSkinTextureOverrideResourceLocked(section, &existing->second, releaseResources);
	InterlockedExchange(&gHasActivePreSkinTextureOverridesFast, 1);
	DX12ActivePreSkinTextureOverride stored = active;
	CachePreSkinOutputResourceFacts(&stored);
	gActivePreSkinTextureOverrides[section] = stored;
	// Plan fix C: publish the reverse index entry for the new output resource.
	if (stored.outputResource) {
		gPreSkinProducerSectionsByOutputResource[stored.outputResource].push_back(section);
	}
	PublishPreSkinExpandedOutputRangeLocked(section, stored);
	BumpPreSkinActiveGeneration();
}


bool DX12ModMayNeedPreSkinIaViewExpansion(
	const D3D12_VERTEX_BUFFER_VIEW *views, const bool *hasViews, UINT count)
{
	if (!views || !hasViews || !count)
		return false;
	// Only outputs whose published byte width exceeds the backing resource can
	// ever require view expansion; the snapshot keeps exactly those ranges
	// (usually zero to a handful), so the common case is a single lock-free
	// flag read instead of a locked full-table scan with per-entry COM calls.
	if (InterlockedCompareExchange(&gHasPreSkinExpandedOutputRangesFast, 0, 0) == 0)
		return false;

	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinLock, "Lock.preSkinShared");
	for (const auto &item : gPreSkinExpandedOutputRangesBySection) {
		const DX12PreSkinExpandedOutputRange &range = item.second;
		for (UINT slot = 0; slot < count; ++slot) {
			if (!hasViews[slot] ||
				views[slot].BufferLocation < range.gpuVirtualAddress)
				continue;
			if (views[slot].BufferLocation - range.gpuVirtualAddress < range.width) {
				ReleaseSRWLockShared(&gPreSkinLock);
				return true;
			}
		}
	}
	ReleaseSRWLockShared(&gPreSkinLock);
	return false;
}


bool DX12ModFindActivePreSkinProducerForOutput(
	ID3D12Resource *outputResource, UINT64 outputByteWidth, UINT outputStride,
	DX12ModPreSkinProducerSummary *summary)
{
	if (summary)
		*summary = DX12ModPreSkinProducerSummary();
	if (!outputResource || !summary)
		return false;

	bool found = false;
	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinLock, "Lock.preSkinShared");
	// Plan fix C: O(1) reverse-index lookup instead of a locked linear scan
	// of the whole active table (which the pre-skin IA replay path hit once
	// per VB slot per matching draw).
	auto indexIt = gPreSkinProducerSectionsByOutputResource.find(outputResource);
	if (indexIt != gPreSkinProducerSectionsByOutputResource.end()) {
		for (const std::wstring &section : indexIt->second) {
			auto item = gActivePreSkinTextureOverrides.find(section);
			if (item == gActivePreSkinTextureOverrides.end())
				continue;
			const DX12ActivePreSkinTextureOverride &active = item->second;
			if (outputByteWidth && active.outputByteWidth &&
			    outputByteWidth != active.outputByteWidth)
				continue;
			if (outputStride && active.outputStride &&
			    outputStride != active.outputStride)
				continue;
			summary->rootParameterIndex = active.producer.rootParameterIndex;
			summary->rangeIndex = active.producer.rangeIndex;
			summary->shaderRegister = active.producer.shaderRegister;
			summary->registerSpace = active.producer.registerSpace;
			summary->descriptorOffset = active.producer.descriptorOffset;
			summary->rootDescriptor = active.producer.rootDescriptor;
			summary->gpuVirtualAddress = active.producer.gpuVirtualAddress;
			summary->descriptor = active.producer.descriptor;
			summary->hasDescriptor = active.producer.hasDescriptor;
			summary->outputByteWidth = active.outputByteWidth;
			summary->outputStride = active.outputStride;
			found = true;
			break;
		}
	}
	ReleaseSRWLockShared(&gPreSkinLock);
	return found;
}


UINT64 HashWideString(UINT64 key, const std::wstring &text)
{
	for (wchar_t ch : text) {
		key = HashCombine64(key, static_cast<UINT64>(ch & 0xffff));
	}
	return key;
}


UINT64 HashComputeBindingLayout(
	UINT64 key, const DX12CurrentComputeUavBinding &binding, bool includeDescriptorHash)
{
	key = HashCombine64(key, binding.rootParameterIndex);
	key = HashCombine64(key, binding.rangeIndex);
	key = HashCombine64(key, binding.shaderRegister);
	key = HashCombine64(key, binding.registerSpace);
	key = HashCombine64(key, binding.descriptorOffset);
	key = HashCombine64(key, binding.descriptor.viewDimension);
	key = HashCombine64(key, binding.descriptor.viewFormat);
	key = HashCombine64(key, binding.descriptor.firstElement);
	key = HashCombine64(key, binding.descriptor.numElements);
	key = HashCombine64(key, binding.descriptor.structureByteStride);
	key = HashCombine64(key, binding.descriptor.bufferViewOffset);
	key = HashCombine64(key, binding.descriptor.bufferViewBytes);
	key = HashCombine64(key, binding.descriptor.cbvSize);
	key = HashCombine64(key, DescriptorViewSize(binding.descriptor));
	if (includeDescriptorHash)
		key = HashCombine64(key, HashComputeBufferBinding(binding));
	return key;
}


uint64_t HashComputeCbvLayoutList(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	uint64_t key = 0xb5316b20a34bf779ull;
	key = HashCombine64(key, cbvs.size());
	for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		key = HashComputeBindingLayout(key, cbv, false);
	return key;
}


UINT64 MakePreSkinDescriptorPatchAbortCacheKey(
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	UINT64 key = HashCombine64(
		gPreSkinSrvCacheGeneration.load(std::memory_order_relaxed),
		0x7d9a4c6b2f1e8351ull);
	key = HashWideString(key, config.section);
	key = HashCombine64(key, srvReplacements.size());
	for (const DX12PreSkinSrvReplacementBinding &replacement : srvReplacements) {
		key = HashCombine64(key, replacement.replaceSlot);
		key = HashCombine64(key, replacement.shaderRegister);
		key = HashWideString(key, replacement.resource);
	}
	key = HashCombine64(key, srvs.size());
	for (const DX12CurrentComputeUavBinding &srv : srvs)
		key = HashComputeBindingLayout(key, srv, true);
	key = HashCombine64(key, HashComputeCbvLayoutList(cbvs));
	return key;
}


void StorePreSkinDescriptorPatchAbort(UINT64 key)
{
	AcquireSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
	if (gPreSkinDescriptorPatchAbortCache.size() > 1024)
		gPreSkinDescriptorPatchAbortCache.clear();
	gPreSkinDescriptorPatchAbortCache.insert(key);
	ReleaseSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
}


void StorePreSkinSrvProbeNegative(UINT64 stableNegativeKey)
{
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinSrvProbeCacheLock, "Lock.srvProbeCacheExclusive");
	if (gPreSkinSrvNegativeCache.size() > 1024)
		gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvNegativeCache.insert(stableNegativeKey);
	ReleaseSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
}


void StorePreSkinSrvProbePositive(
	UINT64 stableNegativeKey,
	const DX12PreSkinSrvPositiveCacheValue &value)
{
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinSrvProbeCacheLock, "Lock.srvProbeCacheExclusive");
	if (gPreSkinSrvPositiveCache.size() > 512)
		gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvPositiveCache[stableNegativeKey] = value;
	ReleaseSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
}


UINT64 MakePreSkinSrvStableProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth)
{
	UINT64 key = 14695981039346656037ull;
	auto append = [&key](UINT64 value) {
		for (size_t i = 0; i < sizeof(value); ++i) {
			key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
			key *= 1099511628211ull;
		}
	};
	append(gPreSkinSrvCacheGeneration.load(std::memory_order_relaxed));
	append(static_cast<UINT64>(srv.rootParameterIndex));
	append(static_cast<UINT64>(srv.rangeIndex));
	append(static_cast<UINT64>(srv.shaderRegister));
	append(static_cast<UINT64>(srv.registerSpace));
	append(static_cast<UINT64>(srv.descriptorOffset));
	append(static_cast<UINT64>(srv.descriptor.viewDimension));
	append(static_cast<UINT64>(srv.descriptor.structureByteStride));
	append(srvByteWidth);
	// Deliberately NOT part of the key: srv.descriptor.gpuVirtualAddress and
	// srv.descriptor.resource. The game rewrites its descriptor arena every
	// frame, so those fields change per frame even when the same logical slot
	// is bound; including them made this "stable" cache miss every frame and
	// forced EnsureLoadedResourceForPreSkin + EnsureResourceSrvLocked to run
	// on every apply. The probe outcome depends only on the slot layout and
	// view shape plus the mod config (the replacement SRV always comes from
	// the mod's own resource), never on which game resource happens to be
	// bound at the slot - the mod replaces the slot wholesale.
	for (wchar_t ch : config.section) {
		key ^= static_cast<unsigned char>(ch & 0xff);
		key *= 1099511628211ull;
		key ^= static_cast<unsigned char>((ch >> 8) & 0xff);
		key *= 1099511628211ull;
	}
	return key;
}


// Bump helper for the sharded active-replacement table (see
// DX12PreSkinReplacementShard): inserts/erases only touch the owning
// command list's shard, so only that shard's generation is bumped. Other
// lists' restore TLS negative caches stay valid and off the lock.
void BumpPreSkinReplacementShardMutation(
	DX12PreSkinReplacementShard *shard)
{
	shard->mutationGeneration.fetch_add(1, std::memory_order_release);
}


bool IsPreSkinOutputResizeCacheEntryReusable(
	const DX12PreSkinOutputResizeCacheEntry &entry)
{
	if (!entry.pendingCommandLists.empty())
		return false;
	for (const auto &completion : entry.completionPoints) {
		if (completion.fence && completion.value &&
		    completion.fence->GetCompletedValue() < completion.value)
			return false;
	}
	return true;
}


void TrackPreSkinOutputResizeCacheEntryUse(
	DX12PreSkinOutputResizeCacheEntry *entry,
	IUnknown *commandListIdentity)
{
	if (!entry)
		return;
	if (commandListIdentity &&
	    std::find(entry->pendingCommandLists.begin(),
		entry->pendingCommandLists.end(), commandListIdentity) ==
		entry->pendingCommandLists.end()) {
		entry->pendingCommandLists.push_back(commandListIdentity);
		InterlockedExchange(
			&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 1);
	}
}


void ReleasePreSkinOutputResizeCacheEntry(
	DX12PreSkinOutputResizeCacheEntry *entry)
{
	if (!entry)
		return;
	if (entry->originalResource)
		entry->originalResource->Release();
	if (entry->resizedResource)
		entry->resizedResource->Release();
	if (entry->vertexResource)
		entry->vertexResource->Release();
	if (entry->descriptorHeap)
		entry->descriptorHeap->Release();
	for (auto &completion : entry->completionPoints) {
		if (completion.fence)
			completion.fence->Release();
	}
	*entry = DX12PreSkinOutputResizeCacheEntry();
}


// Bumped whenever the resize cache entry set changes (create/evict/clear).
// Guards the TLS fast path in DX12ModResolvePreSkinOutputResizeCacheByGpuVa.
inline static volatile LONG64 gPreSkinOutputResizeCacheGeneration = 0;

static void BumpPreSkinOutputResizeCacheGeneration()
{
	InterlockedIncrement64(&gPreSkinOutputResizeCacheGeneration);
}

// TLS fast path for the per-bind output-resize resolve. The previous
// implementation took the global cache lock and linearly scanned the whole
// cache for EVERY IA bind while pre-skin output resizing is active. Binding
// patterns are highly repetitive (the same view VA every frame), so a
// thread-local snapshot guarded by the cache generation serves the common
// case with two loads and no lock. The retained resource reference keeps the
// binding alive even if the cache entry is later evicted.
struct TlsPreSkinResizeResolve
{
	UINT64 generation = 0;
	UINT64 viewGpuVa = 0;
	ID3D12Resource *vertexResource = nullptr; // retained
	const ID3D12Resource *originalResource = nullptr; // identity only, not retained
	UINT64 resizedGpuVa = 0;
	UINT64 resizedByteWidth = 0;
	UINT64 sourceOffset = 0;
	UINT stride = 0;

	~TlsPreSkinResizeResolve()
	{
		if (vertexResource)
			vertexResource->Release();
	}
};
thread_local TlsPreSkinResizeResolve tlsPreSkinResizeResolve;


void ClearPreSkinOutputResizeCache()
{
	std::vector<DX12PreSkinOutputResizeCacheEntry> releaseEntries;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	releaseEntries.reserve(gPreSkinOutputResizeCache.size());
	for (auto &item : gPreSkinOutputResizeCache)
		releaseEntries.push_back(item.second);
	gPreSkinOutputResizeCache.clear();
	gPreSkinOutputResizeCacheUseSerial = 0;
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 0);
	InterlockedExchange(&gHasPreSkinOutputResizeCacheEntriesFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	BumpPreSkinOutputResizeCacheGeneration();
	for (DX12PreSkinOutputResizeCacheEntry &entry : releaseEntries)
		ReleasePreSkinOutputResizeCacheEntry(&entry);
}


bool AcquirePreSkinOutputResizeCacheEntry(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource,
	UINT64 requiredByteWidth, UINT stride, UINT elementCount,
	UINT64 originalViewBytes,
	ID3D12Resource **resizedResourceOut,
	ID3D12Resource **vertexResourceOut,
	ID3D12DescriptorHeap **descriptorHeapOut,
	D3D12_CPU_DESCRIPTOR_HANDLE *descriptorCpuOut,
	bool *cacheHitOut, UINT *ringSlotOut)
{
	if (resizedResourceOut)
		*resizedResourceOut = nullptr;
	if (vertexResourceOut)
		*vertexResourceOut = nullptr;
	if (descriptorHeapOut)
		*descriptorHeapOut = nullptr;
	if (descriptorCpuOut)
		*descriptorCpuOut = {};
	if (cacheHitOut)
		*cacheHitOut = false;
	if (ringSlotOut)
		*ringSlotOut = 0;
	if (!device || !originalResource || !requiredByteWidth || !stride || !elementCount ||
	    !resizedResourceOut || !vertexResourceOut || !descriptorHeapOut ||
	    !descriptorCpuOut)
		return false;
	const D3D12_RESOURCE_DESC originalDesc = originalResource->GetDesc();
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	if (originalDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		!originalGpuVa || !originalDesc.Width)
		return false;

	DX12PreSkinOutputResizeCacheIdentity baseKey = {
		reinterpret_cast<std::uintptr_t>(originalResource), requiredByteWidth, stride, 0
	};
	DX12PreSkinOutputResizeCacheIdentity key = baseKey;
	IUnknown *commandListIdentity = PreSkinCommandListIdentity(commandList);
	auto retainEntry = [resizedResourceOut, vertexResourceOut, descriptorHeapOut,
		descriptorCpuOut](DX12PreSkinOutputResizeCacheEntry &entry) {
		if (!entry.resizedResource || !entry.vertexResource ||
		    !entry.descriptorHeap || !entry.descriptorCpu.ptr)
			return false;
		entry.resizedResource->AddRef();
		entry.vertexResource->AddRef();
		entry.descriptorHeap->AddRef();
		*resizedResourceOut = entry.resizedResource;
		*vertexResourceOut = entry.vertexResource;
		*descriptorHeapOut = entry.descriptorHeap;
		*descriptorCpuOut = entry.descriptorCpu;
		return true;
	};

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	auto cached = gPreSkinOutputResizeCache.find(baseKey);
	if (cached != gPreSkinOutputResizeCache.end()) {
		key = cached->first;
		cached->second.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		// Keep the readable fallback extent current: the same original
		// allocation can be preloaded without view info, then acquired by the
		// actual producer which knows the exact UAV view bytes.
		if (originalViewBytes)
			cached->second.originalViewBytes = originalViewBytes;
		TrackPreSkinOutputResizeCacheEntryUse(
			&cached->second, commandListIdentity);
		const bool retained = retainEntry(cached->second);
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
		if (cacheHitOut)
			*cacheHitOut = retained;
		if (ringSlotOut)
			*ringSlotOut = key.ringSlot;
		return retained;
	}

	// Keep one replacement resource per original PreSkin output. Consumers bind
	// only the original GPU VA and often record on a different command list, so
	// they cannot identify a per-frame ring slot. A stable replacement inherits
	// the application's ordering for the original resource: producer copies and
	// consumer draws observe the same queue/fence dependency chain. Rotating the
	// replacement here lets a later recorded frame overwrite the global VA map
	// before an earlier frame records its draw, which appears as whole-model
	// flicker under FG/capture.
	key.ringSlot = 0;
	if (ringSlotOut)
		*ringSlotOut = key.ringSlot;
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);

	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = requiredByteWidth;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	ID3D12Resource *createdResource = nullptr;
	HRESULT resourceHr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
		IID_PPV_ARGS(&createdResource));
	if (FAILED(resourceHr) || !createdResource)
		return false;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ID3D12Resource *createdVertexResource = nullptr;
	resourceHr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&createdVertexResource));
	if (FAILED(resourceHr) || !createdVertexResource) {
		createdResource->Release();
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ID3D12DescriptorHeap *createdHeap = nullptr;
	HRESULT heapHr = device->CreateDescriptorHeap(
		&heapDesc, IID_PPV_ARGS(&createdHeap));
	if (FAILED(heapHr) || !createdHeap) {
		createdResource->Release();
		createdVertexResource->Release();
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE createdCpu =
		createdHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.StructureByteStride = stride;
	uavDesc.Buffer.NumElements = elementCount;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	device->CreateUnorderedAccessView(
		createdResource, nullptr, &uavDesc, createdCpu);

	DX12PreSkinOutputResizeCacheEntry releaseEntry;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	cached = gPreSkinOutputResizeCache.find(key);
	if (cached == gPreSkinOutputResizeCache.end()) {
		if (gPreSkinOutputResizeCache.size() >= PreSkinOutputResizeCacheCapacity) {
			auto oldest = gPreSkinOutputResizeCache.end();
			for (auto it = gPreSkinOutputResizeCache.begin();
			     it != gPreSkinOutputResizeCache.end(); ++it) {
				if (!IsPreSkinOutputResizeCacheEntryReusable(it->second))
					continue;
				if (oldest == gPreSkinOutputResizeCache.end() ||
				    it->second.useSerial < oldest->second.useSerial)
					oldest = it;
			}
			if (oldest != gPreSkinOutputResizeCache.end()) {
				releaseEntry = oldest->second;
				gPreSkinOutputResizeCache.erase(oldest);
				BumpPreSkinOutputResizeCacheGeneration();
			}
		}
		DX12PreSkinOutputResizeCacheEntry entry;
		originalResource->AddRef();
		entry.originalResource = originalResource;
		entry.resizedResource = createdResource;
		entry.vertexResource = createdVertexResource;
		entry.originalGpuVa = originalGpuVa;
		entry.originalByteWidth = originalDesc.Width;
		entry.originalViewBytes = originalViewBytes;
		entry.vertexGpuVa = createdVertexResource->GetGPUVirtualAddress();
		entry.descriptorHeap = createdHeap;
		entry.descriptorCpu = createdCpu;
		if (commandListIdentity) {
			entry.pendingCommandLists.push_back(commandListIdentity);
			InterlockedExchange(
				&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 1);
		}
		entry.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		cached = gPreSkinOutputResizeCache.emplace(key, entry).first;
		InterlockedExchange(&gHasPreSkinOutputResizeCacheEntriesFast, 1);
		BumpPreSkinOutputResizeCacheGeneration();
		createdResource = nullptr;
		createdVertexResource = nullptr;
		createdHeap = nullptr;
	} else {
		cached->second.useSerial = ++gPreSkinOutputResizeCacheUseSerial;
		TrackPreSkinOutputResizeCacheEntryUse(
			&cached->second, commandListIdentity);
	}
	const bool retained = retainEntry(cached->second);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	ReleasePreSkinOutputResizeCacheEntry(&releaseEntry);
	if (createdResource)
		createdResource->Release();
	if (createdVertexResource)
		createdVertexResource->Release();
	if (createdHeap)
		createdHeap->Release();
	return retained;
}


void PublishPendingPreSkinSubmissionWorkLocked()
{
	const bool hasWork =
		!gPendingPreSkinSubmissions.empty();
	InterlockedExchange(
		&gHasPendingPreSkinSubmissionWorkFast,
		hasWork ? 1 : 0);
}


bool AcquirePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap **heapOut,
	ID3D12Device **retainedDeviceOut)
{
	if (!device || !heapOut || !retainedDeviceOut)
		return false;
	*heapOut = nullptr;
	*retainedDeviceOut = nullptr;

	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	for (size_t index = gPreSkinDescriptorRestoreHeapPool.size(); index > 0; --index) {
		DX12PooledPreSkinDescriptorRestoreHeap &entry =
			gPreSkinDescriptorRestoreHeapPool[index - 1];
		if (entry.device != device || !entry.heap)
			continue;
		*heapOut = entry.heap;
		*retainedDeviceOut = entry.device;
		gPreSkinDescriptorRestoreHeapPool.erase(
			gPreSkinDescriptorRestoreHeapPool.begin() + static_cast<ptrdiff_t>(index - 1));
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
		return true;
	}
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap)
		return false;
	device->AddRef();
	*heapOut = heap;
	*retainedDeviceOut = device;
	return true;
}


void RecyclePreSkinDescriptorRestoreHeap(
	ID3D12Device *device,
	ID3D12DescriptorHeap *heap)
{
	if (!device) {
		if (heap)
			heap->Release();
		return;
	}
	if (!heap) {
		device->Release();
		return;
	}

	bool pooled = false;
	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	if (gPreSkinDescriptorRestoreHeapPool.size() <
			PreSkinDescriptorRestoreHeapPoolCapacity) {
		gPreSkinDescriptorRestoreHeapPool.push_back({ device, heap });
		pooled = true;
	}
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	if (!pooled) {
		heap->Release();
		device->Release();
	}
}


void DrainPreSkinDescriptorRestoreHeapPool(
	DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	AcquireSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
	for (auto &entry : gPreSkinDescriptorRestoreHeapPool) {
		if (entry.heap)
			releaseBatch->descriptorHeaps.push_back(entry.heap);
		if (entry.device)
			releaseBatch->devices.push_back(entry.device);
	}
	gPreSkinDescriptorRestoreHeapPool.clear();
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRestoreHeapPoolLock);
}


void ReleasePreSkinDescriptorRestores(
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *restores)
{
	if (!restores)
		return;
	for (auto &restore : *restores) {
		RecyclePreSkinDescriptorRestoreHeap(restore.device, restore.heap);
		restore.device = nullptr;
		restore.heap = nullptr;
	}
	restores->clear();
}


void ReleasePreSkinReleaseBatch(DX12PreSkinReleaseBatch *batch)
{
	if (!batch)
		return;
	for (ID3D12Resource *resource : batch->resources) {
		if (resource)
			resource->Release();
	}
	for (ID3D12DescriptorHeap *heap : batch->descriptorHeaps) {
		if (heap)
			heap->Release();
	}
	for (ID3D12Fence *fence : batch->fences) {
		if (fence)
			fence->Release();
	}
	for (ID3D12CommandQueue *queue : batch->queues) {
		if (queue)
			queue->Release();
	}
	for (ID3D12Device *device : batch->devices) {
		if (device)
			device->Release();
	}
	batch->resources.clear();
	batch->descriptorHeaps.clear();
	batch->fences.clear();
	batch->queues.clear();
	batch->devices.clear();
}


// -- ring-heap recycling bookkeeping (all under gPreSkinRetireLock) ---------

void NotePreSkinHeapRetainedForListLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	++gPreSkinRetainedHeapListCounts[heap];
}


void NotePreSkinHeapBucketDrainedLocked(
	const std::vector<ID3D12DescriptorHeap*> &heaps)
{
	for (ID3D12DescriptorHeap *heap : heaps)
	{
		if (!heap)
			continue;
		auto it = gPreSkinRetainedHeapListCounts.find(heap);
		if (it == gPreSkinRetainedHeapListCounts.end())
			continue;
		if (--it->second <= 0)
			gPreSkinRetainedHeapListCounts.erase(it);
	}
}


void RecordPreSkinHeapCompletionPointLocked(
	ID3D12DescriptorHeap *heap, ID3D12Fence *fence, UINT64 value,
	LONG submittedPresent)
{
	if (!heap)
		return;
	std::vector<DX12PreSkinHeapCompletionPoint> &points =
		gPreSkinHeapCompletionPoints[heap];
	for (DX12PreSkinHeapCompletionPoint &point : points)
	{
		if (point.fence != fence)
			continue;
		if (point.value < value)
			point.value = value;
		return;
	}
	if (fence)
		fence->AddRef();
	points.push_back({ fence, value, submittedPresent });
	if (points.size() > 8)
		points.erase(points.begin(), points.begin() + (points.size() - 8));
}


void ClearPreSkinHeapCompletionPointsLocked(
	ID3D12DescriptorHeap *heap, std::vector<ID3D12Fence*> *fencesOut)
{
	auto it = gPreSkinHeapCompletionPoints.find(heap);
	if (it == gPreSkinHeapCompletionPoints.end())
		return;
	if (fencesOut)
	{
		for (const DX12PreSkinHeapCompletionPoint &point : it->second)
		{
			if (point.fence)
				fencesOut->push_back(point.fence);
		}
	}
	else
	{
		for (const DX12PreSkinHeapCompletionPoint &point : it->second)
		{
			if (point.fence)
				point.fence->Release();
		}
	}
	gPreSkinHeapCompletionPoints.erase(it);
}


// A retired (or full) ring heap may be rewound only once no unsubmitted
// command list still references it and every submission that did has been
// consumed by the GPU (fence) or aged out (present-count fallback).
bool IsPreSkinRingHeapReusableLocked(
	ID3D12DescriptorHeap *heap, UINT minCapacity)
{
	if (!heap)
		return false;
	if (minCapacity &&
		heap->GetDesc().NumDescriptors < minCapacity)
		return false;
	auto countIt = gPreSkinRetainedHeapListCounts.find(heap);
	if (countIt != gPreSkinRetainedHeapListCounts.end() &&
		countIt->second != 0)
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	auto pointsIt = gPreSkinHeapCompletionPoints.find(heap);
	if (pointsIt != gPreSkinHeapCompletionPoints.end())
	{
		for (const DX12PreSkinHeapCompletionPoint &point : pointsIt->second)
		{
			const bool fenceComplete = point.fence && point.value &&
				point.fence->GetCompletedValue() >= point.value;
			const bool fallbackExpired = !point.fence &&
				present - point.submittedPresent >=
					PreSkinRetiredObjectPresentDelay;
			if (!fenceComplete && !fallbackExpired)
				return false;
		}
	}
	return true;
}


void MovePendingPreSkinSubmission(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinPendingSubmission *target)
{
	if (!source || !target)
		return;
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->resources, &target->resources);
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->descriptorHeaps, &target->descriptorHeaps);
}


void MovePendingPreSkinSubmissionForCommandListLocked(
	IUnknown *identity,
	DX12PreSkinPendingSubmission *target)
{
	if (!identity || !target)
		return;
	auto it = gPendingPreSkinSubmissions.find(identity);
	if (it == gPendingPreSkinSubmissions.end())
		return;
	NotePreSkinHeapBucketDrainedLocked(it->second.descriptorHeaps);
	MovePendingPreSkinSubmission(&it->second, target);
	gPendingPreSkinSubmissions.erase(it);
}


void MovePendingPreSkinSubmissionToReleaseBatch(
	DX12PreSkinPendingSubmission *source,
	DX12PreSkinReleaseBatch *batch)
{
	if (!source || !batch)
		return;
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->resources, &batch->resources);
	DX12DescriptorArenaUtils::MoveUniqueOwned(
		&source->descriptorHeaps, &batch->descriptorHeaps);
}


void CleanupRetiredPreSkinObjectsLocked(
	DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	for (auto it = gRetiredPreSkinResources.begin();
		 it != gRetiredPreSkinResources.end();) {
		const bool fenceComplete = it->completionFence && it->completionValue &&
			it->completionFence->GetCompletedValue() >= it->completionValue;
		const bool fallbackExpired = !it->completionFence &&
			present - it->retirePresent >= PreSkinRetiredObjectPresentDelay;
		if (!fenceComplete && !fallbackExpired) {
			++it;
			continue;
		}
		if (it->resource)
			releaseBatch->resources.push_back(it->resource);
		if (it->completionFence)
			releaseBatch->fences.push_back(it->completionFence);
		it = gRetiredPreSkinResources.erase(it);
	}

	for (auto it = gRetiredPreSkinDescriptorHeaps.begin();
		 it != gRetiredPreSkinDescriptorHeaps.end();) {
		const bool fenceComplete = it->completionFence && it->completionValue &&
			it->completionFence->GetCompletedValue() >= it->completionValue;
		const bool fallbackExpired = !it->completionFence &&
			present - it->retirePresent >= PreSkinRetiredObjectPresentDelay;
		if (!fenceComplete && !fallbackExpired) {
			++it;
			continue;
		}
		if (it->heap)
			releaseBatch->descriptorHeaps.push_back(it->heap);
		if (it->completionFence)
			releaseBatch->fences.push_back(it->completionFence);
		if (it->heap)
			ClearPreSkinHeapCompletionPointsLocked(it->heap, &releaseBatch->fences);
		it = gRetiredPreSkinDescriptorHeaps.erase(it);
	}
}


IUnknown *PreSkinCommandListIdentity(IUnknown *commandList)
{
	if (!commandList)
		return nullptr;
	ID3D12GraphicsCommandList *baseList =
		reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
	IUnknown *identity = DX12CommandListRuntime::GetCommandListIdentity(baseList);
	return identity ? identity : commandList;
}


bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();

	// TLS fast path: repeated binds of the same view resolve without the
	// cache lock and the linear scan (the previous code paid both on every
	// IA bind while pre-skin output resizing was active). The cache
	// generation bumps on every create/evict/clear, so a stale snapshot
	// falls back to the locked scan; the retained reference keeps the
	// binding alive across eviction.
	//
	// VA-reuse guard: the cache retains the original resource, so entries
	// outlive the game's release of the buffer. After a scene switch D3D12
	// can hand the freed VA to a NEW buffer; the VA-range match below would
	// then redirect the new scene's vertex buffer to the old scene's resize
	// output (whole-model flicker / warping). DX11 never had this problem:
	// the UAV view was resized at CreateUnorderedAccessView time with no
	// cross-frame cache. The owner check is the DX12 equivalent of that
	// immediacy - resolve who owns viewGpuVa right now and require it to be
	// the entry's original resource.
	//
	// The TLS snapshot is consulted BEFORE the owner resolve: the resolve
	// chain for an untracked VA (a mod-owned buffer created outside the
	// wrapper tracker, e.g. 0xc73d10000 in ZZZ) ran the full locked
	// fallbacks - 250-800us per slot per frame - and the old early return on
	// "no owner" never stored a negative, so the next lookup paid it again.
	const UINT64 generation = InterlockedCompareExchange64(
		&gPreSkinOutputResizeCacheGeneration, 0, 0);
	TlsPreSkinResizeResolve &tls = tlsPreSkinResizeResolve;
	if (tls.generation == generation && tls.viewGpuVa == viewGpuVa &&
	    !tls.originalResource && !tls.vertexResource) {
		// Cached negative for an untracked VA: no owner exists, so no cache
		// entry can apply while the generation is unchanged (any new entry
		// for a reused VA bumps it).
		return false;
	}
	DX12BufferResourceSummary currentOwnerSummary = {};
	const bool ownerResolved = DX12ResolveBufferResourceByGpuVa(
		viewGpuVa, 1, &currentOwnerSummary);
	const ID3D12Resource *currentOwner = ownerResolved ?
		currentOwnerSummary.resource : nullptr;
	if (!currentOwner) {
		// The VA is not tracked by any live buffer: never redirect (a
		// missing replacement is preferable to a corrupted mesh). Remember
		// the miss so repeated binds of the same VA skip the resolve chain.
		tls.generation = generation;
		tls.viewGpuVa = viewGpuVa;
		tls.originalResource = nullptr;
		if (tls.vertexResource) {
			tls.vertexResource->Release();
			tls.vertexResource = nullptr;
		}
		return false;
	}

	if (tls.generation == generation && tls.viewGpuVa == viewGpuVa &&
	    tls.originalResource == currentOwner) {
		if (tls.vertexResource) {
			tls.vertexResource->AddRef();
			binding->resizedResource = tls.vertexResource;
			binding->retainedResource = true;
			binding->resizedGpuVa = tls.resizedGpuVa;
			binding->resizedByteWidth = tls.resizedByteWidth;
			binding->sourceOffset = tls.sourceOffset;
			binding->stride = tls.stride;
			return true;
		}
		// Cached negative: this VA missed while the cache set was unchanged
		// (generation bumps on create/evict/clear) and is still owned by the
		// same resource, so it cannot have gained an entry. The negative was
		// stored on every scan miss but the fast path below never consulted
		// it, so each repeated bind of the same view re-entered the exclusive
		// locked linear scan (~150-280us per slot, measured on pre-skin
		// redirect binds/replays).
		return false;
	}

	// The publish map is intentionally frame-local, but the resize cache owns a
	// stable replacement for the original output resource until fence-safe
	// eviction or configuration reload. Resolve through that owner when Present,
	// FG, or capture falls between producer and consumer command-list recording.
	// This avoids both a frame-order dependency and a stale persistent GPU-VA map.
	IUnknown *commandListIdentity = PreSkinCommandListIdentity(commandList);
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	DX12PreSkinOutputResizeCacheEntry *bestEntry = nullptr;
	const DX12PreSkinOutputResizeCacheIdentity *bestKey = nullptr;
	UINT64 bestOffset = 0;
	// The same original allocation can have variants for different required
	// widths or strides. useSerial identifies the most recently observed producer
	// and keeps the fallback consistent with the publish map's last writer.
	for (auto &item : gPreSkinOutputResizeCache) {
		DX12PreSkinOutputResizeCacheEntry &candidate = item.second;
		if (!candidate.originalResource || !candidate.vertexResource ||
			!candidate.originalGpuVa || !candidate.originalByteWidth ||
			!candidate.vertexGpuVa)
			continue;
		// VA-reuse guard: skip entries whose original resource no longer
		// owns this VA (scene switch handed the freed VA to a new buffer).
		if (candidate.originalResource != currentOwner)
			continue;
		if (viewGpuVa < candidate.originalGpuVa)
			continue;
		const UINT64 sourceOffset = viewGpuVa - candidate.originalGpuVa;
		// Redirect only views inside the extent the game's consumers actually
		// read (the producer UAV view). The original resource can pack other
		// streams beyond that view (texcoords/normals); retargeting those to
		// the resized positions buffer corrupts them. Entries created without
		// view info (preload) fall back to the resource width.
		const UINT64 readableBytes = candidate.originalViewBytes ?
			candidate.originalViewBytes : candidate.originalByteWidth;
		if (sourceOffset >= readableBytes ||
		    sourceOffset >= item.first.requiredByteWidth)
			continue;
		if (!bestEntry || candidate.useSerial > bestEntry->useSerial) {
			bestEntry = &candidate;
			bestKey = &item.first;
			bestOffset = sourceOffset;
		}
	}

	if (!bestEntry || !bestKey) {
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
		// Negative result: remember it so the same VA does not re-enter the
		// locked scan until the cache set changes.
		tls.generation = generation;
		tls.viewGpuVa = viewGpuVa;
		tls.originalResource = currentOwner;
		if (tls.vertexResource) {
			tls.vertexResource->Release();
			tls.vertexResource = nullptr;
		}
		return false;
	}

	// Bind vertexResource, not resizedResource. PreSkin writes the UAV resource,
	// then records the required barriers and a copy into this COMMON-state
	// consumer resource; IA must not read the UAV allocation directly.
	bestEntry->vertexResource->AddRef();
	binding->resizedResource = bestEntry->vertexResource;
	binding->retainedResource = true;
	binding->originalGpuVa = bestEntry->originalGpuVa;
	binding->originalByteWidth = bestEntry->originalByteWidth;
	binding->resizedGpuVa = bestEntry->vertexGpuVa;
	binding->resizedByteWidth = bestKey->requiredByteWidth;
	binding->sourceOffset = bestOffset;
	binding->stride = bestKey->stride;
	binding->uavBarrierPending = false;
	binding->transitionToVertex = false;
	binding->stateBefore = D3D12_RESOURCE_STATE_COMMON;
	bestEntry->useSerial = ++gPreSkinOutputResizeCacheUseSerial;
	TrackPreSkinOutputResizeCacheEntryUse(bestEntry, commandListIdentity);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);

	// Refresh the TLS snapshot under the same lock the entry was read under.
	if (tls.vertexResource != bestEntry->vertexResource) {
		if (tls.vertexResource)
			tls.vertexResource->Release();
		tls.vertexResource = bestEntry->vertexResource;
		tls.vertexResource->AddRef();
	}
	tls.generation = generation;
	tls.viewGpuVa = viewGpuVa;
	tls.originalResource = currentOwner;
	tls.resizedGpuVa = bestEntry->vertexGpuVa;
	tls.resizedByteWidth = bestKey->requiredByteWidth;
	tls.sourceOffset = bestOffset;
	tls.stride = bestKey->stride;
	return true;
}


bool AcquirePreSkinOutputResizeSubmissionFence(
	ID3D12CommandQueue *queue, ID3D12Fence **fenceOut, UINT64 *valueOut)
{
	if (!queue || !fenceOut || !valueOut)
		return false;
	*fenceOut = nullptr;
	*valueOut = 0;

	AcquireSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
	DX12PreSkinOutputResizeQueueFence &state =
		gPreSkinOutputResizeQueueFences[queue];
	if (!state.fence) {
		ID3D12Device *device = nullptr;
		if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
			gPreSkinOutputResizeQueueFences.erase(queue);
			ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
			return false;
		}
		const HRESULT hr = device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.fence));
		device->Release();
		if (FAILED(hr) || !state.fence) {
			gPreSkinOutputResizeQueueFences.erase(queue);
			ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
			return false;
		}
	}
	if (state.nextValue == UINT64_MAX)
		state.nextValue = 0;
	*valueOut = ++state.nextValue;
	state.fence->AddRef();
	*fenceOut = state.fence;
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeQueueFenceLock);
	return true;
}


// Returns one caller-owned completion fence when requested. Retirement can
// share this signal with the resize cache, keeping submission bookkeeping to
// one queue Signal per ExecuteCommandLists.
bool MarkPreSkinOutputResizeCacheUsesSubmitted(
	ID3D12CommandQueue *queue,
	UINT numCommandLists,
	ID3D12CommandList *const *commandLists,
	ID3D12Fence **completionFenceOut,
	UINT64 *completionValueOut)
{
	if (completionFenceOut)
		*completionFenceOut = nullptr;
	if (completionValueOut)
		*completionValueOut = 0;
	if (!queue || !numCommandLists || !commandLists)
		return false;
	// Submission notifications are on the queue hot path.  Most submissions
	// have no resize-cache users; avoid allocating the identity/use vectors and
	// taking the cache lock in that overwhelmingly common case.  The flag is
	// published whenever a command list is associated with a cache entry and
	// cleared after the last pending use is consumed or abandoned.
	if (InterlockedCompareExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast, 0, 0) == 0)
		return false;
	// ExecuteCommandLists batches are normally small.  Keep the common path
	// allocation-free while retaining support for arbitrarily large batches.
	static constexpr UINT kStackIdentityCount = 8;
	IUnknown *stackIdentities[kStackIdentityCount] = {};
	std::vector<IUnknown*> heapIdentities;
	IUnknown **identityStorage = stackIdentities;
	if (numCommandLists > kStackIdentityCount) {
		heapIdentities.resize(numCommandLists);
		identityStorage = heapIdentities.data();
	}
	for (UINT i = 0; i < numCommandLists; ++i) {
		IUnknown *identity = PreSkinCommandListIdentity(commandLists[i]);
		if (identity)
			identityStorage[i] = identity;
	}
	const UINT identityCount = [&]() {
		UINT count = 0;
		for (UINT i = 0; i < numCommandLists; ++i)
			if (identityStorage[i])
				++count;
		return count;
	}();
	if (identityCount == 0)
		return false;

	// Membership test for the submitted batch.  Small batches stay
	// allocation-free with a linear scan over the stack array; large batches
	// build a hash set once so the per-entry scans below are O(1) per lookup
	// instead of O(batch) - the old code rescanned the identity array for
	// every pending entry of every cache item (O(items x pending x batch))
	// on the ExecuteCommandLists hot path.
	std::unordered_set<IUnknown*> identitySet;
	const bool useIdentitySet = numCommandLists > kStackIdentityCount;
	if (useIdentitySet) {
		identitySet.reserve(numCommandLists);
		for (UINT i = 0; i < numCommandLists; ++i) {
			if (identityStorage[i])
				identitySet.insert(identityStorage[i]);
		}
	}
	const auto isSubmittedIdentity = [&](IUnknown *pending) {
		if (useIdentitySet)
			return identitySet.find(pending) != identitySet.end();
		return std::find(identityStorage, identityStorage + numCommandLists,
			pending) != identityStorage + numCommandLists;
	};

	std::vector<DX12PreSkinOutputResizeSubmittedUse> submittedUses;
	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheShared");
	for (const auto &item : gPreSkinOutputResizeCache) {
		const DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		bool hasSubmittedUse = false;
		for (IUnknown *pending : entry.pendingCommandLists) {
			if (isSubmittedIdentity(pending)) {
				hasSubmittedUse = true;
				break;
			}
		}
		if (!hasSubmittedUse)
			continue;
		submittedUses.push_back({ item.first });
	}
	ReleaseSRWLockShared(&gPreSkinOutputResizeCacheLock);
	if (submittedUses.empty())
		return false;

	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
	if (!AcquirePreSkinOutputResizeSubmissionFence(
			queue, &fence, &fenceValue))
		return false;
	HRESULT signalHr = E_FAIL;
	{
		// The signal must follow the application's ExecuteCommandLists call but
		// bypass our normal queue instrumentation to avoid recursive bookkeeping.
		DX12InternalReplayScope internalReplay;
		signalHr = queue->Signal(fence, fenceValue);
	}
	if (FAILED(signalHr)) {
		fence->Release();
		return false;
	}

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	for (const DX12PreSkinOutputResizeSubmittedUse &submitted : submittedUses) {
		auto found = gPreSkinOutputResizeCache.find(submitted.key);
		if (found == gPreSkinOutputResizeCache.end())
			continue;
		DX12PreSkinOutputResizeCacheEntry &entry = found->second;
		const size_t oldPendingCount = entry.pendingCommandLists.size();
		entry.pendingCommandLists.erase(
			std::remove_if(
				entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(),
				[&isSubmittedIdentity](IUnknown *pending) {
					return isSubmittedIdentity(pending);
				}),
			entry.pendingCommandLists.end());
		if (entry.pendingCommandLists.size() == oldPendingCount)
			continue;
		auto existing = std::find_if(
			entry.completionPoints.begin(), entry.completionPoints.end(),
			[fence](const DX12PreSkinOutputResizeCacheEntry::CompletionPoint &point) {
				return point.fence == fence;
			});
		if (existing != entry.completionPoints.end()) {
			if (fenceValue > existing->value)
				existing->value = fenceValue;
		} else {
			fence->AddRef();
			entry.completionPoints.push_back({ fence, fenceValue });
		}
	}
	bool hasPendingUses = false;
	for (const auto &item : gPreSkinOutputResizeCache) {
		if (!item.second.pendingCommandLists.empty()) {
			hasPendingUses = true;
			break;
		}
	}
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast,
		hasPendingUses ? 1 : 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
	if (completionFenceOut) {
		*completionFenceOut = fence;
		if (completionValueOut)
			*completionValueOut = fenceValue;
	} else {
		fence->Release();
	}
	return true;
}


void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList)
{
	// Fast out when the cache is empty: no entry can reference this list,
	// and the pending-work flag is already 0 (it only ever goes 1 alongside
	// a cache entry's pendingCommandLists and is reset when the cache
	// clears). The entries flag is set under the cache lock before the
	// entry becomes visible to other threads, and an entry naming this
	// list's identity can only be published by the thread recording this
	// list, which the game must externally synchronize with Reset/Release.
	if (InterlockedCompareExchange(
			&gHasPreSkinOutputResizeCacheEntriesFast, 0, 0) == 0)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	if (!identity)
		return;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
	for (auto &item : gPreSkinOutputResizeCache) {
		DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		entry.pendingCommandLists.erase(
			std::remove(entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(), identity),
			entry.pendingCommandLists.end());
	}
	bool hasPendingUses = false;
	for (const auto &item : gPreSkinOutputResizeCache) {
		if (!item.second.pendingCommandLists.empty()) {
			hasPendingUses = true;
			break;
		}
	}
	InterlockedExchange(
		&gHasPendingPreSkinOutputResizeSubmissionWorkFast,
		hasPendingUses ? 1 : 0);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
}


void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource)
{
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	if (!identity || !vertexResource)
		return;
	// 快路径：消费端绘制线程只读扫描，命中已登记的记录则共享锁放行，
	// 避免每次绘制都抢占全局独占锁、把并行录制线程串行化。
	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheShared");
	for (const auto &item : gPreSkinOutputResizeCache) {
		const DX12PreSkinOutputResizeCacheEntry &entry = item.second;
		if (entry.vertexResource != vertexResource)
			continue;
		const bool alreadyTracked =
			std::find(entry.pendingCommandLists.begin(),
				entry.pendingCommandLists.end(), identity) !=
			entry.pendingCommandLists.end();
		ReleaseSRWLockShared(&gPreSkinOutputResizeCacheLock);
		if (alreadyTracked)
			return;
		// 需要登记：升级前条目可能已被容量回收淘汰，独占锁下重新查找。
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeCacheLock, "Lock.outputResizeCacheExclusive");
		for (auto &exclusiveItem : gPreSkinOutputResizeCache) {
			DX12PreSkinOutputResizeCacheEntry &exclusiveEntry =
				exclusiveItem.second;
			if (exclusiveEntry.vertexResource != vertexResource)
				continue;
			TrackPreSkinOutputResizeCacheEntryUse(
				&exclusiveEntry, identity);
			break;
		}
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeCacheLock);
		return;
	}
	ReleaseSRWLockShared(&gPreSkinOutputResizeCacheLock);
}



void RetirePreSkinResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[nullptr].resources, resource);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}


void RetirePreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[identity].resources, resource);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}



void RetirePreSkinDescriptorHeap(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	DX12DescriptorArenaUtils::AppendUniqueOwned(
		&gPendingPreSkinSubmissions[nullptr].descriptorHeaps, heap);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}


void RetainPreSkinDescriptorHeapForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	std::vector<ID3D12DescriptorHeap*> &heaps =
		gPendingPreSkinSubmissions[identity].descriptorHeaps;
	if (std::find(heaps.begin(), heaps.end(), heap) == heaps.end()) {
		heap->AddRef();
		heaps.push_back(heap);
		NotePreSkinHeapRetainedForListLocked(heap);
	}
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}


void DX12ModForgetPendingPreSkinSubmissionsForCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	if (InterlockedCompareExchange(
			&gHasPendingPreSkinSubmissionWorkFast, 0, 0) == 0)
		return;
	IUnknown *identity = PreSkinCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	auto it = gPendingPreSkinSubmissions.find(identity);
	if (it != gPendingPreSkinSubmissions.end()) {
		// The list is being torn down, so no future ExecuteCommandLists will
		// ever consume this bucket. Draining it through the retired deques
		// (Present-cadence fallback, no fence) avoids leaking until config
		// reload and avoids the bucket being inherited by a command list that
		// later reuses this address.
		const LONG present = DX12RuntimeState::GetPresentCount();
		for (ID3D12Resource *resource : it->second.resources) {
			DX12RetiredPreSkinResource retired;
			retired.resource = resource;
			retired.retirePresent = present;
			gRetiredPreSkinResources.push_back(retired);
		}
		NotePreSkinHeapBucketDrainedLocked(it->second.descriptorHeaps);
		for (ID3D12DescriptorHeap *heap : it->second.descriptorHeaps) {
			DX12RetiredPreSkinDescriptorHeap retired;
			retired.heap = heap;
			retired.retirePresent = present;
			gRetiredPreSkinDescriptorHeaps.push_back(retired);
		}
		if (!it->second.resources.empty() ||
			!it->second.descriptorHeaps.empty())
			InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 1);
		gPendingPreSkinSubmissions.erase(it);
		PublishPendingPreSkinSubmissionWorkLocked();
	}
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}


void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	if (!queue || !commandLists || !numCommandLists)
		return;
	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
	MarkPreSkinOutputResizeCacheUsesSubmitted(
		queue, numCommandLists, commandLists, &fence, &fenceValue);

	// Fast path: nothing is waiting on a future submission. The flag mirrors
	// gPendingPreSkinSubmissions emptiness; the executor gate has already paid
	// the CAS reads for the combined flags, so this is one plain volatile read
	// that skips the retire lock and the map lookups below.
	if (gHasPendingPreSkinSubmissionWorkFast == 0) {
		if (fence)
			fence->Release();
		return;
	}

	DX12PreSkinReleaseBatch releaseBatch;
	DX12PreSkinPendingSubmission pending;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked(&releaseBatch);

	auto globalIt = gPendingPreSkinSubmissions.find(nullptr);
	if (globalIt != gPendingPreSkinSubmissions.end()) {
		MovePendingPreSkinSubmission(&globalIt->second, &pending);
		gPendingPreSkinSubmissions.erase(globalIt);
	}

	if (!gPendingPreSkinSubmissions.empty()) {
		for (UINT i = 0; i < numCommandLists; ++i) {
			MovePendingPreSkinSubmissionForCommandListLocked(
				PreSkinCommandListIdentity(commandLists[i]),
				&pending);
		}
	}

	if (pending.resources.empty() && pending.descriptorHeaps.empty()) {
		PublishPendingPreSkinSubmissionWorkLocked();
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		ReleasePreSkinReleaseBatch(&releaseBatch);
		return;
	}

	// The original ExecuteCommandLists has returned. Signal a queue-local fence
	// now, so every object retired by these command lists is released exactly
	// after the GPU has consumed this submission. Do not hold gPreSkinRetireLock
	// across Signal: drivers may synchronously enter queue instrumentation.
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	HRESULT signalHr = E_FAIL;
	if (fence) {
		signalHr = S_OK;
	} else if (AcquirePreSkinOutputResizeSubmissionFence(queue, &fence, &fenceValue)) {
		DX12InternalReplayScope internalReplay;
		signalHr = queue->Signal(fence, fenceValue);
	}

	const LONG retirePresent = DX12RuntimeState::GetPresentCount();
	const bool fenceTracked = SUCCEEDED(signalHr) && fence && fenceValue;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	for (ID3D12Resource *resource : pending.resources) {
		if (!resource)
			continue;
		if (fenceTracked)
			fence->AddRef();
		gRetiredPreSkinResources.push_back(
			{ resource, fenceTracked ? fence : nullptr,
				fenceTracked ? fenceValue : 0, retirePresent });
	}
	for (ID3D12DescriptorHeap *heap : pending.descriptorHeaps) {
		if (!heap)
			continue;
		if (fenceTracked)
			fence->AddRef();
		RecordPreSkinHeapCompletionPointLocked(
			heap, fenceTracked ? fence : nullptr,
			fenceTracked ? fenceValue : 0, retirePresent);
		gRetiredPreSkinDescriptorHeaps.push_back(
			{ heap, fenceTracked ? fence : nullptr,
				fenceTracked ? fenceValue : 0, retirePresent });
	}
	if (!pending.resources.empty() || !pending.descriptorHeaps.empty())
		InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 1);
	PublishPendingPreSkinSubmissionWorkLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	if (fence)
		fence->Release();
	ReleasePreSkinReleaseBatch(&releaseBatch);
}


bool DX12ModNeedsCommandListSubmissionNotifications()
{
	// Plain volatile reads: these flags only gate submission bookkeeping,
	// and every ExecuteCommandLists used to pay two full-fence CAS reads
	// for them on the game's submission thread.
	return gHasPendingPreSkinSubmissionWorkFast != 0 ||
		gHasPendingPreSkinOutputResizeSubmissionWorkFast != 0;
}


void DX12ModRunPresentMaintenance()
{
	if (gHasRetiredPreSkinObjectsFast == 0)
		return;
	DX12PreSkinReleaseBatch releaseBatch;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked(&releaseBatch);
	if (gRetiredPreSkinResources.empty() &&
		gRetiredPreSkinDescriptorHeaps.empty())
		InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
	ReleasePreSkinReleaseBatch(&releaseBatch);
}


void ReleasePreSkinDescriptorRingLocked(DX12PreSkinReleaseBatch *releaseBatch)
{
	if (!releaseBatch)
		return;
	if (gPreSkinDescriptorRing.heap) {
		releaseBatch->descriptorHeaps.push_back(gPreSkinDescriptorRing.heap);
		gPreSkinDescriptorRing.heap = nullptr;
	}
	if (gPreSkinDescriptorRing.device) {
		releaseBatch->devices.push_back(gPreSkinDescriptorRing.device);
		gPreSkinDescriptorRing.device = nullptr;
	}
	gPreSkinDescriptorRing.capacity = 0;
	gPreSkinDescriptorRing.offset = 0;
	gPreSkinDescriptorRing.increment = 0;
	gPreSkinDescriptorRing.present = -1;
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			releaseBatch->descriptorHeaps.push_back(retired.heap);
		if (retired.completionFence)
			releaseBatch->fences.push_back(retired.completionFence);
	}
	gRetiredPreSkinDescriptorHeaps.clear();
}


bool EnsurePreSkinDescriptorRingLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, UINT requiredDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpuBase,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpuBase,
	UINT *increment,
	ID3D12DescriptorHeap **heapOut)
{
	if (!device || !requiredDescriptors || !cpuBase || !gpuBase || !increment || !heapOut)
		return false;
	*heapOut = nullptr;
	
	for (;;) {
		ID3D12DescriptorHeap *oldHeap = nullptr;
		ID3D12Device *oldDevice = nullptr;
		ID3D12DescriptorHeap *usedHeap = nullptr;
		UINT capacityToCreate = 0;
		AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
		const bool needsNewHeap =
			gPreSkinDescriptorRing.device != device ||
			!gPreSkinDescriptorRing.heap ||
			requiredDescriptors > gPreSkinDescriptorRing.capacity ||
			gPreSkinDescriptorRing.offset + requiredDescriptors > gPreSkinDescriptorRing.capacity;
		if (!needsNewHeap) {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu =
				gPreSkinDescriptorRing.heap->GetCPUDescriptorHandleForHeapStart();
			D3D12_GPU_DESCRIPTOR_HANDLE gpu =
				gPreSkinDescriptorRing.heap->GetGPUDescriptorHandleForHeapStart();
			cpu.ptr += static_cast<SIZE_T>(gPreSkinDescriptorRing.offset) *
				gPreSkinDescriptorRing.increment;
			gpu.ptr += static_cast<UINT64>(gPreSkinDescriptorRing.offset) *
				gPreSkinDescriptorRing.increment;
			gPreSkinDescriptorRing.offset += requiredDescriptors;
			usedHeap = gPreSkinDescriptorRing.heap;
			*cpuBase = cpu;
			*gpuBase = gpu;
			*increment = gPreSkinDescriptorRing.increment;
			*heapOut = usedHeap;
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			RetainPreSkinDescriptorHeapForCommandList(commandList, usedHeap);
			return true;
		}
		
		// The ring is full (or belongs to another device). Before paying for a
		// new shader-visible heap (a millisecond-scale driver GPU mapping that
		// used to fire every few seconds once the ring had doubled to its
		// million-descriptor cap), try to rewind the current heap in place or
		// adopt a retired heap. Both are safe only once no unsubmitted command
		// list still references the heap and every submission that did has been
		// consumed by the GPU - the exact conditions the retirement machinery
		// already tracks for plain release.
		ID3D12DescriptorHeap *recycledHeap = nullptr;
		UINT recycledCapacity = 0;
		std::vector<ID3D12Fence*> recycledPointFences;
		{
			const UINT minCapacity = (std::max)(
				requiredDescriptors, gPreSkinDescriptorRing.capacity);
			AcquireSRWLockExclusive(&gPreSkinRetireLock);
			if (gPreSkinDescriptorRing.heap &&
				gPreSkinDescriptorRing.device == device &&
				requiredDescriptors <= gPreSkinDescriptorRing.capacity &&
				IsPreSkinRingHeapReusableLocked(gPreSkinDescriptorRing.heap, 0))
			{
				// Rewind in place: every descriptor written so far is provably dead,
				// so the next mirror simply overwrites them from offset zero.
				recycledHeap = gPreSkinDescriptorRing.heap;
				recycledCapacity = gPreSkinDescriptorRing.capacity;
				ClearPreSkinHeapCompletionPointsLocked(
					recycledHeap, &recycledPointFences);
			}
			else
			{
				const LONG recyclePresent = DX12RuntimeState::GetPresentCount();
				for (auto it = gRetiredPreSkinDescriptorHeaps.begin();
				it != gRetiredPreSkinDescriptorHeaps.end(); ++it)
				{
					if (!it->heap)
						continue;
					const bool fenceComplete =
						it->completionFence && it->completionValue &&
						it->completionFence->GetCompletedValue() >= it->completionValue;
					const bool fallbackExpired = !it->completionFence &&
						recyclePresent - it->retirePresent >=
							PreSkinRetiredObjectPresentDelay;
					if (!fenceComplete && !fallbackExpired)
						continue;
					if (!IsPreSkinRingHeapReusableLocked(it->heap, minCapacity))
						continue;
					ID3D12Device *heapDevice = nullptr;
					if (FAILED(it->heap->GetDevice(IID_PPV_ARGS(&heapDevice))) ||
						!heapDevice)
						continue;
					const bool deviceMatches = heapDevice == device;
					heapDevice->Release();
					if (!deviceMatches)
						continue;
					recycledHeap = it->heap;
					recycledCapacity = it->heap->GetDesc().NumDescriptors;
					if (it->completionFence)
						it->completionFence->Release();
					gRetiredPreSkinDescriptorHeaps.erase(it);
					ClearPreSkinHeapCompletionPointsLocked(
						recycledHeap, &recycledPointFences);
					break;
				}
			}
			ReleaseSRWLockExclusive(&gPreSkinRetireLock);
			for (ID3D12Fence *pointFence : recycledPointFences)
			{
				if (pointFence)
					pointFence->Release();
			}
			recycledPointFences.clear();
		}
		if (recycledHeap)
		{
			if (gPreSkinDescriptorRing.heap != recycledHeap)
			{
				oldHeap = gPreSkinDescriptorRing.heap;
				oldDevice = gPreSkinDescriptorRing.device != device ?
					gPreSkinDescriptorRing.device : nullptr;
				if (oldDevice)
					gPreSkinDescriptorRing.device = nullptr;
				device->AddRef();
				gPreSkinDescriptorRing.device = device;
				gPreSkinDescriptorRing.heap = recycledHeap;
			}
			usedHeap = recycledHeap;
			gPreSkinDescriptorRing.capacity = recycledCapacity;
			gPreSkinDescriptorRing.offset = requiredDescriptors;
			gPreSkinDescriptorRing.increment =
				device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gPreSkinDescriptorRing.present = DX12RuntimeState::GetPresentCount();
			*cpuBase = usedHeap->GetCPUDescriptorHandleForHeapStart();
			*gpuBase = usedHeap->GetGPUDescriptorHandleForHeapStart();
			*increment = gPreSkinDescriptorRing.increment;
			*heapOut = usedHeap;
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			if (oldHeap)
				RetirePreSkinDescriptorHeap(oldHeap);
			if (oldDevice)
				oldDevice->Release();
			RetainPreSkinDescriptorHeapForCommandList(commandList, usedHeap);
			return true;
		}
		
		capacityToCreate = DX12DescriptorArenaUtils::NextCapacity(
			gPreSkinDescriptorRing.capacity,
			gPreSkinDescriptorRing.offset,
			requiredDescriptors,
			PreSkinDescriptorRingInitialCapacity,
			PreSkinDescriptorRingMaximumCapacity);
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
		if (!capacityToCreate)
			return false;
		
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = capacityToCreate;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ID3D12DescriptorHeap *newHeap = nullptr;
		const auto createBegin = std::chrono::steady_clock::now();
		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&newHeap));
		const auto createEnd = std::chrono::steady_clock::now();
		const auto createElapsedUs =
			std::chrono::duration_cast<std::chrono::microseconds>(createEnd - createBegin).count();
		if (FAILED(hr) || !newHeap)
			return false;
		
		AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
		const bool stillNeedsNewHeap =
			gPreSkinDescriptorRing.device != device ||
			!gPreSkinDescriptorRing.heap ||
			requiredDescriptors > gPreSkinDescriptorRing.capacity ||
			gPreSkinDescriptorRing.offset + requiredDescriptors > gPreSkinDescriptorRing.capacity;
		if (!stillNeedsNewHeap) {
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			newHeap->Release();
			continue;
		}
		oldHeap = gPreSkinDescriptorRing.heap;
		oldDevice = gPreSkinDescriptorRing.device != device ? gPreSkinDescriptorRing.device : nullptr;
		if (oldDevice)
			gPreSkinDescriptorRing.device = nullptr;
		device->AddRef();
		gPreSkinDescriptorRing.device = device;
		gPreSkinDescriptorRing.heap = newHeap;
		usedHeap = newHeap;
		gPreSkinDescriptorRing.capacity = capacityToCreate;
		gPreSkinDescriptorRing.offset = requiredDescriptors;
		gPreSkinDescriptorRing.increment =
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gPreSkinDescriptorRing.present = DX12RuntimeState::GetPresentCount();
		*cpuBase = usedHeap->GetCPUDescriptorHandleForHeapStart();
		*gpuBase = usedHeap->GetGPUDescriptorHandleForHeapStart();
		*increment = gPreSkinDescriptorRing.increment;
		*heapOut = usedHeap;
		ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
		
		if (oldHeap)
			RetirePreSkinDescriptorHeap(oldHeap);
		if (oldDevice)
			oldDevice->Release();
		RetainPreSkinDescriptorHeapForCommandList(commandList, usedHeap);
		return true;
	}
}




void ClearPreSkinRuntimeState()
{
	std::vector<ID3D12Resource*> releasePreSkinResources;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinLock, "Lock.preSkinExclusive");
	gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches.clear();
	ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
	// Confirmed match_cs sections outlive Present boundaries because DX12 command
	// list recording is not frame ordered. A configuration reload is the point at
	// which that proof becomes stale and must be discarded.
	if (!gConfirmedPreSkinSections.empty()) {
		gConfirmedPreSkinSections.clear();
		InterlockedExchange(&gHasConfirmedPreSkinSectionsFast, 0);
		BumpPreSkinActiveGeneration();
	}
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinSrvProbeCacheLock, "Lock.srvProbeCacheExclusive");
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
	AcquireSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
	gPreSkinDescriptorPatchAbortCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
	InterlockedExchange(&gPreSkinDescriptorAbortCacheHitLogCount, 0);
	gPreSkinSrvCacheGeneration.fetch_add(1, std::memory_order_relaxed);
	ReleaseSRWLockExclusive(&gPreSkinLock);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	DX12PreSkinReleaseBatch releaseBatch;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	for (auto &pending : gPendingPreSkinSubmissions)
		MovePendingPreSkinSubmissionToReleaseBatch(&pending.second, &releaseBatch);
	gPendingPreSkinSubmissions.clear();
	for (auto &retired : gRetiredPreSkinResources) {
		if (retired.resource)
			releaseBatch.resources.push_back(retired.resource);
		if (retired.completionFence)
			releaseBatch.fences.push_back(retired.completionFence);
	}
	gRetiredPreSkinResources.clear();
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			releaseBatch.descriptorHeaps.push_back(retired.heap);
		if (retired.completionFence)
			releaseBatch.fences.push_back(retired.completionFence);
	}
	gRetiredPreSkinDescriptorHeaps.clear();
	// Recycling bookkeeping dies with the same ownership boundary: every
	// retained-reference count and completion point refers to heaps whose
	// buckets were just drained above.
	gPreSkinRetainedHeapListCounts.clear();
	for (auto &points : gPreSkinHeapCompletionPoints)
	{
		for (const DX12PreSkinHeapCompletionPoint &point : points.second)
		{
			if (point.fence)
				releaseBatch.fences.push_back(point.fence);
		}
	}
	gPreSkinHeapCompletionPoints.clear();
	InterlockedExchange(&gHasRetiredPreSkinObjectsFast, 0);
	InterlockedExchange(&gHasPendingPreSkinSubmissionWorkFast, 0);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);

	AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
	ReleasePreSkinDescriptorRingLocked(&releaseBatch);
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
	DrainPreSkinDescriptorRestoreHeapPool(&releaseBatch);
	// Configuration reload is the ownership boundary for the durable resize
	// cache. Do not move this clear into DX12ModBeginFrame: consumer command lists
	// can legitimately be recorded after Present while referring to the previous
	// producer's original GPU VA.
// The draw-SRV persistent mirror cache holds dedicated descriptor heaps
// created on this device; drop them on the same ownership boundary.
DX12DrawSrvReplacementUtils::ClearDrawSrvMirrorCache();
	ClearPreSkinOutputResizeCache();
	ReleasePreSkinReleaseBatch(&releaseBatch);
}



bool ProducerMatchesReplacementTarget(
	const DX12ComputeUavProducer &producer, const DX12VertexLimitRaiseConfig &vlr)
{
	if (!producer.hasDescriptor)
		return false;
	const DX12DescriptorSummary &descriptor = producer.descriptor;
	if (descriptor.kind != "UAV" || descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	if (vlr.uavByteStride && descriptor.structureByteStride &&
		descriptor.structureByteStride != vlr.uavByteStride)
		return false;
	if (vlr.overrideByteWidth && descriptor.viewSize)
	{
		// A view that already covers the override range means the
		// creation-time resize + UAV expansion won for this buffer: the CS
		// output and the IA views both live in the expanded game buffer
		// (ExpandInPlaceVertexBufferView extends SizeInBytes without
		// redirecting). Hijacking this dispatch's output to the mod scratch
		// would split writes and reads across two buffers (CS writes the
		// scratch, draws read the expanded game buffer -> persistent
		// mismatch twitching), so leave such producers unpatched.
		if (descriptor.viewSize >= vlr.overrideByteWidth)
			return false;
		if (descriptor.viewSize + static_cast<UINT64>(vlr.overrideByteStride) * 4096 < vlr.overrideByteWidth)
			return false;
	}
	return true;
}


void SetBasePaths(const wchar_t *configPath)
{
	gConfigPath = configPath ? configPath : L"";
	gBaseDir = gConfigPath;
	if (!gBaseDir.empty()) {
		wchar_t path[MAX_PATH];
		wcsncpy_s(path, gBaseDir.c_str(), _TRUNCATE);
		PathRemoveFileSpecW(path);
		gBaseDir = path;
	}
	if (gBaseDir.empty())
		gBaseDir = L".";

	wchar_t shaderFixes[MAX_PATH];
	wcsncpy_s(shaderFixes, gBaseDir.c_str(), _TRUNCATE);
	PathAppendW(shaderFixes, L"ShaderFixes");
	gShaderFixesDir = shaderFixes;
}


bool DX12ModResolveExpandedPreSkinBufferWidth(
	ID3D12Resource *resource, UINT vertexStride, UINT64 *outputByteWidth)
{
	if (outputByteWidth)
		*outputByteWidth = 0;
	if (!resource || !outputByteWidth)
		return false;

	DX12ExpandedPreSkinBufferPrivateData info;
	UINT infoSize = sizeof(info);
	if (FAILED(resource->GetPrivateData(
			DX12ExpandedPreSkinBufferPrivateDataGuid, &infoSize, &info)) ||
	    infoSize != sizeof(info) || info.version != 1 ||
	    !info.outputByteWidth ||
	    (vertexStride && info.vertexStride && vertexStride != info.vertexStride))
		return false;

	const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
	    info.outputByteWidth > resourceDesc.Width)
		return false;

	// Private data follows the resource through frame buffering, but the INI can
	// be reloaded while that resource remains alive. Require a current VLR with
	// the same shape so stale tags cannot reactivate a removed configuration.
	bool configured = false;
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.overrideByteWidth == info.outputByteWidth &&
		    (!info.vertexStride || vlr.overrideByteStride == info.vertexStride)) {
			configured = true;
			break;
		}
	}
	ReleaseSRWLockShared(&gModLock);
	if (!configured)
		return false;

	*outputByteWidth = info.outputByteWidth;
	return true;
}


bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source)
{
	if (!resource || !desc || desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	if (gHasVertexLimitRaiseConfigs == 0)
		return false;

	UINT bytesPerElement = desc->Buffer.StructureByteStride;
	if (!bytesPerElement && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW))
		bytesPerElement = 4;
	if (!bytesPerElement)
		return false;

	D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!vlr.overrideNumElements || !vlr.uavByteStride)
			continue;
		if (bytesPerElement != vlr.uavByteStride)
			continue;
		if (desc->Buffer.NumElements >= vlr.overrideNumElements)
			continue;
		const UINT64 requiredBytes =
			(desc->Buffer.FirstElement + vlr.overrideNumElements) * bytesPerElement;
		if (resourceDesc.Width < requiredBytes)
			continue;
		if (!found || vlr.overrideNumElements > match.overrideNumElements) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

	// DX11 stores the VLR size on the resized buffer object. DX12 additionally
	// freezes the readable range in every VBV, so keep the matching VLR shape on
	// the resource itself. A section-global "latest producer" is insufficient:
	// frame generation and capture increase the number of in-flight frame
	// resources, and a later PreSkin dispatch can replace that entry before an
	// earlier command list records its IA binding.
	DX12ExpandedPreSkinBufferPrivateData info;
	info.vertexStride = match.overrideByteStride;
	info.uavStride = match.uavByteStride;
	info.outputByteWidth = match.overrideByteWidth;
	resource->SetPrivateData(
		DX12ExpandedPreSkinBufferPrivateDataGuid, sizeof(info), &info);

	desc->Buffer.NumElements = static_cast<UINT>(match.overrideNumElements);
	return true;
}


UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.viewSize)
		return descriptor.viewSize;
	if (descriptor.hasResourceDesc &&
		descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		descriptor.resourceDesc.Width > descriptor.resourceOffset)
		return descriptor.resourceDesc.Width - descriptor.resourceOffset;
	return 0;
}


void SyncPreSkinDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (runtimeState)
	{
		DX12CommandListRuntime::SetDescriptorHeaps(
			runtimeState, cbvSrvUavHeap, samplerHeap);
	}
	else
	{
		DX12CommandListRuntime::SetDescriptorHeaps(
			commandList, cbvSrvUavHeap, samplerHeap);
	}
	ID3D12DescriptorHeap *heaps[2] = {};
	UINT heapCount = 0;
	if (cbvSrvUavHeap)
		heaps[heapCount++] = cbvSrvUavHeap;
	if (samplerHeap)
		heaps[heapCount++] = samplerHeap;
	DX12BindingTracker::SetDescriptorHeaps(
		commandList, heapCount, heapCount ? heaps : nullptr);
}


void SyncPreSkinComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE table)
{
	if (runtimeState)
	{
		DX12CommandListRuntime::RememberRootDescriptorTable(
			runtimeState, false, rootParameterIndex, table);
	}
	else
	{
		DX12CommandListRuntime::RememberRootDescriptorTable(
			commandList, false, rootParameterIndex, table);
	}
	DX12BindingTracker::SetComputeRootDescriptorTable(
		commandList, rootParameterIndex, table);
}


UINT AlignCbvSize(UINT size)
{
	return (size + 255u) & ~255u;
}


bool ReadPreSkinCbvBytesLocked(
	ID3D12Resource *resource, UINT64 sourceOffset, UINT viewBytes,
	std::vector<unsigned char> *bytes, HRESULT *hrOut)
{
	if (hrOut)
		*hrOut = E_FAIL;
	if (!resource || !viewBytes || !bytes)
		return false;

	// Upload CBVs are commonly rewritten in place every frame. Resource identity
	// and GPU VA therefore do not identify their contents; caching this read made
	// later PreSkin dispatches consume the first frame's constants indefinitely.
	bytes->resize(viewBytes);
	D3D12_RANGE readRange = {
		static_cast<SIZE_T>(sourceOffset),
		static_cast<SIZE_T>(sourceOffset + viewBytes)};
	void *sourceMapped = nullptr;
	const HRESULT hr = resource->Map(0, &readRange, &sourceMapped);
	if (SUCCEEDED(hr) && sourceMapped)
	{
		memcpy(bytes->data(),
			   static_cast<const unsigned char *>(sourceMapped) + sourceOffset,
			   viewBytes);
		D3D12_RANGE writtenRange = {};
		resource->Unmap(0, &writtenRange);
	}
	if (hrOut)
		*hrOut = hr;
	if (FAILED(hr) || !sourceMapped)
		return false;
	return true;
}


// Dropping a patched buffer's last reference is a driver-side free, and on this
// machine that costs ~150-200us per resource. The cache flush used to hand ~65
// entries to Release() in one loop inside the patch path, which measured as a
// 10-13ms stall (CreateInsert maxUs 9239-12894 against a ResourceCommitted
// maxUs of 807-1170 in the same windows) - a visible hitch roughly once per
// 1.3 windows. Entries are parked here instead and dropped a few per patch call:
// the same total work, no longer arriving as a single burst. The list is bounded
// by the flush itself (arrival ~1 resource per frame, drain 2 per patch call).
static constexpr UINT kPreSkinCbvPatchReleasePerCall = 2;
static constexpr size_t kPreSkinCbvPatchPendingReleaseMax = 512;
static SRWLOCK gPreSkinCbvPatchReleaseLock = SRWLOCK_INIT;
static std::vector<ID3D12Resource *> gPreSkinCbvPatchPendingRelease;

// Hands one reference to the deferred-release list. Never release while holding
// the list lock: the final Release re-enters the wrapper registry.
static void ParkPreSkinCbvPatchRelease(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinCbvPatchReleaseLock);
	const bool overflow =
		gPreSkinCbvPatchPendingRelease.size() >= kPreSkinCbvPatchPendingReleaseMax;
	if (!overflow)
		gPreSkinCbvPatchPendingRelease.push_back(resource);
	ReleaseSRWLockExclusive(&gPreSkinCbvPatchReleaseLock);
	if (overflow)
		resource->Release();
}

static void DrainPreSkinCbvPatchRelease()
{
	for (UINT released = 0; released < kPreSkinCbvPatchReleasePerCall; ++released)
	{
		ID3D12Resource *resource = nullptr;
		AcquireSRWLockExclusive(&gPreSkinCbvPatchReleaseLock);
		if (!gPreSkinCbvPatchPendingRelease.empty())
		{
			resource = gPreSkinCbvPatchPendingRelease.back();
			gPreSkinCbvPatchPendingRelease.pop_back();
		}
		ReleaseSRWLockExclusive(&gPreSkinCbvPatchReleaseLock);
		if (!resource)
			return;
		resource->Release();
	}
}


UINT64 HashPreSkinCbvBytes(const std::vector<unsigned char> &bytes)
{
	UINT64 hash = 1469598103934665603ull;
	for (unsigned char value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash ? hash : 1;
}


void ReleasePreSkinCbvPatchCacheEntry(
	DX12PreSkinCbvPatchCacheEntry *entry)
{
	if (!entry)
		return;
	if (entry->patchedResource)
		ParkPreSkinCbvPatchRelease(entry->patchedResource);
	*entry = DX12PreSkinCbvPatchCacheEntry();
}


// Pre-skin CBV patch descriptors live in one shared, non-shader-visible
// CBV/SRV/UAV heap instead of a one-descriptor heap per cache entry. The heap
// is CPU-only - a hit copies its descriptor into the caller's heap with
// CopyDescriptorsSimple and the GPU never samples this heap - so a slot can be
// recycled as soon as the entry that owned it is gone. A CreateDescriptorHeap
// per patch cost hundreds of microseconds on the driver side.
static constexpr UINT kPreSkinCbvPatchPoolSlots = 256;

static ID3D12DescriptorHeap *gPreSkinCbvPatchPoolHeap = nullptr;
static ID3D12Device *gPreSkinCbvPatchPoolDevice = nullptr;
static UINT gPreSkinCbvPatchPoolIncrement = 0;
static UINT gPreSkinCbvPatchPoolUsed = 0;

// Caller holds the mod lock (every caller of the patch cache does).
static D3D12_CPU_DESCRIPTOR_HANDLE AcquirePreSkinCbvPatchDescriptor(
	ID3D12Device *device)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
	if (!device)
		return handle;
	if (gPreSkinCbvPatchPoolHeap && gPreSkinCbvPatchPoolDevice != device)
	{
		gPreSkinCbvPatchPoolHeap->Release();
		gPreSkinCbvPatchPoolHeap = nullptr;
		gPreSkinCbvPatchPoolDevice = nullptr;
		gPreSkinCbvPatchPoolUsed = 0;
	}
	if (!gPreSkinCbvPatchPoolHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC poolDesc = {};
		poolDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		poolDesc.NumDescriptors = kPreSkinCbvPatchPoolSlots;
		poolDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(device->CreateDescriptorHeap(
				&poolDesc, IID_PPV_ARGS(&gPreSkinCbvPatchPoolHeap))) ||
			!gPreSkinCbvPatchPoolHeap)
		{
			gPreSkinCbvPatchPoolHeap = nullptr;
			return handle;
		}
		gPreSkinCbvPatchPoolDevice = device;
		gPreSkinCbvPatchPoolIncrement = device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	if (gPreSkinCbvPatchPoolUsed >= kPreSkinCbvPatchPoolSlots)
		return handle;
	handle = gPreSkinCbvPatchPoolHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(gPreSkinCbvPatchPoolUsed) *
		gPreSkinCbvPatchPoolIncrement;
	++gPreSkinCbvPatchPoolUsed;
	return handle;
}


void ClearPreSkinCbvPatchCache()
{
	for (auto &item : gPreSkinCbvPatchCache)
		ReleasePreSkinCbvPatchCacheEntry(&item.second);
	gPreSkinCbvPatchCache.clear();
	// No entry is left, so every pooled patch descriptor slot is free again.
	gPreSkinCbvPatchPoolUsed = 0;
}


// Re-resolves the resource backing a compute binding from its GPU VA. The
// pre-skin binding caches are validated by content-addressed descriptor value
// IDs which do not capture the resource LIFETIME: after a scene switch the
// game can destroy a buffer and recreate it at the same VA (pooled
// allocations, COM-address reuse), leaving a cached binding with a dangling
// resource pointer while every value ID - and therefore every cache - still
// matches. Dereferencing that stale pointer (Map/GetDesc/GetGPUVirtualAddress)
// is undefined behaviour that corrupts the vertex-count patch and the output
// resize. Resolving by the resource VA always yields the current allocation
// at that address; the embedded pointer is used only when the VA is unknown
// or the tracker cannot resolve it.
static ID3D12Resource *ResolvePreSkinBindingResource(
	const DX12DescriptorSummary &descriptor)
{
	ID3D12Resource *resource = descriptor.resource;
	const UINT64 gpuVa = descriptor.gpuVirtualAddress;
	if (!resource || !gpuVa)
		return resource;
	DX12BufferResourceSummary resolved;
	if (DX12ResolveBufferResourceByGpuVa(
			gpuVa, descriptor.viewSize ? descriptor.viewSize : 1,
			&resolved) &&
		resolved.resource)
		return resolved.resource;
	return resource;
}


bool PatchPreSkinVertexCountCbvLocked(
	ID3D12Device *device, const DX12CurrentComputeUavBinding &cbv,
	UINT expectedOriginalVertexCount, UINT overrideVertexCount,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	std::vector<ID3D12Resource *> *temporaryResources,
	UINT *originalVertexCountOut,
	UINT *overrideVertexCountOut)
{
	// Drop a couple of the buffers the last cache flush parked, so their
	// driver-side free is spread over the calls that follow it instead of
	// landing as one multi-millisecond burst inside the create scope below.
	DrainPreSkinCbvPatchRelease();
	if (!device || !overrideVertexCount || !dst.ptr || !temporaryResources)
		return false;
	if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV")
		return false;
	// The cached binding can embed a resource that was freed and recreated at
	// the same VA after a scene switch; always read through the current
	// allocation at the descriptor's GPU VA (see ResolvePreSkinBindingResource).
	ID3D12Resource *cbvResource = ResolvePreSkinBindingResource(cbv.descriptor);
	if (!cbvResource)
		return false;

	const UINT64 sourceOffset = cbv.descriptor.resourceOffset;
	UINT viewBytes = cbv.descriptor.cbvSize;
	if (!viewBytes && cbv.descriptor.viewSize)
		viewBytes = static_cast<UINT>((std::min<UINT64>)(cbv.descriptor.viewSize, UINT_MAX));
	if (!viewBytes)
		viewBytes = 256;
	viewBytes = AlignCbvSize(viewBytes);

	std::vector<unsigned char> bytes;
	std::vector<UINT> patchOffsets;
	UINT32 originalVertexCount = 0;
	UINT64 patchedContentHash = 0;
	HRESULT hr = E_FAIL;
	{
		// Split of the patch cost for the TIMING logs: this read + scan + hash
		// runs on every call (the cache key embeds the patched content), while
		// the object creation further down only runs on a cache miss.
		DX12_PROF_STAGE_SCOPE(PreSkinCbvReadPatchHash);
		if (!ReadPreSkinCbvBytesLocked(cbvResource, sourceOffset, viewBytes, &bytes, &hr))
			return false;

		patchOffsets.reserve(bytes.size() / sizeof(UINT32));
		if (expectedOriginalVertexCount)
		{
			for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32))
			{
				UINT32 candidate = 0;
				memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
				if (candidate == expectedOriginalVertexCount)
					patchOffsets.push_back(offset);
			}
			if (!patchOffsets.empty())
				originalVertexCount = expectedOriginalVertexCount;
		}
		else if (bytes.size() >= sizeof(UINT32))
		{
			memcpy(&originalVertexCount, bytes.data(), sizeof(originalVertexCount));
			if (originalVertexCount && originalVertexCount != overrideVertexCount)
				patchOffsets.push_back(0);
		}
		if (patchOffsets.empty())
			return false;
		for (UINT offset : patchOffsets)
			memcpy(bytes.data() + offset, &overrideVertexCount, sizeof(overrideVertexCount));
		patchedContentHash = HashPreSkinCbvBytes(bytes);
	}

	// Patched CBV Buffer
	// UPLOAD buffer + CBV descriptor mod
	// Map/Write/Unmap CPU-only CreateCommittedResource
	{
		DX12_PROF_STAGE_SCOPE(PreSkinCbvCacheLookup);
		DX12PreSkinCbvPatchCacheKey patchKey;
		patchKey.resource = cbvResource;
		patchKey.sourceOffset = sourceOffset;
		patchKey.viewBytes = viewBytes;
		patchKey.overrideVertexCount = overrideVertexCount;
		patchKey.contentHash = patchedContentHash;

		UINT64 reloadGen = DX12ModGetReloadGeneration();
		if (gPreSkinCbvPatchCacheGeneration != reloadGen)
		{
			ClearPreSkinCbvPatchCache();
			gPreSkinCbvPatchCacheGeneration = reloadGen;
		}

		auto cached = gPreSkinCbvPatchCache.find(patchKey);
		if (cached != gPreSkinCbvPatchCache.end() &&
			cached->second.patchedResource && cached->second.cbvCpu.ptr &&
			cached->second.generation == reloadGen &&
			cached->second.patchedBytes == bytes)
		{
			// D3D12 upload memory is read by the GPU at execution time. Rewriting a
			// cached CBV while an earlier command list is in flight lets that earlier
			// PreSkin dispatch observe a later frame's constants. FG and capture make
			// the race much easier to hit by increasing queue depth. The read cache
			// content hash and exact byte comparison guarantee this key uses the
			// same snapshot, so keep the allocation immutable and copy its descriptor.
			device->CopyDescriptorsSimple(1, dst, cached->second.cbvCpu,
										  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			temporaryResources->push_back(cached->second.patchedResource);
			cached->second.patchedResource->AddRef();
			if (originalVertexCountOut)
				*originalVertexCountOut = originalVertexCount;
			if (overrideVertexCountOut)
				*overrideVertexCountOut = overrideVertexCount;
			return true;
		}
	}

	ID3D12Resource *patchedResource = nullptr;
	{
		// Costs only a cache miss. TIMING logs pair this with
		// PreSkinCbvReadPatchHash / PreSkinCbvCacheLookup to show where the
		// ~500us per patch actually goes.
		DX12_PROF_STAGE_SCOPE(PreSkinCbvCreateInsert);
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = viewBytes;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		hr = device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&patchedResource));
		if (FAILED(hr) || !patchedResource)
			return false;

		void *patchedMapped = nullptr;
		D3D12_RANGE patchedReadRange = {};
		hr = patchedResource->Map(0, &patchedReadRange, &patchedMapped);
		if (FAILED(hr) || !patchedMapped)
		{
			patchedResource->Release();
			patchedResource = nullptr;
			return false;
		}
		memcpy(patchedMapped, bytes.data(), viewBytes);
		patchedResource->Unmap(0, nullptr);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = patchedResource->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = viewBytes;
		device->CreateConstantBufferView(&cbvDesc, dst);

		// Clear before taking a pooled slot: the clear releases every slot, so
		// the descriptor acquired below cannot alias a surviving entry's.
		if (gPreSkinCbvPatchCache.size() > 64)
			ClearPreSkinCbvPatchCache();
		D3D12_CPU_DESCRIPTOR_HANDLE cachedCpu =
			AcquirePreSkinCbvPatchDescriptor(device);
		if (cachedCpu.ptr)
		{
			device->CreateConstantBufferView(&cbvDesc, cachedCpu);
			DX12PreSkinCbvPatchCacheEntry entry;
			entry.patchedResource = patchedResource;
			entry.cbvCpu = cachedCpu;
			entry.generation = DX12ModGetReloadGeneration();
			entry.patchedBytes = bytes;
			patchedResource->AddRef();

			DX12PreSkinCbvPatchCacheKey patchKey;
			// Must match the lookup key exactly: that one is built from the
			// resolved resource, so storing the unresolved descriptor resource
			// here made every entry unreachable whenever resolution found a
			// different backing resource - which turned the patch cache into a
			// permanent miss and created a buffer + descriptor on every call.
			patchKey.resource = cbvResource;
			patchKey.sourceOffset = sourceOffset;
			patchKey.viewBytes = viewBytes;
			patchKey.overrideVertexCount = overrideVertexCount;
			patchKey.contentHash = patchedContentHash;
			auto old = gPreSkinCbvPatchCache.find(patchKey);
			if (old != gPreSkinCbvPatchCache.end())
			{
				ReleasePreSkinCbvPatchCacheEntry(&old->second);
				gPreSkinCbvPatchCache.erase(old);
			}
			gPreSkinCbvPatchCache[patchKey] = entry;
		}
		// A failed pool acquire just skips caching: `dst` already holds the
		// patched CBV written above, so this call still returns a valid patch.
	}

	temporaryResources->push_back(patchedResource);
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;

	return true;
}


uint32_t HashComputeBufferBinding(const DX12CurrentComputeUavBinding &binding)
{
	if (!binding.hasDescriptor || !binding.descriptor.resource)
		return 0;
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	const bool bufferView =
		(descriptor.kind == "UAV" &&
		 descriptor.viewDimension == D3D12_UAV_DIMENSION_BUFFER) ||
		(descriptor.kind == "SRV" &&
		 descriptor.viewDimension == D3D12_SRV_DIMENSION_BUFFER);
	if (!bufferView)
		return 0;

	UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress + descriptor.resourceOffset;
	if (!fallbackGpuVa)
		fallbackGpuVa = binding.gpuVirtualAddress;
	const UINT64 fallbackSize = DescriptorViewSize(descriptor);
	if (!fallbackGpuVa && !fallbackSize)
		return 0;

	return DX12HashDescriptorBufferView(&descriptor, fallbackGpuVa, fallbackSize);
}


uint32_t HashComputeUavBinding(const DX12CurrentComputeUavBinding &uav)
{
	if (!uav.hasDescriptor || uav.descriptor.kind != "UAV" ||
		uav.descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return 0;
	return HashComputeBufferBinding(uav);
}



void AppendComputeBindingHashList(
	const std::vector<DX12CurrentComputeUavBinding> &bindings,
	bool srvs, char *text, size_t textSize)
{
	if (!text || textSize == 0)
		return;
	text[0] = '\0';
	size_t used = 0;
	const size_t maxItems = srvs ? 8 : 6;
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i)
	{
		const DX12CurrentComputeUavBinding &binding = bindings[i];
		const uint32_t hash = srvs ? HashComputeBufferBinding(binding) : HashComputeUavBinding(binding);
		const UINT64 bytes = DescriptorViewSize(binding.descriptor);
		int written = sprintf_s(
			text + used, textSize - used,
			"%s%u/%u/%u/%u:%08x:%llu:%p",
			used ? ";" : "", binding.rootParameterIndex,
			binding.rangeIndex, binding.shaderRegister,
			binding.descriptorOffset, hash,
			static_cast<unsigned long long>(bytes),
			binding.descriptor.resource);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= textSize - 1)
			break;
	}
}


uint64_t HashComputeBindingListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &bindings, bool srvs)
{
	uint64_t key = srvs ? 0x91f5e20a7d1f0c3bull : 0x4db13be47c2d6a91ull;
	const size_t maxItems = srvs ? 8 : 6;
	for (size_t i = 0; i < bindings.size() && i < maxItems; ++i)
	{
		const DX12CurrentComputeUavBinding &binding = bindings[i];
		key = HashCombine64(key, binding.rootParameterIndex);
		key = HashCombine64(key, binding.rangeIndex);
		key = HashCombine64(key, binding.shaderRegister);
		key = HashCombine64(key, binding.registerSpace);
		key = HashCombine64(key, binding.descriptorOffset);
		key = HashCombine64(key, srvs ? HashComputeBufferBinding(binding) : HashComputeUavBinding(binding));
		key = HashCombine64(key, DescriptorViewSize(binding.descriptor));
	}
	return key;
}


bool CanPatchPreSkinVertexCountCbvLocked(
	const DX12CurrentComputeUavBinding &cbv,
	UINT expectedOriginalVertexCount)
{
	if (!expectedOriginalVertexCount)
		return true;
	if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV")
		return false;
	// Read through the current allocation at the descriptor's GPU VA; the
	// cached binding may embed a resource freed and recreated at the same VA
	// after a scene switch (see ResolvePreSkinBindingResource).
	ID3D12Resource *cbvResource = ResolvePreSkinBindingResource(cbv.descriptor);
	if (!cbvResource)
		return false;

	const UINT64 sourceOffset = cbv.descriptor.resourceOffset;
	UINT viewBytes = cbv.descriptor.cbvSize;
	if (!viewBytes && cbv.descriptor.viewSize)
		viewBytes = static_cast<UINT>((std::min<UINT64>)(cbv.descriptor.viewSize, UINT_MAX));
	if (!viewBytes)
		viewBytes = 256;
	viewBytes = AlignCbvSize(viewBytes);

	std::vector<unsigned char> bytes;
	HRESULT hr = E_FAIL;
	if (!ReadPreSkinCbvBytesLocked(cbvResource, sourceOffset, viewBytes, &bytes, &hr))
		return false;

	for (UINT offset = 0; offset + sizeof(UINT32) <= bytes.size(); offset += sizeof(UINT32))
	{
		UINT32 candidate = 0;
		memcpy(&candidate, bytes.data() + offset, sizeof(candidate));
		if (candidate == expectedOriginalVertexCount)
			return true;
	}
	return false;
}

uint64_t MakePreSkinMatchCsInputCacheKey(
	uint64_t csHash, const DX12PreSkinTextureOverrideCandidate &candidate,
	UINT64 uavViewBytes,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	uint64_t key = HashCombine64(gReloadGeneration, csHash);
	key = HashCombine64(key, candidate.identity);
	key = HashCombine64(key, uavViewBytes);
	// match_cs_t0_hash style input matching only observes the SRV bindings
	// for the configured registers. Hash just those instead of the full
	// binding set: games rewrite unrelated descriptor slots every frame, and
	// a full-binding key invalidates this cache even when the matching SRV
	// inputs are unchanged (observed 42/43 candidate dispatches re-probing
	// every frame). Fall back to the full binding hash when a candidate has
	// no SRV-hash inputs (the pre-skin path always has them, this is
	// defensive for the shared log-dedup call site).
	if (candidate.config && !candidate.config->preSkinMatchCsSrvHashes.empty())
		key = HashCombine64(key, HashPreSkinMatchSrvKeysForProbe(srvs, *candidate.config));
	else {
		key = HashCombine64(key, HashComputeBindingListForProbe(uavs, false));
		key = HashCombine64(key, HashComputeBindingListForProbe(srvs, true));
		key = HashCombine64(key, HashComputeCbvListForProbe(cbvs));
		key = HashCombine64(key, HashComputeRootConstantsForProbe(rootConstants));
	}
	return key;
}


uint64_t HashComputeCbvListForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs)
{
	uint64_t key = 0xa86f31b9dd3a7c55ull;
	const size_t maxItems = 8;
	for (size_t i = 0; i < cbvs.size() && i < maxItems; ++i)
	{
		const DX12CurrentComputeUavBinding &cbv = cbvs[i];
		key = HashCombine64(key, cbv.rootParameterIndex);
		key = HashCombine64(key, cbv.rangeIndex);
		key = HashCombine64(key, cbv.shaderRegister);
		key = HashCombine64(key, cbv.registerSpace);
		key = HashCombine64(key, cbv.descriptorOffset);
		key = HashCombine64(key, DescriptorViewSize(cbv.descriptor));
		key = HashCombine64(key, cbv.descriptor.resourceOffset);
		key = HashCombine64(key, cbv.descriptor.cbvSize);
	}
	return key;
}


uint64_t HashComputeRootConstantsForProbe(
	const std::vector<DX12CurrentRootConstants> &rootConstants)
{
	uint64_t key = 0x53df92ecf49b31a7ull;
	const size_t maxRoots = 4;
	for (size_t i = 0; i < rootConstants.size() && i < maxRoots; ++i)
	{
		const DX12CurrentRootConstants &root = rootConstants[i];
		key = HashCombine64(key, root.rootParameterIndex);
		for (UINT j = 0; j < 64; ++j)
		{
			if (!root.valueValid[j])
				continue;
			key = HashCombine64(key, j);
			key = HashCombine64(key, root.values[j]);
		}
	}
	return key;
}


bool FindComputeSrvByRegister(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	uint32_t shaderRegister,
	const DX12CurrentComputeUavBinding **bindingOut,
	DX12PreSkinSrvHashResult *hashOut)
{
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;
		if (srv.shaderRegister != shaderRegister)
			continue;
		DX12PreSkinSrvHashResult hash = {};
		hash.descriptorHash = HashComputeBufferBinding(srv);
		hash.sourceOffset = srv.descriptor.resourceOffset ? srv.descriptor.resourceOffset : srv.descriptor.bufferViewOffset;
		hash.viewBytes = DescriptorViewSize(srv.descriptor);
		if (!hash.viewBytes && srv.descriptor.bufferViewBytes)
			hash.viewBytes = srv.descriptor.bufferViewBytes;
		hash.stride = srv.descriptor.structureByteStride;
		if (bindingOut)
			*bindingOut = &srv;
		if (hashOut)
			*hashOut = hash;
		return true;
	}
	return false;
}


uint64_t HashPreSkinMatchSrvKeysForProbe(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config)
{
	// Key on the exact bindings MatchComputeInputSrvsTextureOverrideLocked
	// observes: the SRV at each configured register. A missing binding makes
	// the match fail, so that must invalidate the key too. sourceOffset /
	// viewBytes / stride are the same fields the hash probe reports; they are
	// subsumed by descriptorHash but kept as a cheap collision guard.
	uint64_t key = 0x5a3d9f21c7e4b608ull;
	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		key = HashCombine64(key, expected.first);
		key = HashCombine64(key, expected.second);
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
		{
			key = HashCombine64(key, 0xffffffffu);
			continue;
		}
		key = HashCombine64(key, actual.descriptorHash);
		key = HashCombine64(key, actual.sourceOffset);
		key = HashCombine64(key, actual.viewBytes);
		key = HashCombine64(key, actual.stride);
	}
	return key;
}


bool MatchComputeInputSrvsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config)
{
	if (config.preSkinMatchCsSrvHashes.empty())
		return false;
	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
		{
						return false;
		}
		const bool matched = actual.descriptorHash == expected.second;
				if (!matched)
			return false;
	}
	return true;
}


// Populate the SRV replacement set straight from the config's cs-tN table,
// without probing live SRV content hashes. The sticky-match path uses this:
// the hash probe is exactly what flickers across frames (async GPU readback
// hashing), and re-checking it there would reintroduce the dropped-patch
// frames the sticky entry exists to eliminate.
bool BuildPreSkinSrvReplacementsFromConfigLocked(
	const Bunny::TextureOverrideConfig &config,
	std::vector<DX12PreSkinSrvReplacementBinding> *replacements)
{
	if (!replacements)
		return false;
	replacements->clear();
	if (config.preSkinCsSrvResources.empty())
		return false;
	for (const auto &replacement : config.preSkinCsSrvResources)
	{
		DX12PreSkinSrvReplacementBinding binding;
		binding.replaceSlot = replacement.first;
		binding.shaderRegister = replacement.first;
		binding.resource = replacement.second;
		replacements->push_back(std::move(binding));
	}
	return !replacements->empty();
}


bool BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config,
	std::vector<DX12PreSkinSrvReplacementBinding> *replacements)
{
	if (!replacements)
		return false;
	replacements->clear();
	if (config.preSkinMatchCsSrvHashes.empty() ||
		config.preSkinCsSrvResources.empty())
		return false;

	for (const auto &expected : config.preSkinMatchCsSrvHashes)
	{
		const DX12CurrentComputeUavBinding *binding = nullptr;
		DX12PreSkinSrvHashResult actual = {};
		if (!FindComputeSrvByRegister(srvs, expected.first, &binding, &actual))
			return false;
		const bool matched = actual.descriptorHash == expected.second;
				if (!matched)
			return false;
	}

	return BuildPreSkinSrvReplacementsFromConfigLocked(config, replacements);
}


bool FindMatchCsTextureOverrideLocked(
	uint64_t csHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	DX12UavHashTextureOverrideMatch *match)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	if (indexed == gPreSkinMatchCsTextureOverrideIndex.end())
		return false;
	static thread_local UINT64 tlsInputCacheReloadGeneration = 0;
	static thread_local std::unordered_map<uint64_t, bool> tlsInputCache;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (tlsInputCacheReloadGeneration != reloadGeneration)
	{
		tlsInputCache.clear();
		tlsInputCacheReloadGeneration = reloadGeneration;
	}

	for (size_t index : indexed->second)
	{
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || !IsComputePreSkinTextureOverride(*config) ||
			GetPreSkinCsHash(*config) != csHash)
			continue;
		const uint64_t inputKey = MakePreSkinMatchCsInputCacheKey(
			csHash, candidate, 0, uavs, srvs, cbvs, rootConstants);
		bool inputMatched = false;
		auto cachedInput = tlsInputCache.find(inputKey);
		if (cachedInput != tlsInputCache.end())
		{
			inputMatched = cachedInput->second;
		}
		else
		{
			inputMatched = MatchComputeInputSrvsTextureOverrideLocked(srvs, *config);
			if (tlsInputCache.size() > 4096)
				tlsInputCache.clear();
			tlsInputCache[inputKey] = inputMatched;
		}
		if (!inputMatched)
			continue;
		if (match)
		{
			match->config = config;
			match->hash = candidate.hash;
			BuildPreSkinSrvReplacementBindingsTextureOverrideLocked(
				srvs, *config, &match->srvReplacements);
		}
		return true;
	}
	return false;
}


bool HasMatchCsTextureOverrideLocked(uint64_t csHash)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	return indexed != gPreSkinMatchCsTextureOverrideIndex.end() &&
		   !indexed->second.empty();
}


bool IsPreSkinMatchCsUavCandidate(const DX12CurrentComputeUavBinding &binding)
{
	if (!binding.hasDescriptor || binding.rootDescriptor || !binding.descriptorIncrement ||
		!binding.tableCpuStart || !binding.tableGpuStart.ptr)
		return false;
	if (!binding.descriptor.resource || binding.descriptor.viewSize == 0)
		return false;
	if (binding.descriptor.kind != "UAV" ||
		binding.descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	return true;
}


size_t FindPreSkinTextureOverrideCandidateIndex(
	const Bunny::TextureOverrideConfig *config)
{
	if (!config)
		return static_cast<size_t>(-1);
	for (size_t i = 0; i < gPreSkinTextureOverrides.size(); ++i)
	{
		if (gPreSkinTextureOverrides[i].config == config)
			return i;
	}
	return static_cast<size_t>(-1);
}


void ClearPreSkinStickyMatchCache()
{
	for (size_t i = 0; i < kPreSkinStickyMatchSlots; ++i)
		InterlockedExchange(&gPreSkinStickyMatch[i].occupied, 0);
}


size_t PreSkinStickyMatchSlot(uint64_t csHash, UINT64 uavKey)
{
	uint64_t hash = HashCombine64(csHash, uavKey);
	hash ^= hash >> 33;
	hash *= 0xff51afd7ed558ccdull;
	hash ^= hash >> 33;
	return static_cast<size_t>(hash) & (kPreSkinStickyMatchSlots - 1);
}


UINT64 MakePreSkinStickyMatchUavKey(
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
		if (!IsPreSkinMatchCsUavCandidate(*it))
			continue;
		const DX12CurrentComputeUavBinding &uav = *it;
		UINT64 key = uav.rootParameterIndex;
		key = HashCombine64(key, uav.rangeIndex);
		key = HashCombine64(key, uav.shaderRegister);
		key = HashCombine64(key, uav.registerSpace);
		key = HashCombine64(key, uav.descriptorOffset);
		key = HashCombine64(key, reinterpret_cast<uintptr_t>(uav.descriptor.resource));
		key = HashCombine64(key, DescriptorViewSize(uav.descriptor));
		key = HashCombine64(key, uav.descriptor.structureByteStride);
		return key;
	}
	return 0;
}


void StorePreSkinStickyMatch(
	uint64_t csHash, UINT64 uavKey, size_t candidateIndex, uint32_t hash)
{
	if (!csHash || !uavKey || candidateIndex >= gPreSkinTextureOverrides.size())
		return;
	DX12PreSkinStickyMatchEntry &entry =
		gPreSkinStickyMatch[PreSkinStickyMatchSlot(csHash, uavKey)];
	entry.reloadGeneration = DX12ModGetReloadGenerationFast();
	entry.uavKey = uavKey;
	entry.csHash = csHash;
	entry.candidateIndex = candidateIndex;
	entry.hash = hash;
	entry.present = DX12RuntimeState::GetPresentCount();
	InterlockedExchange(&entry.occupied, 1);
}


bool TryLoadPreSkinStickyMatch(
	uint64_t csHash, const std::vector<DX12CurrentComputeUavBinding> &uavs,
	DX12UavHashTextureOverrideMatch *match)
{
	const UINT64 uavKey = MakePreSkinStickyMatchUavKey(uavs);
	if (!uavKey)
		return false;
	DX12PreSkinStickyMatchEntry &entry =
		gPreSkinStickyMatch[PreSkinStickyMatchSlot(csHash, uavKey)];
	if (InterlockedCompareExchange(&entry.occupied, 0, 0) == 0)
		return false;
	if (entry.reloadGeneration != DX12ModGetReloadGenerationFast() ||
		entry.csHash != csHash || entry.uavKey != uavKey)
		return false;
	if (DX12RuntimeState::GetPresentCount() - entry.present >
		kPreSkinStickyMatchFrames)
		return false;
	if (entry.candidateIndex >= gPreSkinTextureOverrides.size())
		return false;
	const DX12PreSkinTextureOverrideCandidate &candidate =
		gPreSkinTextureOverrides[entry.candidateIndex];
	const Bunny::TextureOverrideConfig *config = candidate.config;
	if (!config || !IsComputePreSkinTextureOverride(*config) ||
		GetPreSkinCsHash(*config) != csHash)
		return false;
	if (match)
	{
		match->config = config;
		match->hash = entry.hash;
		match->resourceName = candidate.resourceName;
		// A sticky hit must carry the replacement set like a fresh hit does:
		// without it the apply below silently runs the original dispatch with
		// the mod output buffer untouched, and the draw side reads the stale
		// pose from the previous frames (new/old pose ping-pong = twitching).
		// Build from the config directly instead of re-running the SRV hash
		// probe - that probe is exactly the flicker source this entry rides
		// over.
		BuildPreSkinSrvReplacementsFromConfigLocked(
			*config, &match->srvReplacements);
	}
	return true;
}


// Lock-free prefix of TryLoadPreSkinStickyMatch: true only when a sticky
// entry could pass the cheap checks (occupied, reload generation, CS hash,
// UAV key, freshness). The geometry-keyed negative cache is honored only
// when this returns false, so a cached negative can never suppress a
// sticky-driven positive; the candidate validation itself stays under the
// mod lock in TryLoadPreSkinStickyMatch. A false positive here only costs
// one conservative full probe.
bool MayMatchPreSkinStickyMatch(
	uint64_t csHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	const UINT64 uavKey = MakePreSkinStickyMatchUavKey(uavs);
	if (!uavKey)
		return false;
	DX12PreSkinStickyMatchEntry &entry =
		gPreSkinStickyMatch[PreSkinStickyMatchSlot(csHash, uavKey)];
	if (InterlockedCompareExchange(&entry.occupied, 0, 0) == 0)
		return false;
	if (entry.reloadGeneration != DX12ModGetReloadGenerationFast() ||
		entry.csHash != csHash || entry.uavKey != uavKey)
		return false;
	if (DX12RuntimeState::GetPresentCount() - entry.present >
		kPreSkinStickyMatchFrames)
		return false;
	return true;
}


bool FindPreSkinProducerByMatchCsLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants, uint64_t csHash,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match)
{
	DX12UavHashTextureOverrideMatch candidateMatch = {};
	const bool freshMatch = FindMatchCsTextureOverrideLocked(
			csHash, uavs, srvs, cbvs, rootConstants, &candidateMatch);
	if (!freshMatch && !TryLoadPreSkinStickyMatch(csHash, uavs, &candidateMatch))
		return false;

	const DX12CurrentComputeUavBinding *chosenBinding = nullptr;
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
		if (IsPreSkinMatchCsUavCandidate(*it))
		{
			chosenBinding = &(*it);
			break;
		}
	}
	if (!chosenBinding)
	{
				return false;
	}

	DX12ComputeUavProducer candidate = {};
	candidate.rootParameterIndex = chosenBinding->rootParameterIndex;
	candidate.rangeIndex = chosenBinding->rangeIndex;
	candidate.shaderRegister = chosenBinding->shaderRegister;
	candidate.registerSpace = chosenBinding->registerSpace;
	candidate.descriptorOffset = chosenBinding->descriptorOffset;
	candidate.rootDescriptor = chosenBinding->rootDescriptor;
	candidate.gpuVirtualAddress = chosenBinding->gpuVirtualAddress;
	candidate.hasDescriptor = chosenBinding->hasDescriptor;
	candidate.descriptor = chosenBinding->descriptor;
	if (producer)
		*producer = candidate;
	if (binding)
		*binding = *chosenBinding;
	if (match)
		*match = candidateMatch;
	// Remember the positive match for sticky hysteresis so the replacement
	// stays applied when the SRV hash probe flickers across frames.
	StorePreSkinStickyMatch(
		csHash,
		MakePreSkinStickyMatchUavKey(uavs),
		FindPreSkinTextureOverrideCandidateIndex(candidateMatch.config),
		candidateMatch.hash);
	return true;
}


UINT FindPreSkinUavElementStrideLocked(
	const DX12LoadedResource &resource,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs)
	{
		if (vlr.hasMatchCs &&
			ResourceBlockedByInactiveExplicitMatchCsLocked(resource.name, activePreSkinSections))
			continue;
		if (!ResourceMatchesVlrTarget(resource, vlr))
			continue;
		if (vlr.uavByteStride)
			return vlr.uavByteStride;
		if (vlr.overrideByteStride)
			return vlr.overrideByteStride;
	}
	return resource.stride ? resource.stride : 4;
}


bool FindPreSkinProducerLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	uint64_t computeShaderHash,
	DX12ComputeUavProducer *producer,
	DX12VertexLimitRaiseConfig *vlr)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it)
	{
		if (!it->hasDescriptor || it->rootDescriptor || !it->descriptorIncrement ||
			!it->tableCpuStart || !it->tableGpuStart.ptr)
			continue;
		if (!it->descriptor.resource || it->descriptor.viewSize == 0)
			continue;
		DX12ComputeUavProducer candidate = {};
		candidate.rootParameterIndex = it->rootParameterIndex;
		candidate.rangeIndex = it->rangeIndex;
		candidate.shaderRegister = it->shaderRegister;
		candidate.registerSpace = it->registerSpace;
		candidate.descriptorOffset = it->descriptorOffset;
		candidate.rootDescriptor = it->rootDescriptor;
		candidate.gpuVirtualAddress = it->gpuVirtualAddress;
		candidate.hasDescriptor = it->hasDescriptor;
		candidate.descriptor = it->descriptor;
		DX12VertexLimitRaiseConfig candidateVlr;
		if (!ProducerMatchesAnyVlr(candidate, computeShaderHash, &candidateVlr))
			continue;
		if (producer)
			*producer = candidate;
		if (vlr)
			*vlr = candidateVlr;
		return true;
	}
	return false;
}


void ClearPreSkinPersistentDescriptorPatchCacheLocked()
{
	for (auto &item : gPreSkinPersistentDescriptorPatchCache)
		item.second.Clear();
	gPreSkinPersistentDescriptorPatchCache.clear();
}


bool TryGetPersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	size_t tableCount,
	ID3D12DescriptorHeap **heapOut,
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> *patchedTablesOut)
{
	if (heapOut)
		*heapOut = nullptr;
	if (patchedTablesOut)
		patchedTablesOut->clear();
	if (!bindingFingerprint || !heapOut || !patchedTablesOut)
		return false;

	AcquireSRWLockShared(&gPreSkinPersistentDescriptorPatchLock);
	const bool generationMatches =
		gPreSkinPersistentDescriptorPatchCacheGeneration == reloadGeneration;
	bool found = false;
	if (generationMatches)
	{
		DX12PreSkinPersistentDescriptorPatchKey key;
		key.reloadGeneration = reloadGeneration;
		key.bindingFingerprint = bindingFingerprint;
		key.triggerHash = triggerHash;
		auto it = gPreSkinPersistentDescriptorPatchCache.find(key);
		if (it != gPreSkinPersistentDescriptorPatchCache.end() &&
			it->second.heap &&
			it->second.patchedTables.size() == tableCount)
		{
			it->second.heap->AddRef();
			*heapOut = it->second.heap;
			*patchedTablesOut = it->second.patchedTables;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gPreSkinPersistentDescriptorPatchLock);
	if (found)
		return true;

	if (!generationMatches)
	{
		AcquireSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
		if (gPreSkinPersistentDescriptorPatchCacheGeneration != reloadGeneration)
		{
			ClearPreSkinPersistentDescriptorPatchCacheLocked();
			gPreSkinPersistentDescriptorPatchCacheGeneration = reloadGeneration;
		}
		ReleaseSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
	}
	return false;
}


void StorePersistentPreSkinDescriptorPatchLocked(
	UINT64 reloadGeneration,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	ID3D12DescriptorHeap *heap,
	const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> &patchedTables)
{
	if (!heap || !bindingFingerprint || patchedTables.empty())
		return;

	AcquireSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
	if (gPreSkinPersistentDescriptorPatchCacheGeneration != reloadGeneration)
	{
		ClearPreSkinPersistentDescriptorPatchCacheLocked();
		gPreSkinPersistentDescriptorPatchCacheGeneration = reloadGeneration;
	}
	if (gPreSkinPersistentDescriptorPatchCache.size() >=
		MaxPreSkinPersistentDescriptorPatchEntries)
	{
		ClearPreSkinPersistentDescriptorPatchCacheLocked();
	}
	DX12PreSkinPersistentDescriptorPatchKey key;
	key.reloadGeneration = reloadGeneration;
	key.bindingFingerprint = bindingFingerprint;
	key.triggerHash = triggerHash;
	DX12PreSkinPersistentDescriptorPatchEntry &entry =
		gPreSkinPersistentDescriptorPatchCache[key];
	entry.Clear();
	heap->AddRef();
	entry.heap = heap;
	entry.patchedTables = patchedTables;
	ReleaseSRWLockExclusive(&gPreSkinPersistentDescriptorPatchLock);
}


bool TryReuseDeferredPreSkinDescriptorTable(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingFingerprint,
	uint32_t triggerHash,
	bool *hasPendingState)
{
	if (hasPendingState)
		*hasPendingState = false;
	if (!commandList || !bindingFingerprint)
		return false;
	// Read-only probe: the flag is published with InterlockedExchange under
	// gPreSkinLock together with the map entry, and a same-thread reader always
	// observes its own writes in program order. A stale cross-thread zero only
	// skips a restore for a command list this thread does not own, which is
	// correct, so a plain volatile load (atomic on x86 for aligned 32-bit)
	// avoids a LOCK CMPXCHG on every hot-path call.
	if (gHasActivePreSkinReplacement == 0)
		return false;
	DX12PreSkinReplacementShard &reusableShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_SHARED(&reusableShard.lock, "Lock.preSkinShared");
	auto it = reusableShard.entries.find(commandList);
	if (it == reusableShard.entries.end())
	{
		ReleaseSRWLockShared(&reusableShard.lock);
		return false;
	}
	const DX12PreSkinReplacementState &state = it->second;
	const bool reusable = state.active && state.patchHeap &&
						  state.descriptorRestores.empty() && state.temporaryResources.empty() &&
						  state.computeShaderHash == computeShaderHash &&
						  state.bindingFingerprint == bindingFingerprint &&
						  state.triggerHash == triggerHash;
	if (hasPendingState)
		*hasPendingState = true;
	ReleaseSRWLockShared(&reusableShard.lock);
	return reusable;
}


bool DX12ModCanDeferPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument.
	if (gHasActivePreSkinReplacement == 0)
		return false;
	DX12PreSkinReplacementShard &deferShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_SHARED(&deferShard.lock, "Lock.preSkinShared");
	auto it = deferShard.entries.find(commandList);
	const bool canDefer = it != deferShard.entries.end() &&
						  it->second.active && it->second.patchHeap &&
						  it->second.descriptorRestores.empty() &&
						  it->second.temporaryResources.empty() &&
						  it->second.bindingFingerprint != 0;
	ReleaseSRWLockShared(&deferShard.lock);
	return canDefer;
}


bool DX12ModHasActivePreSkinningUavReplacement()
{
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument. This is called on every graphics binding hook and
	// draw; a LOCK CMPXCHG here costs measurable cycles per call.
	return gHasActivePreSkinReplacement != 0;
}


// Descriptor-table copy plan for ApplyPreSkinDescriptorTablePatchLocked:
// one entry per root table mirrored into the descriptor ring.
class DX12PreSkinTableCopy
{
public:
	UINT root = 0;
	SIZE_T cpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	ID3D12DescriptorHeap *heap = nullptr;
	UINT increment = 0;
	UINT count = 0;
	UINT tempOffset = 0;
};

// Collect the descriptor tables a compute-side patch must mirror into the
// private ring heap: every bound CBV/SRV table plus the UAV target table,
// deduplicated by (root, cpuStart, gpuStart). Switching descriptor heaps
// invalidates every root-table binding, so the whole set has to be mirrored
// and rebound - patching a single table in place is not an option.
static void CollectPreSkinTableCopies(
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const DX12CurrentComputeUavBinding &uavBinding,
	std::vector<DX12PreSkinTableCopy> *tables)
{
	if (!tables)
		return;
	tables->clear();
	auto addTable = [tables](const DX12CurrentComputeUavBinding &binding)
	{
		if (!binding.hasDescriptor || binding.rootDescriptor || !binding.tableCpuStart ||
			!binding.tableGpuStart.ptr || !binding.descriptorIncrement || !binding.tableHeap)
			return;
		const UINT needed = (std::max)(binding.tableCopyCount ? binding.tableCopyCount : 1,
										   binding.descriptorOffset + 1);
		for (DX12PreSkinTableCopy &table : *tables)
		{
			if (table.root == binding.rootParameterIndex &&
				table.cpuStart == binding.tableCpuStart &&
				table.gpuStart.ptr == binding.tableGpuStart.ptr)
			{
				table.count = (std::max)(table.count, needed);
				return;
			}
		}
		DX12PreSkinTableCopy table;
		table.root = binding.rootParameterIndex;
		table.cpuStart = binding.tableCpuStart;
		table.gpuStart = binding.tableGpuStart;
		table.heap = binding.tableHeap;
		table.increment = binding.descriptorIncrement;
		table.count = needed;
		tables->push_back(table);
	};
	for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		addTable(cbv);
	for (const DX12CurrentComputeUavBinding &srv : srvs)
		addTable(srv);
	addTable(uavBinding);
}


static const DX12PreSkinTableCopy *FindPreSkinTableCopy(
	const std::vector<DX12PreSkinTableCopy> &tables,
	const DX12CurrentComputeUavBinding &binding)
{
	for (const DX12PreSkinTableCopy &table : tables)
	{
		if (table.root == binding.rootParameterIndex &&
			table.cpuStart == binding.tableCpuStart &&
			table.gpuStart.ptr == binding.tableGpuStart.ptr)
			return &table;
	}
	return nullptr;
}


static const DX12PreSkinSrvReplacementBinding *FindPreSkinSrvReplacement(
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	UINT shaderRegister)
{
	for (const DX12PreSkinSrvReplacementBinding &replacement : srvReplacements)
	{
		if (replacement.shaderRegister == shaderRegister)
			return &replacement;
	}
	return nullptr;
}


// Batch every table-mirror copy into one CopyDescriptors call: destinations
// are disjoint slots in the fresh ring range, sources live in other heaps.
static void MirrorPreSkinTablesIntoRing(
	ID3D12Device *device,
	const std::vector<DX12PreSkinTableCopy> &tables,
	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase, UINT tempIncrement)
{
	if (!device || tables.empty() || !tempCpuBase.ptr)
		return;
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsDestStarts;
	static thread_local std::vector<UINT> tlsDestSizes;
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsSrcStarts;
	static thread_local std::vector<UINT> tlsSrcSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &destStarts = tlsDestStarts;
	std::vector<UINT> &destSizes = tlsDestSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &srcStarts = tlsSrcStarts;
	std::vector<UINT> &srcSizes = tlsSrcSizes;
	destStarts.clear();
	destSizes.clear();
	srcStarts.clear();
	srcSizes.clear();
	for (const DX12PreSkinTableCopy &table : tables)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = table.cpuStart;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
		destStarts.push_back(dst);
		destSizes.push_back(table.count);
		srcStarts.push_back(src);
		srcSizes.push_back(table.count);
	}
	if (!destStarts.empty())
	{
		device->CopyDescriptors(
			static_cast<UINT>(destStarts.size()), destStarts.data(),
			destSizes.data(),
			static_cast<UINT>(srcStarts.size()), srcStarts.data(),
			srcSizes.data(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

bool ApplyPreSkinDescriptorTablePatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &uavBinding, const DX12ComputeUavProducer &producer,
	const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12PreSkinSrvReplacementBinding> &srvReplacements,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut,
	UINT64 bindingFingerprint, uint64_t computeShaderHash)
{
	if (!device || !commandList || !uavBinding.hasDescriptor || !uavBinding.tableGpuStart.ptr)
		return false;
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	static thread_local std::vector<DX12PreSkinTableCopy> tlsTables;
	std::vector<DX12PreSkinTableCopy> &tables = tlsTables;
	CollectPreSkinTableCopies(cbvs, srvs, uavBinding, &tables);
	if (tables.empty())
	{
		return false;
	}
	(void)rootConstants;

	const UINT64 abortCacheKey = MakePreSkinDescriptorPatchAbortCacheKey(
		config, srvReplacements, srvs, cbvs);
	bool abortCacheHit = false;
	AcquireSRWLockShared(&gPreSkinDescriptorPatchAbortLock);
	abortCacheHit = gPreSkinDescriptorPatchAbortCache.find(abortCacheKey) !=
		gPreSkinDescriptorPatchAbortCache.end();
	ReleaseSRWLockShared(&gPreSkinDescriptorPatchAbortLock);
	if (abortCacheHit)
	{
		return false;
	}
	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	bool requiresCountPatch = false;
	bool canPatchCount = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		const DX12PreSkinSrvReplacementBinding *replacementBinding = nullptr;
		for (const DX12PreSkinSrvReplacementBinding &binding : srvReplacements)
		{
			if (binding.shaderRegister == srv.shaderRegister)
			{
				replacementBinding = &binding;
				break;
			}
		}
		if (!replacementBinding || !srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;

		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
		const UINT elementStride =
			srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride : (resource ? resource->stride : 0);
		if (!resource || !elementStride)
			continue;

		const UINT srvOriginalVertexCount = static_cast<UINT>(
			(std::min)(srvByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		const UINT srvOverrideVertexCount = static_cast<UINT>(
			(std::min)(resource->byteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
		if (srvOriginalVertexCount && srvOverrideVertexCount)
		{
			if (!originalVertexCount || srvOriginalVertexCount < originalVertexCount)
				originalVertexCount = srvOriginalVertexCount;
			if (!overrideVertexCount || srvOverrideVertexCount < overrideVertexCount)
				overrideVertexCount = srvOverrideVertexCount;
		}
	}
	if (originalVertexCount && overrideVertexCount && originalVertexCount != overrideVertexCount)
	{
		requiresCountPatch = true;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		{
			if (CanPatchPreSkinVertexCountCbvLocked(cbv, originalVertexCount))
			{
				canPatchCount = true;
				break;
			}
		}
		if (!canPatchCount)
		{
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
			return false;
		}
	}
	bool hasPendingState = false;
	if (!requiresCountPatch && TryReuseDeferredPreSkinDescriptorTable(
								   commandList, computeShaderHash, bindingFingerprint, triggerHash,
								   &hasPendingState))
	{
		if (originalVertexCountOut)
			*originalVertexCountOut = originalVertexCount;
		if (overrideVertexCountOut)
			*overrideVertexCountOut = overrideVertexCount;
		return true;
	}
	if (hasPendingState)
		DX12ModRestorePreSkinningUavReplacement(commandList);
	static thread_local DX12PreSkinDescriptorTableCloneCache tlsCloneCache;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	{
		ID3D12DescriptorHeap *persistentHeap = nullptr;
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> persistentPatchedTables;
		if (!requiresCountPatch && bindingFingerprint &&
			TryGetPersistentPreSkinDescriptorPatchLocked(
				reloadGeneration,
				bindingFingerprint,
				triggerHash,
				tables.size(),
				&persistentHeap,
				&persistentPatchedTables))
		{
			ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
			ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
			DX12BindingTracker::GetCurrentDescriptorHeaps(
				commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
			if (restoreCbvSrvUavHeap != persistentHeap)
			{
				ID3D12DescriptorHeap *heaps[2] = {
					persistentHeap, restoreSamplerHeap};
				commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
				SyncPreSkinDescriptorHeaps(
					commandList, runtimeState, persistentHeap, restoreSamplerHeap);
			}
			DX12PreSkinReplacementState state;
			state.active = true;
			state.computeShaderHash = computeShaderHash;
			state.bindingFingerprint = bindingFingerprint;
			state.triggerHash = triggerHash;
			state.rootParameterIndex = uavBinding.rootParameterIndex;
			state.originalTable = uavBinding.tableGpuStart;
			state.originalHeap = uavBinding.tableHeap;
			state.resource = uavBinding.descriptor.resource;
			state.byteWidth = DescriptorViewSize(uavBinding.descriptor);
			state.stride = uavBinding.descriptor.structureByteStride;
			state.hasActiveTextureOverride =
				state.resource && state.byteWidth && state.stride;
			if (state.hasActiveTextureOverride)
			{
				state.activeConfig = config;
				state.activeProducer = producer;
				state.resource->AddRef();
			}
			state.patchHeap = persistentHeap;
			state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
			state.restoreSamplerHeap = restoreSamplerHeap;
			for (size_t index = 0; index < tables.size() &&
								   index < persistentPatchedTables.size();
				 ++index)
			{
				commandList->SetComputeRootDescriptorTable(
					tables[index].root,
					persistentPatchedTables[index]);
				SyncPreSkinComputeRootDescriptorTable(
					commandList, runtimeState,
					tables[index].root,
					persistentPatchedTables[index]);
				DX12PreSkinReplacementState::TableRestore restore;
				restore.rootParameterIndex = tables[index].root;
				restore.originalTable = tables[index].gpuStart;
				state.tableRestores.push_back(restore);
			}
			RetainPreSkinDescriptorHeapForCommandList(commandList, persistentHeap);
			persistentHeap->Release();
			if (originalVertexCountOut)
				*originalVertexCountOut = originalVertexCount;
			if (overrideVertexCountOut)
				*overrideVertexCountOut = overrideVertexCount;
			DX12PreSkinReplacementShard &replacementShard =
				PreSkinReplacementShardFor(commandList);
			DX12_LOCK_ACQUIRE_EXCLUSIVE(&replacementShard.lock, "Lock.preSkinExclusive");
			auto existingReplacement = replacementShard.entries.find(commandList);
			if (existingReplacement != replacementShard.entries.end())
				existingReplacement->second = std::move(state);
			else {
				replacementShard.entries.emplace(commandList, std::move(state));
				InterlockedIncrement(&gHasActivePreSkinReplacement);
			}
			ReleaseSRWLockExclusive(&replacementShard.lock);
			BumpPreSkinReplacementShardMutation(&replacementShard);
			return true;
		}
	}
	if (!requiresCountPatch && bindingFingerprint && tlsCloneCache.Matches(commandList, reloadGeneration, bindingFingerprint, triggerHash))
	{
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
		ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
		DX12BindingTracker::GetCurrentDescriptorHeaps(
			commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
		if (restoreCbvSrvUavHeap != tlsCloneCache.heap)
		{
			ID3D12DescriptorHeap *heaps[2] = {
				tlsCloneCache.heap, restoreSamplerHeap};
			commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
			SyncPreSkinDescriptorHeaps(
				commandList, runtimeState, tlsCloneCache.heap, restoreSamplerHeap);
		}
		DX12PreSkinReplacementState state;
		state.active = true;
		state.computeShaderHash = computeShaderHash;
		state.bindingFingerprint = bindingFingerprint;
		state.triggerHash = triggerHash;
		state.rootParameterIndex = uavBinding.rootParameterIndex;
		state.originalTable = uavBinding.tableGpuStart;
		state.originalHeap = uavBinding.tableHeap;
		state.resource = uavBinding.descriptor.resource;
		state.byteWidth = DescriptorViewSize(uavBinding.descriptor);
		state.stride = uavBinding.descriptor.structureByteStride;
		state.hasActiveTextureOverride =
			state.resource && state.byteWidth && state.stride;
		if (state.hasActiveTextureOverride)
		{
			state.activeConfig = config;
			state.activeProducer = producer;
			state.resource->AddRef();
		}
		state.patchHeap = tlsCloneCache.heap;
		state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
		state.restoreSamplerHeap = restoreSamplerHeap;
		state.tableRestores = tlsCloneCache.tableRestores;
		for (size_t index = 0; index < state.tableRestores.size(); ++index)
		{
			commandList->SetComputeRootDescriptorTable(
				state.tableRestores[index].rootParameterIndex,
				tlsCloneCache.patchedTables[index]);
			SyncPreSkinComputeRootDescriptorTable(
				commandList, runtimeState,
				state.tableRestores[index].rootParameterIndex,
				tlsCloneCache.patchedTables[index]);
		}
		RetainPreSkinDescriptorHeapForCommandList(
			commandList, tlsCloneCache.heap);
		if (originalVertexCountOut)
			*originalVertexCountOut = originalVertexCount;
		if (overrideVertexCountOut)
			*overrideVertexCountOut = overrideVertexCount;
		DX12PreSkinReplacementShard &replacementShard =
			PreSkinReplacementShardFor(commandList);
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&replacementShard.lock, "Lock.preSkinExclusive");
		auto existingReplacement = replacementShard.entries.find(commandList);
		if (existingReplacement != replacementShard.entries.end())
			existingReplacement->second = std::move(state);
		else {
			replacementShard.entries.emplace(commandList, std::move(state));
			InterlockedIncrement(&gHasActivePreSkinReplacement);
		}
		ReleaseSRWLockExclusive(&replacementShard.lock);
		BumpPreSkinReplacementShardMutation(&replacementShard);
		return true;
	}

	UINT totalDescriptors = 0;
	for (DX12PreSkinTableCopy &table : tables)
	{
		table.tempOffset = totalDescriptors;
		totalDescriptors += table.count;
	}
	if (!totalDescriptors)
	{
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
	UINT tempIncrement = 0;
	ID3D12DescriptorHeap *tempHeap = nullptr;
	DX12_PROF_STAGE_BEGIN(PreSkinRingAlloc);
	const bool ringAllocated = EnsurePreSkinDescriptorRingLocked(
		device, commandList, totalDescriptors, &tempCpuBase, &tempGpuBase, &tempIncrement,
		&tempHeap);
	DX12_PROF_STAGE_END(PreSkinRingAlloc);
	if (!ringAllocated)
	{
		return false;
	}
	MirrorPreSkinTablesIntoRing(device, tables, tempCpuBase, tempIncrement);
	// SRV patches are collected into their own batch below, so their staging
	// vectors must start empty. The table mirror above owns different TLS
	// vectors, so this clear is the only reset this batch needs.
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsPreSkinSrvPatchDestStarts;
	static thread_local std::vector<UINT> tlsPreSkinSrvPatchDestSizes;
	static thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tlsPreSkinSrvPatchSrcStarts;
	static thread_local std::vector<UINT> tlsPreSkinSrvPatchSrcSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &tableCopyDestStarts = tlsPreSkinSrvPatchDestStarts;
	std::vector<UINT> &tableCopyDestSizes = tlsPreSkinSrvPatchDestSizes;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &tableCopySrcStarts = tlsPreSkinSrvPatchSrcStarts;
	std::vector<UINT> &tableCopySrcSizes = tlsPreSkinSrvPatchSrcSizes;
	tableCopyDestStarts.clear();
	tableCopyDestSizes.clear();
	tableCopySrcStarts.clear();
	tableCopySrcSizes.clear();
	DX12_PROF_STAGE_BEGIN(PreSkinCopyPatch);
	static thread_local std::vector<ID3D12Resource *> tlsTemporaryResources;
	std::vector<ID3D12Resource *> &temporaryResources = tlsTemporaryResources;
	temporaryResources.clear();
	bool patchedAnySrv = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs)
	{
		// The old PreSkinLoadResource probe here was dropped: it began a stage
		// that was never ended (the `continue` below skipped every END), so its
		// numbers were garbage and it misattributed the enclosing stage.
		const DX12PreSkinSrvReplacementBinding *replacementBinding =
			FindPreSkinSrvReplacement(srvReplacements, srv.shaderRegister);
		if (!replacementBinding)
			continue;

		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		const UINT64 stableNegativeKey =
			MakePreSkinSrvStableProbeCacheKey(srv, config, srvByteWidth);
		bool probeKnownBad = false;
		DX12LoadedResource *resource = nullptr;
		UINT elementStride = 0;
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinSrvProbeCacheLock, "Lock.srvProbeCacheExclusive");
		if (gPreSkinSrvNegativeCache.find(stableNegativeKey) !=
		    gPreSkinSrvNegativeCache.end())
		{
			probeKnownBad = true;
		}
		else
		{
			const auto positiveIt = gPreSkinSrvPositiveCache.find(stableNegativeKey);
			if (positiveIt != gPreSkinSrvPositiveCache.end())
			{
				resource = positiveIt->second.resource;
				elementStride = positiveIt->second.elementStride;
			}
		}
		ReleaseSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
		if (probeKnownBad)
			continue;
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
		{
			StorePreSkinSrvProbeNegative(stableNegativeKey);
			continue;
		}
		const DX12PreSkinTableCopy *table = FindPreSkinTableCopy(tables, srv);
		if (!table)
		{
			StorePreSkinSrvProbeNegative(stableNegativeKey);
			continue;
		}
		if (!resource)
		{
			resource = EnsureLoadedResourceForPreSkin(device, replacementBinding->resource);
			elementStride =
				srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride : (resource ? resource->stride : 0);
			if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
				!resource->srvCpu.ptr)
			{
				StorePreSkinSrvProbeNegative(stableNegativeKey);
				continue;
			}
			const DX12PreSkinSrvPositiveCacheValue cacheValue =
				{resource, elementStride, srvByteWidth, srv.shaderRegister};
			StorePreSkinSrvProbePositive(stableNegativeKey, cacheValue);
		}
		if (!resource || !resource->srvCpu.ptr)
		{
			StorePreSkinSrvProbeNegative(stableNegativeKey);
			continue;
		}
		if (!elementStride)
		{
			StorePreSkinSrvProbeNegative(stableNegativeKey);
			continue;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table->tempOffset + srv.descriptorOffset) * tempIncrement;
		tableCopyDestStarts.push_back(dst);
		tableCopyDestSizes.push_back(1);
		tableCopySrcStarts.push_back(resource->srvCpu);
		tableCopySrcSizes.push_back(1);
		patchedAnySrv = true;
	}

	// Flush the collected SRV patches as one batched copy before the CBV
	// count-patch phase; destination slots are distinct table offsets.
	if (!tableCopyDestStarts.empty())
	{
		device->CopyDescriptors(
			static_cast<UINT>(tableCopyDestStarts.size()), tableCopyDestStarts.data(),
			tableCopyDestSizes.data(),
			static_cast<UINT>(tableCopySrcStarts.size()), tableCopySrcStarts.data(),
			tableCopySrcSizes.data(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	if (!patchedAnySrv)
	{
		DX12_PROF_STAGE_END(PreSkinCopyPatch);
		return false;
	}

	if (requiresCountPatch && canPatchCount)
	{
		bool patchedCountCbv = false;
		for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		{
			if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV")
				continue;
			const DX12PreSkinTableCopy *table = FindPreSkinTableCopy(tables, cbv);
			if (!table)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
			dst.ptr += static_cast<SIZE_T>(table->tempOffset + cbv.descriptorOffset) * tempIncrement;
			if (PatchPreSkinVertexCountCbvLocked(
					device, cbv, originalVertexCount, overrideVertexCount, dst,
					&temporaryResources, originalVertexCountOut, overrideVertexCountOut))
				patchedCountCbv = true;
		}
		if (!patchedCountCbv)
		{
			for (ID3D12Resource *resource : temporaryResources)
			{
				if (resource)
					resource->Release();
			}
			temporaryResources.clear();
			StorePreSkinDescriptorPatchAbort(abortCacheKey);
			DX12_PROF_STAGE_END(PreSkinCopyPatch);
			return false;
		}
	}
	// Resize output UAV when mod vertex count exceeds original buffer capacity.
	// The compute shader writes a producer UAV, then the completed output is
	// copied to a separate IA-only buffer after dispatch.
	ID3D12Resource *resizedUavResource = nullptr;
	ID3D12Resource *resizedVertexResource = nullptr;
	ID3D12DescriptorHeap *resizedUavHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE resizedUavCpu = {};
	bool resizedUavCacheHit = false;
	UINT resizedOutputSlot = 0;
	bool outputUavResized = false;
	// The binding may embed a resource freed and recreated at the same VA
	// after a scene switch; resolve the current allocation once so every
	// dereference below (resize cache GetDesc/VA query, active-override
	// publication, restore-time copy-back) targets live memory.
	ID3D12Resource *uavResource =
		uavBinding.hasDescriptor && !uavBinding.rootDescriptor &&
				uavBinding.descriptor.kind == "UAV" ?
			ResolvePreSkinBindingResource(uavBinding.descriptor) : nullptr;
	if (overrideVertexCount && originalVertexCount &&
		overrideVertexCount > originalVertexCount &&
		uavBinding.hasDescriptor && !uavBinding.rootDescriptor &&
		uavBinding.descriptor.kind == "UAV" &&
		uavBinding.descriptor.structureByteStride)
	{
		const UINT uavStride = uavBinding.descriptor.structureByteStride;
		const UINT64 requiredByteWidth =
			static_cast<UINT64>(uavStride) * overrideVertexCount;
		const UINT64 currentByteWidth = DescriptorViewSize(uavBinding.descriptor);
		if (requiredByteWidth > currentByteWidth)
		{
			const DX12PreSkinTableCopy *uavTable = FindPreSkinTableCopy(tables, uavBinding);
			if (uavResource && uavTable &&
				AcquirePreSkinOutputResizeCacheEntry(
					device, commandList, uavResource,
					requiredByteWidth, uavStride, overrideVertexCount,
					currentByteWidth,
					&resizedUavResource, &resizedVertexResource,
					&resizedUavHeap, &resizedUavCpu, &resizedUavCacheHit,
					&resizedOutputSlot))
			{
				D3D12_CPU_DESCRIPTOR_HANDLE uavDst = tempCpuBase;
				uavDst.ptr += static_cast<SIZE_T>(
								  uavTable->tempOffset + uavBinding.descriptorOffset) *
							  tempIncrement;
				device->CopyDescriptorsSimple(
					1, uavDst, resizedUavCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				outputUavResized = true;
				// Acquired references follow this command list through the
				// delayed-retirement path. The cache owns separate base refs.
				temporaryResources.push_back(resizedUavResource);
				temporaryResources.push_back(resizedVertexResource);
				if (resizedUavHeap)
				{
					resizedUavHeap->Release();
					resizedUavHeap = nullptr;
				}
			}
			if (!outputUavResized)
			{
				if (resizedUavResource)
					resizedUavResource->Release();
				if (resizedVertexResource)
					resizedVertexResource->Release();
				if (resizedUavHeap)
					resizedUavHeap->Release();
				resizedUavResource = nullptr;
				resizedVertexResource = nullptr;
				resizedUavHeap = nullptr;
				resizedUavCpu = {};
			}
		}
	}
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	DX12BindingTracker::GetCurrentDescriptorHeaps(
		commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
	if (restoreCbvSrvUavHeap != tempHeap)
	{
		ID3D12DescriptorHeap *heaps[2] = {tempHeap, restoreSamplerHeap};
		commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
		SyncPreSkinDescriptorHeaps(
			commandList, runtimeState, tempHeap, restoreSamplerHeap);
	}
	DX12PreSkinReplacementState state;
	state.active = true;
	state.computeShaderHash = computeShaderHash;
	state.bindingFingerprint = bindingFingerprint;
	state.triggerHash = triggerHash;
	DX12_PROF_STAGE_END(PreSkinCopyPatch);
	state.rootParameterIndex = uavBinding.rootParameterIndex;
	DX12_PROF_STAGE_BEGIN(PreSkinBindHeaps);
	state.originalTable = uavBinding.tableGpuStart;
	state.originalHeap = uavBinding.tableHeap;
	state.resource = outputUavResized ? resizedUavResource : uavResource;
	state.outputVertexResource = outputUavResized ? resizedVertexResource : nullptr;
	state.originalOutputResource = outputUavResized ? uavResource : nullptr;
	state.originalOutputByteWidth = outputUavResized ? DescriptorViewSize(uavBinding.descriptor) : 0;
	state.outputResizeSlot = outputUavResized ? resizedOutputSlot : 0;
	state.byteWidth = outputUavResized ? (static_cast<UINT64>(uavBinding.descriptor.structureByteStride) * overrideVertexCount) : DescriptorViewSize(uavBinding.descriptor);
	state.stride = uavBinding.descriptor.structureByteStride;
	state.outputUavResized = outputUavResized;
	state.hasActiveTextureOverride = state.resource && state.byteWidth && state.stride;
	if (state.hasActiveTextureOverride)
	{
		state.activeConfig = config;
		state.activeProducer = producer;
		if (state.resource)
			state.resource->AddRef();
	}
	state.patchHeap = tempHeap;
	state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
	state.restoreSamplerHeap = restoreSamplerHeap;
	state.temporaryResources = temporaryResources;
	for (const DX12PreSkinTableCopy &table : tables)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
		gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
		commandList->SetComputeRootDescriptorTable(table.root, gpu);
		SyncPreSkinComputeRootDescriptorTable(
			commandList, runtimeState, table.root, gpu);
		DX12PreSkinReplacementState::TableRestore restore;
		restore.rootParameterIndex = table.root;
		restore.originalTable = table.gpuStart;
		state.tableRestores.push_back(restore);
	}
	if (!requiresCountPatch && !outputUavResized && bindingFingerprint)
	{
		// Migrate the mirror into a cache-owned heap. The descriptor ring is
		// transient now - its offsets are rewound once every GPU reference
		// drains - so a cross-frame cache must not point into it. One heap
		// creation plus one full-range copy per cache fill; cache hits only
		// rebind the dedicated heap afterwards.
		D3D12_DESCRIPTOR_HEAP_DESC dedicatedDesc = {};
		dedicatedDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		dedicatedDesc.NumDescriptors = totalDescriptors;
		dedicatedDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ID3D12DescriptorHeap *dedicatedHeap = nullptr;
		if (SUCCEEDED(device->CreateDescriptorHeap(
				&dedicatedDesc, IID_PPV_ARGS(&dedicatedHeap))) && dedicatedHeap)
		{
			device->CopyDescriptorsSimple(
				totalDescriptors,
				dedicatedHeap->GetCPUDescriptorHandleForHeapStart(),
				tempCpuBase,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const D3D12_GPU_DESCRIPTOR_HANDLE dedicatedGpuBase =
				dedicatedHeap->GetGPUDescriptorHandleForHeapStart();
			tlsCloneCache.Clear();
			dedicatedHeap->AddRef();
			tlsCloneCache.commandList = commandList;
			tlsCloneCache.reloadGeneration = reloadGeneration;
			tlsCloneCache.bindingFingerprint = bindingFingerprint;
			tlsCloneCache.triggerHash = triggerHash;
			tlsCloneCache.heap = dedicatedHeap;
			tlsCloneCache.tableRestores = state.tableRestores;
			tlsCloneCache.patchedTables.reserve(tables.size());
			for (const DX12PreSkinTableCopy &table : tables)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE gpu = dedicatedGpuBase;
				gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
				tlsCloneCache.patchedTables.push_back(gpu);
			}
			StorePersistentPreSkinDescriptorPatchLocked(
				reloadGeneration,
				bindingFingerprint,
				triggerHash,
				dedicatedHeap,
				tlsCloneCache.patchedTables);
			dedicatedHeap->Release();
		}
	}
	// END must fire unconditionally: the positional scope stack pairs every
	// BEGIN with an END, and a skipped END would misattribute all later stages
	// on this thread (the old name-keyed profiler tolerated the mismatch).
	DX12_PROF_STAGE_END(PreSkinBindHeaps);
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;
	DX12PreSkinReplacementShard &replacementShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&replacementShard.lock, "Lock.preSkinExclusive");
	auto existingReplacement = replacementShard.entries.find(commandList);
	if (existingReplacement != replacementShard.entries.end())
		existingReplacement->second = std::move(state);
	else {
		replacementShard.entries.emplace(commandList, std::move(state));
		InterlockedIncrement(&gHasActivePreSkinReplacement);
	}
	ReleaseSRWLockExclusive(&replacementShard.lock);
	BumpPreSkinReplacementShardMutation(&replacementShard);
	return true;
}


bool HasExplicitPreSkinDispatchOverride(
	const Bunny::TextureOverrideConfig &config)
{
	if (!config.handlingSkip)
		return false;
	for (const Bunny::CommandListAction &action : config.actions)
	{
		if (action.kind == Bunny::CommandListActionKind::Dispatch &&
			action.argCount >= 3)
			return true;
	}
	return false;
}

void FillPreSkinDispatchOverrideFromConfig(
	const Bunny::TextureOverrideConfig &config,
	DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (!dispatchOverride)
		return;
	dispatchOverride->handlingSkip = config.handlingSkip;
	dispatchOverride->hasDispatch = false;
	dispatchOverride->groupsX = 0;
	dispatchOverride->groupsY = 0;
	dispatchOverride->groupsZ = 0;
	for (const Bunny::CommandListAction &action : config.actions)
	{
		if (action.kind != Bunny::CommandListActionKind::Dispatch ||
			action.argCount < 3)
			continue;
		dispatchOverride->hasDispatch = true;
		dispatchOverride->groupsX = action.args[0];
		dispatchOverride->groupsY = action.args[1];
		dispatchOverride->groupsZ = action.args[2];
	}
}


// Runs the VM-level actions (control flow + variable assignments) of a
// matched pre-skin TextureOverride section. DX11 executes the whole section
// command list at match time, so mods publish per-frame state with plain
// assignments such as "$active0 = 1" that [Key] conditions and draw command
// lists then read. The DX12 pre-skin path consumed only handling=skip /
// dispatch= / cs-tN and silently dropped these assignments, leaving every
// key condition that depends on them permanently false.
#if DX12_TIMING_BUILD
// TIMING-build trace: dump the post-match value of every variable the
// matched pre-skin section assigns (e.g. "$active0 = 1") so the log shows
// exactly when the "mesh is being replaced this frame" flag flips. Compiled
// out of regular Release builds.
static void TracePreSkinMatchVmValues(const Bunny::TextureOverrideConfig &config)
{
	std::vector<std::wstring> names;
	names.reserve(8);
	for (const Bunny::CommandListAction &action : config.actions) {
		if (action.kind != Bunny::CommandListActionKind::VariableAssignment &&
		    action.kind != Bunny::CommandListActionKind::PostVariableAssignment &&
		    action.kind != Bunny::CommandListActionKind::LocalVariableAssignment)
			continue;
		const std::wstring name =
			MigotoCommandVmUtils::ExtractVariableName(action.key);
		if (name.empty() || names.size() >= 8)
			continue;
		bool already = false;
		for (const std::wstring &existing : names) {
			if (existing == name) {
				already = true;
				break;
			}
		}
		if (!already)
			names.push_back(name);
	}
	if (names.empty())
		return;
	char buffer[1024];
	size_t used = 0;
	buffer[0] = '\0';
	MigotoCommandVmUtils::ExpressionContext context;
	for (const std::wstring &name : names) {
		const double value =
			MigotoCommandVmUtils::LookupVariable(context, name);
		const int written = snprintf(
			buffer + used, sizeof(buffer) - used, "%s%ls=%g",
			used ? " " : "", name.c_str(), value);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
	}
	if (used)
		DX12_LOG("DX12PreSkinVmDiag",
			"event=match_vm_values section=%S values=%s",
			config.section.c_str(), buffer);
}
#endif

void RunPreSkinMatchVmActions(
	const Bunny::TextureOverrideConfig &config)
{
	MigotoCommandVmUtils::State state;
	for (const Bunny::CommandListAction &action : config.actions)
	{
		switch (action.kind)
		{
		case Bunny::CommandListActionKind::ControlFlow:
			state.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			break;
		case Bunny::CommandListActionKind::VariableAssignment:
			if (state.Active())
				state.Assign(action.key, action.value);
			break;
		case Bunny::CommandListActionKind::PostVariableAssignment:
			if (state.Active())
				state.QueuePostAssign(action.key, action.value);
			break;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			if (state.Active())
				state.DeclareLocal(action.key, action.value);
			break;
		default:
			break;
		}
	}
	// The section's dispatch is issued by the game immediately after the
	// match, so queued post assignments flush here, mirroring DX11 running
	// the post command list right after the API call returns.
	state.FlushPostAssignments();
#if DX12_TIMING_BUILD
	TracePreSkinMatchVmValues(config);
#endif
}

bool ApplyPreSkinBindingPatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &binding, const DX12ComputeUavProducer &producer,
	DX12LoadedResource *replacement, const DX12VertexLimitRaiseConfig &vlr,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut)
{
	if (originalVertexCountOut)
		*originalVertexCountOut = 0;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = 0;
	if (!device || !commandList || !replacement || !replacement->uavResource ||
		!replacement->uavCpu.ptr || !binding.hasDescriptor || !binding.tableGpuStart.ptr)
		return false;
	if (replacement->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = replacement->uavResource;
		barrier.Transition.StateBefore = replacement->uavState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
		replacement->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	replacement->uavWritten = false;
	replacement->uavValid = false;
	
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	
	// DX12 descriptor heaps are read by the GPU at execution time, not at
	// recording time. The old VLR path patched the game's heap in place and
	// every restore trigger (next binding mutation, next dispatch, list
	// teardown) undid it before the GPU ever executed the dispatch, so the
	// replacement was invisible or frame-dependent (the twitching). Mirror
	// the compute tables into the private ring heap, patch only the mirrored
	// UAV slot, and rebind the root tables - recorded commands, ordered with
	// the dispatch on the GPU timeline exactly like the draw-SRV mirror and
	// the match_cs clone-table path.
	static thread_local std::vector<DX12PreSkinTableCopy> tlsVlrTables;
	std::vector<DX12PreSkinTableCopy> &tables = tlsVlrTables;
	CollectPreSkinTableCopies(cbvs, srvs, binding, &tables);
	if (tables.empty())
		return false;
	
	UINT totalDescriptors = 0;
	for (DX12PreSkinTableCopy &table : tables)
	{
		table.tempOffset = totalDescriptors;
		totalDescriptors += table.count;
	}
	if (!totalDescriptors)
		return false;
	
	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
	UINT tempIncrement = 0;
	ID3D12DescriptorHeap *tempHeap = nullptr;
	if (!EnsurePreSkinDescriptorRingLocked(
			device, commandList, totalDescriptors, &tempCpuBase, &tempGpuBase, &tempIncrement,
			&tempHeap))
	{
		return false;
	}
	MirrorPreSkinTablesIntoRing(device, tables, tempCpuBase, tempIncrement);
	
	const DX12PreSkinTableCopy *uavTable = FindPreSkinTableCopy(tables, binding);
	if (!uavTable)
		return false;
	D3D12_CPU_DESCRIPTOR_HANDLE uavDst = tempCpuBase;
	uavDst.ptr += static_cast<SIZE_T>(
		uavTable->tempOffset + binding.descriptorOffset) * tempIncrement;
	device->CopyDescriptorsSimple(
		1, uavDst, replacement->uavCpu,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	// Vertex-count CBV patch: the CS loop bound can come from a game constant
	// holding the original vertex count. Patch it best-effort, matching DX11
	// VLR semantics: mods whose CS handles the raised count itself (shader
	// fixes / cs-cb commands) have no patchable constant here and must still
	// get the expanded output, so a failed probe never aborts the patch.
	std::vector<ID3D12Resource *> temporaryResources;
	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	if (vlr.overrideVertexCount && binding.descriptor.structureByteStride)
	{
		originalVertexCount = static_cast<UINT>((std::min)(
			DescriptorViewSize(binding.descriptor) /
				binding.descriptor.structureByteStride,
			static_cast<UINT64>(UINT_MAX)));
		overrideVertexCount = vlr.overrideVertexCount;
	}
	if (originalVertexCount && overrideVertexCount > originalVertexCount)
	{
		for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		{
			if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV")
				continue;
			const DX12PreSkinTableCopy *cbvTable = FindPreSkinTableCopy(tables, cbv);
			if (!cbvTable)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE cbvDst = tempCpuBase;
			cbvDst.ptr += static_cast<SIZE_T>(
				cbvTable->tempOffset + cbv.descriptorOffset) * tempIncrement;
			PatchPreSkinVertexCountCbvLocked(
				device, cbv, originalVertexCount, overrideVertexCount, cbvDst,
				&temporaryResources, originalVertexCountOut, overrideVertexCountOut);
		}
	}
	
	// The binding may embed a resource freed and recreated at the same VA
	// after a scene switch; resolve the current allocation so the VA alias
	// and the restore-time copy-back target live memory.
	ID3D12Resource *originalOutputResource =
		binding.descriptor.kind == "UAV" ?
			ResolvePreSkinBindingResource(binding.descriptor) : nullptr;
	const UINT64 originalOutputByteWidth = DescriptorViewSize(binding.descriptor);
	
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	DX12BindingTracker::GetCurrentDescriptorHeaps(
		commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
	if (restoreCbvSrvUavHeap != tempHeap)
	{
		ID3D12DescriptorHeap *heaps[2] = {tempHeap, restoreSamplerHeap};
		commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
		SyncPreSkinDescriptorHeaps(
			commandList, runtimeState, tempHeap, restoreSamplerHeap);
	}
	DX12PreSkinReplacementState state;
	state.active = true;
	state.triggerHash = triggerHash;
	state.rootParameterIndex = binding.rootParameterIndex;
	state.originalTable = binding.tableGpuStart;
	state.originalHeap = binding.tableHeap;
	// The scratch is both the dispatch output and the IA-read vertex source:
	// there is no separate vertex twin (self-output). The restore mirrors the
	// leading span back into the game's buffer and leaves the scratch in
	// COMMON for the redirected reads.
	state.resource = replacement->uavResource;
	state.outputVertexResource = replacement->uavResource;
	state.originalOutputResource = originalOutputResource;
	state.originalOutputByteWidth = originalOutputByteWidth;
	state.byteWidth = replacement->byteWidth;
	state.stride = vlr.overrideByteStride ?
		vlr.overrideByteStride : replacement->stride;
	state.outputUavResized = true;
	state.loadedReplacement = replacement;
	state.hasActiveTextureOverride =
		state.resource && state.byteWidth && state.stride &&
		producer.hasDescriptor && section && section[0];
	if (state.hasActiveTextureOverride)
	{
		state.activeConfig.section = section;
		state.activeProducer = producer;
		state.resource->AddRef();
	}
	state.patchHeap = tempHeap;
	state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
	state.restoreSamplerHeap = restoreSamplerHeap;
	state.temporaryResources = temporaryResources;
	for (const DX12PreSkinTableCopy &table : tables)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
		gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
		commandList->SetComputeRootDescriptorTable(table.root, gpu);
		SyncPreSkinComputeRootDescriptorTable(
			commandList, runtimeState, table.root, gpu);
		DX12PreSkinReplacementState::TableRestore restore;
		restore.rootParameterIndex = table.root;
		restore.originalTable = table.gpuStart;
		state.tableRestores.push_back(restore);
	}
	if (originalVertexCountOut && !*originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut && !*overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;
	DX12PreSkinReplacementShard &replacementShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&replacementShard.lock, "Lock.preSkinExclusive");
	auto existingReplacement = replacementShard.entries.find(commandList);
	if (existingReplacement != replacementShard.entries.end())
		existingReplacement->second = std::move(state);
	else {
		replacementShard.entries.emplace(commandList, std::move(state));
		InterlockedIncrement(&gHasActivePreSkinReplacement);
	}
	ReleaseSRWLockExclusive(&replacementShard.lock);
	BumpPreSkinReplacementShardMutation(&replacementShard);
	
	return true;
}


// TLS plan cache for DX12ModApplyPreSkinningUavReplacement: the full match
// outcome of the previous dispatch on this thread, re-validated against the
// live bindings before reuse.
class PreSkinMatchPlanCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 bindingFingerprint = 0;
	UINT64 bindingDependencyFingerprint = 0;
	UINT64 bufferContentGeneration = 0;
	uint64_t computeShaderHash = 0;
	DX12ComputeUavProducer producer;
	DX12VertexLimitRaiseConfig vlr;
	std::wstring vlrResourceName;
	DX12CurrentComputeUavBinding hashBinding;
	DX12UavHashTextureOverrideMatch hashMatch;
	bool valid = false;
	bool hashMatched = false;
	bool explicitMatchCs = false;
	bool hasExplicitDispatch = false;
	bool vlrMatched = false;
};


bool DX12ModApplyPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	UINT *originalVertexCount,
	UINT *overrideVertexCount,
	DX12PreSkinDispatchOverride *dispatchOverride,
	ID3D12Device *cachedDevice,
	UINT64 bindingFingerprint,
	UINT64 bindingDependencyFingerprint,
	bool *matchPhaseMiss)
{
	if (originalVertexCount)
		*originalVertexCount = 0;
	if (overrideVertexCount)
		*overrideVertexCount = 0;
	if (dispatchOverride)
		*dispatchOverride = DX12PreSkinDispatchOverride();
	if (matchPhaseMiss)
		*matchPhaseMiss = false;
	if (!commandList || !DX12ModHasActiveTextureOverrides())
		return false;

	DX12_DIAG_QPC(acquireDeviceBegin);
	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool ownsDevice = cachedDevice == nullptr;
	DX12_PROF_CPU_STAGE(
		"PreSkinRuntime",
		"apply_acquire_device",
		0,
		1,
		DX12_DIAG_ELAPSED_US(acquireDeviceBegin));
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	DX12ComputeUavProducer producer = {};
	DX12VertexLimitRaiseConfig vlr = {};
	std::wstring vlrResourceName;
	DX12LoadedResource *replacement = nullptr;
	DX12CurrentComputeUavBinding hashBinding = {};
	DX12UavHashTextureOverrideMatch hashMatch = {};
	bool hashMatched = false;
	bool explicitMatchCs = false;
	bool hasExplicitDispatch = false;
	bool vlrMatched = false;
	thread_local PreSkinMatchPlanCache tlsMatchPlan;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	// The plan key deliberately uses the cross-frame stable binding
	// fingerprint plus the content-addressed descriptor value IDs, which do
	// NOT capture the descriptor-ring location. The game rewrites its ring
	// every frame, and after a scene switch the same logical bindings can be
	// re-copied at different ring positions with identical value IDs (pooled
	// buffers recreated at reused pointers/GPU VAs) - the plan then hits with
	// the OLD ring location stored when it was built. hashBinding/producer
	// are therefore re-validated against the LIVE bindings of this dispatch
	// below; a plan whose cached slot is no longer present is treated as
	// stale and re-matched from scratch.
	bool matchPlanCached = bindingFingerprint != 0 &&
						   tlsMatchPlan.valid &&
						   tlsMatchPlan.commandList == commandList &&
						   tlsMatchPlan.reloadGeneration == reloadGeneration &&
						   tlsMatchPlan.bindingFingerprint == bindingFingerprint &&
						   tlsMatchPlan.bindingDependencyFingerprint ==
							   bindingDependencyFingerprint &&
						   tlsMatchPlan.bufferContentGeneration ==
							   DX12GetIaHashContentGeneration() &&
						   tlsMatchPlan.computeShaderHash == computeShaderHash;

	DX12_DIAG_QPC(matchBegin);
	if (matchPlanCached)
	{
		producer = tlsMatchPlan.producer;
		vlr = tlsMatchPlan.vlr;
		vlrResourceName = tlsMatchPlan.vlrResourceName;
		hashBinding = tlsMatchPlan.hashBinding;
		hashMatch = tlsMatchPlan.hashMatch;
		hashMatched = tlsMatchPlan.hashMatched;
		explicitMatchCs = tlsMatchPlan.explicitMatchCs;
		hasExplicitDispatch = tlsMatchPlan.hasExplicitDispatch;
		vlrMatched = tlsMatchPlan.vlrMatched;

		// Re-select the producer UAV binding from the LIVE bindings of this
		// dispatch by the cached slot identity (root/range/descriptor offset),
		// mirroring the VLR path's binding lookup below. This keeps the
		// clone-table patch - and the output-UAV resize / active-override
		// publication that follow from it - anchored to the CURRENT ring
		// location instead of the one captured when the plan was stored. The
		// cached slot can legitimately move: the descriptor arena is rewritten
		// every frame and can relocate after a scene switch while the value
		// IDs (and therefore every fingerprint in the plan key) stay equal.
		// If the slot is missing from the live bindings, the plan is stale:
		// fall through to the full match, which also re-validates the SRV
		// input hashes against the current descriptors.
		if (hashMatched && !uavs.empty())
		{
			const DX12CurrentComputeUavBinding *liveBinding = nullptr;
			for (const DX12CurrentComputeUavBinding &uav : uavs)
			{
				if (uav.rootParameterIndex != producer.rootParameterIndex ||
				    uav.rangeIndex != producer.rangeIndex ||
				    uav.descriptorOffset != producer.descriptorOffset)
					continue;
				if (!IsPreSkinMatchCsUavCandidate(uav))
					continue;
				liveBinding = &uav;
				break;
			}
			if (liveBinding)
			{
				hashBinding = *liveBinding;
				producer.rootParameterIndex = liveBinding->rootParameterIndex;
				producer.rangeIndex = liveBinding->rangeIndex;
				producer.shaderRegister = liveBinding->shaderRegister;
				producer.registerSpace = liveBinding->registerSpace;
				producer.descriptorOffset = liveBinding->descriptorOffset;
				producer.rootDescriptor = liveBinding->rootDescriptor;
				producer.gpuVirtualAddress = liveBinding->gpuVirtualAddress;
				producer.hasDescriptor = liveBinding->hasDescriptor;
				producer.descriptor = liveBinding->descriptor;
				// Keep the plan warm with the fresh binding so the next
				// dispatch hits with an already-correct location.
				tlsMatchPlan.hashBinding = hashBinding;
				tlsMatchPlan.producer = producer;
			}
			else
			{
				matchPlanCached = false;
			}
		}
	}
	if (!matchPlanCached)
	{
		DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
		DX12_DIAG_QPC(matchCsBegin);
		explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
		hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
											 uavs, srvs, cbvs, rootConstants, computeShaderHash, &producer, &hashBinding, &hashMatch);
		DX12_PROF_CPU_STAGE(
			"PreSkinRuntime",
			hashMatched ? "match_cs_hit" : "match_cs_miss",
			0,
			static_cast<LONG64>(uavs.size() + srvs.size() + cbvs.size()),
			DX12_DIAG_ELAPSED_US(matchCsBegin));
		if (hashMatched && hashMatch.config)
			hasExplicitDispatch = HasExplicitPreSkinDispatchOverride(*hashMatch.config);
		if ((!hashMatched || !hasExplicitDispatch) && !uavs.empty())
		{
			DX12_DIAG_QPC(vlrMatchBegin);
			vlrMatched = FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr) &&
						 FindReplacementResourceNameForVlrLocked(vlr, &vlrResourceName);
			DX12_PROF_CPU_STAGE(
				"PreSkinRuntime",
				vlrMatched ? "vlr_match_hit" : "vlr_match_miss",
				0,
				static_cast<LONG64>(uavs.size()),
				DX12_DIAG_ELAPSED_US(vlrMatchBegin));
		}
		ReleaseSRWLockShared(&gModLock);
		if (bindingFingerprint != 0)
		{
			tlsMatchPlan.commandList = commandList;
			tlsMatchPlan.reloadGeneration = reloadGeneration;
			tlsMatchPlan.bindingFingerprint = bindingFingerprint;
			tlsMatchPlan.bindingDependencyFingerprint =
				bindingDependencyFingerprint;
			tlsMatchPlan.bufferContentGeneration =
				DX12GetIaHashContentGeneration();
			tlsMatchPlan.computeShaderHash = computeShaderHash;
			tlsMatchPlan.producer = producer;
			tlsMatchPlan.vlr = vlr;
			tlsMatchPlan.vlrResourceName = vlrResourceName;
			tlsMatchPlan.hashBinding = hashBinding;
			tlsMatchPlan.hashMatch = hashMatch;
			tlsMatchPlan.valid = true;
			tlsMatchPlan.hashMatched = hashMatched;
			tlsMatchPlan.explicitMatchCs = explicitMatchCs;
			tlsMatchPlan.hasExplicitDispatch = hasExplicitDispatch;
			tlsMatchPlan.vlrMatched = vlrMatched;
		}
	}
	DX12_PROF_CPU_STAGE(
		"PreSkinRuntime",
		"mod_lock_match_total",
		0,
		static_cast<LONG64>(uavs.size() + srvs.size() + cbvs.size()),
		DX12_DIAG_ELAPSED_US(matchBegin));
	// A pure match-phase miss depends only on the inputs the match observes
	// (reload/sticky generation, CS hash, live SRV view hashes, live UAV
	// identities), so the caller may cache it on a geometry key. A positive
	// that later fails its descriptor patch is content-dependent and must
	// keep flowing through the full probe every time.
	if (matchPhaseMiss)
		*matchPhaseMiss = !hashMatched && !vlrMatched;

	if (hashMatched)
	{
		// DX11 runs the matched section's command list at match time, which
		// is how per-frame flags like "$active0 = 1" reach [Key] conditions.
		// Replay the section's VM actions here so those conditions can ever
		// become true; without this the cycle key is permanently dead.
		if (hashMatch.config)
			RunPreSkinMatchVmActions(*hashMatch.config);
		// DX11 BeforeDispatch semantics: a match_cs command list runs
		// unconditionally, so cs-tN replacements apply with or without an
		// explicit handling=skip + dispatch= override. Gating the table patch
		// on the explicit dispatch silently dropped DX11-style mods that only
		// set match_cs + cs-tN: the skinning CS kept reading the original
		// vertices while the IA side consumed the mod buffers.
		{
			// Fill the dispatch override unconditionally from the matched
			// config BEFORE running the table patch. The override is a
			// property of the config (handling=skip + dispatch=...), not of
			// the patch result: if the patch succeeds but the override is
			// missing, the dispatch flow below would either skip the CS
			// entirely or run it with the game's original thread-group count
			// while the SRV/CBV are already patched for the mod vertex count,
			// leaving the skinning output partially written and the IA stage
			// reading garbage (exploding/spherical geometry + broken UVs).
			if (hasExplicitDispatch)
				FillPreSkinDispatchOverrideFromConfig(*hashMatch.config, dispatchOverride);
			if (!hashMatch.srvReplacements.empty())
			{
				DX12_DIAG_QPC(patchBegin);
				applied = ApplyPreSkinDescriptorTablePatchLocked(
					device, commandList, hashBinding, producer,
					*hashMatch.config, hashMatch.srvReplacements,
					srvs, cbvs, rootConstants,
					hashMatch.config->originalSection.empty() ? hashMatch.config->section.c_str() : hashMatch.config->originalSection.c_str(),
					hashMatch.hash, originalVertexCount, overrideVertexCount,
					bindingFingerprint, computeShaderHash);
				DX12_PROF_CPU_STAGE(
					"PreSkinRuntime",
					applied ? "descriptor_table_patch_hit" : "descriptor_table_patch_miss",
					0,
					static_cast<LONG64>(hashMatch.srvReplacements.size()),
					DX12_DIAG_ELAPSED_US(patchBegin));
			}
			if (applied)
			{
				DX12_PROF_COUNT("PreSkinApplied");
			}
		}
	}
	if (!applied && vlrMatched)
	{
		DX12_DIAG_QPC(activeSectionsBegin);
		const std::unordered_set<std::wstring> &activePreSkinSections =
			GetActivePreSkinSectionsSnapshot();
		DX12_PROF_CPU_STAGE(
			"PreSkinRuntime",
			"snapshot_active_sections",
			0,
			static_cast<LONG64>(activePreSkinSections.size()),
			DX12_DIAG_ELAPSED_US(activeSectionsBegin));
		DX12_DIAG_QPC(loadBegin);
		replacement = EnsureLoadedResourceForPreSkin(device, vlrResourceName);
		DX12_PROF_CPU_STAGE(
			"PreSkinRuntime",
			replacement ? "vlr_load_resource_hit" : "vlr_load_resource_miss",
			0,
			1,
			DX12_DIAG_ELAPSED_US(loadBegin));
		DX12_DIAG_QPC(ensureUavBegin);
		if (replacement && EnsureResourceUavLocked(
							   device, commandList, replacement,
							   vlr.uavByteStride ? vlr.uavByteStride : vlr.overrideByteStride,
							   0, true, &activePreSkinSections))
		{
			DX12_PROF_CPU_STAGE(
				"PreSkinRuntime",
				"vlr_ensure_uav_hit",
				0,
				1,
				DX12_DIAG_ELAPSED_US(ensureUavBegin));
			DX12_DIAG_QPC(findBindingBegin);
			const DX12CurrentComputeUavBinding *binding = nullptr;
			for (const DX12CurrentComputeUavBinding &uav : uavs)
			{
				if (uav.rootParameterIndex == producer.rootParameterIndex &&
					uav.rangeIndex == producer.rangeIndex &&
					uav.descriptorOffset == producer.descriptorOffset &&
					uav.hasDescriptor && uav.tableGpuStart.ptr)
				{
					binding = &uav;
					break;
				}
			}
			DX12_PROF_CPU_STAGE(
				"PreSkinRuntime",
				binding ? "vlr_find_binding_hit" : "vlr_find_binding_miss",
				0,
				static_cast<LONG64>(uavs.size()),
				DX12_DIAG_ELAPSED_US(findBindingBegin));
			if (binding)
			{
				DX12_DIAG_QPC(bindingPatchBegin);
				applied = ApplyPreSkinBindingPatchLocked(
					device, commandList, *binding, producer, replacement,
					vlr, srvs, cbvs, vlr.section.c_str(), 0,
					originalVertexCount, overrideVertexCount);
				DX12_PROF_CPU_STAGE(
					"PreSkinRuntime",
					applied ? "vlr_binding_patch_hit" : "vlr_binding_patch_miss",
					0,
					1,
					DX12_DIAG_ELAPSED_US(bindingPatchBegin));
				if (applied)
					DX12_PROF_COUNT("PreSkinApplied");
			}
		}
		else
		{
			DX12_PROF_CPU_STAGE(
				"PreSkinRuntime",
				"vlr_ensure_uav_miss",
				0,
				1,
				DX12_DIAG_ELAPSED_US(ensureUavBegin));
		}
	}
	DX12_PROF_COUNT("PreSkinUavTest");
	if (applied)
		DX12_PROF_COUNT("PreSkinUavHit");
	DX12_DIAG_QPC(releaseDeviceBegin);
	if (ownsDevice)
		device->Release();
	DX12_PROF_CPU_STAGE(
		"PreSkinRuntime",
		"apply_release_device",
		0,
		1,
		DX12_DIAG_ELAPSED_US(releaseDeviceBegin));
	return applied;
}



void DX12ModPublishPreSkinOutputResizeEarly(ID3D12GraphicsCommandList *commandList)
{
	// Publish the VA alias before the skinning dispatch runs. Draws recorded
	// on other lists (or earlier on this list) resolve IA views while
	// recording; registering only at restore time (after the dispatch) lets
	// those draws miss the mapping and render the un-skinned original
	// buffer - shadow/body mismatch and flicker. Re-registration from the
	// restore path with equal content is a no-op that does not bump the
	// generation (see DX12ModRegisterPreSkinOutputResize).
	if (!commandList || gHasActivePreSkinReplacement == 0)
		return;
	// Snapshot the fields under the shared lock. The restore path erases this
	// entry under the exclusive lock, so dereferencing the map node after
	// release is a use-after-free the moment an erase ever runs on another
	// thread (today erases happen on the list's own recording thread, but
	// nothing enforces that here).
	ID3D12Resource *originalOutputResource = nullptr;
	ID3D12Resource *outputVertexResource = nullptr;
	UINT64 originalOutputByteWidth = 0;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	bool canPublish = false;
	DX12PreSkinReplacementShard &publishShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_SHARED(&publishShard.lock, "Lock.preSkinShared");
	auto it = publishShard.entries.find(commandList);
	if (it != publishShard.entries.end())
	{
		const DX12PreSkinReplacementState &state = it->second;
		canPublish = state.active && state.outputUavResized &&
					 state.resource && state.outputVertexResource && state.byteWidth &&
					 state.originalOutputResource && state.originalOutputByteWidth;
		if (canPublish)
		{
			originalOutputResource = state.originalOutputResource;
			outputVertexResource = state.outputVertexResource;
			originalOutputByteWidth = state.originalOutputByteWidth;
			byteWidth = state.byteWidth;
			stride = state.stride;
		}
	}
	ReleaseSRWLockShared(&publishShard.lock);
	if (!canPublish)
		return;
	DX12ModRegisterPreSkinOutputResize(
		commandList,
		originalOutputResource,
		outputVertexResource,
		originalOutputByteWidth,
		byteWidth,
		stride);
}


// Command-list-local negative cache entry for the restore probe below: while
// no mutation has happened on THIS list's shard, "this list has no pending
// restore" cannot change. Recording is single-threaded per command list, so
// a thread-local entry keyed by the list pointer plus its shard's mutation
// generation is exact; inserts/erases on other shards no longer invalidate
// it (a pre-skin apply on one list used to force every other recording
// thread back onto the global exclusive lock on its next draw). This keeps
// the per-draw cost at two loads instead of an SRW lock + unordered_map
// lookup on every draw/dispatch/binding mutation while any pre-skin
// replacement is pending anywhere in the process.
class DX12PreSkinTlsNoPendingRestore
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 generation = 0;
};


void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList)
{
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable for the
	// ordering argument. The map lookup below is still lock-protected.
	if (!commandList || gHasActivePreSkinReplacement == 0)
		return;

	thread_local DX12PreSkinTlsNoPendingRestore tlsNoPendingRestore;
	DX12PreSkinReplacementShard &restoreShard =
		PreSkinReplacementShardFor(commandList);
	const UINT64 mutationGeneration =
		restoreShard.mutationGeneration.load(std::memory_order_acquire);
	if (tlsNoPendingRestore.commandList == commandList &&
		tlsNoPendingRestore.generation == mutationGeneration)
		return;

	DX12InternalReplayScope internalReplay;
	DX12PreSkinReplacementState state = {};
	bool found = false;
	bool erased = false;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&restoreShard.lock, "Lock.preSkinExclusive");
	auto it = restoreShard.entries.find(commandList);
	if (it != restoreShard.entries.end())
	{
		state = std::move(it->second);
		restoreShard.entries.erase(it);
		found = state.active;
		erased = true;
		InterlockedDecrement(&gHasActivePreSkinReplacement);
	}
	ReleaseSRWLockExclusive(&restoreShard.lock);
	// Only bump the generation when the map actually changed. This lookup
	// runs on every draw/dispatch of every command list while any pre-skin
	// replacement is pending; an unconditional bump would invalidate the
	// TLS negative cache of every other list on this shard on each call,
	// forcing them back onto the lock plus a map lookup for no reason. Same
	// conditional-bump pattern as DX12ModClearPreSkinOutputResizeMap.
	if (erased)
		BumpPreSkinReplacementShardMutation(&restoreShard);

	// Re-read the generation after the mutation so the negative entry stays
	// valid for exactly the state we just observed.
	tlsNoPendingRestore.commandList = commandList;
	tlsNoPendingRestore.generation =
		restoreShard.mutationGeneration.load(std::memory_order_relaxed);

	if (!found)
		return;
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);

// Self-output (VLR scratch): the dispatch wrote directly into the buffer
// the draws will read through the VA alias, so there is no vertex-twin
// copy. Mirror the leading original-capacity span back into the game's
// original buffer so a missed redirect degrades to un-skinned original
// vertices instead of garbage, then leave the scratch in COMMON for the
// redirected IA reads; the next patch transitions it back to UAV.
if (state.outputUavResized && state.resource &&
state.outputVertexResource && state.byteWidth &&
state.outputVertexResource == state.resource)
{
const bool copyBackOriginal =
state.originalOutputResource && state.originalOutputByteWidth != 0;
if (copyBackOriginal)
{
D3D12_RESOURCE_BARRIER beforeCopy[2] = {};
beforeCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
beforeCopy[0].Transition.pResource = state.resource;
beforeCopy[0].Transition.StateBefore =
D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
beforeCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
beforeCopy[0].Transition.Subresource =
D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
beforeCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
beforeCopy[1].Transition.pResource = state.originalOutputResource;
beforeCopy[1].Transition.StateBefore =
D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
beforeCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
beforeCopy[1].Transition.Subresource =
D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
commandList->ResourceBarrier(2, beforeCopy);
commandList->CopyBufferRegion(
state.originalOutputResource, 0, state.resource, 0,
(std::min)(state.originalOutputByteWidth, state.byteWidth));
D3D12_RESOURCE_BARRIER afterCopy[2] = {};
afterCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
afterCopy[0].Transition.pResource = state.resource;
afterCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
afterCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
afterCopy[0].Transition.Subresource =
D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
afterCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
afterCopy[1].Transition.pResource = state.originalOutputResource;
afterCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
afterCopy[1].Transition.StateAfter =
D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
afterCopy[1].Transition.Subresource =
D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
commandList->ResourceBarrier(2, afterCopy);
}
else
{
D3D12_RESOURCE_BARRIER toCommon = {};
toCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
toCommon.Transition.pResource = state.resource;
toCommon.Transition.StateBefore =
D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
toCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
toCommon.Transition.Subresource =
D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
commandList->ResourceBarrier(1, &toCommon);
}
if (state.loadedReplacement)
state.loadedReplacement->uavState = D3D12_RESOURCE_STATE_COMMON;
DX12ModRegisterPreSkinOutputResize(
commandList,
state.originalOutputResource,
state.outputVertexResource,
state.originalOutputByteWidth,
state.byteWidth,
state.stride);
}

// Keep compute writes and IA reads on separate resources. The copy is ordered
// after dispatch on this producer command list, while later draw lists only
// ever see a COMMON buffer eligible for independent read promotion.
if (state.outputUavResized && state.resource &&
state.outputVertexResource && state.byteWidth &&
state.outputVertexResource != state.resource)
{		// Restore the game invariant on the original output buffer too. The
		// dispatch wrote the resized UAV instead, so the original still holds
		// stale bytes; any consumer whose IA view misses the VA-alias redirect
		// (another command list, or recording after Present cleared the publish
		// map) then reads garbage vertices and explodes into full-screen
		// triangles. Copy the leading original-capacity span back so a missed
		// redirect degrades to un-skinned original vertices instead. The game
		// left this buffer in UNORDERED_ACCESS for the dispatch, and this
		// restore runs before its next command, so transitioning from and back
		// to UAV keeps the game's own barrier bookkeeping intact.
		const bool copyBackOriginal =
			state.originalOutputResource && state.originalOutputByteWidth != 0;
		D3D12_RESOURCE_BARRIER beforeCopy[3] = {};
		beforeCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		beforeCopy[0].Transition.pResource = state.resource;
		beforeCopy[0].Transition.StateBefore =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		beforeCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		beforeCopy[0].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		beforeCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		beforeCopy[1].Transition.pResource = state.outputVertexResource;
		beforeCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		beforeCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		beforeCopy[1].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		UINT beforeCopyCount = 2;
		if (copyBackOriginal)
		{
			beforeCopy[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			beforeCopy[2].Transition.pResource = state.originalOutputResource;
			beforeCopy[2].Transition.StateBefore =
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			beforeCopy[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			beforeCopy[2].Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			beforeCopyCount = 3;
		}
		commandList->ResourceBarrier(beforeCopyCount, beforeCopy);
		commandList->CopyBufferRegion(
			state.outputVertexResource, 0, state.resource, 0, state.byteWidth);
		if (copyBackOriginal)
			commandList->CopyBufferRegion(
				state.originalOutputResource, 0, state.resource, 0,
				(std::min)(state.originalOutputByteWidth, state.byteWidth));

		D3D12_RESOURCE_BARRIER afterCopy[3] = {};
		afterCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		afterCopy[0].Transition.pResource = state.resource;
		afterCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		afterCopy[0].Transition.StateAfter =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		afterCopy[0].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		afterCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		afterCopy[1].Transition.pResource = state.outputVertexResource;
		afterCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		afterCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		afterCopy[1].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		UINT afterCopyCount = 2;
		if (copyBackOriginal)
		{
			afterCopy[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			afterCopy[2].Transition.pResource = state.originalOutputResource;
			afterCopy[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			afterCopy[2].Transition.StateAfter =
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			afterCopy[2].Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			afterCopyCount = 3;
		}
		commandList->ResourceBarrier(afterCopyCount, afterCopy);

		DX12ModRegisterPreSkinOutputResize(
			commandList,
			state.originalOutputResource,
			state.outputVertexResource,
			state.originalOutputByteWidth,
			state.byteWidth,
			state.stride);
	}

	if (state.patchHeap)
	{
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap =
			state.restoreCbvSrvUavHeap ? state.restoreCbvSrvUavHeap : state.originalHeap;
		ID3D12DescriptorHeap *heaps[2] = {};
		UINT heapCount = 0;
		if (restoreCbvSrvUavHeap)
			heaps[heapCount++] = restoreCbvSrvUavHeap;
		if (state.restoreSamplerHeap)
			heaps[heapCount++] = state.restoreSamplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps : nullptr);
		if (runtimeState)
		{
			DX12CommandListRuntime::SetDescriptorHeaps(
				runtimeState, restoreCbvSrvUavHeap, state.restoreSamplerHeap);
		}
		else
		{
			DX12CommandListRuntime::SetDescriptorHeaps(
				commandList, restoreCbvSrvUavHeap, state.restoreSamplerHeap);
		}
		DX12BindingTracker::SetDescriptorHeaps(
			commandList, heapCount, heapCount ? heaps : nullptr);
		for (const auto &restore : state.tableRestores)
		{
			commandList->SetComputeRootDescriptorTable(
				restore.rootParameterIndex, restore.originalTable);
			if (runtimeState)
			{
				DX12CommandListRuntime::RememberRootDescriptorTable(
					runtimeState, false, restore.rootParameterIndex,
					restore.originalTable);
			}
			else
			{
				DX12CommandListRuntime::RememberRootDescriptorTable(
					commandList, false, restore.rootParameterIndex,
					restore.originalTable);
			}
			DX12BindingTracker::SetComputeRootDescriptorTable(
				commandList, restore.rootParameterIndex, restore.originalTable);
		}
		state.patchHeap = nullptr;
	}
	for (ID3D12Resource *resource : state.temporaryResources)
	{
		if (resource)
			RetirePreSkinResourceForCommandList(commandList, resource);
	}
	state.temporaryResources.clear();
	ID3D12Device *device = AcquireModDevice(commandList);
	if (device)
	{
		for (const auto &restore : state.descriptorRestores)
		{
			if (!restore.heap || !restore.cpu.ptr || !restore.targetCpu)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE target = {};
			target.ptr = restore.targetCpu;
			device->CopyDescriptorsSimple(
				1, target, restore.cpu,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		device->Release();
	}
	ReleasePreSkinDescriptorRestores(&state.descriptorRestores);

	if (state.hasActiveTextureOverride && state.resource)
	{
		std::vector<ID3D12Resource *> releasePreSkinResources;
		DX12ActivePreSkinTextureOverride active = {};
		active.config = state.activeConfig;
		active.producer = state.activeProducer;
		active.outputResource = state.resource;
		active.outputByteWidth = state.byteWidth;
		active.outputStride = state.stride;
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinLock, "Lock.preSkinExclusive");
		StoreActivePreSkinTextureOverrideLocked(
			active.config.section, active, &releasePreSkinResources);
		ReleaseSRWLockExclusive(&gPreSkinLock);
		state.resource = nullptr;
		ReleaseMovedPreSkinResources(&releasePreSkinResources);
	}

}


void DX12ModForgetPreSkinReplacementForCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	// Teardown half of the lazy restore: DX12ModRestorePreSkinningUavReplacement
	// only runs on the next dispatch/draw/binding mutation on the same list,
	// so a list that is destroyed or reset while a replacement is still
	// pending would otherwise keep the entry (and every reference it owns)
	// forever, pin gHasActivePreSkinReplacement at 1, and let a recycled list
	// pointer "restore" state belonging to the dead list (ABA). Reset also
	// makes the pending state meaningless: it refers to a recording session
	// and descriptor-heap bindings that no longer exist. This performs the
	// quiet half of the restore only: undo CPU-side descriptor patches and
	// release owned references, recording no GPU commands on the list.
	// Plain volatile probe, see DX12ModIsPreSkinReplacementReusable.
	if (!commandList || gHasActivePreSkinReplacement == 0)
		return;

	DX12PreSkinReplacementState state = {};
	bool erased = false;
	DX12PreSkinReplacementShard &forgetShard =
		PreSkinReplacementShardFor(commandList);
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&forgetShard.lock, "Lock.preSkinExclusive");
	auto it = forgetShard.entries.find(commandList);
	if (it != forgetShard.entries.end())
	{
		state = std::move(it->second);
		forgetShard.entries.erase(it);
		erased = true;
		InterlockedDecrement(&gHasActivePreSkinReplacement);
	}
	ReleaseSRWLockExclusive(&forgetShard.lock);
	if (!erased)
		return;
	// The shard map changed, so other lists' TLS negative caches on this
	// shard must re-check (same conditional-bump contract as the restore path).
	BumpPreSkinReplacementShardMutation(&forgetShard);

	// Undo the CPU-side descriptor patch: the game's heap outlives this
	// command list, so leaving our UAV descriptor behind would bind a
	// recycled mod resource to later draws. restore.device holds the
	// pool-retained reference (AcquirePreSkinDescriptorRestoreHeap AddRefs
	// it), so it stays valid here even while the list is being destroyed.
	for (const auto &restore : state.descriptorRestores)
	{
		if (!restore.device || !restore.cpu.ptr || !restore.targetCpu)
			continue;
		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = restore.targetCpu;
		restore.device->CopyDescriptorsSimple(
			1, target, restore.cpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	ReleasePreSkinDescriptorRestores(&state.descriptorRestores);

	// Retire through the global bucket rather than this list's: nothing is
	// recorded onto the list anymore, so the next submission fence (or the
	// present-count fallback) is a safe release point, and a dead list's
	// per-identity bucket would never be consumed by NotifySubmitted.
	for (ID3D12Resource *resource : state.temporaryResources)
	{
		if (resource)
			RetirePreSkinResource(resource);
	}
	state.temporaryResources.clear();

	// The active-override AddRef normally transfers to the override store
	// during restore; here nothing consumes it, so drop it.
	if (state.hasActiveTextureOverride && state.resource)
		state.resource->Release();
}


// ---------------------------------------------------------------------------
// DX12ModComputePreSkinMatchOnly performs matching off the critical path.
// Runs only the data-structure lookup phase of PreSkinProbe without emitting
// any D3D12 commands.  Safe to call from the async worker thread.
// Populates gPreSkinUavMatchCache so that the next render-thread Dispatch
// hits the cache and applies synchronously (fast path).
// ---------------------------------------------------------------------------
bool DX12ModComputePreSkinMatchOnly(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	bool *outMatched,
	DX12PreSkinDispatchOverride *outDispatchOverride)
{
	if (outMatched)
		*outMatched = false;
	if (outDispatchOverride)
		*outDispatchOverride = DX12PreSkinDispatchOverride();
	if (!DX12ModHasActiveTextureOverrides())
		return false;

	DX12ComputeUavProducer producer = {};
	DX12VertexLimitRaiseConfig vlr = {};
	std::wstring vlrResourceName;
	DX12CurrentComputeUavBinding hashBinding = {};
	DX12UavHashTextureOverrideMatch hashMatch = {};
	bool hashMatched = false;
	bool explicitMatchCs = false;
	bool hasExplicitDispatch = false;
	bool vlrMatched = false;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
	hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
										 uavs, srvs, cbvs, rootConstants, computeShaderHash, &producer, &hashBinding, &hashMatch);
	if (hashMatched && hashMatch.config)
		hasExplicitDispatch = HasExplicitPreSkinDispatchOverride(*hashMatch.config);
	if ((!hashMatched || !hasExplicitDispatch) && !uavs.empty())
	{
		vlrMatched = FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr) &&
					 FindReplacementResourceNameForVlrLocked(vlr, &vlrResourceName);
	}
	ReleaseSRWLockShared(&gModLock);

	bool matched = hashMatched || vlrMatched;
	if (outMatched)
		*outMatched = matched;
	if (matched && outDispatchOverride && hashMatched && hasExplicitDispatch)
	{
		// MatchCs path: populate dispatch override from the matched config.
		// VLR path sets matched=true but does not populate dispatchOverride;
		// the apply phase handles VLR via resource loading and UAV setup.
		FillPreSkinDispatchOverrideFromConfig(*hashMatch.config, outDispatchOverride);
	}
	return matched;
}


bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage)
{
	if (!DX12ModHasActiveShaderOverrides())
		return false;
	if (!stage || !source.pShaderBytecode || source.BytecodeLength == 0 ||
		!replacement || !storage)
		return false;

	const uint64_t hash = DX12ModHashShaderBytecode(
		source.pShaderBytecode, source.BytecodeLength);

	std::wstring section;
	std::wstring shaderFixesDir;
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
	{
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	section = it->second.section;
	shaderFixesDir = gShaderFixesDir;
	ReleaseSRWLockShared(&gModLock);

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.bin",
			   shaderFixesDir.c_str(), static_cast<unsigned long long>(hash), stage);

	std::vector<unsigned char> bytes;
	if (!ReadFileBytes(path, &bytes))
	{
		return false;
	}

	storage->swap(bytes);
	replacement->pShaderBytecode = storage->data();
	replacement->BytecodeLength = storage->size();
	return true;
}


bool DX12ModNeedsPreSkinningUavProbe()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRE_SKIN_PROBE) != 0;
}


// Returns true when pre-skin binding tracking is actively needed.
// Automatically disables after kPreSkinBindingIdleMax consecutive frames
// with zero active overrides, saving ~1500+ lock acquisitions per frame.
// Note: idle counter is reset when a matching pre-skin CS dispatch is probed.
bool DX12ModNeedsPreSkinningBindingTracking()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK) != 0;
}


bool DX12ModHasActivePreSkinTextureOverrides()
{
	if (gDx12SafeMode != 0)
		return false;
	return gHasActivePreSkinTextureOverridesFast != 0;
}


// TLS verdict cache entry for the CS-hash probe below: the configured CS
// hash set only changes on mod reload, so the dispatch hot path does not
// take the global mod lock on every dispatch (this predicate is consulted
// 2-3 times per dispatch and the game rotates through a small CS set).
class DX12PreSkinTlsProbeCsCacheEntry
{
public:
	uint64_t hash = 0;
	bool result = false;
	bool valid = false;
};


bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash)
{
	if (gDx12SafeMode != 0 ||
		gHasTextureOverrides == 0 ||
		gHasShaderTriggeredTextureOverrides == 0 ||
		gHasPreSkinTextureOverrideCandidates == 0)
		return false;

	thread_local std::array<DX12PreSkinTlsProbeCsCacheEntry, 32> tlsProbeCsCache;
	static thread_local UINT64 tlsProbeCsReloadGeneration = 0;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (reloadGeneration != tlsProbeCsReloadGeneration)
	{
		tlsProbeCsCache = {};
		tlsProbeCsReloadGeneration = reloadGeneration;
	}
	const UINT slotMask =
		static_cast<UINT>(tlsProbeCsCache.size()) - 1;
	const UINT slot = static_cast<UINT>(computeShaderHash ^
										(computeShaderHash >> 32)) &
					  slotMask;
	DX12PreSkinTlsProbeCsCacheEntry &tlsEntry = tlsProbeCsCache[slot];
	if (tlsEntry.valid && tlsEntry.hash == computeShaderHash)
		return tlsEntry.result;

	bool shouldProbe = false;
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	if (gHasPreSkinVlrWithoutMatchCs)
	{
		shouldProbe = true;
	}
	else if (computeShaderHash)
	{
		shouldProbe = gPreSkinMatchCsHashes.find(computeShaderHash) !=
					  gPreSkinMatchCsHashes.end();
	}
	ReleaseSRWLockShared(&gModLock);
	tlsEntry.valid = true;
	tlsEntry.hash = computeShaderHash;
	tlsEntry.result = shouldProbe;
	// Reset binding tracking idle counter: as long as a matching CS dispatch
	// runs, keep binding tracking active so the probe can access bindings.
	if (shouldProbe)
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	DX12_PROF_COUNT("PreSkinCsTest");
	return shouldProbe;
}


bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash)
{
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}


bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	(void)uavs;
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}


void UpdatePreSkinningUavMatchCacheLocked(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	UINT64 cacheKey = MakePreSkinUavMatchCacheKey(
		commandList, computeShaderHash, computeBindingSerial,
		bindingDependencyFingerprint);
	DX12PreSkinUavMatchCacheEntry &entry = gPreSkinUavMatchCache[cacheKey];
	bool isNewEntry = !entry.valid;
	entry.computeShaderHash = computeShaderHash;
	entry.computeBindingSerial = computeBindingSerial;
	entry.valid = true;
	entry.matched = matched;
	if (dispatchOverride)
		entry.dispatchOverride = *dispatchOverride;
	if (isNewEntry && gPreSkinUavMatchCache.size() > gPreSkinUavMatchCacheGrowthBudget)
	{
		size_t nextBudget = gPreSkinUavMatchCacheGrowthBudget ? gPreSkinUavMatchCacheGrowthBudget : MaxPreSkinUavMatchCacheEntries;
		while (nextBudget < gPreSkinUavMatchCache.size())
		{
			const size_t doubled = nextBudget * 2;
			if (doubled <= nextBudget)
			{
				nextBudget = gPreSkinUavMatchCache.size();
				break;
			}
			nextBudget = doubled;
		}
		gPreSkinUavMatchCache.reserve(nextBudget);
		gPreSkinUavMatchCacheGrowthBudget = nextBudget;
	}
}


void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (!commandList || !computeShaderHash || computeBindingSerial == 0)
		return;

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinUavMatchCacheLock, "Lock.uavMatchCacheExclusive");
	UpdatePreSkinningUavMatchCacheLocked(
		commandList, computeShaderHash, computeBindingSerial,
		bindingDependencyFingerprint, matched, dispatchOverride);
	ReleaseSRWLockExclusive(&gPreSkinUavMatchCacheLock);
}


bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint,
	DX12PreSkinProbeCacheEntry *result)
{
	if (!result || !commandList || !computeShaderHash || bindingHash == 0)
		return false;

	UINT64 cacheKey = MakePreSkinUavMatchCacheKey(
		commandList, computeShaderHash, bindingHash,
		bindingDependencyFingerprint);
	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinUavMatchCacheLock, "Lock.uavMatchCacheShared");
	auto cached = gPreSkinUavMatchCache.find(cacheKey);
	if (cached != gPreSkinUavMatchCache.end() &&
		cached->second.valid)
	{
		result->applied = cached->second.matched;
		result->dispatchOverride = cached->second.dispatchOverride;
		ReleaseSRWLockShared(&gPreSkinUavMatchCacheLock);
		return true;
	}
	ReleaseSRWLockShared(&gPreSkinUavMatchCacheLock);
	return false;
}


// ---------------------------------------------------------------------------
// Geometry-keyed negative match verdict cache.
//
// The match phase (fresh match_cs SRV view-hash compare, sticky probe, VLR
// producer scan) is a pure function of the reload generation, the CS hash,
// the live SRV view hashes and the live UAV identities. The legacy verdict
// caches additionally key on descriptor-ring generations and the global
// buffer-content generation, which the game invalidates with every
// descriptor rewrite / buffer copy, so they can never hit across frames.
// Keying strictly on the inputs the match observes lets the repeated
// non-matching dispatches (every non-modded character sharing the skinning
// CS) skip the fingerprint build, the legacy lookups and the apply/store
// round trip. The sticky probe is the only match input not folded into the
// key, so callers must gate a hit on MayMatchPreSkinStickyMatch returning
// false. Only match-phase misses are stored; a positive that fails its
// descriptor patch is content-dependent and always re-probed.
// ---------------------------------------------------------------------------
static SRWLOCK gPreSkinNegativeMatchLock = SRWLOCK_INIT;
static std::unordered_set<uint64_t> gPreSkinNegativeMatchKeys;
static constexpr size_t kPreSkinNegativeMatchMaxKeys = 4096;

uint64_t DX12ModMakePreSkinNegativeMatchKey(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs)
{
	if (!computeShaderHash)
		return 0;
	static thread_local std::vector<uint64_t> srvHashes;
	srvHashes.clear();
	if (srvHashes.capacity() < srvs.size())
		srvHashes.reserve(srvs.size());
	// Exactly the fresh-match inputs: FindComputeSrvByRegister only considers
	// buffer SRVs at a shader register and only compares the view-geometry
	// hash, so nothing else may enter the key (volatile extras - source
	// offset, ring location - would just fragment it). Sorted so table
	// iteration order cannot fragment the key.
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
			srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;
		uint64_t bindingHash = 0x517cc1b727220a95ull;
		bindingHash = HashCombine64(bindingHash, srv.shaderRegister);
		bindingHash = HashCombine64(bindingHash, HashComputeBufferBinding(srv));
		srvHashes.push_back(bindingHash);
	}
	std::sort(srvHashes.begin(), srvHashes.end());
	uint64_t key = 0x6a09e667f3bcc909ull;
	key = HashCombine64(key, DX12ModGetReloadGenerationFast());
	key = HashCombine64(key, computeShaderHash);
	key = HashCombine64(key, srvHashes.size());
	for (uint64_t bindingHash : srvHashes)
		key = HashCombine64(key, bindingHash);
	// UAV identity: the sticky key fields plus the candidacy bits the match
	// and VLR scans read. Ring addresses only contribute their zero-ness;
	// HashComputeBufferBinding covers the view shape.
	for (const DX12CurrentComputeUavBinding &uav : uavs) {
		key = HashCombine64(key, uav.rootParameterIndex);
		key = HashCombine64(key, uav.rangeIndex);
		key = HashCombine64(key, uav.shaderRegister);
		key = HashCombine64(key, uav.registerSpace);
		key = HashCombine64(key, uav.descriptorOffset);
		key = HashCombine64(key, uav.rootDescriptor ? 1 : 0);
		key = HashCombine64(key, uav.hasDescriptor ? 1 : 0);
		key = HashCombine64(key, HashComputeBufferBinding(uav));
		key = HashCombine64(key, uav.descriptorIncrement ? 1 : 0);
		key = HashCombine64(key, uav.tableCpuStart ? 1 : 0);
		key = HashCombine64(key, uav.tableGpuStart.ptr ? 1 : 0);
		key = HashCombine64(key,
			reinterpret_cast<uintptr_t>(uav.descriptor.resource));
	}
	if (!key)
		key = 1;
	return key;
}

bool DX12ModTestPreSkinNegativeMatch(uint64_t key)
{
	if (!key)
		return false;
	AcquireSRWLockShared(&gPreSkinNegativeMatchLock);
	const bool found =
		gPreSkinNegativeMatchKeys.find(key) != gPreSkinNegativeMatchKeys.end();
	ReleaseSRWLockShared(&gPreSkinNegativeMatchLock);
	return found;
}

void DX12ModStorePreSkinNegativeMatch(uint64_t key)
{
	if (!key)
		return;
	AcquireSRWLockExclusive(&gPreSkinNegativeMatchLock);
	// Stale entries are unreachable anyway (the key carries the reload and
	// sticky generations), so a full cache simply starts over.
	if (gPreSkinNegativeMatchKeys.size() >= kPreSkinNegativeMatchMaxKeys &&
		gPreSkinNegativeMatchKeys.find(key) == gPreSkinNegativeMatchKeys.end())
		gPreSkinNegativeMatchKeys.clear();
	gPreSkinNegativeMatchKeys.insert(key);
	ReleaseSRWLockExclusive(&gPreSkinNegativeMatchLock);
}


// Returns true when every candidate has been loaded. The preload used to run
// to completion in a single Present (synchronous file I/O + resource creation
// for every mod buffer at once), which froze the first frame for seconds with
// a large mod. Loading is now spread over several frames, kPreloadBatchSize
// resources at a time; the caller invokes this once per frame until it
// returns true.
bool PreloadPreSkinReplacementResources(ID3D12Device *device)
{
	if (!device)
		return true;
	static constexpr size_t kPreloadBatchSize = 32;
	// Collect every buffer-like mod resource under the shared mod lock
	// (stable within a reload generation), then load each once outside the
	// lock. This covers preSkinCsSrvResources, vertexBufferResources, VLR
	// candidates and any other lazy EnsureLoadedResource* path, and moves the
	// first-match synchronous file I/O + resource creation off the
	// command-recording thread to the frame boundary, spread across frames.
	std::vector<std::wstring> names;
	{
		DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
		names.reserve(gModResources.size());
		for (const auto &entry : gModResources)
		{
			if (ResourceConfigLooksLikeBuffer(entry.second))
				names.push_back(entry.first);
		}
		ReleaseSRWLockShared(&gModLock);
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());

	// Progress state is reset when the reload generation changes (the caller
	// only invokes this for a new generation, but keep the guard here so a
	// re-entrant call after reload cannot resume stale offsets).
	static UINT64 sPreloadGeneration = 0;
	static size_t sPreloadNextIndex = 0;
	const UINT64 generation = DX12ModGetReloadGenerationFast();
	if (sPreloadGeneration != generation) {
		sPreloadGeneration = generation;
		sPreloadNextIndex = 0;
	}

	const size_t end = (std::min)(
		names.size(), sPreloadNextIndex + kPreloadBatchSize);
	for (; sPreloadNextIndex < end; ++sPreloadNextIndex)
	{
		const std::wstring &name = names[sPreloadNextIndex];
		DX12LoadedResource *resource =
			EnsureLoadedResourceForPreSkin(device, name);
		if (resource && resource->resource && resource->stride &&
			resource->byteWidth)
			EnsureResourceSrvLocked(
				device, resource, resource->stride, resource->byteWidth);
	}
	return sPreloadNextIndex >= names.size();
}


// (byteWidth, stride) pair offered by a configured pre-skin texture override,
// used by the preload pass below to size the output-resize cache.
struct PreSkinOutputResizeCandidate
{
	UINT64 byteWidth = 0;
	UINT stride = 0;
};


bool PreloadPreSkinOutputResizeCache(ID3D12Device *device)
{
	if (!device || gHasPreSkinTextureOverrideCandidates == 0)
		return false;

	std::vector<PreSkinOutputResizeCandidate> candidates;
	{
		DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
		candidates.reserve(gPreSkinTextureOverrides.size() * 2);
		for (const DX12PreSkinTextureOverrideCandidate &overrideCandidate :
				gPreSkinTextureOverrides)
		{
			const Bunny::TextureOverrideConfig *config = overrideCandidate.config;
			if (!config || !IsComputePreSkinTextureOverride(*config))
				continue;
			for (const auto &entry : config->preSkinCsSrvResources)
			{
				auto loaded = gLoadedResources.find(entry.second);
				if (loaded == gLoadedResources.end() || loaded->second.failed ||
				    !loaded->second.resource || !loaded->second.byteWidth ||
				    !loaded->second.stride)
					continue;
				candidates.push_back({
					loaded->second.byteWidth,
					loaded->second.stride});
			}
		}
		ReleaseSRWLockShared(&gModLock);
	}
	if (candidates.empty())
		return false;

	std::sort(candidates.begin(), candidates.end(),
		[](const PreSkinOutputResizeCandidate &a,
		   const PreSkinOutputResizeCandidate &b) {
			if (a.byteWidth != b.byteWidth)
				return a.byteWidth < b.byteWidth;
			return a.stride < b.stride;
		});
	candidates.erase(
		std::unique(candidates.begin(), candidates.end(),
			[](const PreSkinOutputResizeCandidate &a,
			   const PreSkinOutputResizeCandidate &b) {
				return a.byteWidth == b.byteWidth && a.stride == b.stride;
			}),
		candidates.end());

	std::vector<DX12ResourceSummary> resources;
	DX12GetRetainedTrackedResourceSnapshot(&resources);
	bool madeProgress = false;
	// Cap the synchronous GPU resource creation per frame: the previous
	// implementation created every missing entry in a single Present, which
	// froze the frame boundary for milliseconds at a time while the preload
	// warmed up.
	static constexpr UINT kPreloadMaxCreatePerFrame = 8;
	UINT createdThisFrame = 0;
	for (const DX12ResourceSummary &resourceSummary : resources)
	{
		if (createdThisFrame >= kPreloadMaxCreatePerFrame)
			break;
		if (!resourceSummary.resource || !resourceSummary.hasResourceDesc ||
			resourceSummary.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			(resourceSummary.resourceDesc.Flags &
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
			continue;

		for (const PreSkinOutputResizeCandidate &candidate : candidates)
		{
			if (!candidate.stride ||
				resourceSummary.resourceDesc.Width >= candidate.byteWidth ||
				resourceSummary.resourceDesc.Width + candidate.stride <
					candidate.byteWidth)
				continue;
			const UINT64 elementCount64 = candidate.byteWidth / candidate.stride;
			if (elementCount64 == 0 ||
				elementCount64 > static_cast<UINT64>(UINT_MAX))
				continue;

			ID3D12Resource *resizedUav = nullptr;
			ID3D12Resource *resizedVertex = nullptr;
			ID3D12DescriptorHeap *resizedHeap = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE resizedCpu = {};
			bool cacheHit = false;
			UINT ringSlot = 0;
			if (!AcquirePreSkinOutputResizeCacheEntry(
					device, nullptr, resourceSummary.resource,
					candidate.byteWidth, candidate.stride,
					static_cast<UINT>(elementCount64),
					0, // no producer view info at preload time
					&resizedUav, &resizedVertex, &resizedHeap, &resizedCpu,
					&cacheHit, &ringSlot))
				continue;

			DX12ModRegisterPreSkinOutputResize(
				nullptr, resourceSummary.resource, resizedVertex,
				resourceSummary.resourceDesc.Width,
				candidate.byteWidth, candidate.stride);
			if (!cacheHit) {
				madeProgress = true;
				++createdThisFrame;
			}
			if (resizedUav)
				resizedUav->Release();
			if (resizedVertex)
				resizedVertex->Release();
			if (resizedHeap)
				resizedHeap->Release();
			break;
		}
		if (createdThisFrame >= kPreloadMaxCreatePerFrame)
			break;
	}
	DX12ReleaseTrackedResourceSnapshot(&resources);
	return madeProgress;
}


void BumpPreSkinOutputResizeGeneration()
{
	UINT64 current = gPreSkinOutputResizeGeneration.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gPreSkinOutputResizeGeneration.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}


IUnknown *PreSkinOutputResizeCommandListIdentity(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	IUnknown *identity = DX12CommandListRuntime::GetCommandListIdentity(commandList);
	return identity ? identity : commandList;
}


ID3D12Resource *StorePreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	const DX12PreSkinOutputResizeEntry &entry)
{
	if (!entries || !entry.originalGpuVa || !entry.resizedResource)
		return nullptr;
	ID3D12Resource *oldResource = nullptr;
	auto oldIt = entries->find(entry.originalGpuVa);
	if (oldIt != entries->end())
		oldResource = oldIt->second.resizedResource;
	entry.resizedResource->AddRef();
	(*entries)[entry.originalGpuVa] = entry;
	return oldResource;
}


void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride)
{
	if (!originalResource || !resizedResource)
		return;
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	const UINT64 resizedGpuVa = resizedResource->GetGPUVirtualAddress();
	if (!originalGpuVa || !resizedGpuVa)
		return;
	if (!originalByteWidth) {
		const D3D12_RESOURCE_DESC desc = originalResource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			originalByteWidth = desc.Width;
	}
	if (!originalByteWidth || !resizedByteWidth)
		return;
	DX12PreSkinOutputResizeEntry entry;
	entry.resizedResource = resizedResource;
	entry.originalGpuVa = originalGpuVa;
	entry.originalByteWidth = originalByteWidth;
	entry.resizedByteWidth = resizedByteWidth;
	entry.stride = stride;
	entry.resizedGpuVa = resizedGpuVa;

	// The fallback map is safe only because the resize cache keeps one stable
	// replacement for each original output resource. Producer-local entries make
	// lookup precise when dispatch and draw share a command list; consumers on a
	// different list resolve the same stable resource through the global entry.
	// Do not publish a rotating per-frame allocation here: IA bindings contain
	// only the original GPU VA and cannot identify which ring slot produced them.
	ID3D12Resource *oldGlobalResource = nullptr;
	ID3D12Resource *oldCommandListResource = nullptr;
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeLock, "Lock.outputResizeExclusive");
	// Bump the generation only when a published mapping actually changes.
	// The resize maps are rebuilt every frame (Present clears them, each
	// matching dispatch re-registers the same stable replacement); bumping on
	// every store made RedirectVertexBufferViews' negative cache miss on
	// every matching draw and redo the full GPU-VA lookup + barriers three
	// times a frame (~2ms). A content-equal re-registration keeps the cached
	// negative valid: the redirection outcome depends only on
	// resizedGpuVa/width/stride, so identical values yield identical results.
	// Both the per-command-list and global maps are checked because the
	// resolver consults local first, then global.
	bool resizeMappingChanged = true;
	{
		auto globalIt = gPreSkinOutputResizeMap.find(entry.originalGpuVa);
		if (globalIt != gPreSkinOutputResizeMap.end()) {
			const DX12PreSkinOutputResizeEntry &old = globalIt->second;
			resizeMappingChanged =
				old.resizedGpuVa != entry.resizedGpuVa ||
				old.originalByteWidth != entry.originalByteWidth ||
				old.resizedByteWidth != entry.resizedByteWidth ||
				old.stride != entry.stride;
			if (!resizeMappingChanged && commandListIdentity) {
				auto localIt =
					gPreSkinOutputResizeCommandListMaps.find(commandListIdentity);
				if (localIt != gPreSkinOutputResizeCommandListMaps.end()) {
					auto localEntryIt = localIt->second.find(entry.originalGpuVa);
					if (localEntryIt != localIt->second.end()) {
						const DX12PreSkinOutputResizeEntry &oldLocal =
							localEntryIt->second;
						resizeMappingChanged =
							oldLocal.resizedGpuVa != entry.resizedGpuVa ||
							oldLocal.originalByteWidth != entry.originalByteWidth ||
							oldLocal.resizedByteWidth != entry.resizedByteWidth ||
							oldLocal.stride != entry.stride;
					}
				}
			}
		}
	}
	oldGlobalResource =
		StorePreSkinOutputResizeEntryLocked(&gPreSkinOutputResizeMap, entry);
	if (commandListIdentity) {
		oldCommandListResource = StorePreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeCommandListMaps[commandListIdentity], entry);
	}
	gHasPreSkinOutputResizeBindings.store(true, std::memory_order_release);
	if (resizeMappingChanged)
		BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	if (oldGlobalResource)
		oldGlobalResource->Release();
	if (oldCommandListResource)
		oldCommandListResource->Release();
}


DX12PreSkinOutputResizeEntry *FindPreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	UINT64 viewGpuVa,
	UINT64 *sourceOffset)
{
	if (sourceOffset)
		*sourceOffset = 0;
	if (!entries)
		return nullptr;
	auto exactIt = entries->find(viewGpuVa);
	if (exactIt != entries->end())
		return &exactIt->second;
	for (auto &item : *entries) {
		DX12PreSkinOutputResizeEntry &candidate = item.second;
		if (!candidate.originalGpuVa || !candidate.originalByteWidth ||
		    viewGpuVa < candidate.originalGpuVa)
			continue;
		const UINT64 candidateOffset = viewGpuVa - candidate.originalGpuVa;
		if (candidateOffset >= candidate.originalByteWidth)
			continue;
		if (sourceOffset)
			*sourceOffset = candidateOffset;
		return &candidate;
	}
	return nullptr;
}


bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	DX12_LOCK_ACQUIRE_SHARED(&gPreSkinOutputResizeLock, "Lock.outputResizeShared");
	DX12PreSkinOutputResizeEntry *entry = nullptr;
	UINT64 sourceOffset = 0;
	if (commandListIdentity) {
		auto local = gPreSkinOutputResizeCommandListMaps.find(commandListIdentity);
		if (local != gPreSkinOutputResizeCommandListMaps.end()) {
			entry = FindPreSkinOutputResizeEntryLocked(
				&local->second, viewGpuVa, &sourceOffset);
		}
	}
	if (!entry)
		entry = FindPreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeMap, viewGpuVa, &sourceOffset);
	if (!entry || !entry->resizedResource || sourceOffset >= entry->resizedByteWidth) {
		ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
		// A miss is expected when Present/FG occurs between producer and consumer
		// recording. Do not turn this into an immediate failure or republish a raw
		// GPU VA globally; resolve through the cache that owns both COM lifetime and
		// queue-fence completion for the stable replacement resource.
		return DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
			commandList, viewGpuVa, binding);
	}
	entry->resizedResource->AddRef();
	binding->resizedResource = entry->resizedResource;
	binding->retainedResource = true;
	binding->originalGpuVa = entry->originalGpuVa;
	binding->originalByteWidth = entry->originalByteWidth;
	binding->resizedGpuVa = entry->resizedGpuVa;
	binding->resizedByteWidth = entry->resizedByteWidth;
	binding->sourceOffset = sourceOffset;
	binding->stride = entry->stride;
	binding->uavBarrierPending = false;
	binding->transitionToVertex = false;
	binding->stateBefore = D3D12_RESOURCE_STATE_COMMON;
	ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
	DX12ModTrackPreSkinOutputResizeCacheUse(
		commandList, binding->resizedResource);
	return true;
}


UINT64 DX12ModGetPreSkinOutputResizeGeneration()
{
	return gPreSkinOutputResizeGeneration.load(std::memory_order_acquire);
}


bool DX12ModHasPreSkinOutputResizeBindings()
{
	return gHasPreSkinOutputResizeBindings.load(std::memory_order_acquire);
}


// Plain volatile probe: the flag only ever goes 0->1 on the first cache entry
// after a clear and is reset under the cache lock, so a stale 1 is
// conservative (the IA replay gate does a lookup and finds nothing), never a
// missed redirect. Present clears the frame-local publish map, so this
// fence-owned-cache hint is what keeps the gate trying when producer and
// consumer record on opposite sides of that boundary.
bool DX12ModHasPreSkinOutputResizeCacheEntries()
{
	return InterlockedCompareExchange(
		&gHasPreSkinOutputResizeCacheEntriesFast, 0, 0) != 0;
}


void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	DX12ModAbandonPreSkinOutputResizeCacheUses(commandList);
	// No resize bindings published this frame: the per-list map is
	// guaranteed empty (the flag is stored under gPreSkinOutputResizeLock
	// on every publish and only cleared when both maps are emptied), so
	// skip the identity unwrap and the global exclusive lock entirely.
	// Any entry for this list's identity is published by the thread
	// recording this list, which is externally synchronized with Reset.
	if (!gHasPreSkinOutputResizeBindings.load(std::memory_order_acquire))
		return;
	IUnknown *identity = PreSkinOutputResizeCommandListIdentity(commandList);
	if (!identity)
		return;
	std::vector<ID3D12Resource*> releaseResources;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeLock, "Lock.outputResizeExclusive");
	auto found = gPreSkinOutputResizeCommandListMaps.find(identity);
	if (found != gPreSkinOutputResizeCommandListMaps.end()) {
		releaseResources.reserve(found->second.size());
		for (auto &item : found->second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
		}
		gPreSkinOutputResizeCommandListMaps.erase(found);
		BumpPreSkinOutputResizeGeneration();
	}
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}


void DX12ModClearPreSkinOutputResizeMap()
{
	std::vector<ID3D12Resource*> releaseResources;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinOutputResizeLock, "Lock.outputResizeExclusive");
	// Plan fix G: only bump the resize generation when entries were actually
	// cleared. Present calls this every frame; unconditionally bumping made
	// the per-draw pre-skin replay gate and slot negative caches (keyed on
	// the resize generation) miss every frame even when no resize happened,
	// forcing the full GPUVA lookup chain on every matching draw.
	const bool hadContent =
		!gPreSkinOutputResizeMap.empty() || !gPreSkinOutputResizeCommandListMaps.empty();
	for (auto &item : gPreSkinOutputResizeMap) {
		if (item.second.resizedResource)
			releaseResources.push_back(item.second.resizedResource);
		item.second.resizedResource = nullptr;
	}
	for (auto &commandList : gPreSkinOutputResizeCommandListMaps) {
		for (auto &item : commandList.second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
			item.second.resizedResource = nullptr;
		}
	}
	gPreSkinOutputResizeMap.clear();
	gPreSkinOutputResizeCommandListMaps.clear();
	gHasPreSkinOutputResizeBindings.store(false, std::memory_order_release);
	if (hadContent)
		BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}
