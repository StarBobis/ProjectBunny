// ============================================================
// DX12Runtime.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "Runtime/DX12DescriptorHeapShadowUtils.h"
#include "Runtime/DX12DeviceHooks.h"
#include "Runtime/DX12ResourceTrackerHooks.h"
#include "Runtime/DX12ExecuteIndirectRuntime.h"
#include "Runtime/DX12FrameAnalysisManifest.h"
#include "Runtime/DX12VmFrameVariablesUtils.h"
#include "Runtime/DX12Overlay.h"
#include "Runtime/DX12Input.h"
