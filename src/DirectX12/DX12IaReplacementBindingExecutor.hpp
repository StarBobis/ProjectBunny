#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12ActiveIaState.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12IaReplacementExecutorCallbacks.hpp"
#include "DX12IaBufferViewUtils.hpp"

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
