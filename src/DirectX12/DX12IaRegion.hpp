#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);

class DX12IaRegionDraw
{
public:
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;

	static DX12IaRegionDraw ForDrawInstanced(UINT vertexCount, UINT startVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.startVertexLocation = startVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw ForDrawIndexedInstanced(
		UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.startIndexLocation = startIndexLocation;
		draw.baseVertexLocation = baseVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw FromDrawArguments(
		bool indexed, UINT vertexCount, UINT indexCount, UINT startVertexLocation,
		UINT startIndexLocation, INT baseVertexLocation)
	{
		return indexed ?
			ForDrawIndexedInstanced(indexCount, startIndexLocation, baseVertexLocation) :
			ForDrawInstanced(vertexCount, startVertexLocation);
	}

	UINT EstimatedVertexCountForRegionHash() const
	{
		if (vertexCount)
			return vertexCount;
		if (!indexCount)
			return 0;
		return (3 * indexCount + 10) / 20 + 3;
	}

	UINT FirstVertexForRegionHash() const
	{
		if (!indexed)
			return startVertexLocation;
		// D3D12 BaseVertexLocation is signed; negative values move indexed reads
		// before index 0, so hashing must not reinterpret them as huge UINT offsets.
		if (baseVertexLocation <= 0)
			return 0;
		return static_cast<UINT>(baseVertexLocation);
	}
};

class DX12IaRegion
{
public:
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewOffset = 0;
	uint32_t hash = 0;

	bool SetFromViewRange(UINT64 viewGpuVa, UINT64 viewBytes, UINT64 offset, UINT64 bytes)
	{
		*this = DX12IaRegion();
		if (!viewGpuVa || !viewBytes || !bytes)
			return false;
		if (offset >= viewBytes)
			return false;
		if (bytes > viewBytes - offset)
			bytes = viewBytes - offset;
		if (!bytes)
			return false;
		gpuVa = viewGpuVa + offset;
		size = bytes;
		viewOffset = offset;
		return true;
	}

	bool SetFromIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		const UINT stride = IndexFormatStride(view.Format);
		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(stride) * draw.startIndexLocation,
			static_cast<UINT64>(stride) * draw.indexCount);
	}

	bool SetFromVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		(void)slot;
		if (!view.StrideInBytes)
			return false;

		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(view.StrideInBytes) * draw.FirstVertexForRegionHash(),
			static_cast<UINT64>(view.StrideInBytes) * draw.EstimatedVertexCountForRegionHash());
	}

	uint32_t HashIndexBuffer(UINT format)
	{
		hash = DX12HashIaBufferView(gpuVa, size, 0, format, 0);
		return hash;
	}

	uint32_t HashVertexBuffer(UINT stride, UINT slot)
	{
		hash = DX12HashIaBufferView(gpuVa, size, stride, 0, slot);
		return hash;
	}

	static uint32_t HashIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromIndexBufferView(view, draw))
			return 0;
		return region.HashIndexBuffer(static_cast<UINT>(view.Format));
	}

	static uint32_t HashVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromVertexBufferView(slot, view, draw))
			return 0;
		return region.HashVertexBuffer(view.StrideInBytes, slot);
	}

	static UINT IndexFormatStride(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_R32_UINT ? 4 : 2;
	}
};
