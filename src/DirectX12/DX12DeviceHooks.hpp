#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12CommandListHooks.hpp"
#include "DX12CommandQueueState.hpp"
#include "DX12HookManager.hpp"
#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHooks.hpp"
#include "DX12DeviceHookCallbacks.hpp"
#include "DX12DeviceHookState.hpp"
#include "DX12DeviceHooksForward.hpp"
#include "DX12DeviceHookSlotTypes.hpp"

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

	DX12LogManager::JsonFunc("DeviceHooksReady", "\"device\":\"%p\",\"mode\":\"wrapper_only\"", baseDevice);
	baseDevice->Release();
}

inline void DX12DeviceHooks::HookDeviceFactory(IUnknown *factory)
{
	(void)factory;
}

inline void DX12DeviceHooks::HookDeviceFromCommandQueue(IUnknown *commandQueue)
{
	(void)commandQueue;
}
