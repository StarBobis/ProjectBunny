// ============================================================
// DX12Resources.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "Resources/DX12ResourceLifetime.h"
#include "Resources/DX12PreSkinBindingCache.h"
#include "Resources/DX12PsoDecisionCache.h"
#include "Resources/DX12ResourceHookRuntime.h"
#include "Resources/DX12RuntimeControl.h"

// In-game text overlay. The overlay records into a caller-owned command list;
// DX12PresentOverlayCompose (Core/DX12ModState.h) owns the channel, the
// back-buffer barrier and the single submission all overlay sources share.
bool DX12NativeOverlayHasContent();
// Creates the overlay's GPU objects and uploads the font atlas. MUST run
// before DX12PresentOverlayCompose acquires the overlay channel: the atlas
// upload takes a channel slot of its own.
bool DX12NativeOverlayPrepare(
	IDXGISwapChain *swapChain, ID3D12CommandQueue *queue, DXGI_FORMAT format);
UINT DX12NativeOverlayRecord(
	ID3D12GraphicsCommandList *commandList,
	IDXGISwapChain *swapChain,
	ID3D12CommandQueue *queue,
	D3D12_CPU_DESCRIPTOR_HANDLE rtv,
	DXGI_FORMAT format,
	UINT width,
	UINT height);
void DX12NativeOverlayOnSubmitted(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue);
// True only once the overlay's GPU objects already exist. The skipAll fast path
// uses this so it can show the profiling HUD without creating a device, a PSO,
// a heap or a queue submission from inside the fast path.
bool DX12NativeOverlayReady(IDXGISwapChain *swapChain);
// One-shot permission for the fast path to create those objects, armed by the
// HUD hotkey. Without it, F11 would do nothing in a session whose mod does no
// per-draw work: skipAll never clears, so the normal path never runs and
// nothing would ever initialise the overlay.
void DX12NativeOverlayRequestInit();
void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12NativeOverlayShutdown();
