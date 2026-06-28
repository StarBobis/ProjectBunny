#include "DX12DrawHookFlow.h"

bool DX12DrawHookFlowNeedsModWork()
{
	return DX12ModNeedsPresentReplacement() || DX12ModHasActiveShaderOverrides();
}

void DX12DrawHookFlowExecute(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12CommandListRuntimeState *runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks)
{
	if (runtimeState && DX12ModNeedsPresentReplacement())
		DX12IaReplacementExecutor::ExecutePresent(commandList, *runtimeState, callbacks);
	if (runtimeState &&
	    DX12IaReplacementExecutor::HandleDrawOverrides(commandList, draw, *runtimeState, callbacks))
		return;
	DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
}

