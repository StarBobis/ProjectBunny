#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

static constexpr UINT WM_DX12_OVERLAY_SHOW = WM_APP + 0x512;
static constexpr UINT WM_DX12_OVERLAY_HIDE = WM_APP + 0x513;
static constexpr UINT WM_DX12_OVERLAY_DESTROY = WM_APP + 0x514;
static constexpr UINT WM_DX12_OVERLAY_POSITION = WM_APP + 0x515;
static constexpr UINT WM_DX12_OVERLAY_REPAINT = WM_APP + 0x516;

DWORD WINAPI DX12OverlayThread(void*);
void DX12EnsureOverlayWindow();
void DX12ShowOverlayWindow();
void DX12HideOverlayWindow();
void DX12RequestOverlayRepaint();
bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain);
void DX12CloseOverlayWindow();

