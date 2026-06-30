#pragma once

#include <Windows.h>
#include <d3d12.h>

class DX12CommandListHookSignatures
{
public:
	using CreateCommandQueue = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
	using CreateCommandAllocator = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, D3D12_COMMAND_LIST_TYPE, REFIID, void**);
	using CreateCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
	using CreateCommandList1 = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device4*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);

	using QueueUpdateTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS);
	using QueueCopyTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS);
	using QueueExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, ID3D12CommandList *const*);
	using QueueSetMarker = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueEndEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*);
	using QueueSignal = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueWait = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueGetTimestampFrequency = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*);
	using QueueGetClockCalibration = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*, UINT64*);

	using CloseCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ResetCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	using ClearState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using DrawInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using CopyBufferRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
	using CopyTextureRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
	using CopyResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
	using CopyTiles = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
	using ResolveSubresource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
	using IaSetPrimitiveTopology = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	using RsSetViewports = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	using RsSetScissorRects = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	using OmSetBlendFactor = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const FLOAT[4]);
	using OmSetStencilRef = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT);
	using SetPipelineState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using ResourceBarrier = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
	using ExecuteBundle = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
	using SetDescriptorHeaps = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap *const*);
	using SetRootSignature = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	using SetRootDescriptorTable = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	using SetRoot32BitConstant = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetRoot32BitConstants = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	using SetRootGpuVa = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	using IaSetIndexBuffer = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using IaSetVertexBuffers = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	using SoSetTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW*);
	using OmSetRenderTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	using ClearDepthStencilView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
	using ClearRenderTargetView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*);
	using ClearUavUint = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT[4], UINT, const D3D12_RECT*);
	using ClearUavFloat = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT[4], UINT, const D3D12_RECT*);
	using DiscardResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
	using BeginQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using EndQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using ResolveQueryData = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT, UINT, ID3D12Resource*, UINT64);
	using SetPredication = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, D3D12_PREDICATION_OP);
	using CommandListSetMarker = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListEndEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ExecuteIndirect = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
};
