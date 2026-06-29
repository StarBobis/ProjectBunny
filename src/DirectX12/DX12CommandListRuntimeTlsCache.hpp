#pragma once

#include <d3d12.h>

#include "DX12CommandListRuntimeShard.hpp"
#include "DX12CommandListRuntimeState.hpp"
#include "DX12CommandListTrackingCache.hpp"

class DX12CommandListRuntimeTlsCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12CommandListRuntimeState *statePtr = nullptr;
};
