#include "DX12DrawHookFlow.h"

bool DX12DrawHookFlowNeedsModWork()
{
	return DX12ModNeedsPresentReplacement() || DX12ModHasActiveShaderOverrides();
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

void DX12DrawHookFlowExecute(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12CommandListRuntimeState *runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (runtimeState && DX12ModNeedsPresentReplacement())
		DX12IaReplacementExecutePresent(commandList, *runtimeState, callbacks);
	if (runtimeState && DX12IaReplacementHandleDrawOverrides(commandList, draw, *runtimeState, callbacks))
		return;
	ExecuteOriginalDraw(commandList, draw, callbacks);
}
