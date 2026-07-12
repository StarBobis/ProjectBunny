#pragma once

#include <d3d12.h>

#include <cstdint>
#include <array>
#include <vector>

#include "DX12ActiveIaState.hpp"
#include "DX12CompiledDispatchDecision.hpp"
#include "DX12CompiledDrawDecision.hpp"
#include "DX12CommandListTrackingCache.hpp"
#include "DX12PreSkinCacheIdentity.hpp"
#include "binding/DX12BindingTrackerState.hpp"
#include "ia/DX12DirectIaProbeCacheUtils.hpp"
#include "ia/DX12DirectIaFastRejectCacheUtils.hpp"
#include "ia/DX12IaHashStateCacheUtils.hpp"

// Exact, command-list-local negative result. Unlike the legacy unordered cache,
// this key uses the IA mutation epoch as the identity of all bound views. The
// global generations cover writes/resource reuse, so no content-hash result is
// reused after any dependency can have changed.
class DX12ResidentDrawNegativeEntry
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 iaEpoch = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;
	bool indexed = false;
	bool valid = false;
};

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	// Wrapper-side object translation cache. These pointers are only compared;
	// ownership remains with the application/wrapper objects.
	ID3D12PipelineState *lastWrappedPipelineState = nullptr;
	ID3D12PipelineState *lastUnwrappedPipelineState = nullptr;
	ID3D12RootSignature *lastWrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastWrappedGraphicsRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedGraphicsRootSignature = nullptr;
	// Reused barrier scratch storage avoids one allocation/free cycle per API call.
	std::vector<D3D12_RESOURCE_BARRIER> barrierScratch;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	UINT64 iaEpoch = 1;
	UINT64 iaContentEpoch = 1;
	DX12IaHashStateCache iaHashStateCache;
	// Direct-target and full-target key prefixes. Resolved IA resources carry
	// their own content/lifetime generations; global epochs are only consulted
	// for unresolved fallback keys.
	std::array<DX12IaHashKeyPrefixCacheEntry, 2> iaHashKeyPrefixes;
	DX12DirectIaProbeCache directIaProbeCache;
	DX12DirectIaFastRejectCache directIaFastRejectCache;
	// Small direct-mapped resident cache: the common repeated negative draw is a
	// handful of integer comparisons, with no 32-slot scan or unordered lookup.
	std::array<DX12ResidentDrawNegativeEntry, 16> residentDrawNegativeCache;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	bool runtimeSamplerHeapResolved = false;
	DX12DescriptorHeapSummary runtimeSamplerHeap = {};
	UINT64 bindingQuerySerial = 1;
	UINT graphicsRootTableValidCount = 0;
	UINT computeRootTableValidCount = 0;
	RootTableState graphicsRootTables[MaxRootParameters] = {};
	RootTableState computeRootTables[MaxRootParameters] = {};

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
	bool cachedPreSkinBindingsValid = false;
	bool cachedPreSkinBindingsFoundAny = false;
	UINT64 cachedPreSkinBindingsSerial = 0;
	UINT64 cachedPreSkinBindingsDependencyFingerprint = 0;
	ID3D12DescriptorHeap *cachedPreSkinBindingsHeap = nullptr;
	std::vector<UINT> cachedPreSkinObservedSlots;
	std::vector<UINT64> cachedPreSkinObservedSlotVersions;
	std::vector<UINT64> cachedPreSkinObservedSlotValueIds;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedPreSkinCbvs;
	std::vector<DX12CurrentRootConstants> cachedPreSkinRootConstants;
	UINT64 cachedPreSkinDependencyFingerprint = 0;
	UINT64 cachedPreSkinBindingFingerprint = 0;
	UINT64 cachedPreSkinProbeFingerprint = 0;
	uint64_t cachedPreSkinProbeCsHash = 0;
	UINT64 cachedPreSkinProbeReloadGeneration = 0;
	UINT64 cachedPreSkinProbeBindingSerial = 0;
	UINT64 cachedPreSkinProbeIaContentEpoch = 0;
	bool cachedPreSkinProbeResultValid = false;
	bool cachedPreSkinProbeApplied = false;
	bool cachedPreSkinProbeDispatchHandlingSkip = false;
	bool cachedPreSkinProbeDispatchHasDispatch = false;
	UINT cachedPreSkinProbeGroupsX = 0;
	UINT cachedPreSkinProbeGroupsY = 0;
	UINT cachedPreSkinProbeGroupsZ = 0;
};
