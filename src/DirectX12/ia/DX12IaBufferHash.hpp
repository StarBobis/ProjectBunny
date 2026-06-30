#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};
