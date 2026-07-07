#pragma once

#include <d3d12.h>

#include <cstdint>
#include <vector>

#include "DX12ActiveIaState.hpp"
#include "DX12CompiledDispatchDecision.hpp"
#include "DX12CompiledDrawDecision.hpp"
#include "DX12CommandListTrackingCache.hpp"
#include "binding/DX12BindingTrackerTypes.hpp"
#include "ia/DX12IaHashStateCacheUtils.hpp"

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	UINT64 iaEpoch = 1;
	UINT64 iaContentEpoch = 1;
	UINT64 iaHashStateCacheClock = 0;
	UINT iaHashStateCacheCursor = 0;
	std::unordered_map<DX12IaHashStateCacheKey, DX12IaHashStateCacheEntry, DX12IaHashStateCacheKeyHash> iaHashStateCache;
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

	// PreSkin binding collection cache — keyed by computeBindingSerial.
	// Avoids re-collecting CBV/SRV/UAV/constants on every dispatch when
	// compute bindings have not changed since the last collection.
	UINT64 lastPreSkinBindingSerial = 0;
	bool preSkinBindingsCached = false;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinCbvs;
	std::vector<DX12CurrentRootConstants> cachedPreSkinConstants;
};
