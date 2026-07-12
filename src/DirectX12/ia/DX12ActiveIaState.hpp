#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <cstdint>

#include "types/DX12ResourceTypes.hpp"

class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferResolved = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	bool vertexBufferResolved[32] = {};
	DX12BufferResourceSummary vertexBufferResources[32] = {};
	bool preSkinRedirectNegativeValid[32] = {};
	UINT64 preSkinRedirectNegativeGeneration[32] = {};
	D3D12_VERTEX_BUFFER_VIEW preSkinRedirectNegativeSource[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};
