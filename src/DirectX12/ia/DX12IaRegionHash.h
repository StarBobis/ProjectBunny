#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

struct DX12IaRegionDraw
{
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
};

struct DX12IaRegion
{
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewOffset = 0;
	uint32_t hash = 0;
};

uint32_t DX12HashIndexBufferRegion(
	const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw);
uint32_t DX12HashVertexBufferRegion(
	UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw);
bool DX12GetIndexBufferRegion(
	const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw, DX12IaRegion *region);
bool DX12GetVertexBufferRegion(
	UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw,
	DX12IaRegion *region);
