#include "DXGIHooks.hpp"

#include <dxgi1_4.h>

#include "DX12CommandListHooks.hpp"
#include "DX12DeviceHooks.hpp"
#include "DX12HookManager.hpp"
#include "DX12LogManager.hpp"
#include "DX12NativeOverlay.hpp"
#include "DX12RuntimeState.hpp"
#include "DXGIHookCallbacks.hpp"
#include "DXGIHookDiagnostics.hpp"
#include "DXGIHookRegistry.hpp"
#include "DXGIHookRuntime.hpp"
#include "DXGIHookSignatures.hpp"
#include "DXGIHookState.hpp"
#include "types/DXGIHookSlotTypes.hpp"

void DXGIHooks::HookSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return;
	swapChain->AddRef();
	IDXGISwapChain *oldSwapChain = nullptr;
	AcquireSRWLockExclusive(&DXGIHookState::KnownSwapChainLock);
	oldSwapChain = DXGIHookState::KnownSwapChain;
	DXGIHookState::KnownSwapChain = swapChain;
	ReleaseSRWLockExclusive(&DXGIHookState::KnownSwapChainLock);
	if (oldSwapChain)
		oldSwapChain->Release();

	DX12VTableHook swapChainHooks[] = {
		{DXGIHookSlotTypes::IDXGISwapChain::Present, reinterpret_cast<void**>(&DXGIHookState::OrigPresent), DXGIHookCallbacks::HookedPresent, "IDXGISwapChain::Present"},
		{DXGIHookSlotTypes::IDXGISwapChain::ResizeBuffers, reinterpret_cast<void**>(&DXGIHookState::OrigResizeBuffers), DXGIHookCallbacks::HookedResizeBuffers, "IDXGISwapChain::ResizeBuffers"},
	};
	DX12HookManager::InstallVTableHooks(swapChain, swapChainHooks);

	IDXGISwapChain1 *swapChain1 = nullptr;
	if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
		DX12VTableHook swapChain1Hooks[] = {
			{DXGIHookSlotTypes::IDXGISwapChain1::Present1, reinterpret_cast<void**>(&DXGIHookState::OrigPresent1), DXGIHookCallbacks::HookedPresent1, "IDXGISwapChain1::Present1"},
		};
		DX12HookManager::InstallVTableHooks(swapChain1, swapChain1Hooks);
		swapChain1->Release();
	}
}

void DXGIHooks::ActivateHotPathHooks()
{
	IDXGISwapChain *swapChain = nullptr;
	AcquireSRWLockShared(&DXGIHookState::KnownSwapChainLock);
	swapChain = DXGIHookState::KnownSwapChain;
	if (swapChain)
		swapChain->AddRef();
	ReleaseSRWLockShared(&DXGIHookState::KnownSwapChainLock);
	if (swapChain) {
		DXGIHooks::HookSwapChain(swapChain);
		swapChain->Release();
	}
	DX12CommandListHooks::ActivateCommandQueueHotPathHooks();
}

void DXGIHooks::HookDeviceFromDXGIArgument(IUnknown *device)
{
	DX12DeviceHooks::HookDevice(device);
	DX12DeviceHooks::HookDeviceFromCommandQueue(device);
}

void DXGIHooks::HookFactory(IUnknown *factory)
{
	if (!factory)
		return;

	IDXGIFactory *factory0 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory0)))) {
		DX12VTableHook factoryHooks[] = {
			{DXGIHookSlotTypes::IDXGIFactory::CreateSwapChain, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChain), DXGIHookCallbacks::HookedCreateSwapChain,
				"IDXGIFactory::CreateSwapChain"},
		};
		DX12HookManager::InstallVTableHooks(factory0, factoryHooks);
		factory0->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
		DX12VTableHook factory2Hooks[] = {
			{DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForHwnd, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForHwnd),
				DXGIHookCallbacks::HookedCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd"},
			{DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForCoreWindow, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForCoreWindow),
				DXGIHookCallbacks::HookedCreateSwapChainForCoreWindow, "IDXGIFactory2::CreateSwapChainForCoreWindow"},
			{DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForComposition, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForComposition),
				DXGIHookCallbacks::HookedCreateSwapChainForComposition, "IDXGIFactory2::CreateSwapChainForComposition"},
		};
		DX12HookManager::InstallVTableHooks(factory2, factory2Hooks);
		factory2->Release();
	}
}

void DXGIHooks::HookDXGIFactoryVTables(HMODULE dxgi)
{
	auto createFactory = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory>(
		GetProcAddress(dxgi, "CreateDXGIFactory"));
	auto createFactory1 = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory>(
		GetProcAddress(dxgi, "CreateDXGIFactory1"));
	auto createFactory2 = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory2>(
		GetProcAddress(dxgi, "CreateDXGIFactory2"));

	IDXGIFactory *factory = nullptr;
	if (createFactory1 && SUCCEEDED(createFactory1(IID_PPV_ARGS(&factory)))) {
		DXGIHooks::HookFactory(factory);
		factory->Release();
		return;
	}

	if (createFactory && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory)))) {
		DXGIHooks::HookFactory(factory);
		factory->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (createFactory2 && SUCCEEDED(createFactory2(0, IID_PPV_ARGS(&factory2)))) {
		DXGIHooks::HookFactory(factory2);
		factory2->Release();
	}
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory(REFIID riid, void **factory)
{
	DXGIHookDiagnostics::LogHookCall("CreateDXGIFactory", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory(riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHooks::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory1(REFIID riid, void **factory)
{
	DXGIHookDiagnostics::LogHookCall("CreateDXGIFactory1", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory1(riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory1",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHooks::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
	DXGIHookDiagnostics::LogHookCall("CreateDXGIFactory2", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory2(flags, riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory2",
		"\"flags\":\"0x%x\",\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		flags, &riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHooks::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChain(
	IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain)
{
	DXGIHookDiagnostics::LogHookCall("IDXGIFactory::CreateSwapChain", factory);
	DXGIHooks::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookRegistry::GetCreateSwapChainOriginal(factory);
	HRESULT hr = original ? original(factory, device, desc, swapChain) : DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory::CreateSwapChain",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && (!desc || !DX12NativeOverlayIsOverlayWindow(desc->OutputWindow)))
		DXGIHooks::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForHwnd(
	IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookDiagnostics::LogHookCall("IDXGIFactory2::CreateSwapChainForHwnd", factory);
	DXGIHooks::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookRegistry::GetCreateSwapChainForHwndOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, fullscreenDesc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForHwnd",
		"\"device\":\"%p\",\"hwnd\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, window, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && !DX12NativeOverlayIsOverlayWindow(window))
		DXGIHooks::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForCoreWindow(
	IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookDiagnostics::LogHookCall("IDXGIFactory2::CreateSwapChainForCoreWindow", factory);
	DXGIHooks::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookRegistry::GetCreateSwapChainForCoreWindowOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForCoreWindow",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		DXGIHooks::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForComposition(
	IDXGIFactory2 *factory, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookDiagnostics::LogHookCall("IDXGIFactory2::CreateSwapChainForComposition", factory);
	DXGIHooks::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookRegistry::GetCreateSwapChainForCompositionOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForComposition",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		DXGIHooks::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
{
	return DXGIHookRuntime::Present(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedPresent1(
	IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters)
{
	return DXGIHookRuntime::Present1(swapChain, syncInterval, flags, presentParameters);
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedResizeBuffers(
	IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
	DXGI_FORMAT newFormat, UINT swapChainFlags)
{
	DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::ResizeBuffers", swapChain);
	if (!DX12InternalReplayState::IsActive())
		DX12NativeOverlayInvalidate(swapChain);
	auto original = DXGIHookRegistry::GetResizeBuffersOriginal(swapChain);
	HRESULT hr = original ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGISwapChain::ResizeBuffers",
		"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
		swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
	return hr;
}

void DXGIHooks::Install()
{
	AcquireSRWLockExclusive(&DXGIHookState::HookLock);
	if (DXGIHookState::HookAttempted) {
		ReleaseSRWLockExclusive(&DXGIHookState::HookLock);
		return;
	}
	DXGIHookState::HookAttempted = true;
	ReleaseSRWLockExclusive(&DXGIHookState::HookLock);

	HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
	if (!dxgi) {
		static volatile LONG deferredLogs = 0;
		if (InterlockedIncrement(&deferredLogs) <= 4)
			DX12LogManager::Text("dxgi.dll is not loaded yet; DXGI hook deferred\n");
		AcquireSRWLockExclusive(&DXGIHookState::HookLock);
		DXGIHookState::HookAttempted = false;
		ReleaseSRWLockExclusive(&DXGIHookState::HookLock);
		return;
	}

	DXGIHooks::HookDXGIFactoryVTables(dxgi);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory), DXGIHookCallbacks::HookedCreateDXGIFactory);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory1",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory1), DXGIHookCallbacks::HookedCreateDXGIFactory1);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory2",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory2), DXGIHookCallbacks::HookedCreateDXGIFactory2);
}

void DXGIHooks::Shutdown()
{
	DXGIHookState::ReleaseKnownSwapChain();
}




