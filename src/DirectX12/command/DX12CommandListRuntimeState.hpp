#pragma once

#include <d3d12.h>

#include "DX12ActiveIaState.hpp"
#include "DX12CommandListTrackingCache.hpp"
#include "DX12IaHashState.hpp"

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

	// IA hash state 增量缓存：同一 command list 连续绘制同一 mesh 时，
	// VB/IB view 不变且 draw args 不变 → 跳过 hash 重算。
	// iaStateVersion 在 VB/IB change 时递增。
	UINT64 iaStateVersion = 0;
	struct IaHashCacheEntry
	{
		bool indexed = false;
		UINT vertexCount = 0;
		UINT indexCount = 0;
		UINT firstVertex = 0;
		UINT firstIndex = 0;
		INT baseVertex = 0;
		UINT instanceCount = 0;
		DX12IaHashState hashState;
		bool valid = false;
	};
	IaHashCacheEntry iaHashCache;
};
