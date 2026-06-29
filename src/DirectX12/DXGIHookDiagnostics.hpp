#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "DX12LogManager.hpp"
#include "DX12OriginalFunctionRegistry.hpp"
#include "DX12RuntimeState.hpp"

class DXGIHookDiagnostics
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		DX12OriginalFunctionRegistry::LogHookCall(api, object);
	}

	static void LogPresentStage(
		const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK)
	{
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		DX12LogManager::DebugJsonFuncFlush("DX12PresentStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), swapChain, hr);
	}

	static void LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr)
	{
		if (SUCCEEDED(presentHr) || !swapChain)
			return;

		ID3D12Device *device = nullptr;
		HRESULT queryHr = swapChain->GetDevice(IID_PPV_ARGS(&device));
		if (FAILED(queryHr) || !device) {
			DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
				"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"queryHr\":\"0x%lx\"",
				api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, queryHr);
			return;
		}

		const HRESULT reason = device->GetDeviceRemovedReason();
		DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
			"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"reason\":\"0x%lx\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, reason);
		device->Release();
	}
};
