#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

#include "DX12OriginalFunctionRegistry.hpp"
#include "DXGIHookSignatures.hpp"
#include "DXGIHookState.hpp"
#include "types/DXGIHookSlotTypes.hpp"

class DXGIHookRegistry
{
public:
	template <typename T>
	static T GetOriginal(void *object, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::Resolve(object, slot, fallback, name);
	}

	static DXGIHookSignatures::Present GetPresentOriginal(IDXGISwapChain *swapChain)
	{
		return GetOriginal<DXGIHookSignatures::Present>(
			swapChain, DXGIHookSlotTypes::IDXGISwapChain::Present,
			DXGIHookState::OrigPresent, "IDXGISwapChain::Present");
	}

	static DXGIHookSignatures::Present1 GetPresent1Original(IDXGISwapChain1 *swapChain)
	{
		return GetOriginal<DXGIHookSignatures::Present1>(
			swapChain, DXGIHookSlotTypes::IDXGISwapChain1::Present1,
			DXGIHookState::OrigPresent1, "IDXGISwapChain1::Present1");
	}

	static DXGIHookSignatures::ResizeBuffers GetResizeBuffersOriginal(IDXGISwapChain *swapChain)
	{
		return GetOriginal<DXGIHookSignatures::ResizeBuffers>(
			swapChain, DXGIHookSlotTypes::IDXGISwapChain::ResizeBuffers,
			DXGIHookState::OrigResizeBuffers, "IDXGISwapChain::ResizeBuffers");
	}

	static DXGIHookSignatures::CreateSwapChain GetCreateSwapChainOriginal(IDXGIFactory *factory)
	{
		return GetOriginal<DXGIHookSignatures::CreateSwapChain>(
			factory, DXGIHookSlotTypes::IDXGIFactory::CreateSwapChain,
			DXGIHookState::OrigCreateSwapChain, "IDXGIFactory::CreateSwapChain");
	}

	static DXGIHookSignatures::CreateSwapChainForHwnd GetCreateSwapChainForHwndOriginal(IDXGIFactory2 *factory)
	{
		return GetOriginal<DXGIHookSignatures::CreateSwapChainForHwnd>(
			factory, DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForHwnd,
			DXGIHookState::OrigCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
	}

	static DXGIHookSignatures::CreateSwapChainForCoreWindow GetCreateSwapChainForCoreWindowOriginal(IDXGIFactory2 *factory)
	{
		return GetOriginal<DXGIHookSignatures::CreateSwapChainForCoreWindow>(
			factory, DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForCoreWindow,
			DXGIHookState::OrigCreateSwapChainForCoreWindow,
			"IDXGIFactory2::CreateSwapChainForCoreWindow");
	}

	static DXGIHookSignatures::CreateSwapChainForComposition GetCreateSwapChainForCompositionOriginal(IDXGIFactory2 *factory)
	{
		return GetOriginal<DXGIHookSignatures::CreateSwapChainForComposition>(
			factory, DXGIHookSlotTypes::IDXGIFactory2::CreateSwapChainForComposition,
			DXGIHookState::OrigCreateSwapChainForComposition,
			"IDXGIFactory2::CreateSwapChainForComposition");
	}
};
