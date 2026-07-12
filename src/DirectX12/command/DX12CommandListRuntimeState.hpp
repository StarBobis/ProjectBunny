#pragma once

#include <d3d12.h>

#include <cstdint>
#include <vector>

#include "DX12ActiveIaState.hpp"
#include "DX12CompiledDispatchDecision.hpp"
#include "DX12CompiledDrawDecision.hpp"
#include "DX12CommandListTrackingCache.hpp"
#include "DX12PreSkinCacheIdentity.hpp"
#include "binding/DX12BindingTrackerTypes.hpp"
#include "ia/DX12DirectIaProbeCacheUtils.hpp"
#include "ia/DX12DirectIaFastRejectCacheUtils.hpp"
#include "ia/DX12IaHashStateCacheUtils.hpp"

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	UINT64 iaEpoch = 1;
	UINT64 iaContentEpoch = 1;
	DX12IaHashStateCache iaHashStateCache;
	DX12DirectIaProbeCache directIaProbeCache;
	DX12DirectIaFastRejectCache directIaFastRejectCache;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;

	// Per-PSO override cache aligned with HotPathState generation.
	volatile LONG psoHasOverridesGeneration = -1;
	bool psoHasShaderOverrides = true;

	// Replacement PSO cache. First use may query global records; repeated use is direct.
	ID3D12PipelineState *cachedReplacementPsoKey = nullptr;
	ID3D12PipelineState *cachedReplacementPso = nullptr;

	// Compute shader hash cache, equivalent to DX11 current compute shader state.
	uint64_t cachedCsHash = 0;
	bool cachedCsHashValid = false;
	bool cachedCsHashResolved = false;

	// Stable compute binding fingerprint used as a dispatch probe cache key.
	uint64_t computeBindingHash = 0;

	// Compiled per-PSO dispatch policy. Dispatch consumes this instead of
	// rediscovering non-candidate PreSkin state on every command.
	DX12CompiledDispatchDecision dispatchDecision;

	// Compiled per-PSO draw policy. Draw consumes this instead of rediscovering
	// PSO shader override metadata on every command.
	DX12CompiledDrawDecision drawDecision;

	// Cached device pointer to eliminate per-draw AcquireModDevice QI overhead.
	// Set during Reset from the command list; device must outlive the command list
	// per DX12 spec (device -> command list is a parent-child relationship).
	ID3D12Device *device = nullptr;

	// PreSkin probe cache keyed by the stable dependency-aware fingerprint.
	// Exact binding dependencies live in the shared PreSkin binding cache.
	// Runtime state only retains the final Mod probe result.
	UINT64 cachedPreSkinDependencyFingerprint = 0;
	UINT64 cachedPreSkinBindingFingerprint = 0;
	UINT64 cachedPreSkinProbeFingerprint = 0;
	uint64_t cachedPreSkinProbeCsHash = 0;
	UINT64 cachedPreSkinProbeReloadGeneration = 0;
	bool cachedPreSkinProbeResultValid = false;
	bool cachedPreSkinProbeApplied = false;
	bool cachedPreSkinProbeDispatchHandlingSkip = false;
	bool cachedPreSkinProbeDispatchHasDispatch = false;
	UINT cachedPreSkinProbeGroupsX = 0;
	UINT cachedPreSkinProbeGroupsY = 0;
	UINT cachedPreSkinProbeGroupsZ = 0;
};
