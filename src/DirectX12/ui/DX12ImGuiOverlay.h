#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

bool DX12ImGuiOverlayRender(IDXGISwapChain *swapChain);
void DX12ImGuiOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12ImGuiOverlayShutdown();
bool DX12ImGuiOverlayIsOverlayWindow(HWND hwnd);
