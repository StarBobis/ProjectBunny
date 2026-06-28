#pragma once

#include <d3d12.h>

class DX12CommandListCommandRecorder
{
public:
	static void RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
	static void RecordExecuteBundle(ID3D12GraphicsCommandList *bundle, ID3D12PipelineState *initialState);
	static void RecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
};

