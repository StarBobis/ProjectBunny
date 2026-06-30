#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

#include "DXGIHookSignatures.hpp"

class DXGIHookState
{
public:
	inline static bool HookAttempted = false;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static DXGIHookSignatures::CreateSwapChain OrigCreateSwapChain = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForHwnd OrigCreateSwapChainForHwnd = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForCoreWindow OrigCreateSwapChainForCoreWindow = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForComposition OrigCreateSwapChainForComposition = nullptr;
	inline static DXGIHookSignatures::Present OrigPresent = nullptr;
	inline static DXGIHookSignatures::Present1 OrigPresent1 = nullptr;
	inline static DXGIHookSignatures::ResizeBuffers OrigResizeBuffers = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory1 = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory2 OrigCreateDXGIFactory2 = nullptr;
	inline static SRWLOCK KnownSwapChainLock = SRWLOCK_INIT;
	inline static IDXGISwapChain *KnownSwapChain = nullptr;

	static void ReleaseKnownSwapChain()
	{
		IDXGISwapChain *swapChain = nullptr;
		AcquireSRWLockExclusive(&KnownSwapChainLock);
		swapChain = KnownSwapChain;
		KnownSwapChain = nullptr;
		ReleaseSRWLockExclusive(&KnownSwapChainLock);
		if (swapChain)
			swapChain->Release();
	}
};
