#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12ModRuntime.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12OriginalFunctionRegistry.hpp"
#include "hooks/DX12DeviceHookState.hpp"
#include "hooks/DX12DeviceHookTypes.hpp"
#include "hooks/DX12DeviceHooksForward.hpp"
#include "types/DX12DeviceHookSlotTypes.hpp"

class DX12DeviceHookCallbacks
{
public:
	static HRESULT CreateGraphicsPipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device::CreateGraphicsPipelineState,
			DX12DeviceHookState::OrigCreateGraphicsPipelineState,
			"ID3D12Device::CreateGraphicsPipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT CreateComputePipelineStateOriginal(
		ID3D12Device *device,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device::CreateComputePipelineState,
			DX12DeviceHookState::OrigCreateComputePipelineState,
			"ID3D12Device::CreateComputePipelineState");
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	}

	static HRESULT STDMETHODCALLTYPE HookedCreateGraphicsPipelineState(
		ID3D12Device *device,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
		REFIID riid,
		void **pipelineState)
	{
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device::CreateGraphicsPipelineState", device);
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
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device::CreateComputePipelineState", device);
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
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12Device2::CreatePipelineState", device);
		auto original = DX12OriginalFunctionRegistry::Resolve(
			device, DX12DeviceHookSlotTypes::ID3D12Device2::CreatePipelineState,
			DX12DeviceHookState::OrigCreatePipelineState,
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
		DX12OriginalFunctionRegistry::LogHookCall("ID3D12DeviceFactory::CreateDevice", factory);
		auto original = DX12OriginalFunctionRegistry::Resolve(
			factory, DX12DeviceHookSlotTypes::ID3D12DeviceFactory::CreateDevice,
			DX12DeviceHookState::OrigDeviceFactoryCreateDevice,
			"ID3D12DeviceFactory::CreateDevice");
		HRESULT hr = original ? original(factory, adapter, featureLevel, riid, device) : E_FAIL;
		DX12LogManager::JsonFunc("ID3D12DeviceFactory::CreateDevice",
			"\"adapter\":\"%p\",\"featureLevel\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
			adapter, featureLevel, hr, device ? *device : nullptr);
		if (SUCCEEDED(hr) && device && *device)
			DX12DeviceHooks::HookDevice(static_cast<IUnknown*>(*device));
		return hr;
	}
};
