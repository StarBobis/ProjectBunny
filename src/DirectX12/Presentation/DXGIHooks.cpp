// DXGIHooks: CreateDXGIFactory* export hooks and bootstrap.
// Split from DX12Wrapper.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <d3d12sdklayers.h>
#include <intrin.h>

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory(REFIID riid, void **factory)
{
		HRESULT hr = DXGIHookState::OrigCreateDXGIFactory ?
		DXGIHookState::OrigCreateDXGIFactory(riid, factory) : DXGI_ERROR_INVALID_CALL;
	DX12_LOG("CreateDXGIFactory",
		"riid=%p,hr=0x%lx,factory=%p",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapDXGIFactoryResult(riid, factory);
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory1(REFIID riid, void **factory)
{
		HRESULT hr = DXGIHookState::OrigCreateDXGIFactory1 ?
		DXGIHookState::OrigCreateDXGIFactory1(riid, factory) : DXGI_ERROR_INVALID_CALL;
	DX12_LOG("CreateDXGIFactory1",
		"riid=%p,hr=0x%lx,factory=%p",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapDXGIFactoryResult(riid, factory);
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
		HRESULT hr = DXGIHookState::OrigCreateDXGIFactory2 ?
		DXGIHookState::OrigCreateDXGIFactory2(flags, riid, factory) : DXGI_ERROR_INVALID_CALL;
	DX12_LOG("CreateDXGIFactory2",
		"flags=0x%x,riid=%p,hr=0x%lx,factory=%p",
		flags, &riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapDXGIFactoryResult(riid, factory);
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
			DX12_LOG_TEXT("dxgi.dll is not loaded yet; DXGI wrapper bootstrap deferred\n");
		AcquireSRWLockExclusive(&DXGIHookState::HookLock);
		DXGIHookState::HookAttempted = false;
		ReleaseSRWLockExclusive(&DXGIHookState::HookLock);
		return;
	}

	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory), DXGIHookCallbacks::HookedCreateDXGIFactory);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory1",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory1), DXGIHookCallbacks::HookedCreateDXGIFactory1);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory2",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory2), DXGIHookCallbacks::HookedCreateDXGIFactory2);
}

void DXGIHooks::Shutdown()
{
}

