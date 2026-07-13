#pragma once

#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>

#include "DX12ResourceTypes.hpp"

class DX12ResourceDumpSafetyUtils
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static UINT64 MaxSingleReadbackBytes()
	{
		return UINT64_MAX;
	}

	static UINT MaxTextureSubresourcesForDump()
	{
		return static_cast<UINT>(~0u);
	}

	static bool TextureSubresourceCountForDump(
		const D3D12_RESOURCE_DESC &desc, UINT *subresourceCount)
	{
		if (!subresourceCount)
			return false;
		*subresourceCount = 0;
		if (!IsTextureResourceDimension(desc.Dimension))
			return false;
		if (desc.Width == 0 || desc.Height == 0 || desc.DepthOrArraySize == 0)
			return false;
		if (desc.SampleDesc.Count == 0 || desc.SampleDesc.Count > 64)
			return false;
		const UINT mipLevels = desc.MipLevels ? desc.MipLevels : 1;
		const UINT64 arrayOrDepth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
			1 : desc.DepthOrArraySize;
		const UINT64 count = static_cast<UINT64>(mipLevels) * arrayOrDepth;
		if (count == 0 || count > static_cast<UINT64>(MaxTextureSubresourcesForDump()))
			return false;
		*subresourceCount = static_cast<UINT>(count);
		return true;
	}

	static bool ResourceDescLooksDumpable(const D3D12_RESOURCE_DESC &desc)
	{
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			return desc.Width > 0;
		if (IsTextureResourceDimension(desc.Dimension)) {
			UINT subresources = 0;
			return TextureSubresourceCountForDump(desc, &subresources);
		}
		return false;
	}

	static bool DescriptorResourceDescLooksDumpable(const DX12DescriptorSummary &descriptor)
	{
		return descriptor.resource &&
			descriptor.hasResourceDesc &&
			ResourceDescLooksDumpable(descriptor.resourceDesc);
	}

	static bool HeapTypeAllowsSafeDiscoveryCopy(UINT heapType, bool hasHeapType)
	{
		if (!hasHeapType)
			return false;
		return heapType == D3D12_HEAP_TYPE_UPLOAD ||
			heapType == D3D12_HEAP_TYPE_READBACK;
	}

	static bool ResourceStateAllowsLiveDiscoveryCopy(const DX12ResourceSummary &resource)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				resource.resourceHeapType, resource.hasResourceHeapType))
			return true;

		return resource.hasCurrentState;
	}

	static bool DescriptorStateAllowsLiveDiscoveryCopy(const DX12DescriptorSummary &descriptor)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				descriptor.resourceHeapType, descriptor.hasResourceHeapType))
			return true;

		return descriptor.hasCurrentState;
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12ResourceSummary &resource)
	{
		if (!resource.hasCurrentState)
			return "resource_state_unknown";
		if (!resource.stateTrackedByBarrier)
			return "resource_state_create_only";
		return "resource_discovery_copy_unsafe";
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12DescriptorSummary &descriptor)
	{
		if (!descriptor.hasCurrentState)
			return "descriptor_state_unknown";
		if (!descriptor.stateTrackedByBarrier)
			return "descriptor_state_create_only";
		return "descriptor_discovery_copy_unsafe";
	}

	static bool DiscoveryBudgetAllows(
		UINT queuedTasks, UINT64 queuedBytes, UINT64 nextBytes,
		UINT taskBudget, UINT64 byteBudget)
	{
		if (taskBudget > 0 && queuedTasks >= taskBudget)
			return false;
		if (byteBudget > 0 &&
			(queuedBytes > byteBudget || nextBytes > byteBudget - queuedBytes))
			return false;
		return true;
	}
};
