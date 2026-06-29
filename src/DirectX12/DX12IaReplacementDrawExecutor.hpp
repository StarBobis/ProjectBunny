#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12IaReplacementDrawScope.hpp"
#include "DX12ModRuntime.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"

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
