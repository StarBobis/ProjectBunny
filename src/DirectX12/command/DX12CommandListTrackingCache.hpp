#pragma once

#include <Windows.h>

#include "DX12CommandListTrackingState.hpp"

class DX12CommandListTrackingCache : public DX12CommandListTrackingState
{
public:
	UINT64 generation = 0;
};
