#pragma once

#include <d3d12.h>

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "DX12CommandListRuntime.hpp"
#include "DX12IaReplacementBindingExecutor.hpp"
#include "DX12IaReplacementDrawExecutor.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12RuntimeState.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"
#include "utils/DX12PreSkinIaRedirectUtils.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

// Legacy stub - referenced by compiled code but definition was never provided.
inline bool TryQuickRejectPsoNoMatch(ID3D12PipelineState *)
{
	return false;
}

class DX12PsoDrawFastRejectKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaFingerprint = 0;
	uint32_t fingerprintKind = 0;
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12PsoDrawFastRejectKey &other) const
	{
		return pipelineState == other.pipelineState &&
			reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			iaFingerprint == other.iaFingerprint &&
			fingerprintKind == other.fingerprintKind &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
	}
};

class DX12PsoDrawFastRejectKeyHash
{
public:
	static size_t Mix(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	size_t operator()(const DX12PsoDrawFastRejectKey &key) const
	{
		size_t hash = reinterpret_cast<size_t>(key.pipelineState);
		hash = Mix(hash, key.reloadGeneration);
		hash = Mix(hash, key.targetPlanFingerprint);
		hash = Mix(hash, key.iaFingerprint);
		hash = Mix(hash, key.fingerprintKind);
		hash = Mix(hash, key.indexed ? 1 : 0);
		hash = Mix(hash, key.vertexCount);
		hash = Mix(hash, key.indexCount);
		hash = Mix(hash, key.instanceCount);
		hash = Mix(hash, key.firstVertex);
		hash = Mix(hash, key.firstIndex);
		hash = Mix(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		hash = Mix(hash, key.firstInstance);
		return hash;
	}
};

class DX12IaReplacementExecutor
{
public:
	inline static std::unordered_set<
		DX12PsoDrawFastRejectKey,
		DX12PsoDrawFastRejectKeyHash> s_psoDrawFastRejectCache;
	inline static SRWLOCK s_psoDrawFastRejectCacheLock = SRWLOCK_INIT;
	static constexpr size_t MaxPsoDrawFastRejectEntries = 16384;

	static UINT64 MixCheapValue(UINT64 key, UINT64 value)
	{
		key ^= value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		return key;
	}

	static DX12PsoDrawFastRejectKey MakeDrawFastRejectKey(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		UINT64 iaFingerprint,
		uint32_t fingerprintKind,
		const DX12IaDrawInvocation &draw)
	{
		DX12PsoDrawFastRejectKey key;
		key.pipelineState = pipelineState;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.iaFingerprint = iaFingerprint;
		key.fingerprintKind = fingerprintKind;
		key.indexed = draw.indexed;
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static DX12PsoDrawFastRejectKey MakePsoIaNegativeKey(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaHashState &iaState,
		const DX12IaDrawInvocation &draw)
	{
		UINT64 fingerprint = 0x4a2d8b31c6e9075full;
		fingerprint = MixCheapValue(fingerprint, iaState.hasIndexBuffer ? 1ull : 0ull);
		if (iaState.hasIndexBuffer) {
			fingerprint = MixCheapValue(fingerprint, iaState.indexHash);
			fingerprint = MixCheapValue(fingerprint, iaState.indexView.BufferLocation);
			fingerprint = MixCheapValue(fingerprint, iaState.indexView.SizeInBytes);
			fingerprint = MixCheapValue(
				fingerprint, static_cast<UINT64>(iaState.indexView.Format));
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			fingerprint = MixCheapValue(fingerprint, static_cast<UINT64>(buffer.slot));
			fingerprint = MixCheapValue(fingerprint, buffer.hash);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.BufferLocation);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.SizeInBytes);
			fingerprint = MixCheapValue(fingerprint, buffer.vertexView.StrideInBytes);
		}
		return MakeDrawFastRejectKey(
			pipelineState, reloadGeneration, targetPlan, fingerprint, 1, draw);
	}

	static bool TryFindPsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		AcquireSRWLockShared(&s_psoDrawFastRejectCacheLock);
		bool found = s_psoDrawFastRejectCache.find(key) != s_psoDrawFastRejectCache.end();
		ReleaseSRWLockShared(&s_psoDrawFastRejectCacheLock);
		return found;
	}

	static bool HasAnyDirectTextureOverrideCheckTarget(
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed)
	{
		if (!targetPlan.hasTextureOverrideChecks)
			return false;
		if (targetPlan.requireAllIaBuffers)
			return true;
		if (targetPlan.hasDescriptorTargets)
			return true;
		if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer)
			return true;
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (targetPlan.RequiresVertexBuffer(slot) && ia.hasVertexBuffer[slot])
				return true;
		}
		return false;
	}

	static void StorePsoDrawFastReject(const DX12PsoDrawFastRejectKey &key)
	{
		AcquireSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
		if (s_psoDrawFastRejectCache.find(key) != s_psoDrawFastRejectCache.end()) {
			ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
			return;
		}
		if (s_psoDrawFastRejectCache.size() >= MaxPsoDrawFastRejectEntries)
			s_psoDrawFastRejectCache.erase(s_psoDrawFastRejectCache.begin());
		s_psoDrawFastRejectCache.insert(key);
		ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
	}



	static void ClearPsoDrawFastRejectCache()
	{
		AcquireSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
		s_psoDrawFastRejectCache.clear();
		ReleaseSRWLockExclusive(&s_psoDrawFastRejectCacheLock);
	}

	static bool ExecutePresent(
		ID3D12GraphicsCommandList *commandList,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12ModIaReplacement replacement;
		if (!DX12ModPreparePresentReplacement(commandList, &replacement))
			return false;

		const DX12ActiveIaState originalIa = runtimeState.ia;
		DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		bindingExecutor.Apply(replacement);
		DX12ModApplyDrawSrvReplacement(commandList, &replacement, false);
		DX12IaDrawInvocation noDraw = {};
		drawExecutor.ExecuteReplacementDraws(replacement, noDraw);
		bindingExecutor.Restore(replacement, originalIa);
		DX12ModRestoreDrawSrvReplacement(commandList, &replacement);
		return true;
	}

	static bool ApplyAndExecute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaHashState &iaState,
		DX12ModIaReplacement *replacement,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !replacement)
			return false;
		if (replacement->skip && replacement->draws.empty() &&
		    replacement->dispatches.empty() && replacement->drawSrvPatches.empty()) {
			return true;
		}

		DX12ActiveIaState originalIa = runtimeState.ia;
		DX12PreSkinIaRedirectUtils::ReplayActiveVertexBuffers(
			commandList, callbacks, &originalIa);
		DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		bindingExecutor.Apply(*replacement);
		DX12ModApplyDrawSrvReplacement(
			commandList, replacement, !replacement->skip);

		if (!replacement->skip)
			drawExecutor.ExecuteOriginalDraw(draw);

		drawExecutor.ExecuteReplacementDraws(*replacement, draw);
		bindingExecutor.Restore(*replacement, originalIa);
		DX12ModRestoreDrawSrvReplacement(commandList, replacement);
		DX12Profiling::RecordIaApplied();
		DX12ModRunPostShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			draw.vertexCount, draw.indexCount, draw.instanceCount,
			draw.firstVertex, draw.firstIndex, draw.firstInstance, replacement);
		return true;
	}

	static bool HandleDrawOverrides(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12CommandListRuntimeState &runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawOverrideTotal);

		// Draw consumes the compiled PSO decision instead of rediscovering
		// shader override metadata on every command.
		DX12_HOTPATH_TIME_BEGIN(DrawPsoEarlyExit);
		LARGE_INTEGER psoDecisionBegin = DX12OuterTimingProbeUtils::Now();
		const bool activeShaderOverrides = DX12ModHasActiveShaderOverrides();
		DX12CommandListRuntimeState *mutableRuntimeState =
			const_cast<DX12CommandListRuntimeState*>(&runtimeState);
		// The runtime deliberately preserves this cache across command-list Reset.
		// Using a second TLS cache here defeated that ownership model, mixed entries
		// from unrelated command lists, and forced every draw through a much larger
		// LRU. Keep hash state resident with the command list whose IA generations
		// form the key, matching DX11's context-local binding state.
		DX12IaHashStateCache *iaHashStateCache =
			&mutableRuntimeState->iaHashStateCache;
		const DX12CompiledDrawDecision *drawDecision =
			DX12CommandListRuntime::EnsureDrawDecision(
				mutableRuntimeState, activeShaderOverrides);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"IaReplacement",
			"ensure_draw_decision",
			draw.indexed ? 1u : 0u,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(psoDecisionBegin));
		if (!drawDecision || drawDecision->CanFastReject()) {
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionFastReject();
			DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->NeedsDescriptorMatch())
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDescriptorDraw();
		DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);

		DX12Profiling::RecordIaDrawOverrideCheck();
		DX12ModIaReplacement iaReplacement;
		DX12IaHashState iaState;
		const DX12IaHashState *activeIaState = &iaState;
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		// Cheap negative cache lookup uses PSO, IA view identity, and draw args
		// before falling back to stable IA content hashing.
		DX12IaHashTargetPlan targetPlan = drawDecision->targetPlan;
		const DX12IaHashTargetPlan &directTargetPlan = drawDecision->directTargetPlan;
		const bool targetPlanHasIaTargets = drawDecision->targetPlanHasIaTargets;
		const bool psoIaNegativeCacheSafe = drawDecision->negativeCacheSafe;
		const bool shaderOverrideNegativeCacheAllowed =
			drawDecision->shaderOverrideNegativeCacheAllowed;
		bool hasIaHashState = false;
		DX12PsoDrawFastRejectKey psoPreFilterKey;
		DX12IaHashState directIaState;
		const DX12IaHashState *directIaStateView = &directIaState;
		bool reusedDirectIaState = false;
		DX12DirectIaFastRejectCacheKey directFastRejectKey;
		bool directFastRejectKeyValid = false;
		if (drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    !DX12IaReplacementExecutor::HasAnyDirectTextureOverrideCheckTarget(
			    runtimeState.ia, directTargetPlan, draw.indexed)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"skip_missing_direct_check_target",
				draw.indexed ? 1u : 0u,
				1,
				0);
			DX12Profiling::RecordIaHashStateResult(false);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->directTargetPlanResolved &&
		    directTargetPlan.hasTextureOverrideChecks &&
		    directTargetPlan.HasIaTargets() &&
		    !directTargetPlan.hasDescriptorTargets &&
		    !directTargetPlan.requireAllIaBuffers) {
			// Reject repeated negative draws from immutable command-list state before
			// resolving GPU virtual addresses. Global content and resource-lifetime
			// generations make the shortcut conservative across uploads and reuse.
			directFastRejectKey = DX12DirectIaFastRejectCacheUtils::MakeKey(
				drawDecision->reloadGeneration,
				DX12GetIaHashContentGeneration(),
				DX12GetIaHashResolveGeneration(),
				runtimeState.ia,
				directTargetPlan,
				draw);
			directFastRejectKeyValid = true;
			if (DX12DirectIaFastRejectCacheUtils::Contains(
					mutableRuntimeState->directIaFastRejectCache,
					directFastRejectKey)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"direct_ia_fast_reject_cache_hit",
					draw.indexed ? 1u : 0u,
					1,
					0);
				DX12Profiling::RecordIaHashStateResult(false);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}

				// The direct probe and the full override path consume the same stable IA
				// hash state. Cache it before building the probe key so a negative probe
				// hit does not require re-hashing the same draw region first.
				LARGE_INTEGER directProbeBuildBegin = DX12OuterTimingProbeUtils::Now();
				const DX12IaHashStateCacheKey directHashCacheKey =
					DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
						mutableRuntimeState,
						0,
						directTargetPlan,
						draw.indexed,
						draw.vertexCount,
						draw.indexCount,
						draw.firstVertex,
						draw.firstIndex,
						draw.baseVertex);
				bool directHashFound = false;
				const bool directHashCacheHit = DX12IaHashStateCacheUtils::TryLoad(
					iaHashStateCache,
					directHashCacheKey,
					&directIaStateView,
					&directHashFound);
				if (!directHashCacheHit) {
					directHashFound = DX12CommandListRuntime::BuildIaHashStateForTargets(
						runtimeState.ia,
						directTargetPlan,
						draw.indexed,
						draw.vertexCount,
						draw.indexCount,
						draw.firstVertex,
						draw.firstIndex,
						draw.baseVertex,
						&directIaState);
					bool directHashCacheEvicted = false;
					const DX12IaHashState *storedDirectState =
						DX12IaHashStateCacheUtils::Store(
							iaHashStateCache,
							directHashCacheKey,
							directHashFound,
							std::move(directIaState),
							&directHashCacheEvicted);
					if (storedDirectState)
						directIaStateView = storedDirectState;
					if (directHashCacheEvicted)
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheEviction();
				}
				if (directHashFound) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						directHashCacheHit ?
							"direct_ia_probe_state_cache_hit" :
							"build_direct_ia_probe_state",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeBuildBegin));

					LARGE_INTEGER directProbeKeyBegin = DX12OuterTimingProbeUtils::Now();
					const DX12DirectIaProbeCacheKey directProbeCacheKey =
							DX12DirectIaProbeCacheUtils::MakeKey(
								drawDecision->reloadGeneration,
								directTargetPlan.Fingerprint(),
								*directIaStateView,
								draw);
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"build_direct_ia_probe_key",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeKeyBegin));

					DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheLookup();
					LARGE_INTEGER directProbeCacheBegin = DX12OuterTimingProbeUtils::Now();
					bool hasDirectCandidate = false;
					if (DX12DirectIaProbeCacheUtils::TryLoad(
							&mutableRuntimeState->directIaProbeCache,
							directProbeCacheKey,
							&hasDirectCandidate)) {
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							hasDirectCandidate ?
								"direct_ia_probe_cache_positive_hit" :
								"direct_ia_probe_cache_negative_hit",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeCacheBegin));
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheHit(
							hasDirectCandidate);
					} else {
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"direct_ia_probe_cache_miss",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeCacheBegin));
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheMiss();
						LARGE_INTEGER directProbeMatchBegin = DX12OuterTimingProbeUtils::Now();
							hasDirectCandidate = DX12ModHasAnyDirectIaTextureOverrideCandidate(
								*directIaStateView,
							directTargetPlan,
							draw.indexed,
							draw.vertexCount,
							draw.indexCount,
							draw.instanceCount,
							draw.firstVertex,
							draw.firstIndex,
							draw.firstInstance);
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							hasDirectCandidate ?
								"direct_ia_probe_positive_result" :
								"direct_ia_probe_negative_result",
							draw.indexed ? 1u : 0u,
							1,
							DX12OuterTimingProbeUtils::ElapsedUs(directProbeMatchBegin));
						bool directProbeCacheEvicted = false;
						DX12DirectIaProbeCacheUtils::Store(
							&mutableRuntimeState->directIaProbeCache,
							directProbeCacheKey,
							hasDirectCandidate,
							&directProbeCacheEvicted);
						DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionDirectProbeCacheStore(
							directProbeCacheEvicted);
					}

					if (!hasDirectCandidate) {
						if (directFastRejectKeyValid) {
							DX12DirectIaFastRejectCacheUtils::Store(
								&mutableRuntimeState->directIaFastRejectCache,
								directFastRejectKey);
						}
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"skip_direct_ia_probe_miss",
							draw.indexed ? 1u : 0u,
							1,
							0);
						DX12Profiling::RecordIaHashStateResult(false);
						DX12_HOTPATH_TIME_END(DrawOverrideTotal);
						return false;
					}
					if (targetPlan.HasSameIaHashTargets(directTargetPlan)) {
						activeIaState = directIaStateView;
						hasIaHashState = true;
						reusedDirectIaState = true;
						DX12OuterTimingProbeUtils::LogCpuStage(
							"IaReplacement",
							"reuse_direct_ia_state",
							draw.indexed ? 1u : 0u,
							1,
							0);
					}
				} else {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"build_direct_ia_probe_state_failed",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(directProbeBuildBegin));
				}
		}
			if (targetPlanHasIaTargets && !reusedDirectIaState) {
				LARGE_INTEGER iaKeyBegin = DX12OuterTimingProbeUtils::Now();
			DX12IaHashStateCacheKey iaCacheKey =
				DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargetsCached(
					mutableRuntimeState,
					1,
					targetPlan,
					draw.indexed,
					draw.vertexCount,
					draw.indexCount,
					draw.firstVertex,
					draw.firstIndex,
					draw.baseVertex);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"build_ia_cache_key",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(iaKeyBegin));
			psoPreFilterKey = DX12IaReplacementExecutor::MakeDrawFastRejectKey(
				runtimeState.pipelineState,
				drawDecision->reloadGeneration,
				targetPlan,
				iaCacheKey.iaViewFingerprint,
				0,
				draw);
			if (DX12IaReplacementExecutor::TryFindPsoDrawFastReject(psoPreFilterKey)
				&& !drawDecision->targetPlanHasDescriptorTargets) {
				DX12Profiling::RecordIaHashStateResult(false);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheLookup();
			LARGE_INTEGER iaCacheBegin = DX12OuterTimingProbeUtils::Now();
			if (DX12IaHashStateCacheUtils::TryLoad(
					iaHashStateCache,
					iaCacheKey,
					&activeIaState,
					&hasIaHashState)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"ia_hash_cache_hit",
					draw.indexed ? 1u : 0u,
					hasIaHashState ? 1 : 0,
					DX12OuterTimingProbeUtils::ElapsedUs(iaCacheBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheHit(
					hasIaHashState);
			} else {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"ia_hash_cache_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(iaCacheBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheMiss();
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashDraw();
				DX12_HOTPATH_TIME_BEGIN(DrawBuildIaHash);
				LARGE_INTEGER buildHashBegin = DX12OuterTimingProbeUtils::Now();
				hasIaHashState = DX12CommandListRuntime::BuildIaHashStateForTargets(
					runtimeState.ia, targetPlan, draw.indexed, draw.vertexCount, draw.indexCount,
					draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState);
				activeIaState = &iaState;
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_ia_hash_state",
					draw.indexed ? 1u : 0u,
					hasIaHashState ? 1 : 0,
					DX12OuterTimingProbeUtils::ElapsedUs(buildHashBegin));
				DX12_HOTPATH_TIME_END(DrawBuildIaHash);
				LARGE_INTEGER storeHashBegin = DX12OuterTimingProbeUtils::Now();
				bool cacheEvicted = false;
				const DX12IaHashState *storedIaState = DX12IaHashStateCacheUtils::Store(
					iaHashStateCache,
					iaCacheKey,
					hasIaHashState,
					std::move(iaState),
					&cacheEvicted);
				if (storedIaState)
					activeIaState = storedIaState;
				if (cacheEvicted)
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheEviction();
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"store_ia_hash_cache",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(storeHashBegin));
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheStore();
			}
			DX12Profiling::RecordIaHashStateResult(hasIaHashState);
		} else {
			DX12Profiling::RecordIaHashStateResult(
				targetPlanHasIaTargets && reusedDirectIaState);
		}
		if (hasIaHashState || !targetPlanHasIaTargets ||
		    drawDecision->targetPlanHasDescriptorTargets) {
			const bool canUsePsoIaNegativeCache =
				psoIaNegativeCacheSafe && (!targetPlanHasIaTargets || hasIaHashState);
			DX12PsoDrawFastRejectKey psoIaNegKey;

			// PSO and IA negative cache.
			if (canUsePsoIaNegativeCache) {
				DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
				LARGE_INTEGER negKeyBegin = DX12OuterTimingProbeUtils::Now();
				psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
					runtimeState.pipelineState,
					drawDecision->reloadGeneration,
					targetPlan,
					*activeIaState,
					draw);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_pso_ia_negative_key",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negKeyBegin));
				LARGE_INTEGER negLookupBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::TryFindPsoDrawFastReject(psoIaNegKey)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"pso_ia_negative_hit",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(negLookupBegin));
					DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"pso_ia_negative_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negLookupBegin));
				DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
			}

			DX12_HOTPATH_TIME_BEGIN(DrawModPrepareOverride);
			LARGE_INTEGER prepareBegin = DX12OuterTimingProbeUtils::Now();
			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, *activeIaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, draw.firstInstance, &iaReplacement,
				shaderOverrideNegativeCacheAllowed,
				runtimeState.device)) {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"prepare_shader_override_match",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(prepareBegin));
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
				DX12_HOTPATH_TIME_BEGIN(DrawApplyAndExecute);
				LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::ApplyAndExecute(
					commandList, draw, *activeIaState, &iaReplacement, runtimeState, callbacks)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"apply_and_execute_match",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
					DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return true;
				}
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"apply_and_execute_failed",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
				DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
			} else {
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"prepare_shader_override_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(prepareBegin));
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
			}

			if (canUsePsoIaNegativeCache) {
				// No match: store both the full negative key and cheap-to-full map.
				LARGE_INTEGER negStoreBegin = DX12OuterTimingProbeUtils::Now();
				DX12IaReplacementExecutor::StorePsoDrawFastReject(psoIaNegKey);
				if (targetPlanHasIaTargets)
					DX12IaReplacementExecutor::StorePsoDrawFastReject(psoPreFilterKey);

				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"store_pso_ia_negative",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negStoreBegin));
			}
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (drawDecision->hasShaderOverrideSkip) {
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return true;
		}
		DX12_HOTPATH_TIME_END(DrawOverrideTotal);
		return false;
	}
};

