#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntime.h"

typedef void(STDMETHODCALLTYPE *DX12DispatchHookFlowDispatch)(
	ID3D12GraphicsCommandList*, UINT, UINT, UINT);

void DX12DispatchHookFlowExecute(
	ID3D12GraphicsCommandList *commandList,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ,
	const DX12CommandListRuntimeState *runtimeState,
	DX12DispatchHookFlowDispatch originalDispatch);
