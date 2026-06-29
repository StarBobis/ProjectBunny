#pragma once

#include <d3d12.h>
#include <stdint.h>

#include "../types/DX12ResourceTypes.hpp"

class DX12ResourceDumpSafetyUtils
{
public:
	static UINT64 HuntingResourceCopyBudgetBytes()
	{
		return 384ull * 1024ull * 1024ull;
	}

	static UINT HuntingResourceCopyTaskBudget()
	{
		return 1024;
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

		return resource.hasCurrentState && resource.stateTrackedByBarrier;
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12ResourceSummary &resource)
	{
		if (!resource.hasCurrentState)
			return "resource_state_unknown";
		if (!resource.stateTrackedByBarrier)
			return "resource_state_create_only";
		return "resource_discovery_copy_unsafe";
	}

	static bool ResourceDiscoveryBudgetAllows(
		UINT queuedTasks, UINT64 queuedBytes, UINT64 nextBytes)
	{
		if (queuedTasks >= HuntingResourceCopyTaskBudget())
			return false;
		if (queuedBytes > HuntingResourceCopyBudgetBytes())
			return false;
		return nextBytes <= HuntingResourceCopyBudgetBytes() - queuedBytes;
	}
};
