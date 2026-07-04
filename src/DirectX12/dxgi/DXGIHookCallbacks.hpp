#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookCallbacks
{
public:
	static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
};
