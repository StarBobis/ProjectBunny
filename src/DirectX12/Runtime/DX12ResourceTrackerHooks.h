// Resource tracker hook entry points.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12DeviceHooks.h"

class DX12ResourceTrackerHooks
{
public:
	static void Install(ID3D12Device *device);
};


inline HRESULT DX12DeviceHooks::CreateGraphicsPipelineStateOriginal(
	ID3D12Device *device,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
	REFIID riid,
	void **pipelineState)
{
	return DX12DeviceHookCallbacks::CreateGraphicsPipelineStateOriginal(device, desc, riid, pipelineState);
}

inline HRESULT DX12DeviceHooks::CreateComputePipelineStateOriginal(
	ID3D12Device *device,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
	REFIID riid,
	void **pipelineState)
{
	return DX12DeviceHookCallbacks::CreateComputePipelineStateOriginal(device, desc, riid, pipelineState);
}

inline void DX12DeviceHooks::HookDevice(IUnknown *device)
{
	if (!device)
		return;

	ID3D12Device *baseDevice = nullptr;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&baseDevice))))
		return;

	DX12_LOG("DeviceHooksReady", "\"device\":\"%p\",\"mode\":\"wrapper_only\"", baseDevice);
	baseDevice->Release();
}


