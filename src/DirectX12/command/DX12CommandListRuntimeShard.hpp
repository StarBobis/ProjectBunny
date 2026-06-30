#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>

#include "DX12CommandListRuntimeState.hpp"

class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState> states;
};
