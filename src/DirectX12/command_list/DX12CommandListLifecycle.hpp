#pragma once

#include <d3d12.h>

class DX12CommandListLifecycle
{
public:
	static void Register(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void ClearState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
};

