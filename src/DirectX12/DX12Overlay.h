#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

DWORD WINAPI DX12OverlayThread(void*);
void DX12EnsureOverlayWindow();
bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain);
bool DX12RenderOverlayToSwapChain(IDXGISwapChain *swapChain);
void DX12DrawSwapChainText(IDXGISwapChain *swapChain);
void DX12CloseOverlayWindow();

