#pragma once

#include <d3d12.h>

#include <unordered_map>
#include <unordered_set>

#include "DX12CommandListRuntime.hpp"
#include "DX12IaReplacementBindingExecutor.hpp"
#include "DX12IaReplacementDrawExecutor.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"

// Legacy stub — referenced by compiled code but definition was never provided
inline bool TryQuickRejectPsoNoMatch(ID3D12PipelineState *)
{
	return false;
}

class DX12IaReplacementExecutor
{
public:
	// PSO+IA 组合级负缓存：记录 (PSO, indexHash, vertexHashes[]) 无匹配结果。
	// 跨帧持久，仅在 mod 重载时清空。
	inline static std::unordered_set<UINT64> s_psoIaNegativeCache;
	inline static SRWLOCK s_psoIaNegativeCacheLock = SRWLOCK_INIT;

	// 廉价指纹缓存：用 (PSO, IB/VB GpuVa, draw args) 替代内容 hash 作为 key。
	// 指纹计算只需读取指针+整数运算（~0.3us），vs BuildIaHash 的 ~35us。
	// 指纹命中 → 映射到 full PsoIaNegKey → 检查 PsoIaNegCache → 跳过 hash。
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
				key = MixCheapValue(key, ia.indexBuffer.BufferLocation);
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
			key = MixCheapValue(key, ia.vertexBuffers[slot].BufferLocation);
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
		if (!DX12ModHasActiveShaderOverrides())
			return false;

		DX12_HOTPATH_TIME_BEGIN(DrawOverrideTotal);

		// PSO 级提前退出：利用 runtimeState 缓存的 PSO 标志，避免每 draw 获取 gModLock。
		// 缓存在 tracking generation 变化时自动失效（通过 generation 比对）。
		DX12_HOTPATH_TIME_BEGIN(DrawPsoEarlyExit);
		if (runtimeState.pipelineState) {
			const LONG gen = static_cast<LONG>(DX12CommandListRuntime::TrackingGeneration());
			const LONG cachedGen = InterlockedCompareExchange(
				const_cast<volatile LONG*>(&runtimeState.psoHasOverridesGeneration), 0, 0);
			if (cachedGen == gen) {
				if (!runtimeState.psoHasShaderOverrides) {
					DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			} else {
				const bool hasOverrides = DX12ModHasShaderOverridesForPso(runtimeState.pipelineState);
				InterlockedExchange(
					const_cast<volatile LONG*>(&runtimeState.psoHasOverridesGeneration), gen);
				const_cast<DX12CommandListRuntimeState&>(runtimeState).psoHasShaderOverrides = hasOverrides;
				if (!hasOverrides) {
					DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			}
		}
		DX12_HOTPATH_TIME_END(DrawPsoEarlyExit);

		DX12Profiling::RecordIaDrawOverrideCheck();
		DX12ModIaReplacement iaReplacement;
		DX12IaHashState iaState;
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		// ── 廉价指纹缓存：用 (PSO, GpuVa, draw args) 替代内容 hash ──
		// 指纹计算只需指针读取+整数运算（~0.3us vs BuildIaHash 的 ~35us）。
		// 命中时直接跳过 BuildIaHash 链条。
		DX12IaHashTargetPlan targetPlan;
		if (!DX12ModCollectShaderOverrideIaHashTargets(
			    runtimeState.pipelineState, &targetPlan))
			targetPlan.RequireAll();

		const bool targetPlanHasIaTargets = targetPlan.HasIaTargets();
		const bool psoIaNegativeCacheSafe =
			!targetPlan.hasDescriptorTargets &&
			!DX12ModNeedsShaderDescriptorTracking();
		UINT64 cheapKey = 0;

		if (psoIaNegativeCacheSafe && targetPlanHasIaTargets) {
			cheapKey = DX12IaReplacementExecutor::MakeCheapIaFingerprint(
				runtimeState.pipelineState, targetPlan, runtimeState.ia,
				draw.indexed, draw.vertexCount, draw.indexCount,
				draw.firstVertex, draw.firstIndex, draw.baseVertex, draw.instanceCount);
			UINT64 mappedFullKey = 0;
			bool cheapHit = false;
			AcquireSRWLockShared(&s_cheapToFullKeyMapLock);
			std::unordered_map<UINT64, UINT64>::iterator it =
				s_cheapToFullKeyMap.find(cheapKey);
			if (it != s_cheapToFullKeyMap.end()) {
				mappedFullKey = it->second;
				cheapHit = true;
			}
			ReleaseSRWLockShared(&s_cheapToFullKeyMapLock);

			if (cheapHit) {
				// Cheap fingerprint hits let repeated no-match draws skip
				// draw-region IA hashing.
				if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(mappedFullKey)) {
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
			}
		}

		bool hasIaHashState = false;
		if (targetPlanHasIaTargets) {
			DX12_HOTPATH_TIME_BEGIN(DrawBuildIaHash);
			hasIaHashState = DX12CommandListRuntime::BuildIaHashStateForTargets(
				runtimeState.ia, targetPlan, draw.indexed, draw.vertexCount, draw.indexCount,
				draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState);
			DX12_HOTPATH_TIME_END(DrawBuildIaHash);
			DX12Profiling::RecordIaHashStateResult(hasIaHashState);
		} else {
			DX12Profiling::RecordIaHashStateResult(false);
		}
		if (hasIaHashState || !targetPlanHasIaTargets || targetPlan.hasDescriptorTargets) {
			const bool canUsePsoIaNegativeCache =
				psoIaNegativeCacheSafe && (!targetPlanHasIaTargets || hasIaHashState);
			UINT64 psoIaNegKey = 0;

			// PSO+IA 组合级负缓存
			if (canUsePsoIaNegativeCache) {
				DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
				psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
					runtimeState.pipelineState, targetPlan, iaState);
				if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(psoIaNegKey)) {
					// Cache the cheap fingerprint so later identical no-match draws
					// can skip BuildIaHashState entirely.
					if (cheapKey)
						DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
					DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return false;
				}
				DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
			}

			DX12_HOTPATH_TIME_BEGIN(DrawModPrepareOverride);
			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, iaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, &iaReplacement,
				psoIaNegativeCacheSafe)) {
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
				DX12_HOTPATH_TIME_BEGIN(DrawApplyAndExecute);
				if (DX12IaReplacementExecutor::ApplyAndExecute(
					commandList, draw, iaState, &iaReplacement, runtimeState, callbacks)) {
					DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
					DX12_HOTPATH_TIME_END(DrawOverrideTotal);
					return true;
				}
				DX12_HOTPATH_TIME_END(DrawApplyAndExecute);
			} else {
				DX12_HOTPATH_TIME_END(DrawModPrepareOverride);
			}

			if (canUsePsoIaNegativeCache) {
				// No match: store both the full negative key and cheap-to-full map.
				DX12IaReplacementExecutor::StorePsoIaNegativeCache(psoIaNegKey);
				if (cheapKey)
					DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
			}
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		if (DX12ModShouldSkipPipelineState(runtimeState.pipelineState, false)) {
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return true;
		}
		DX12_HOTPATH_TIME_END(DrawOverrideTotal);
		return false;
	}
};

