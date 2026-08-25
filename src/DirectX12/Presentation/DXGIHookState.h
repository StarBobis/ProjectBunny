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


class DXGIHookSlotTypes
{
public:
	class IDXGISwapChain
	{
	public:
		static constexpr UINT Present = 8;
		static constexpr UINT ResizeBuffers = 13;
	};

	class IDXGISwapChain1
	{
	public:
		static constexpr UINT Present1 = 22;
	};

	class IDXGIFactory
	{
	public:
		static constexpr UINT CreateSwapChain = 10;
	};

	class IDXGIFactory2
	{
	public:
		static constexpr UINT CreateSwapChainForHwnd = 15;
		static constexpr UINT CreateSwapChainForCoreWindow = 16;
		static constexpr UINT CreateSwapChainForComposition = 24;
	};
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
