#pragma once

#include <d3d12.h>

class DX12ResourceTrackerHookRuntime
{
public:
	static bool IsInternalReplayActive();
	static void AttachResourceReleaseTracker(ID3D12Resource *resource);
	static const D3D12_UNORDERED_ACCESS_VIEW_DESC *PrepareUavDesc(
		ID3D12Resource *resource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC *PrepareResourceDesc(
		const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC1 *PrepareResourceDesc1(
		const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_DESC1 *storage,
		const char *source);
	static bool ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc);
	static void RecordCreatedDescriptorHeap(
		HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap);
	static void RecordCreatedRootSignature(
		HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature);
	static void RecordCreatedConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedShaderResourceView(
		ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedUnorderedAccessView(
		ID3D12Resource *resource,
		ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedRenderTargetView(
		ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedDepthStencilView(
		ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedSampler(
		const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCopiedDescriptors(
		ID3D12Device *device,
		UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes,
		UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCopiedDescriptorsSimple(
		ID3D12Device *device,
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCreatedResource(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResource1(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResourceWithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_BARRIER_LAYOUT initialLayout);
	static void RecordCreatedResource1WithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_BARRIER_LAYOUT initialLayout);
};
