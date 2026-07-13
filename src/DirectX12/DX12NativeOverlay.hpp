#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain);
void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12NativeOverlayShutdown();
bool DX12NativeOverlayIsOverlayWindow(HWND hwnd);
