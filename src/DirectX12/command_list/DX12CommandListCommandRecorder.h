#pragma once

#include <d3d12.h>

void DX12CommandListCommandRecordResourceBarrier(
	UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
void DX12CommandListCommandRecordExecuteBundle(
	ID3D12GraphicsCommandList *bundle,
	ID3D12PipelineState *initialState);
void DX12CommandListCommandRecordExecuteIndirect(
	ID3D12GraphicsCommandList *commandList);
