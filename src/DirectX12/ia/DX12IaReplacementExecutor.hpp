#pragma once

#include <d3d12.h>

#include <unordered_map>
#include <unordered_set>

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
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

// Legacy stub - referenced by compiled code but definition was never provided.
inline bool TryQuickRejectPsoNoMatch(ID3D12PipelineState *)
{
	return false;
}

class DX12IaReplacementExecutor
{
public:
	// PSO and IA negative cache for no-match results.
	// The cache persists across frames and is cleared only on mod reload.
	inline static std::unordered_set<UINT64> s_psoIaNegativeCache;
	inline static SRWLOCK s_psoIaNegativeCacheLock = SRWLOCK_INIT;

	// Cheap fingerprint cache maps PSO, IA GPUVA, and draw args to a full
	// negative key so repeated no-match draws can skip content hashing.
	inline static std::unordered_map<UINT64, UINT64> s_cheapToFullKeyMap;
	inline static SRWLOCK s_cheapToFullKeyMapLock = SRWLOCK_INIT;

	static UINT64 MixCheapValue(UINT64 key, UINT64 value)
	{
		key ^= value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		return key;
	}

	static UINT64 MakeCheapIaFingerprint(
		ID3D12PipelineState *pipelineState,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12ActiveIaState &ia,
		bool indexed, UINT vertexCount, UINT indexCount,
		UINT firstVertex, UINT firstIndex, INT baseVertex, UINT instanceCount)
	{
		UINT64 key = reinterpret_cast<UINT64>(pipelineState);
		key = MixCheapValue(key, targetPlan.Fingerprint());
		key = MixCheapValue(key, (static_cast<UINT64>(indexed) << 63) |
			(static_cast<UINT64>(vertexCount) << 32) | static_cast<UINT64>(indexCount));
		key = MixCheapValue(key,
			(static_cast<UINT64>(firstVertex) << 32) | static_cast<UINT64>(firstIndex));
		key = MixCheapValue(key,
			(static_cast<UINT64>(static_cast<UINT32>(baseVertex)) << 32) |
			static_cast<UINT64>(instanceCount));
		if (targetPlan.RequiresIndexBuffer(indexed)) {
			key = MixCheapValue(key, ia.hasIndexBuffer ? 0x1bull : 0x10ull);
			if (ia.hasIndexBuffer) {
				key = MixCheapValue(key, ia.indexBuffer.SizeInBytes);
				key = MixCheapValue(key, static_cast<UINT64>(ia.indexBuffer.Format));
			}
		}
		for (UINT slot = 0; slot < 32; ++slot) {
			if (!targetPlan.RequiresVertexBuffer(slot))
				continue;
			key = MixCheapValue(key, (static_cast<UINT64>(slot) << 32) |
				(ia.hasVertexBuffer[slot] ? 0x1ull : 0x0ull));
			if (!ia.hasVertexBuffer[slot])
				continue;
			key = MixCheapValue(key, ia.vertexBuffers[slot].SizeInBytes);
			key = MixCheapValue(key, ia.vertexBuffers[slot].StrideInBytes);
		}
		return key ? key : 1;
	}

	static UINT64 MakePsoIaNegativeKey(
		ID3D12PipelineState *pipelineState,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaHashState &iaState)
	{
		UINT64 key = reinterpret_cast<UINT64>(pipelineState);
		key = MixCheapValue(key, targetPlan.Fingerprint());
		key = MixCheapValue(key, iaState.hasIndexBuffer ? 1ull : 0ull);
		if (iaState.hasIndexBuffer) {
			key = MixCheapValue(key, iaState.indexHash);
			key = MixCheapValue(key, iaState.indexView.BufferLocation);
			key = MixCheapValue(key, iaState.indexView.SizeInBytes);
			key = MixCheapValue(key, static_cast<UINT64>(iaState.indexView.Format));
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			key = MixCheapValue(key, static_cast<UINT64>(buffer.slot));
			key = MixCheapValue(key, buffer.hash);
			key = MixCheapValue(key, buffer.vertexView.BufferLocation);
			key = MixCheapValue(key, buffer.vertexView.SizeInBytes);
			key = MixCheapValue(key, buffer.vertexView.StrideInBytes);
		}
		return key ? key : 1;
	}

	static bool TryFindPsoIaNegativeCache(UINT64 key)
	{
		AcquireSRWLockShared(&s_psoIaNegativeCacheLock);
		bool found = s_psoIaNegativeCache.find(key) != s_psoIaNegativeCache.end();
		ReleaseSRWLockShared(&s_psoIaNegativeCacheLock);
		return found;
	}

	static void StorePsoIaNegativeCache(UINT64 key)
	{
		AcquireSRWLockExclusive(&s_psoIaNegativeCacheLock);
		s_psoIaNegativeCache.insert(key);
		ReleaseSRWLockExclusive(&s_psoIaNegativeCacheLock);
	}

	static void StoreCheapToFullKeyMap(UINT64 cheapKey, UINT64 fullKey)
	{
		if (!cheapKey || !fullKey)
			return;

		AcquireSRWLockExclusive(&s_cheapToFullKeyMapLock);
		if (s_cheapToFullKeyMap.size() > 4096)
			s_cheapToFullKeyMap.clear();
		s_cheapToFullKeyMap[cheapKey] = fullKey;
		ReleaseSRWLockExclusive(&s_cheapToFullKeyMapLock);
	}

	static void ClearPsoIaNegativeCache()
	{
		AcquireSRWLockExclusive(&s_psoIaNegativeCacheLock);
		s_psoIaNegativeCache.clear();
		ReleaseSRWLockExclusive(&s_psoIaNegativeCacheLock);
		AcquireSRWLockExclusive(&s_cheapToFullKeyMapLock);
		s_cheapToFullKeyMap.clear();
		ReleaseSRWLockExclusive(&s_cheapToFullKeyMapLock);
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
		DX12IaDrawInvocation noDraw = {};
		drawExecutor.ExecuteReplacementDraws(replacement, noDraw);
		bindingExecutor.Restore(replacement, originalIa);
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
		if (replacement->skip && replacement->draws.empty() && replacement->dispatches.empty()) {
			return true;
		}

		const DX12ActiveIaState originalIa = runtimeState.ia;
		DX12IaReplacementBindingExecutor bindingExecutor(commandList, callbacks);
		DX12IaReplacementDrawExecutor drawExecutor(commandList, callbacks);
		bindingExecutor.Apply(*replacement);

		if (!replacement->skip)
			drawExecutor.ExecuteOriginalDraw(draw);

		drawExecutor.ExecuteReplacementDraws(*replacement, draw);
		bindingExecutor.Restore(*replacement, originalIa);
		DX12Profiling::RecordIaApplied();
		DX12ModRunPostShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			draw.vertexCount, draw.indexCount, draw.instanceCount,
			draw.firstVertex, draw.firstIndex, replacement);
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
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		// Cheap negative cache lookup uses PSO, IA view identity, and draw args
		// before falling back to stable IA content hashing.
		DX12IaHashTargetPlan targetPlan = drawDecision->targetPlan;
		const bool targetPlanHasIaTargets = drawDecision->targetPlanHasIaTargets;
		const bool psoIaNegativeCacheSafe = drawDecision->negativeCacheSafe;
		UINT64 cheapKey = 0;

		if (psoIaNegativeCacheSafe && targetPlanHasIaTargets) {
			LARGE_INTEGER cheapBuildBegin = DX12OuterTimingProbeUtils::Now();
			cheapKey = DX12IaReplacementExecutor::MakeCheapIaFingerprint(
				runtimeState.pipelineState, targetPlan, runtimeState.ia,
				draw.indexed, draw.vertexCount, draw.indexCount,
				draw.firstVertex, draw.firstIndex, draw.baseVertex, draw.instanceCount);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"cheap_fingerprint",
				draw.indexed ? 1u : 0u,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cheapBuildBegin));
			UINT64 mappedFullKey = 0;
			bool cheapHit = false;
			LARGE_INTEGER cheapLookupBegin = DX12OuterTimingProbeUtils::Now();
			AcquireSRWLockShared(&s_cheapToFullKeyMapLock);
			std::unordered_map<UINT64, UINT64>::iterator it =
				s_cheapToFullKeyMap.find(cheapKey);
			if (it != s_cheapToFullKeyMap.end()) {
				mappedFullKey = it->second;
				cheapHit = true;
			}
			ReleaseSRWLockShared(&s_cheapToFullKeyMapLock);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"IaReplacement",
				"cheap_negative_lookup",
				draw.indexed ? 1u : 0u,
				cheapHit ? 1 : 0,
				DX12OuterTimingProbeUtils::ElapsedUs(cheapLookupBegin));

			if (cheapHit) {
				// Cheap fingerprint hits let repeated no-match draws skip
				// draw-region IA hashing.
				LARGE_INTEGER fullLookupBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(mappedFullKey)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"cheap_full_negative_hit",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(fullLookupBegin));
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"cheap_full_negative_miss",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(fullLookupBegin));
			}
		}

		bool hasIaHashState = false;
		if (targetPlanHasIaTargets) {
			LARGE_INTEGER iaKeyBegin = DX12OuterTimingProbeUtils::Now();
			DX12IaHashStateCacheKey iaCacheKey =
				DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargets(
					runtimeState.ia,
					runtimeState.pipelineState,
					drawDecision->reloadGeneration,
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
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawDecisionIaHashCacheLookup();
			LARGE_INTEGER iaCacheBegin = DX12OuterTimingProbeUtils::Now();
			if (DX12IaHashStateCacheUtils::TryLoad(
					&mutableRuntimeState->iaHashStateCache,
					&mutableRuntimeState->iaHashStateCacheClock,
					iaCacheKey,
					&iaState,
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
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_ia_hash_state",
					draw.indexed ? 1u : 0u,
					hasIaHashState ? 1 : 0,
					DX12OuterTimingProbeUtils::ElapsedUs(buildHashBegin));
				DX12_HOTPATH_TIME_END(DrawBuildIaHash);
				LARGE_INTEGER storeHashBegin = DX12OuterTimingProbeUtils::Now();
				DX12IaHashStateCacheUtils::Store(
					&mutableRuntimeState->iaHashStateCache,
					&mutableRuntimeState->iaHashStateCacheClock,
					iaCacheKey,
					hasIaHashState,
					iaState);
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
			DX12Profiling::RecordIaHashStateResult(false);
		}
		if (hasIaHashState || !targetPlanHasIaTargets ||
		    drawDecision->targetPlanHasDescriptorTargets) {
			const bool canUsePsoIaNegativeCache =
				psoIaNegativeCacheSafe && (!targetPlanHasIaTargets || hasIaHashState);
			UINT64 psoIaNegKey = 0;

			// PSO and IA negative cache.
			if (canUsePsoIaNegativeCache) {
				DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
				LARGE_INTEGER negKeyBegin = DX12OuterTimingProbeUtils::Now();
				psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
					runtimeState.pipelineState, targetPlan, iaState);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"IaReplacement",
					"build_pso_ia_negative_key",
					draw.indexed ? 1u : 0u,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(negKeyBegin));
				LARGE_INTEGER negLookupBegin = DX12OuterTimingProbeUtils::Now();
				if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(psoIaNegKey)) {
					DX12OuterTimingProbeUtils::LogCpuStage(
						"IaReplacement",
						"pso_ia_negative_hit",
						draw.indexed ? 1u : 0u,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(negLookupBegin));
					// Cache the cheap fingerprint so later identical no-match draws
					// can skip BuildIaHashState entirely.
					if (cheapKey)
						DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
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
				commandList, runtimeState.pipelineState, iaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, &iaReplacement,
				psoIaNegativeCacheSafe)) {
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
					commandList, draw, iaState, &iaReplacement, runtimeState, callbacks)) {
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
				DX12IaReplacementExecutor::StorePsoIaNegativeCache(psoIaNegKey);
				if (cheapKey)
					DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
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

