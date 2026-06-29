#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <vector>

#include "DX12CommandListHooks.hpp"
#include "DX12HookManager.hpp"
#include "DX12ModRuntime.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12CommandQueueState.hpp"
#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12DeviceHookSignatures
{
public:
	using CreateGraphicsPipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreateComputePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	using CreatePipelineState = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	using DeviceFactoryCreateDevice = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12DeviceFactory*, IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
};

class DX12GraphicsPsoReplacement
{
public:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;
};

class DX12ComputePsoReplacement
{
public:
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	bool changed = false;
};

class DX12DeviceHookState
{
public:
	inline static DX12DeviceHookSignatures::CreateGraphicsPipelineState OrigCreateGraphicsPipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreateComputePipelineState OrigCreateComputePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::CreatePipelineState OrigCreatePipelineState = nullptr;
	inline static DX12DeviceHookSignatures::DeviceFactoryCreateDevice OrigDeviceFactoryCreateDevice = nullptr;
};

class DX12DeviceHookUtils
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12FallbackPath",
			"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
			"\"slot\":%u,\"fallback\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
	}

	template <typename T>
	static T GetOriginal(void *object, UINT slot, T fallback, const char *name)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *original = DX12HookRegistry::GetOriginalFunction(vtable[slot]);
				if (original)
					return reinterpret_cast<T>(original);
			}
		}
		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}
		DX12LogManager::JsonFunc(name ? name : "ID3D12Device::Unknown",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};

class DX12DeviceHooks
{
public:
	static HRESULT CreateGraphicsPipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12DeviceHookUtils::GetOriginal(
			device, 10, DX12DeviceHookState::OrigCreateGraphicsPipelineState,
			"ID3D12Device::CreateGraphicsPipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT CreateComputePipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12DeviceHookUtils::GetOriginal(
			device, 11, DX12DeviceHookState::OrigCreateComputePipelineState,
			"ID3D12Device::CreateComputePipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreateGraphicsPipelineState(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12DeviceHookUtils::LogHookCall("ID3D12Device::CreateGraphicsPipelineState", device);
		if (DX12InternalReplayState::IsActive())
			return CreateGraphicsPipelineStateOriginal(device, desc, riid, pipelineState);
		DX12GraphicsPsoReplacement replacement;
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc = desc;
		if (desc) {
			replacement.desc = *desc;
			if (DX12ModReplaceShaderBytecode("vs", desc->VS, &replacement.vs, &replacement.vsBytes)) {
				replacement.desc.VS = replacement.vs;
				replacement.changed = true;
			}
			if (DX12ModReplaceShaderBytecode("ps", desc->PS, &replacement.ps, &replacement.psBytes)) {
				replacement.desc.PS = replacement.ps;
				replacement.changed = true;
			}
			if (replacement.changed)
				activeDesc = &replacement.desc;
		}

		HRESULT hr = CreateGraphicsPipelineStateOriginal(device, activeDesc, riid, pipelineState);
		if (SUCCEEDED(hr) && activeDesc) {
			ID3D12PipelineState *created =
				pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
			if (desc)
				DX12ModRecordGraphicsPipelineState(device, created, desc);
			DX12RecordGraphicsPipelineState(created, activeDesc);
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreateComputePipelineState(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12DeviceHookUtils::LogHookCall("ID3D12Device::CreateComputePipelineState", device);
		if (DX12InternalReplayState::IsActive())
			return CreateComputePipelineStateOriginal(device, desc, riid, pipelineState);
		DX12ComputePsoReplacement replacement;
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc = desc;
		if (desc) {
			replacement.desc = *desc;
			if (DX12ModReplaceShaderBytecode("cs", desc->CS, &replacement.cs, &replacement.csBytes)) {
				replacement.desc.CS = replacement.cs;
				replacement.changed = true;
				activeDesc = &replacement.desc;
			}
		}

		HRESULT hr = CreateComputePipelineStateOriginal(device, activeDesc, riid, pipelineState);
		if (SUCCEEDED(hr) && activeDesc) {
			ID3D12PipelineState *created =
				pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
			if (desc)
				DX12ModRecordComputePipelineState(device, created, desc);
			DX12RecordComputePipelineState(created, activeDesc);
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreatePipelineState(
		ID3D12Device2 *device,
		const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12DeviceHookUtils::LogHookCall("ID3D12Device2::CreatePipelineState", device);
		auto original = DX12DeviceHookUtils::GetOriginal(
			device, 47, DX12DeviceHookState::OrigCreatePipelineState,
			"ID3D12Device2::CreatePipelineState");
		if (DX12InternalReplayState::IsActive())
			return original ? original(device, desc, riid, pipelineState) : E_FAIL;
		HRESULT hr = original ? original(device, desc, riid, pipelineState) : E_FAIL;
		if (SUCCEEDED(hr) && desc)
			DX12RecordPipelineStateStream(
				pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr, desc);
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE HookedDeviceFactoryCreateDevice(
		ID3D12DeviceFactory *factory,
		IUnknown *adapter,
		D3D_FEATURE_LEVEL featureLevel,
		REFIID riid,
		void **device)
	{
		DX12DeviceHookUtils::LogHookCall("ID3D12DeviceFactory::CreateDevice", factory);
		auto original = DX12DeviceHookUtils::GetOriginal(
			factory, 9, DX12DeviceHookState::OrigDeviceFactoryCreateDevice,
			"ID3D12DeviceFactory::CreateDevice");
		HRESULT hr = original ? original(factory, adapter, featureLevel, riid, device) : E_FAIL;
		DX12LogManager::JsonFunc("ID3D12DeviceFactory::CreateDevice",
			"\"adapter\":\"%p\",\"featureLevel\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
			adapter, featureLevel, hr, device ? *device : nullptr);
		if (SUCCEEDED(hr) && device && *device)
			HookDevice(static_cast<IUnknown*>(*device));
		return hr;
	}

	static void HookDevice(IUnknown *device)
	{
		if (!device)
			return;

		ID3D12Device *baseDevice = nullptr;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&baseDevice))))
			return;

		// D3D12 device vtable slots are stable COM ABI positions. Keep the named indices here
		// so hook installation changes stay local to this module.
		constexpr UINT CreateGraphicsPipelineStateIndex = 10;
		constexpr UINT CreateComputePipelineStateIndex = 11;
		DX12VTableHook deviceHooks[] = {
			{CreateGraphicsPipelineStateIndex, reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreateGraphicsPipelineState),
				reinterpret_cast<void*>(HookedCreateGraphicsPipelineState), "ID3D12Device::CreateGraphicsPipelineState"},
			{CreateComputePipelineStateIndex, reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreateComputePipelineState),
				reinterpret_cast<void*>(HookedCreateComputePipelineState), "ID3D12Device::CreateComputePipelineState"},
		};
		DX12HookManager::InstallVTableHooks(baseDevice, deviceHooks);
		DX12HookResourceMetadata(baseDevice);
		DX12CommandListHooks::HookCommandListCreation(baseDevice);
		DX12LogManager::JsonFunc("DeviceHooksReady", "\"device\":\"%p\"", baseDevice);

		baseDevice->Release();

		ID3D12Device2 *device2 = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2)))) {
			constexpr UINT CreatePipelineStateIndex = 47;
			DX12VTableHook device2Hooks[] = {
				{CreatePipelineStateIndex, reinterpret_cast<void**>(&DX12DeviceHookState::OrigCreatePipelineState),
					reinterpret_cast<void*>(HookedCreatePipelineState), "ID3D12Device2::CreatePipelineState"},
			};
			DX12HookManager::InstallVTableHooks(device2, device2Hooks);
			device2->Release();
		}
	}

	static void HookDeviceFactory(IUnknown *factory)
	{
		if (!factory)
			return;

		ID3D12DeviceFactory *deviceFactory = nullptr;
		if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&deviceFactory))))
			return;

		constexpr UINT CreateDeviceIndex = 9;
		DX12VTableHook factoryHooks[] = {
			{CreateDeviceIndex, reinterpret_cast<void**>(&DX12DeviceHookState::OrigDeviceFactoryCreateDevice),
				reinterpret_cast<void*>(HookedDeviceFactoryCreateDevice), "ID3D12DeviceFactory::CreateDevice"},
		};
		DX12HookManager::InstallVTableHooks(deviceFactory, factoryHooks);
		deviceFactory->Release();
	}

	static void HookDeviceFromCommandQueue(IUnknown *commandQueue)
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
};
