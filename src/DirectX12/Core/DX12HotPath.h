#pragma once

#include "DX12CommandState.h"

// Hot-path feature gates and shared hot-path state.
// Umbrella header: profiling moved to the unified diagnostics facade
// (Core/Diag/DX12Diag.h, pulled in via DX12CommandState.h's diagnostics
// chain); this header keeps the feature-gate flags and shared state.
//
//   HotPath/DX12HotPathState.h - hot-path feature gate flags + shared state

#include "HotPath/DX12HotPathState.h"
