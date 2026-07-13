#pragma once

#include <Windows.h>

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
