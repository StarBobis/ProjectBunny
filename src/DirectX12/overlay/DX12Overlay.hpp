#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DX12Overlay
{
public:
	static constexpr UINT ShowMessage = WM_APP + 0x512;
	static constexpr UINT HideMessage = WM_APP + 0x513;
	static constexpr UINT DestroyMessage = WM_APP + 0x514;
	static constexpr UINT PositionMessage = WM_APP + 0x515;
	static constexpr UINT RepaintMessage = WM_APP + 0x516;

	static DWORD WINAPI Thread(void*)
	{
		return 0;
	}

	static void EnsureWindow()
	{
	}

	static void Show()
	{
	}

	static void Hide()
	{
	}

	static void RequestRepaint()
	{
	}

	static bool UpdateWindowForSwapChain(IDXGISwapChain *swapChain)
	{
		(void)swapChain;
		return false;
	}

	static void Close()
	{
	}
};
