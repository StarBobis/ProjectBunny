#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntime.h"
#include "DX12IaReplacementExecutor.h"

void DX12DrawHookFlowExecute(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaDrawInvocation &draw,
	const DX12CommandListRuntimeState *runtimeState,
	const DX12IaReplacementExecutorCallbacks &callbacks);
bool DX12DrawHookFlowNeedsModWork();
