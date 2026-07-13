#pragma once

#include <d3d12.h>

#include "DX12BindingTrackerTypes.hpp"

class DX12ResourceStateTracker
{
public:
	// Microsoft documents ResourceBarrier as the explicit D3D12 transition
	// mechanism. These inference helpers are only used when no observed state
	// is available from creation or barrier tracking.
	static D3D12_RESOURCE_STATES FromBarrierLayout(D3D12_BARRIER_LAYOUT layout)
	{
		switch (layout) {
		case D3D12_BARRIER_LAYOUT_COMMON:
			return D3D12_RESOURCE_STATE_COMMON;
		case D3D12_BARRIER_LAYOUT_GENERIC_READ:
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
			return D3D12_RESOURCE_STATE_DEPTH_READ;
		case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_DEST:
			return D3D12_RESOURCE_STATE_COPY_DEST;
		case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
			return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
			return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	static D3D12_RESOURCE_STATES InferDescriptorSourceState(const DX12FrameResourceBinding &binding)
	{
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (binding.descriptor.kind == DescriptorKind::UAV)
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (binding.descriptor.kind == DescriptorKind::RTV)
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (binding.descriptor.kind == DescriptorKind::DSV)
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	static D3D12_RESOURCE_STATES InferIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
	{
		if (buffer.role == IaBufferRole::IB)
			return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	}

	static bool HeapTypeAllowsDirectCopy(UINT heapType, bool hasHeapType)
	{
		return hasHeapType &&
			(heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK);
	}
};
