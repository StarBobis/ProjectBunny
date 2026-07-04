#pragma once

#include <d3d12.h>

#include "DX12ActiveIaState.hpp"
#include "DX12CommandListTrackingCache.hpp"

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;

	// PSO 级缓存：同一 PSO 在本帧是否有匹配的 shader override 配置。
	// generation 与 HotPathState 同步，避免每 draw 都获取 gModLock 查询。
	volatile LONG psoHasOverridesGeneration = -1;
	bool psoHasShaderOverrides = true;
};
