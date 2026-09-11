// Resource tracker hook registry, runtime, signatures and state.
// Split from DX12Resources.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12PsoDecisionCache.h"

class DX12ResourceTrackerHookRegistry
{
public:
	template <typename T>
	static T GetDeviceOriginal(void *device, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::Resolve(device, slot, fallback, name);
	}
};


class DX12ResourceTrackerHookRuntime
{
public:
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
	static void RecordCreatedShaderResourceView(
		ID3D12Device *device,
		ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedUnorderedAccessView(
		ID3D12Device *device,
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
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType,
		UINT descriptorIncrement);
	// Resolves the increment from the per-type cache populated at heap
	// creation; only falls back to a COM query on a cold cache. Preferred on
	// hot paths (CopyDescriptorsSimple runs thousands of times per frame).
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
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static PlacedResourceInfo MakePlacedResourceInfo(ID3D12Heap *heap, UINT64 heapOffset);
	static void RecordCreatedResource1(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResourceWithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);
	static void RecordCreatedResource1WithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);

	static void InstallMapUnmapHooks(ID3D12Resource *resource);

	// Wrapper-mode Map/Unmap interception. ResourceWrapper calls these so
	// buffer content capture keeps working without patching the shared
	// ID3D12Resource code (RenderDoc-style, DLSS FG compatible).
	static HRESULT WrappedResourceMap(
		ID3D12Resource *resource, UINT subresource, const D3D12_RANGE *readRange, void **data);
	static void WrappedResourceUnmap(
		ID3D12Resource *resource, UINT subresource,
		const D3D12_RANGE *writtenRange, const void *caller);
};


class DX12ResourceTrackerHookSignatures
{
public:
	using CopyDescriptorsSimple = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
		D3D12_DESCRIPTOR_HEAP_TYPE);
};


class DX12ResourceTrackerHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CopyDescriptorsSimple = 24;
	};
};


