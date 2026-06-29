#pragma once

#include <Windows.h>
#include <d3d12.h>

class DX12DeviceHooks
{
public:
	static HRESULT CreateGraphicsPipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState);
	static HRESULT CreateComputePipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState);
	static void HookDevice(IUnknown *device);
	static void HookDeviceFactory(IUnknown *factory);
	static void HookDeviceFromCommandQueue(IUnknown *commandQueue);
};
