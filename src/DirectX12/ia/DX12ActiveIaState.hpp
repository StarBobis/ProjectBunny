#pragma once

#include <Windows.h>
#include <d3d12.h>

class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	// 预计算 hash：bind-time 用 full buffer 范围计算一次，draw-time 直接读。
	// 对齐 DX11 的 push-based 模式——VB/IB 绑定时计算，不是 draw 时。
	uint32_t precomputedVertexHashes[32] = {};
	bool vertexHashValid[32] = {};
	uint32_t precomputedIndexHash = 0;
	bool indexHashValid = false;
};
