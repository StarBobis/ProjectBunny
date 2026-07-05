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

	static UINT64 MakeCheapIaFingerprint(
		ID3D12PipelineState *pipelineState,
		const DX12ActiveIaState &ia,
		bool indexed, UINT vertexCount, UINT indexCount,
		UINT firstVertex, UINT firstIndex, INT baseVertex, UINT instanceCount)
	{
		UINT64 key = reinterpret_cast<UINT64>(pipelineState);
		key ^= (static_cast<UINT64>(indexed) << 63) |
		       (static_cast<UINT64>(vertexCount) << 32) | static_cast<UINT64>(indexCount);
		key ^= (static_cast<UINT64>(firstVertex) << 32) | static_cast<UINT64>(firstIndex);
		key ^= (static_cast<UINT64>(static_cast<UINT32>(baseVertex)) << 32) |
		       static_cast<UINT64>(instanceCount);
		if (ia.hasIndexBuffer && indexed)
			key ^= ia.indexBuffer.BufferLocation ^ (ia.indexBuffer.BufferLocation >> 29);
		UINT vbMixed = 0;
		for (UINT slot = 0; slot < 32 && vbMixed < 4; ++slot) {
			if (!ia.hasVertexBuffer[slot])
				continue;
			UINT64 vb = ia.vertexBuffers[slot].BufferLocation;
			key ^= (vb << (vbMixed * 13)) ^ (vb >> (53 - vbMixed * 7));
			++vbMixed;
		}
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		return key;
	}

	static UINT64 MakePsoIaNegativeKey(
		ID3D12PipelineState *pipelineState, const DX12IaHashState &iaState)
	{
		UINT64 key = reinterpret_cast<UINT64>(pipelineState);
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		key ^= iaState.indexHash;
		key ^= key >> 33;
		key *= 0xc4ceb9fe1a85ec53ull;
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers)
			key ^= (static_cast<UINT64>(buffer.hash) << 17) ^ (static_cast<UINT64>(buffer.slot) << 49);
		return key;
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
		const UINT64 cheapKey = DX12IaReplacementExecutor::MakeCheapIaFingerprint(
			runtimeState.pipelineState, runtimeState.ia,
			draw.indexed, draw.vertexCount, draw.indexCount,
			draw.firstVertex, draw.firstIndex, draw.baseVertex, draw.instanceCount);

		{
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

		DX12_HOTPATH_TIME_BEGIN(DrawBuildIaHash);
		if (DX12CommandListRuntime::BuildIaHashState(
			runtimeState.ia, draw.indexed, draw.vertexCount, draw.indexCount,
			draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState)) {
			DX12_HOTPATH_TIME_END(DrawBuildIaHash);
			DX12Profiling::RecordIaHashStateResult(true);

			// PSO+IA 组合级负缓存
			DX12_HOTPATH_TIME_BEGIN(DrawPsoIaNegCache);
			const UINT64 psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
				runtimeState.pipelineState, iaState);
			if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(psoIaNegKey)) {
				// Cache the cheap fingerprint so later identical no-match draws
				// can skip BuildIaHashState entirely.
				DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
				DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);
				DX12_HOTPATH_TIME_END(DrawOverrideTotal);
				return false;
			}
			DX12_HOTPATH_TIME_END(DrawPsoIaNegCache);

			DX12_HOTPATH_TIME_BEGIN(DrawModPrepareOverride);
			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, iaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, &iaReplacement)) {
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

			// No match: store both the full negative key and cheap-to-full map.
			DX12IaReplacementExecutor::StorePsoIaNegativeCache(psoIaNegKey);
			DX12IaReplacementExecutor::StoreCheapToFullKeyMap(cheapKey, psoIaNegKey);
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return false;
		}
		DX12_HOTPATH_TIME_END(DrawBuildIaHash);

		DX12Profiling::RecordIaHashStateResult(false);
		if (DX12ModShouldSkipPipelineState(runtimeState.pipelineState, false)) {
			DX12_HOTPATH_TIME_END(DrawOverrideTotal);
			return true;
		}
		DX12_HOTPATH_TIME_END(DrawOverrideTotal);
		return false;
	}
};

