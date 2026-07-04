#pragma once

#include <d3d12.h>

#include <unordered_set>

#include "DX12CommandListRuntime.hpp"
#include "DX12IaReplacementBindingExecutor.hpp"
#include "DX12IaReplacementDrawExecutor.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"

class DX12IaReplacementExecutor
{
public:
	// PSO+IA 组合级负缓存：记录 (PSO, indexHash, vertexHashes[]) 无匹配结果。
	// per-frame 生命周期，在 DX12ModBeginFrame 中清空。
	inline static std::unordered_set<UINT64> s_psoIaNegativeCache;
	inline static SRWLOCK s_psoIaNegativeCacheLock = SRWLOCK_INIT;

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

	static void ClearPsoIaNegativeCache()
	{
		AcquireSRWLockExclusive(&s_psoIaNegativeCacheLock);
		s_psoIaNegativeCache.clear();
		ReleaseSRWLockExclusive(&s_psoIaNegativeCacheLock);
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

		// PSO 级提前退出：若该 PSO 已缓存且无匹配 shader override 配置，
		// 则跳过后续 IA hash 计算，直接返回。
		if (runtimeState.pipelineState &&
		    !DX12ModHasShaderOverridesForPso(runtimeState.pipelineState))
			return false;

		DX12Profiling::RecordIaDrawOverrideCheck();
		DX12ModIaReplacement iaReplacement;
		DX12IaHashState iaState;
		const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
		const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
		const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
		const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

		if (DX12CommandListRuntime::BuildIaHashState(
			runtimeState.ia, draw.indexed, draw.vertexCount, draw.indexCount,
			draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState)) {
			DX12Profiling::RecordIaHashStateResult(true);

			// PSO+IA 组合级负缓存：同一 PSO + 同一 IA hash 组合上次已确认无匹配
			// → 跳过 DX12ModPrepareShaderOverrideReplacement 的完整流程。
			// Key 不含 draw 参数 → 命中率远高于 ShaderOverrideNegativeCache。
			const UINT64 psoIaNegKey = DX12IaReplacementExecutor::MakePsoIaNegativeKey(
				runtimeState.pipelineState, iaState);
			if (DX12IaReplacementExecutor::TryFindPsoIaNegativeCache(psoIaNegKey))
				return false;

			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, iaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, &iaReplacement)) {
				if (DX12IaReplacementExecutor::ApplyAndExecute(
					commandList, draw, iaState, &iaReplacement, runtimeState, callbacks))
					return true;
			}

			// 未匹配 → 存入 PSO+IA 负缓存
			DX12IaReplacementExecutor::StorePsoIaNegativeCache(psoIaNegKey);
			return false;
		}

		DX12Profiling::RecordIaHashStateResult(false);
		if (DX12ModShouldSkipPipelineState(runtimeState.pipelineState, false))
			return true;
		return false;
	}
};

