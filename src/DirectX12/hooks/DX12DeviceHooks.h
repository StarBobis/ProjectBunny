#pragma once

#include <Windows.h>
#include <d3d12.h>

void DX12HookDevice(IUnknown *device);
void DX12HookDeviceFactory(IUnknown *factory);
void DX12HookDeviceFromCommandQueue(IUnknown *commandQueue);
HRESULT DX12CreateGraphicsPipelineStateOriginal(
	ID3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState);
HRESULT DX12CreateComputePipelineStateOriginal(
	ID3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState);
