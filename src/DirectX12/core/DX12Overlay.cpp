#include "DX12Overlay.h"

DWORD WINAPI DX12OverlayThread(void*)
{
	return 0;
}

void DX12EnsureOverlayWindow()
{
}

void DX12ShowOverlayWindow()
{
}

void DX12HideOverlayWindow()
{
}

void DX12RequestOverlayRepaint()
{
}

bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain)
{
	(void)swapChain;
	return false;
}

void DX12CloseOverlayWindow()
{
}

