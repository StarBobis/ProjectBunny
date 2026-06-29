#pragma once

#include <d3d12.h>

#include "DX12IaReplacementExecutorCallbacks.hpp"

class DX12ThreadLocalIaCallbacksCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};
