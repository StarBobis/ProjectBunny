#include "DX12IaRegionHash.h"

#include "DX12ResourceTracker.h"

static UINT DX12IndexFormatStride(DXGI_FORMAT format)
{
	return format == DXGI_FORMAT_R32_UINT ? 4 : 2;
}

static UINT DX12RegionFirstVertex(const DX12IaRegionDraw &draw)
{
	if (!draw.indexed)
		return draw.firstVertex;
	return static_cast<UINT>(draw.baseVertex);
}

static UINT DX12RegionVertexCount(const DX12IaRegionDraw &draw)
{
	if (draw.vertexCount)
		return draw.vertexCount;
	if (!draw.indexCount)
		return 0;
	return (3 * draw.indexCount + 10) / 20 + 3;
}

static bool DX12ClampRegion(
	UINT64 gpuVa, UINT64 viewBytes, UINT64 offset, UINT64 bytes, DX12IaRegion *region)
{
	if (!region)
		return false;
	*region = DX12IaRegion();
	if (!gpuVa || !viewBytes || !bytes)
		return false;
	if (offset >= viewBytes)
		return false;
	if (bytes > viewBytes - offset)
		bytes = viewBytes - offset;
	if (!bytes)
		return false;
	region->gpuVa = gpuVa + offset;
	region->size = bytes;
	region->viewOffset = offset;
	return true;
}

bool DX12GetIndexBufferRegion(
	const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw, DX12IaRegion *region)
{
	const UINT stride = DX12IndexFormatStride(view.Format);
	return DX12ClampRegion(
		view.BufferLocation,
		view.SizeInBytes,
		static_cast<UINT64>(stride) * draw.firstIndex,
		static_cast<UINT64>(stride) * draw.indexCount,
		region);
}

bool DX12GetVertexBufferRegion(
	UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw,
	DX12IaRegion *region)
{
	(void)slot;
	if (!view.StrideInBytes)
		return false;

	return DX12ClampRegion(
		view.BufferLocation,
		view.SizeInBytes,
		static_cast<UINT64>(view.StrideInBytes) * DX12RegionFirstVertex(draw),
		static_cast<UINT64>(view.StrideInBytes) * DX12RegionVertexCount(draw),
		region);
}

uint32_t DX12HashIndexBufferRegion(
	const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
{
	DX12IaRegion region;
	if (!DX12GetIndexBufferRegion(view, draw, &region))
		return 0;
	region.hash = DX12HashIaBufferView(
		region.gpuVa, region.size, 0, static_cast<UINT>(view.Format), 0);
	return region.hash;
}

uint32_t DX12HashVertexBufferRegion(
	UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
{
	DX12IaRegion region;
	if (!DX12GetVertexBufferRegion(slot, view, draw, &region))
		return 0;
	region.hash = DX12HashIaBufferView(
		region.gpuVa, region.size, view.StrideInBytes, 0, slot);
	return region.hash;
}
