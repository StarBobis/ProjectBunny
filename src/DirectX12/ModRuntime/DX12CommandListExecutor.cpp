// DX12CommandListExecutor.cpp: command list execution entry points, frame begin and present replacement.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"


bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (!config.hasMatchCs && !config.hasCsHash)
		return true;
	return activePreSkinSections.find(config.section) != activePreSkinSections.end();
}


bool TextureOverrideDependsOnActivePreSkin(
	const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchCs || config.hasCsHash;
}


const std::unordered_set<std::wstring> &EmptyActivePreSkinSections()
{
	static const std::unordered_set<std::wstring> empty;
	return empty;
}


bool IaCandidateIndexesDependOnActivePreSkin(
	const std::vector<size_t> &candidateIndexes)
{
	for (size_t index : candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		if (candidate.config &&
		    TextureOverrideDependsOnActivePreSkin(*candidate.config))
			return true;
	}
	return false;
}


bool IaTriggeredCandidatesDependOnActivePreSkin(
	const std::vector<DX12TriggeredTextureOverride> &candidateTriggers)
{
	for (const DX12TriggeredTextureOverride &triggered : candidateTriggers) {
		if (triggered.config &&
		    TextureOverrideDependsOnActivePreSkin(*triggered.config))
			return true;
	}
	return false;
}


void DX12ModBeginFrame(IDXGISwapChain *swapChain)
{
	// Frame-variable collection (window/cursor snapshot + ~20 global writes
	// per frame) only matters when command lists can actually read the
	// variables; skip the whole snapshot when the mod has none.
	if (gHasCommandLists.load(std::memory_order_relaxed))
		DX12VmFrameVariablesUtils::UpdateForSwapChain(swapChain);
	ProcessDx12KeyBindingsForFrame();

	std::vector<DX12RetiredLoadedResource> expired;
	std::vector<ID3D12Resource*> releasePreSkinResources;
	// Without pre-skin candidates there is no active-producer set to clear
	// and no idle tracking to maintain: skip the per-frame exclusive lock
	// entirely instead of serializing recording threads on it for nothing.
	if (gHasPreSkinTextureOverrideCandidates != 0) {
		DX12ModLockTelemetryUtils::AcquirePreSkinExclusive(&gPreSkinLock);
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
		ReleaseSRWLockExclusive(&gPreSkinLock);
	}
	// Best-effort: deferring expired-resource retirement by one frame is
	// harmless, and the frame boundary must never block on the global mod
	// lock while recording threads contend for shared acquisitions.
	if (TryAcquireSRWLockExclusive(&gModLock)) {
		MoveExpiredRetiredLoadedResourcesLocked(&expired);
		ReleaseSRWLockExclusive(&gModLock);
	}
	for (DX12RetiredLoadedResource &resource : expired)
		ReleaseLoadedResourceObjects(&resource.resource);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	// Preload pre-skin replacement resources once per mod generation so the
	// first match does not pay synchronous file I/O + resource creation on
	// the command-recording thread. Loading is spread across frames (a fixed
	// batch per Present) so a large mod cannot freeze a single frame; the
	// generation is only marked complete when the batch loop finishes.
	{
		UINT64 sPreSkinResourcesPreloadedGeneration = 0;
		static UINT64 sOutputResizePreloadGeneration = 0;
		static LONG sOutputResizePreloadIdleFrames = 0;
		static UINT sOutputResizePreloadFrameBudget = 0;
		const UINT64 preloadGeneration = DX12ModGetReloadGenerationFast();
		if (gHasPreSkinTextureOverrideCandidates != 0 &&
			sPreSkinResourcesPreloadedGeneration != preloadGeneration)
		{
			ID3D12Device *preloadDevice = nullptr;
			if (swapChain &&
				SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
				preloadDevice)
			{
				if (PreloadPreSkinReplacementResources(preloadDevice))
					sPreSkinResourcesPreloadedGeneration = preloadGeneration;
				preloadDevice->Release();
			}
		}
		if (gHasPreSkinTextureOverrideCandidates != 0)
		{
			if (sOutputResizePreloadGeneration != preloadGeneration)
			{
				sOutputResizePreloadGeneration = preloadGeneration;
				sOutputResizePreloadIdleFrames = 0;
				sOutputResizePreloadFrameBudget = 0;
			}
			// Idle-frame gate plus a hard frame budget: progress-based
			// re-arming alone could keep the preload loop alive indefinitely
			// when new tracked resources keep appearing, charging the frame
			// boundary with a full resource scan every Present.
			if (sOutputResizePreloadIdleFrames < 30 &&
			    sOutputResizePreloadFrameBudget < 120)
			{
				++sOutputResizePreloadFrameBudget;
				ID3D12Device *preloadDevice = nullptr;
				if (swapChain &&
					SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
					preloadDevice)
				{
					const bool progress =
						PreloadPreSkinOutputResizeCache(preloadDevice);
					sOutputResizePreloadIdleFrames =
						progress ? 0 : sOutputResizePreloadIdleFrames + 1;
					preloadDevice->Release();
				}
			}
		}
	}

}


bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present)
			return false;
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
	}

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device) {
		InterlockedCompareExchange(&gPresentCommandListExecutedPresent, present - 1, present);
		return false;
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


bool DX12ModPresentReplacementPendingForCurrentPresent()
{
	if (!DX12ModNeedsPresentReplacement())
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	// Plain volatile probe: the value is only meaningful to the recording
	// thread and is published with interlocked ops, so a LOCK CMPXCHG here
	// (once per Draw while a Present mod is active) buys nothing.
	return gPresentCommandListExecutedPresent != present;
}


DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration,
	const DX12TextureOverrideDrawKeyUsage &usage)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	return key;
}


void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource)
		return;
	if (snapshot->retained)
		snapshot->resource->Release();
	snapshot->resource = nullptr;
	snapshot->retained = false;
}


void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource || snapshot->retained)
		return;
	snapshot->resource->AddRef();
	snapshot->retained = true;
}


void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	ReleaseIaResourceViewSnapshot(snapshot);
	if (!resource)
		return;
	if (resource->uavResource && resource->uavValid &&
		resource->uavValidPresent == DX12RuntimeState::GetPresentCount()) {
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


static bool IaResourceViewSnapshotRequiresWrite(const DX12LoadedResource &resource)
{
	return resource.uavResource && resource.uavValid &&
		resource.uavValidPresent == DX12RuntimeState::GetPresentCount();
}


static void SnapshotIaResourceViewReadOnlyLocked(
	const DX12LoadedResource &resource, DX12IaResourceViewSnapshot *snapshot)
{
	ReleaseIaResourceViewSnapshot(snapshot);
	if (resource.srvResource && resource.srvByteWidth > resource.byteWidth) {
		snapshot->resource = resource.srvResource;
		snapshot->metadata = resource;
		return;
	}
	if (resource.resource) {
		snapshot->resource = resource.resource;
		snapshot->metadata = resource;
	}
}


void SnapshotIaResourceViewForCommandList(
	const std::wstring &name, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	// Fast path: most IA replacement resources are plain buffers whose
	// snapshot is a pure read. A shared lock lets concurrent recording
	// threads resolve them in parallel instead of serializing every modded
	// draw on the global exclusive gModLock (a DX11-style single-thread
	// pattern; DX12 resolves resource metadata without a global write lock).
	DX12ModLockTelemetryUtils::AcquireModShared(&gModLock);
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end() &&
	    !IaResourceViewSnapshotRequiresWrite(loaded->second)) {
		SnapshotIaResourceViewReadOnlyLocked(loaded->second, snapshot);
		ReleaseSRWLockShared(&gModLock);
		return;
	}
	ReleaseSRWLockShared(&gModLock);

	// UAV-backed entries update tracking state while snapshotting (transition
	// to VERTEX_AND_CONSTANT_BUFFER and clear the pending-barrier flag), which
	// needs the exclusive lock. Re-find and re-check: another thread may have
	// changed the entry between the two acquisitions.
	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end())
		SnapshotIaResourceViewLocked(&loaded->second, snapshot);
	ReleaseSRWLockExclusive(&gModLock);
}


UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}

void ApplyIaResourceBarriers(
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


bool AppendResourceViewsForCommandList(
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
			SnapshotIaResourceViewForCommandList(indexResource, &snapshot);
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
					SnapshotIaResourceViewForCommandList(item.resource, &snapshot);
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
								changed = true;
				ReleaseIaResourceViewSnapshot(&snapshot);
			}
		}
	}
	return changed;
}


bool ApplyPendingResourceViewsForCommandList(
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


bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name)
{
	if (!snapshot)
		return true;
	auto it = snapshot->commandListRuntimeEffect.find(name);
	return it == snapshot->commandListRuntimeEffect.end() || it->second;
}


bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name,
	bool includePost)
{
	if (!snapshot)
		return true;
	const auto &effects = includePost ?
		snapshot->commandListPostRuntimeEffect :
		snapshot->commandListPreRuntimeEffect;
	if (!effects.empty()) {
		auto it = effects.find(name);
		return it == effects.end() || it->second;
	}
	return CommandListRuntimeEffectLocked(snapshot, name);
}



bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name, includePost);
}


void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	for (const DX12TriggeredTextureOverride &triggered : source) {
		if (!triggered.config || !triggered.sectionId)
			continue;
		sectionIds->push_back(triggered.sectionId);
	}
}


bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	if (!snapshot.textureOverridesWithoutDrawContext.empty() ||
	    !snapshot.textureOverridesGenericDrawContext.empty())
		return true;
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end() && !indexed->second.empty())
			return true;
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end() && !vertex->second.empty())
			return true;
	}
	return false;
}


void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}


DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}


bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews)
{
	if (!device || !commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffect(name, true))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, 0,
		replacement, pendingResourceViews);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
	return executor.Changed();
}


bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost)
{
	if (name.empty())
		return false;
	if (!CommandListRuntimeEffect(name, includePost))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, nullptr, nullptr);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, includePost);
	return true;
}


void ProcessDx12KeyBindingsForFrame()
{
	std::vector<Bunny::MigotoKeyBindingUtils::RuntimeAction> actions;
	// Key processing is best-effort input handling; it must never block the
	// frame boundary on the global mod lock while recording threads take
	// shared acquisitions on every draw/dispatch. A skipped round simply
	// delays a hotkey by one frame.
	if (!TryAcquireSRWLockExclusive(&gModLock))
		return;
	if (gLoaded)
		Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
	ReleaseSRWLockExclusive(&gModLock);


	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
}


void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}
