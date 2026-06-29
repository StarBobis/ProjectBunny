#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntime.hpp"
#include "DX12IaReplacementExecutor.hpp"

class DX12DrawHookFlow
{
public:
	static bool NeedsModWork()
	{
		return DX12ModNeedsPresentReplacement() || DX12ModHasActiveShaderOverrides();
	}

	static void Execute(
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
};
