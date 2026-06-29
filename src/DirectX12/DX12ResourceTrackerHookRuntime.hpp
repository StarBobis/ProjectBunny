#pragma once

#include <d3d12.h>

class DX12ResourceTrackerHookRuntime
{
public:
	static void AttachResourceReleaseTracker(ID3D12Resource *resource);
	static bool ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc);
};
