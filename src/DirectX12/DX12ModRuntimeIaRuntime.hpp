static void ReleaseLoadedResourcesLocked()
{
	for (auto &item : gLoadedResources) {
		DX12RetiredLoadedResource retired;
		retired.resource = item.second;
		retired.retirePresent = DX12RuntimeState::GetPresentCount();
		gRetiredLoadedResources.push_back(retired);
		item.second = DX12LoadedResource();
	}
	gLoadedResources.clear();
}

static void MoveExpiredRetiredLoadedResourcesLocked(
	std::vector<DX12RetiredLoadedResource> *expired)
{
	if (!expired)
		return;
	expired->clear();
	if (InterlockedCompareExchange(&gActiveLoadedResourceSnapshots, 0, 0) != 0)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	auto it = gRetiredLoadedResources.begin();
	while (it != gRetiredLoadedResources.end()) {
		if (present - it->retirePresent < RetiredLoadedResourcePresentDelay) {
			++it;
			continue;
		}
		expired->push_back(*it);
		it = gRetiredLoadedResources.erase(it);
	}
}

static void ReleaseLoadedResourceObjects(DX12LoadedResource *resource)
{
	if (!resource)
		return;
	if (resource->resource) {
		resource->resource->Release();
		resource->resource = nullptr;
	}
	if (resource->uavResource) {
		RetirePreSkinResource(resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeap(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
}

static DX12IaHashState EmptyIaHashState();
static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);
static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews);

static bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (!config.hasMatchCs && !config.hasCsHash)
		return true;
	return activePreSkinSections.find(config.section) != activePreSkinSections.end();
}

void DX12ModBeginFrame()
{
	std::vector<DX12RetiredLoadedResource> expired;
	std::vector<ID3D12Resource*> releasePreSkinResources;
	AcquireSRWLockExclusive(&gPreSkinLock);
	// Track idle frames: if no active pre-skin overrides existed last frame, count idle.
	// If pre-skin was actively matching, reset idle counter immediately.
	if (gActivePreSkinTextureOverrides.empty()) {
		LONG idle = InterlockedIncrement(&gPreSkinBindingIdleFrames);
		if (idle > kPreSkinBindingIdleMax)
			InterlockedExchange(&gPreSkinBindingIdleFrames, kPreSkinBindingIdleMax);
	} else {
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	}
	ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinLock);

	AcquireSRWLockExclusive(&gModLock);
	for (auto &item : gLoadedResources) {
		item.second.uavWritten = false;
		item.second.uavValid = false;
	}
	gPreSkinSectionAppliedPresent.clear();
	MoveExpiredRetiredLoadedResourcesLocked(&expired);
	ReleaseSRWLockExclusive(&gModLock);
	for (DX12RetiredLoadedResource &resource : expired)
		ReleaseLoadedResourceObjects(&resource.resource);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);
}

bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present) {
			device->Release();
			return false;
		}
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
	}

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	bool changed = RunNamedCommandList(
		device, commandList, L"Present", replacement, &pendingResourceViews);
	DX12IaHashState emptyIa = EmptyIaHashState();
	changed |= ApplyPendingResourceViewsForCommandList(
		device, commandList, emptyIa, pendingResourceViews, replacement);
	device->Release();
	return changed;
}

static DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.firstInstance = firstInstance;
	return key;
}

static const Bunny::TextureOverrideConfig *ConfigFromIaCandidateIndexLocked(size_t index)
{
	if (index >= gIaTextureOverrides.size())
		return nullptr;
	return &gIaTextureOverrides[index].config;
}

static bool TryFindCachedIaTextureOverrideLocked(
	const DX12TextureOverrideLookupCacheKey &key,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	auto cached = gTextureOverrideLookupCache.find(key);
	if (cached == gTextureOverrideLookupCache.end())
		return false;
	if (!cached->second.matched)
		return true;
	if (configs)
		configs->reserve(cached->second.iaCandidateIndexes.size());
	for (size_t index : cached->second.iaCandidateIndexes) {
		const Bunny::TextureOverrideConfig *cachedConfig =
			ConfigFromIaCandidateIndexLocked(index);
		if (!cachedConfig)
			return false;
		if (configs)
			configs->push_back(*cachedConfig);
	}
	return true;
}

static void StoreIaTextureOverrideLookupCacheLocked(
	const DX12TextureOverrideLookupCacheKey &key, const std::vector<size_t> &candidateIndexes)
{
	if (gTextureOverrideLookupCache.size() >= MaxTextureOverrideLookupCacheEntries)
		gTextureOverrideLookupCache.clear();
	DX12TextureOverrideLookupCacheEntry entry = {};
	entry.matched = !candidateIndexes.empty();
	entry.iaCandidateIndexes = candidateIndexes;
	gTextureOverrideLookupCache[key] = entry;
}

static bool FindIaTextureOverridesCachedLocked(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	const std::unordered_set<std::wstring> &activePreSkinSections,
	UINT64 preSkinGeneration,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, preSkinGeneration);
	if (TryFindCachedIaTextureOverrideLocked(key, configs))
		return configs && !configs->empty();

	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverrideCandidatesLocked(hash, indexBuffer, vertexSlot);
	if (!candidateIndexes || candidateIndexes->empty()) {
		StoreIaTextureOverrideLookupCacheLocked(key, std::vector<size_t>());
		return false;
	}

	std::vector<size_t> matchedIndexes;
	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!TextureOverrideMatchCsSatisfied(config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(config, indexBuffer, vertexSlot))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		matchedIndexes.push_back(index);
		if (configs)
			configs->push_back(config);
	}

	StoreIaTextureOverrideLookupCacheLocked(key, matchedIndexes);
	return !matchedIndexes.empty();
}

static bool FindIaTextureOverridesCached(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	std::unordered_set<std::wstring> activePreSkinSections;
	const UINT64 preSkinGeneration =
		SnapshotActivePreSkinSections(&activePreSkinSections);

	AcquireSRWLockShared(&gModLock);
	DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, preSkinGeneration);
	if (TryFindCachedIaTextureOverrideLocked(key, configs)) {
		const bool matched = configs && !configs->empty();
		ReleaseSRWLockShared(&gModLock);
		return matched;
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	const bool matched =
		FindIaTextureOverridesCachedLocked(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, indexBuffer, vertexSlot,
			activePreSkinSections, preSkinGeneration, configs);
	ReleaseSRWLockExclusive(&gModLock);
	return matched;
}

static const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot == slot)
			return &buffer;
	}
	return nullptr;
}

static bool TargetStageIsCompute(Bunny::CommandListShaderStage stage)
{
	return stage == Bunny::CommandListShaderStage::Compute;
}

static D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
	Bunny::CommandListTargetKind kind)
{
	switch (kind) {
	case Bunny::CommandListTargetKind::ConstantBuffer:
		return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	case Bunny::CommandListTargetKind::ShaderResource:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	case Bunny::CommandListTargetKind::UnorderedAccessView:
		return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	default:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}
}

static bool DescriptorBindingMatchesTarget(
	const DX12CurrentShaderResourceBinding &binding,
	const Bunny::CommandListTarget &target)
{
	if (binding.shaderRegister != target.slot || binding.registerSpace != 0)
		return false;
	if (binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL)
		return true;
	if (target.stage == Bunny::CommandListShaderStage::Vertex)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX;
	if (target.stage == Bunny::CommandListShaderStage::Pixel)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL;
	if (target.stage == Bunny::CommandListShaderStage::Compute)
		return true;
	return false;
}

static uint32_t HashDescriptorBinding(const DX12CurrentShaderResourceBinding &binding)
{
	if (binding.hasDescriptor)
		return DX12HashDescriptorBufferView(
			&binding.descriptor, binding.gpuVirtualAddress,
			binding.descriptor.viewSize);
	if (binding.rootDescriptor && binding.gpuVirtualAddress) {
		DX12BufferResourceSummary summary = {};
		const UINT64 probeSize = 1;
		if (DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, probeSize, &summary)) {
			const UINT64 size = summary.hasResourceDesc ?
				summary.resourceDesc.Width - summary.resourceOffset : summary.viewSize;
			return DX12HashBufferResourceView(
				&summary, binding.gpuVirtualAddress, size ? size : probeSize);
		}
		return DX12HashBufferResourceView(nullptr, binding.gpuVirtualAddress, probeSize);
	}
	return 0;
}

static bool FindTextureOverridesByHashLocked(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const std::unordered_set<std::wstring> &activePreSkinSections,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	if (!hash)
		return false;

	auto bucket = gTextureOverrides.find(hash);
	if (bucket == gTextureOverrides.end())
		return false;

	for (const Bunny::TextureOverrideConfig &config : bucket->second) {
		if (!TextureOverrideMatchCsSatisfied(config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		if (configs)
			configs->push_back(config);
	}
	return configs && !configs->empty();
}

static bool FindTextureOverridesByHash(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	std::unordered_set<std::wstring> activePreSkinSections;
	SnapshotActivePreSkinSections(&activePreSkinSections);

	AcquireSRWLockShared(&gModLock);
	const bool matched =
		FindTextureOverridesByHashLocked(
			hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, activePreSkinSections, configs);
	ReleaseSRWLockShared(&gModLock);
	return matched;
}

static bool FindDescriptorTextureOverrides(
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	if (!commandList)
		return false;

	std::vector<DX12CurrentShaderResourceBinding> bindings;
	const bool compute = TargetStageIsCompute(target.stage);
	if (!DX12BindingTracker::GetCurrentShaderResourceBindings(
		    commandList, compute, DescriptorRangeTypeForTarget(target.kind), &bindings))
		return false;

	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		if (!DescriptorBindingMatchesTarget(binding, target))
			continue;
		const uint32_t hash = HashDescriptorBinding(binding);
		std::vector<Bunny::TextureOverrideConfig> bindingConfigs;
		if (FindTextureOverridesByHash(
			    hash, vertexCount, indexCount, instanceCount,
			    firstVertex, firstIndex, 0, &bindingConfigs) && configs)
			configs->insert(configs->end(), bindingConfigs.begin(), bindingConfigs.end());
	}
	return configs && !configs->empty();
}

class DX12IaResourceViewSnapshot

{

public:
	ID3D12Resource *resource = nullptr;
	DX12LoadedResource metadata;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
	bool retained = false;
};

static void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource)
		return;
	if (snapshot->retained)
		snapshot->resource->Release();
	snapshot->resource = nullptr;
	snapshot->retained = false;
}

static void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource || snapshot->retained)
		return;
	snapshot->resource->AddRef();
	snapshot->retained = true;
}

class DX12LoadedResourceSnapshotReadScope

{

public:
	DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedIncrement(&gActiveLoadedResourceSnapshots);
	}

	~DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedDecrement(&gActiveLoadedResourceSnapshots);
	}
};

static void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	ReleaseIaResourceViewSnapshot(snapshot);
	if (!resource)
		return;
	if (resource->uavResource && resource->uavValid) {
		snapshot->resource = resource->uavResource;
		snapshot->metadata = *resource;
		snapshot->uavBarrierPending = resource->uavBarrierPending;
		if (resource->uavState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
			snapshot->transitionToVertex = true;
			snapshot->stateBefore = resource->uavState;
			resource->uavState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		resource->uavBarrierPending = false;
		return;
	}
	if (resource->srvResource && resource->srvByteWidth > resource->byteWidth) {
		snapshot->resource = resource->srvResource;
		snapshot->metadata = *resource;
		return;
	}
	if (resource->resource) {
		snapshot->resource = resource->resource;
		snapshot->metadata = *resource;
	}
}

static UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}
static void LogIaResourceBindLimited(
	const char *target, uint32_t slot, const DX12LoadedResource &resource,
	ID3D12Resource *iaResource)
{
#if defined(_DEBUG)
	const bool usingUav = resource.uavResource && iaResource == resource.uavResource;
	const bool usingSrv = resource.srvResource && iaResource == resource.srvResource;
	DX12LogManager::DebugJsonFunc("DX12IaResourceBind",
		"\"target\":\"%s\",\"slot\":%u,\"resource\":\"%S\",\"source\":\"%s\",\"bytes\":%llu,\"viewBytes\":%u,\"uavBytes\":%llu,\"stride\":%u,\"gpuVa\":\"0x%llx\"",
		target ? target : "", slot, resource.name.c_str(),
		usingUav ? "uav" : (usingSrv ? "srv" : "upload"),
		static_cast<unsigned long long>(resource.byteWidth),
		IaViewByteWidthForResource(resource, iaResource),
		static_cast<unsigned long long>(resource.uavByteWidth),
		resource.stride,
		iaResource ? static_cast<unsigned long long>(iaResource->GetGPUVirtualAddress()) : 0ull);
#else
	(void)target;
	(void)slot;
	(void)resource;
	(void)iaResource;
#endif
}

static void ApplyIaResourceBarriers(
	ID3D12GraphicsCommandList *commandList, const DX12IaResourceViewSnapshot &snapshot)
{
	if (!commandList || !snapshot.resource)
		return;
	if (snapshot.uavBarrierPending) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = snapshot.resource;
		commandList->ResourceBarrier(1, &barrier);
	}
	if (snapshot.transitionToVertex) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = snapshot.resource;
		barrier.Transition.StateBefore = snapshot.stateBefore;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
	}
}

static bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement)
{
	if (!device || !commandList || !replacement)
		return false;

	if (!indexResource.empty())
		EnsureLoadedResourceForCommandList(device, indexResource);
	for (const auto &item : vertexResources)
		EnsureLoadedResourceForCommandList(device, item.resource);

	bool changed = false;
	if (!indexResource.empty()) {
		DX12IaResourceViewSnapshot snapshot;
		{
			DX12LoadedResourceSnapshotReadScope scope;
			AcquireSRWLockExclusive(&gModLock);
			auto loaded = gLoadedResources.find(indexResource);
			if (loaded != gLoadedResources.end())
				SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
			ReleaseSRWLockExclusive(&gModLock);
			AddRefIaResourceViewSnapshot(&snapshot);
		}
		if (snapshot.resource) {
			ApplyIaResourceBarriers(commandList, snapshot);
			replacement->indexBuffer.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
			replacement->indexBuffer.SizeInBytes =
				IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
			replacement->indexBuffer.Format =
				snapshot.metadata.format == DXGI_FORMAT_R16_UINT ||
				snapshot.metadata.format == DXGI_FORMAT_R32_UINT ?
				snapshot.metadata.format :
				(iaState.hasIndexBuffer && iaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
					iaState.indexView.Format : DXGI_FORMAT_R32_UINT);
			replacement->RetainResource(snapshot.resource);
			replacement->hasIndexBuffer = true;
			LogIaResourceBindLimited("ib", 0, snapshot.metadata, snapshot.resource);
			changed = true;
		}
		ReleaseIaResourceViewSnapshot(&snapshot);
	}

	if (!vertexResources.empty()) {
		uint32_t minSlot = vertexResources.front().slot;
		uint32_t maxSlot = vertexResources.back().slot;
		if (maxSlot >= minSlot && maxSlot - minSlot < 32) {
			if (replacement->vertexBuffers.empty() ||
			    minSlot < replacement->vertexBufferStartSlot ||
			    maxSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
				uint32_t oldStart = replacement->vertexBuffers.empty() ?
					minSlot : replacement->vertexBufferStartSlot;
				uint32_t oldEnd = replacement->vertexBuffers.empty() ?
					maxSlot : replacement->vertexBufferStartSlot +
						static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1;
				uint32_t newStart = (std::min)(oldStart, minSlot);
				uint32_t newEnd = (std::max)(oldEnd, maxSlot);
				if (newEnd - newStart >= 32)
					return changed;

				std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(newEnd - newStart + 1);
				for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
					uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
					resized[slot - newStart] = replacement->vertexBuffers[i];
				}
				replacement->vertexBufferStartSlot = newStart;
				replacement->vertexBuffers.swap(resized);
			}

			for (const auto &item : vertexResources) {
				DX12IaResourceViewSnapshot snapshot;
				{
					DX12LoadedResourceSnapshotReadScope scope;
					AcquireSRWLockExclusive(&gModLock);
					auto loaded = gLoadedResources.find(item.resource);
					if (loaded != gLoadedResources.end())
						SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
					ReleaseSRWLockExclusive(&gModLock);
					AddRefIaResourceViewSnapshot(&snapshot);
				}
				if (!snapshot.resource) {
					ReleaseIaResourceViewSnapshot(&snapshot);
					continue;
				}
				ApplyIaResourceBarriers(commandList, snapshot);
				D3D12_VERTEX_BUFFER_VIEW &view =
					replacement->vertexBuffers[item.slot - replacement->vertexBufferStartSlot];
				view.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
				view.SizeInBytes = IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
				const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, item.slot);
				view.StrideInBytes = snapshot.metadata.stride ? snapshot.metadata.stride :
					(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
				replacement->RetainResource(snapshot.resource);
				LogIaResourceBindLimited("vb", item.slot, snapshot.metadata, snapshot.resource);
				changed = true;
				ReleaseIaResourceViewSnapshot(&snapshot);
			}
		}
	}
	return changed;
}

static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement)
{
	bool changed = false;
	for (const DX12PendingResourceViewRequest &request : requests) {
		changed |= AppendResourceViewsForCommandList(
			device, commandList, iaState,
			request.indexResource, request.vertexResources, replacement);
	}
	return changed;
}

static bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	std::vector<Bunny::TextureOverrideConfig> *configs)
{
	if (configs)
		configs->clear();
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer || !iaState.indexHash)
			return false;
		return FindIaTextureOverridesCached(
			target, iaState.indexHash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, 0, true, 0, configs);
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
		if (!slot || !slot->hash)
			return false;
		return FindIaTextureOverridesCached(
			target, slot->hash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, 0, false, target.slot, configs);
	}

	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
	    target.kind == Bunny::CommandListTargetKind::ShaderResource ||
	    target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		return FindDescriptorTextureOverrides(
			commandList, target, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, configs);
	}

	return false;
}


