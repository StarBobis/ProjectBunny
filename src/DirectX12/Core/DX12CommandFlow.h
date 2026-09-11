#pragma once

#include "DX12HotPath.h"

// Command-list lifecycle, draw/dispatch binding flow, and state capture.
// Umbrella header: split into per-responsibility headers under
// Core/CommandFlow. Include order matches the original declaration order.
// Profiling/telemetry macros now live in the unified diagnostics facade
// (Core/Diag/DX12Diag.h).
//
//   CommandFlow/DX12CommandListBindingFlow.h      - compute binding flow,
//                                                   lifecycle, command recorder
//   CommandFlow/DX12HuntListModels.h              - hunt list models/selection
//   CommandFlow/DX12CommandListStateFlow.h        - state capture/replay flow

#include "CommandFlow/DX12CommandListBindingFlow.h"
#include "CommandFlow/DX12HuntListModels.h"
#include "CommandFlow/DX12CommandListStateFlow.h"
