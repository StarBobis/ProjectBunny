#pragma once

#include <d3d12.h>
#include <stdint.h>

#include "../types/DX12ResourceTypes.hpp"

class DX12ResourceDumpSafetyUtils
{
public:
	static UINT64 HuntingResourceCopyBudgetBytes()
	{
		return 128ull * 1024ull * 1024ull;
	}

	static UINT HuntingResourceCopyTaskBudget()
	{
		return 1024;
	}

	static UINT64 MetadataTextureCopyBudgetBytes()
	{
		return 192ull * 1024ull * 1024ull;
	}

	static UINT MetadataTextureCopyTaskBudget()
	{
		return 512;
	}

	static UINT64 MetadataSrvUavBufferCopyBudgetBytes()
	{
		return 128ull * 1024ull * 1024ull;
	}

	static UINT MetadataSrvUavBufferCopyTaskBudget()
	{
		return 1024;
	}

	static UINT64 MetadataCbvBufferCopyBudgetBytes()
	{
		return 64ull * 1024ull * 1024ull;
	}

	static UINT MetadataCbvBufferCopyTaskBudget()
	{
		return 512;
	}

	static bool EnableTrackedResourceInventoryDiscovery()
	{
		return false;
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

	static bool ResourceDiscoveryBudgetAllows(
		UINT queuedTasks, UINT64 queuedBytes, UINT64 nextBytes)
	{
		return DiscoveryBudgetAllows(
			queuedTasks, queuedBytes, nextBytes,
			HuntingResourceCopyTaskBudget(),
			HuntingResourceCopyBudgetBytes());
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
