#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12IaReplacementExecutorCallbacks.hpp"

class DX12CommandListHookCallbacks
{
public:
	static DX12IaReplacementExecutorCallbacks MakeIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static const DX12IaReplacementExecutorCallbacks &GetCachedIaReplacementCallbacks(
		ID3D12GraphicsCommandList *commandList);
	static void RegisterCommandListBase(
		ID3D12GraphicsCommandList *baseList, ID3D12PipelineState *initialState);
	static void RegisterCommandList(IUnknown *commandList, ID3D12PipelineState *initialState);
	static void RegisterSubmittedCommandList(IUnknown *commandList);
	static void RegisterCommandQueue(IUnknown *commandQueue);

	static HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
		ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **commandQueue);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandAllocator(
		ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **allocator);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList(
		ID3D12Device *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState,
		REFIID riid, void **commandList);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommandList1(
		ID3D12Device4 *device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
		D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **commandList);
	static void STDMETHODCALLTYPE HookedQueueUpdateTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *resource, UINT numResourceRegions,
		const D3D12_TILED_RESOURCE_COORDINATE *resourceRegionStartCoordinates,
		const D3D12_TILE_REGION_SIZE *resourceRegionSizes, ID3D12Heap *heap, UINT numRanges,
		const D3D12_TILE_RANGE_FLAGS *rangeFlags, const UINT *heapRangeStartOffsets,
		const UINT *rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueCopyTileMappings(
		ID3D12CommandQueue *queue, ID3D12Resource *dstResource,
		const D3D12_TILED_RESOURCE_COORDINATE *dstRegionStartCoordinate,
		ID3D12Resource *srcResource, const D3D12_TILED_RESOURCE_COORDINATE *srcRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *regionSize, D3D12_TILE_MAPPING_FLAGS flags);
	static void STDMETHODCALLTYPE HookedQueueExecuteCommandLists(
		ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
	static void STDMETHODCALLTYPE HookedQueueSetMarker(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueBeginEvent(
		ID3D12CommandQueue *queue, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedQueueEndEvent(ID3D12CommandQueue *queue);
	static HRESULT STDMETHODCALLTYPE HookedQueueSignal(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueWait(
		ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetTimestampFrequency(
		ID3D12CommandQueue *queue, UINT64 *frequency);
	static HRESULT STDMETHODCALLTYPE HookedQueueGetClockCalibration(
		ID3D12CommandQueue *queue, UINT64 *gpuTimestamp, UINT64 *cpuTimestamp);
	static HRESULT STDMETHODCALLTYPE HookedCloseCommandList(ID3D12GraphicsCommandList *commandList);
	static HRESULT STDMETHODCALLTYPE HookedResetCommandList(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState);
	static void STDMETHODCALLTYPE HookedClearState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedDrawInstanced(
		ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount,
		UINT startVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount,
		UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void STDMETHODCALLTYPE HookedDispatch(
		ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
		UINT threadGroupCountY, UINT threadGroupCountZ);
	static void STDMETHODCALLTYPE HookedCopyBufferRegion(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstBuffer, UINT64 dstOffset,
		ID3D12Resource *srcBuffer, UINT64 srcOffset, UINT64 numBytes);
	static void STDMETHODCALLTYPE HookedCopyTextureRegion(
		ID3D12GraphicsCommandList *commandList, const D3D12_TEXTURE_COPY_LOCATION *dst,
		UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION *src,
		const D3D12_BOX *srcBox);
	static void STDMETHODCALLTYPE HookedCopyResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, ID3D12Resource *srcResource);
	static void STDMETHODCALLTYPE HookedCopyTiles(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *tiledResource,
		const D3D12_TILED_RESOURCE_COORDINATE *tileRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE *tileRegionSize, ID3D12Resource *buffer,
		UINT64 bufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS flags);
	static void STDMETHODCALLTYPE HookedResolveSubresource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *dstResource, UINT dstSubresource,
		ID3D12Resource *srcResource, UINT srcSubresource, DXGI_FORMAT format);
	static void STDMETHODCALLTYPE HookedIASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void STDMETHODCALLTYPE HookedRSSetViewports(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_VIEWPORT *viewports);
	static void STDMETHODCALLTYPE HookedRSSetScissorRects(
		ID3D12GraphicsCommandList *commandList, UINT count, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedOMSetBlendFactor(
		ID3D12GraphicsCommandList *commandList, const FLOAT blendFactor[4]);
	static void STDMETHODCALLTYPE HookedOMSetStencilRef(
		ID3D12GraphicsCommandList *commandList, UINT stencilRef);
	static void STDMETHODCALLTYPE HookedSetPipelineState(
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void STDMETHODCALLTYPE HookedResourceBarrier(
		ID3D12GraphicsCommandList *commandList, UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers);
	static void STDMETHODCALLTYPE HookedExecuteBundle(
		ID3D12GraphicsCommandList *commandList, ID3D12GraphicsCommandList *bundle);
	static void STDMETHODCALLTYPE HookedSetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS address);
	static void STDMETHODCALLTYPE HookedIASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void STDMETHODCALLTYPE HookedIASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedSOSetTargets(
		ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
		const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views);
	static void STDMETHODCALLTYPE HookedOMSetRenderTargets(
		ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
		const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
		BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
	static void STDMETHODCALLTYPE HookedClearDepthStencilView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
		D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil, UINT numRects,
		const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearRenderTargetView(
		ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
		const FLOAT colorRGBA[4], UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewUint(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const UINT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedClearUnorderedAccessViewFloat(
		ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE viewGpuHandleInCurrentHeap,
		D3D12_CPU_DESCRIPTOR_HANDLE viewCpuHandle, ID3D12Resource *resource, const FLOAT values[4],
		UINT numRects, const D3D12_RECT *rects);
	static void STDMETHODCALLTYPE HookedDiscardResource(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
		const D3D12_DISCARD_REGION *region);
	static void STDMETHODCALLTYPE HookedBeginQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedEndQuery(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap,
		D3D12_QUERY_TYPE type, UINT index);
	static void STDMETHODCALLTYPE HookedResolveQueryData(
		ID3D12GraphicsCommandList *commandList, ID3D12QueryHeap *queryHeap, D3D12_QUERY_TYPE type,
		UINT startIndex, UINT numQueries, ID3D12Resource *destinationBuffer,
		UINT64 alignedDestinationBufferOffset);
	static void STDMETHODCALLTYPE HookedSetPredication(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *buffer,
		UINT64 alignedBufferOffset, D3D12_PREDICATION_OP operation);
	static void STDMETHODCALLTYPE HookedCommandListSetMarker(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListBeginEvent(
		ID3D12GraphicsCommandList *commandList, UINT metadata, const void *data, UINT size);
	static void STDMETHODCALLTYPE HookedCommandListEndEvent(ID3D12GraphicsCommandList *commandList);
	static void STDMETHODCALLTYPE HookedExecuteIndirect(
		ID3D12GraphicsCommandList *commandList, ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount, ID3D12Resource *argumentBuffer, UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer, UINT64 countBufferOffset);
};
