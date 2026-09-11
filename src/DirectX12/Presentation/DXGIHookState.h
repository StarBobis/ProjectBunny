// DXGI hook callbacks, signatures, slot types and hook state.
// Split from DX12Presentation.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12WrapperRuntimeUtils.h"

class DXGIHookCallbacks
{
public:
	static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
};


class DXGIHookSignatures
{
public:
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};


class DXGIHookState
{
public:
	inline static bool HookAttempted = false;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory1 = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory2 OrigCreateDXGIFactory2 = nullptr;
};
