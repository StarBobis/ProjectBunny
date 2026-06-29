#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12CommandListHooks.hpp"
#include "DX12CommandQueueState.hpp"
#include "DX12HookManager.hpp"
#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerHooks.hpp"
#include "hooks/DX12DeviceHookCallbacks.hpp"
#include "hooks/DX12DeviceHookState.hpp"
#include "hooks/DX12DeviceHooksForward.hpp"
#include "types/DX12DeviceHookSlotTypes.hpp"

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

	DX12VTableHook deviceHooks[] = {
		{DX12DeviceHookSlotTypes::ID3D12Device::CreateGraphicsPipelineState,
			reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreateGraphicsPipelineState),
			reinterpret_cast<void*>(DX12DeviceHookCallbacks::HookedCreateGraphicsPipelineState), "ID3D12Device::CreateGraphicsPipelineState"},
		{DX12DeviceHookSlotTypes::ID3D12Device::CreateComputePipelineState,
			reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreateComputePipelineState),
			reinterpret_cast<void*>(DX12DeviceHookCallbacks::HookedCreateComputePipelineState), "ID3D12Device::CreateComputePipelineState"},
	};
	DX12HookManager::InstallVTableHooks(baseDevice, deviceHooks);
	DX12ResourceTrackerHooks::Install(baseDevice);
	DX12CommandListHooks::HookCommandListCreation(baseDevice);
	DX12LogManager::JsonFunc("DeviceHooksReady", "\"device\":\"%p\"", baseDevice);

	baseDevice->Release();

	ID3D12Device2 *device2 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2)))) {
		DX12VTableHook device2Hooks[] = {
			{DX12DeviceHookSlotTypes::ID3D12Device2::CreatePipelineState,
				reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreatePipelineState),
				reinterpret_cast<void*>(DX12DeviceHookCallbacks::HookedCreatePipelineState), "ID3D12Device2::CreatePipelineState"},
		};
		DX12HookManager::InstallVTableHooks(device2, device2Hooks);
		device2->Release();
	}
}

inline void DX12DeviceHooks::HookDeviceFactory(IUnknown *factory)
{
	if (!factory)
		return;

	ID3D12DeviceFactory *deviceFactory = nullptr;
	if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&deviceFactory))))
		return;

	DX12VTableHook factoryHooks[] = {
		{DX12DeviceHookSlotTypes::ID3D12DeviceFactory::CreateDevice,
			reinterpret_cast<void**>(&DX12DeviceHookState::OrigDeviceFactoryCreateDevice),
			reinterpret_cast<void*>(DX12DeviceHookCallbacks::HookedDeviceFactoryCreateDevice), "ID3D12DeviceFactory::CreateDevice"},
	};
	DX12HookManager::InstallVTableHooks(deviceFactory, factoryHooks);
	deviceFactory->Release();
}

inline void DX12DeviceHooks::HookDeviceFromCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	DX12CommandQueueState::Set(queue);

	ID3D12Device *device = nullptr;
	HRESULT hr = queue->GetDevice(IID_PPV_ARGS(&device));
	DX12LogManager::JsonFunc("ID3D12CommandQueue::GetDevice",
		"\"hr\":\"0x%lx\",\"device\":\"%p\"", hr, device);
	DX12CommandListHooks::HookCommandQueue(queue);
	if (SUCCEEDED(hr) && device) {
		HookDevice(device);
		device->Release();
	}
	queue->Release();
}
