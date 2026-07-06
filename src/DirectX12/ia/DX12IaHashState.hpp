#pragma once

#include <d3d12.h>

#include <vector>

#include "DX12IaBufferHash.hpp"

class DX12IaHashState
{
public:
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
	// Compute dispatch bindings: UAV/SRV/CBV buffer hashes for PreSkin matching.
	std::vector<DX12IaBufferHash> computeBuffers;
};
