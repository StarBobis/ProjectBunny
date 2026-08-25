#pragma once

// Pre-skin output-resize binding state.
// Split out of Core/DX12ModState.h; include that umbrella header from existing code.
#include "../DX12CommandFlow.h"

class DX12PreSkinOutputResizeBinding
{
public:
	DX12PreSkinOutputResizeBinding() = default;
	DX12PreSkinOutputResizeBinding(const DX12PreSkinOutputResizeBinding &) = delete;
	DX12PreSkinOutputResizeBinding &operator=(const DX12PreSkinOutputResizeBinding &) = delete;
	~DX12PreSkinOutputResizeBinding()
	{
		Clear();
	}

	void Clear()
	{
		if (retainedResource && resizedResource)
			resizedResource->Release();
		resizedResource = nullptr;
		retainedResource = false;
		originalGpuVa = 0;
		originalByteWidth = 0;
		resizedGpuVa = 0;
		resizedByteWidth = 0;
		sourceOffset = 0;
		stride = 0;
		uavBarrierPending = false;
		transitionToVertex = false;
		stateBefore = D3D12_RESOURCE_STATE_COMMON;
	}

	ID3D12Resource *resizedResource = nullptr;
	bool retainedResource = false;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedGpuVa = 0;
	UINT64 resizedByteWidth = 0;
	UINT64 sourceOffset = 0;
	UINT stride = 0;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
};
