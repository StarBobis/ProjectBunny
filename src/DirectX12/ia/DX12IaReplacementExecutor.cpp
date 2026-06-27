#include "DX12IaReplacementExecutor.h"

#include "DX12Profiling.h"

static thread_local int tIaReplacementDrawDepth = 0;

struct DX12IaReplacementDrawScope
{
	DX12IaReplacementDrawScope()
	{
		++tIaReplacementDrawDepth;
	}

	~DX12IaReplacementDrawScope()
	{
		--tIaReplacementDrawDepth;
	}
};

bool DX12IaReplacementIsExecutingInternalDraw()
{
	return tIaReplacementDrawDepth > 0;
}

static bool VertexBufferViewIsBound(const D3D12_VERTEX_BUFFER_VIEW &view)
{
	return view.BufferLocation != 0 && view.SizeInBytes != 0;
}

static void ApplyIaReplacement(
	ID3D12GraphicsCommandList *commandList,
	const DX12ModIaReplacement &replacement,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (replacement.hasIndexBuffer && callbacks.setIndexBuffer) {
		DX12CommandListRuntimeRememberIndexBuffer(commandList, &replacement.indexBuffer);
		callbacks.setIndexBuffer(commandList, &replacement.indexBuffer);
	}

	if (!replacement.vertexBuffers.empty() && callbacks.setVertexBuffers) {
		const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
		UINT runStart = 0;
		while (runStart < count) {
			while (runStart < count &&
			       !VertexBufferViewIsBound(replacement.vertexBuffers[runStart]))
				++runStart;
			if (runStart >= count)
				break;

			UINT runEnd = runStart + 1;
			while (runEnd < count &&
			       VertexBufferViewIsBound(replacement.vertexBuffers[runEnd]))
				++runEnd;

			const UINT slot = replacement.vertexBufferStartSlot + runStart;
			const UINT runCount = runEnd - runStart;
			const D3D12_VERTEX_BUFFER_VIEW *views =
				replacement.vertexBuffers.data() + runStart;
			DX12CommandListRuntimeRememberVertexBuffers(commandList, slot, runCount, views);
			callbacks.setVertexBuffers(commandList, slot, runCount, views);
			runStart = runEnd;
		}
	}
}

static void RestoreIaReplacement(
	ID3D12GraphicsCommandList *commandList,
	const DX12ModIaReplacement &replacement,
	const DX12ActiveIaState &originalState,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (replacement.hasIndexBuffer && callbacks.setIndexBuffer) {
		DX12CommandListRuntimeRememberIndexBuffer(commandList,
			originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
		callbacks.setIndexBuffer(commandList,
			originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
	}

	if (!replacement.vertexBuffers.empty() && callbacks.setVertexBuffers) {
		D3D12_VERTEX_BUFFER_VIEW restore[32] = {};
		const UINT start = replacement.vertexBufferStartSlot;
		const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
		for (UINT i = 0; i < count && start + i < ARRAYSIZE(originalState.vertexBuffers); ++i)
			restore[i] = originalState.hasVertexBuffer[start + i] ?
				originalState.vertexBuffers[start + i] : D3D12_VERTEX_BUFFER_VIEW();
		DX12CommandListRuntimeRememberVertexBuffers(commandList, start, count, restore);
		callbacks.setVertexBuffers(commandList, start, count, restore);
	}
}

static void ExecuteOriginalDraw(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (draw.indexed) {
		if (callbacks.drawIndexedInstanced) {
			callbacks.drawIndexedInstanced(commandList, draw.indexCount, draw.instanceCount,
				draw.firstIndex, draw.baseVertex, draw.firstInstance);
		}
		return;
	}

	if (callbacks.drawInstanced) {
		callbacks.drawInstanced(commandList, draw.vertexCount, draw.instanceCount,
			draw.firstVertex, draw.firstInstance);
	}
}

static void ExecuteReplacementDraws(
	ID3D12GraphicsCommandList *commandList,
	const DX12ModIaReplacement &replacement,
	const DX12IaDrawInvocation &originalDraw,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	for (const DX12ModIaReplacement::DispatchCall &dispatch : replacement.dispatches) {
		if (!dispatch.groupsX || !dispatch.groupsY || !dispatch.groupsZ || !callbacks.dispatch)
			continue;
		if (callbacks.recordReplacementDispatch)
			callbacks.recordReplacementDispatch();
		callbacks.dispatch(commandList, dispatch.groupsX, dispatch.groupsY, dispatch.groupsZ);
	}

	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		UINT count = draw.count;
		UINT start = draw.start;
		INT baseVertex = draw.baseVertex;

		if (draw.fromCaller) {
			if (draw.indexed) {
				count = originalDraw.indexCount;
				start = originalDraw.firstIndex;
				baseVertex = originalDraw.baseVertex;
			} else {
				count = originalDraw.vertexCount;
				start = originalDraw.firstVertex;
			}
			if (!count)
				continue;
		} else {
			if (!draw.count)
				continue;
		}

		DX12IaReplacementDrawScope replacementDrawScope;
		if (draw.indexed) {
			if (callbacks.drawIndexedInstanced) {
				if (callbacks.recordReplacementDraw)
					callbacks.recordReplacementDraw(true);
				callbacks.drawIndexedInstanced(
					commandList, count, originalDraw.instanceCount,
					start, baseVertex, originalDraw.firstInstance);
			}
		} else if (callbacks.drawInstanced) {
			if (callbacks.recordReplacementDraw)
				callbacks.recordReplacementDraw(false);
			callbacks.drawInstanced(
				commandList, count, originalDraw.instanceCount,
				start, originalDraw.firstInstance);
		}
	}
}

bool DX12IaReplacementExecutePresent(
	ID3D12GraphicsCommandList *commandList,
	const DX12CommandListRuntimeState &runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	DX12ModIaReplacement replacement;
	if (!DX12ModPreparePresentReplacement(commandList, &replacement))
		return false;

	const DX12ActiveIaState originalIa = runtimeState.ia;
	ApplyIaReplacement(commandList, replacement, callbacks);
	DX12IaDrawInvocation noDraw = {};
	ExecuteReplacementDraws(commandList, replacement, noDraw, callbacks);
	RestoreIaReplacement(commandList, replacement, originalIa, callbacks);
	return true;
}

bool DX12IaReplacementApplyAndExecute(
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
	ApplyIaReplacement(commandList, *replacement, callbacks);

	if (!replacement->skip)
		ExecuteOriginalDraw(commandList, draw, callbacks);

	ExecuteReplacementDraws(commandList, *replacement, draw, callbacks);
	RestoreIaReplacement(commandList, *replacement, originalIa, callbacks);
	DX12Profiling::RecordIaApplied();
	DX12ModRunPostShaderOverrideReplacement(
		commandList, runtimeState.pipelineState, iaState,
		draw.vertexCount, draw.indexCount, draw.instanceCount,
		draw.firstVertex, draw.firstIndex, replacement);
	return true;
}

bool DX12IaReplacementHandleDrawOverrides(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12CommandListRuntimeState &runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (!DX12ModHasActiveShaderOverrides())
		return false;

	DX12Profiling::RecordIaDrawOverrideCheck();
	DX12ModIaReplacement iaReplacement;
	DX12IaHashState iaState;
	const uint32_t vertexCount = draw.indexed ? 0 : draw.vertexCount;
	const uint32_t indexCount = draw.indexed ? draw.indexCount : 0;
	const uint32_t firstVertex = draw.indexed ? 0 : draw.firstVertex;
	const uint32_t firstIndex = draw.indexed ? draw.firstIndex : 0;

	if (DX12CommandListRuntimeBuildIaHashState(
		runtimeState.ia, draw.indexed, draw.vertexCount, draw.indexCount,
		draw.firstVertex, draw.firstIndex, draw.baseVertex, &iaState)) {
		DX12Profiling::RecordIaHashStateResult(true);
		if (DX12ModPrepareShaderOverrideReplacement(
			commandList, runtimeState.pipelineState, iaState,
			vertexCount, indexCount, draw.instanceCount,
			firstVertex, firstIndex, &iaReplacement)) {
			if (DX12IaReplacementApplyAndExecute(
				commandList, draw, iaState, &iaReplacement, runtimeState, callbacks))
				return true;
		}
		return false;
	}

	DX12Profiling::RecordIaHashStateResult(false);
	if (DX12ModShouldSkipPipelineState(runtimeState.pipelineState, false))
		return true;
	return false;
}
