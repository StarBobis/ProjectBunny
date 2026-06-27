#pragma once

#include <d3d12.h>

void DX12CommandListLifecycleRegister(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState);
void DX12CommandListLifecycleReset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState);
void DX12CommandListLifecycleClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState);
