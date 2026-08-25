#pragma once

#include "DX12HotPath.h"

// Command-list lifecycle, draw/dispatch binding flow, and state capture.
// Umbrella header: split into per-responsibility headers under
// Core/CommandFlow. Include order matches the original declaration order.
//
//   CommandFlow/DX12ProfilingImpl.h               - DX12_PROFILE_* macros +
//                                                   DX12Profiling definitions
//   CommandFlow/DX12CommandListBindingFlow.h      - compute binding flow,
//                                                   lifecycle, command recorder
//   CommandFlow/DX12HuntListModels.h              - hunt list models/selection
//   CommandFlow/DX12ResidentMaintenanceTelemetry.h - resident-maintenance
//                                                    telemetry
//   CommandFlow/DX12CommandListStateFlow.h        - state capture/replay flow

#include "CommandFlow/DX12ProfilingImpl.h"
#include "CommandFlow/DX12CommandListBindingFlow.h"
#include "CommandFlow/DX12HuntListModels.h"
#include "CommandFlow/DX12ResidentMaintenanceTelemetry.h"
#include "CommandFlow/DX12CommandListStateFlow.h"
