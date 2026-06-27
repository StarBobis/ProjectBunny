#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

DWORD WINAPI DX12OverlayThread(void*);
void DX12EnsureOverlayWindow();
void DX12ShowOverlayWindowFallback();
bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain);
void DX12DrawSwapChainText(IDXGISwapChain *swapChain);
void DX12CloseOverlayWindow();

