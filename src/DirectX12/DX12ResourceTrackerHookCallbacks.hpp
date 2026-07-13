#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

class DX12ResourceTrackerHookCallbacks
{
public:
	static HRESULT STDMETHODCALLTYPE HookedCreateDescriptorHeap(
		ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap);
	static HRESULT STDMETHODCALLTYPE HookedCreateRootSignature(
		ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
		REFIID riid, void **rootSignature);
	static void STDMETHODCALLTYPE HookedCreateConstantBufferView(
		ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateShaderResourceView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateUnorderedAccessView(
		ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateRenderTargetView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateDepthStencilView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateSampler(
		ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCopyDescriptors(
		ID3D12Device *device, UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
		ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
		ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
		ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource(
		ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateFence(
		ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
		REFIID riid, void **fence);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource1(
		ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource1(
		ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource2(
		ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource1(
		ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource3(
		ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource2(
		ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
		const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource2(
		ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
};
