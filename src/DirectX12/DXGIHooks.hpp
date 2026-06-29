#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHooks
{
public:
	static void Install();
	static void ActivateHotPathHooks();
	static void Shutdown();
	static void HookSwapChain(IDXGISwapChain *swapChain);
	static void HookDeviceFromDXGIArgument(IUnknown *device);
	static void HookFactory(IUnknown *factory);
	static void HookDXGIFactoryVTables(HMODULE dxgi);
};
