#pragma once

#include <d3d12.h>

class DX12ResourceTrackerHooks
{
public:
	static void Install(ID3D12Device *device);
};
