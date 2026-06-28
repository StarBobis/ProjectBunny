#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntime.hpp"
#include "DX12IaCommandSignatures.hpp"
#include "DX12IaRegion.hpp"
#include "DX12ModRuntime.h"
#include "DX12Profiling.hpp"

class DX12IaDrawInvocation
{
public:
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	static DX12IaDrawInvocation ForDrawInstanced(
		uint32_t vertexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.instanceCount = instanceCount;
		draw.firstVertex = firstVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}

	static DX12IaDrawInvocation ForDrawIndexedInstanced(
		uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstIndex, INT baseVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.instanceCount = instanceCount;
		draw.firstIndex = firstIndex;
		draw.baseVertex = baseVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}
};

class DX12IaReplacementExecutorCallbacks
{
public:
	DX12IaCommandSignatures::DrawInstanced drawInstanced = nullptr;
	DX12IaCommandSignatures::DrawIndexedInstanced drawIndexedInstanced = nullptr;
	DX12IaCommandSignatures::Dispatch dispatch = nullptr;
	DX12IaCommandSignatures::SetIndexBuffer setIndexBuffer = nullptr;
	DX12IaCommandSignatures::SetVertexBuffers setVertexBuffers = nullptr;
	void (*recordReplacementDraw)(bool indexed) = nullptr;
	void (*recordReplacementDispatch)() = nullptr;
};

class DX12IaReplacementDrawScope
{
public:
	DX12IaReplacementDrawScope()
	{
		++Depth();
	}

	~DX12IaReplacementDrawScope()
	{
		--Depth();
	}

	static bool IsExecuting()
	{
		return Depth() > 0;
	}

	static int &Depth()
	{
		static thread_local int depth = 0;
		return depth;
	}
};

class DX12IaReplacementBindingExecutor
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	const DX12IaReplacementExecutorCallbacks *callbacks = nullptr;

	DX12IaReplacementBindingExecutor() = default;

	DX12IaReplacementBindingExecutor(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks)
		: commandList(commandList),
		  callbacks(&callbacks)
	{
	}

	void Apply(const DX12ModIaReplacement &replacement) const
	{
		if (!commandList || !callbacks)
			return;

		if (replacement.hasIndexBuffer && callbacks->setIndexBuffer) {
			DX12CommandListRuntime::RememberIndexBuffer(commandList, &replacement.indexBuffer);
			callbacks->setIndexBuffer(commandList, &replacement.indexBuffer);
		}

		if (!replacement.vertexBuffers.empty() && callbacks->setVertexBuffers) {
			const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
			UINT runStart = 0;
			while (runStart < count) {
				while (runStart < count &&
				       !DX12IaBufferViewUtils::IsVertexBufferBound(
					       replacement.vertexBuffers[runStart]))
					++runStart;
				if (runStart >= count)
					break;

				UINT runEnd = runStart + 1;
				while (runEnd < count &&
				       DX12IaBufferViewUtils::IsVertexBufferBound(
					       replacement.vertexBuffers[runEnd]))
					++runEnd;

				const UINT slot = replacement.vertexBufferStartSlot + runStart;
				const UINT runCount = runEnd - runStart;
				const D3D12_VERTEX_BUFFER_VIEW *views =
					replacement.vertexBuffers.data() + runStart;
				DX12CommandListRuntime::RememberVertexBuffers(commandList, slot, runCount, views);
				callbacks->setVertexBuffers(commandList, slot, runCount, views);
				runStart = runEnd;
			}
		}
	}

	void Restore(
		const DX12ModIaReplacement &replacement,
		const DX12ActiveIaState &originalState) const
	{
		if (!commandList || !callbacks)
			return;

		if (replacement.hasIndexBuffer && callbacks->setIndexBuffer) {
			DX12CommandListRuntime::RememberIndexBuffer(commandList,
				originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
			callbacks->setIndexBuffer(commandList,
				originalState.hasIndexBuffer ? &originalState.indexBuffer : nullptr);
		}

		if (!replacement.vertexBuffers.empty() && callbacks->setVertexBuffers) {
			D3D12_VERTEX_BUFFER_VIEW restore[32] = {};
			const UINT start = replacement.vertexBufferStartSlot;
			const UINT count = static_cast<UINT>(replacement.vertexBuffers.size());
			for (UINT i = 0; i < count && start + i < ARRAYSIZE(originalState.vertexBuffers); ++i)
				restore[i] = originalState.hasVertexBuffer[start + i] ?
					originalState.vertexBuffers[start + i] : D3D12_VERTEX_BUFFER_VIEW();
			DX12CommandListRuntime::RememberVertexBuffers(commandList, start, count, restore);
			callbacks->setVertexBuffers(commandList, start, count, restore);
		}
	}
};

class DX12IaReplacementDrawExecutor
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	const DX12IaReplacementExecutorCallbacks *callbacks = nullptr;

	DX12IaReplacementDrawExecutor() = default;

	DX12IaReplacementDrawExecutor(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaReplacementExecutorCallbacks &callbacks)
		: commandList(commandList),
		  callbacks(&callbacks)
	{
	}

	void ExecuteOriginalDraw(const DX12IaDrawInvocation &draw) const
	{
		if (!commandList || !callbacks)
			return;

		if (draw.indexed) {
			if (callbacks->drawIndexedInstanced) {
				callbacks->drawIndexedInstanced(commandList, draw.indexCount, draw.instanceCount,
					draw.firstIndex, draw.baseVertex, draw.firstInstance);
			}
			return;
		}

		if (callbacks->drawInstanced) {
			callbacks->drawInstanced(commandList, draw.vertexCount, draw.instanceCount,
				draw.firstVertex, draw.firstInstance);
		}
	}

	void ExecuteReplacementDraws(
		const DX12ModIaReplacement &replacement,
		const DX12IaDrawInvocation &originalDraw) const
	{
		if (!commandList || !callbacks)
			return;

		for (const DX12ModIaReplacement::DispatchCall &dispatch : replacement.dispatches) {
			if (!dispatch.groupsX || !dispatch.groupsY || !dispatch.groupsZ || !callbacks->dispatch)
				continue;
			if (callbacks->recordReplacementDispatch)
				callbacks->recordReplacementDispatch();
			callbacks->dispatch(commandList, dispatch.groupsX, dispatch.groupsY, dispatch.groupsZ);
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
				if (callbacks->drawIndexedInstanced) {
					if (callbacks->recordReplacementDraw)
						callbacks->recordReplacementDraw(true);
					callbacks->drawIndexedInstanced(
						commandList, count, originalDraw.instanceCount,
						start, baseVertex, originalDraw.firstInstance);
				}
			} else if (callbacks->drawInstanced) {
				if (callbacks->recordReplacementDraw)
					callbacks->recordReplacementDraw(false);
				callbacks->drawInstanced(
					commandList, count, originalDraw.instanceCount,
					start, originalDraw.firstInstance);
			}
		}
	}
};

class DX12IaReplacementExecutor
{
public:
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
			if (DX12ModPrepareShaderOverrideReplacement(
				commandList, runtimeState.pipelineState, iaState,
				vertexCount, indexCount, draw.instanceCount,
				firstVertex, firstIndex, &iaReplacement)) {
				if (DX12IaReplacementExecutor::ApplyAndExecute(
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
};

