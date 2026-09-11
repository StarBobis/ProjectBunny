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

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain);
void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain);
void DX12NativeOverlayShutdown();
