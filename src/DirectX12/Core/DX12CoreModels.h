#pragma once

#include "DX12Platform.h"

// Pipeline, descriptor, binding, and frame-analysis data models.
// Umbrella header: the models were split into per-responsibility headers
// under Core/Models. Include order matches the original declaration order.
//
//   Models/DX12PsoModels.h            - PSO / root signature / descriptor summaries
//   Models/DX12BindingModels.h        - resource summaries, frame/current binding
//                                       state, binding events
//   Models/DX12BindingTrackerModels.h - binding tracker shards/TLS caches, IA
//                                       regions, binding table exports

class DX12SimulateContext;

#include "Models/DX12PsoModels.h"
#include "Models/DX12BindingModels.h"
#include "Models/DX12BindingTrackerModels.h"


// DX11-aligned global log utilities
//
// DX12LogInfo and DX12LogDebug use direct fprintf without timestamps.
// Timestamped variants prefix the line with GetTickCount64.
//
// Format (TS variants):  raw_tick<TAB>message<NEWLINE>
// Format (no-TS variants): message as-is, caller controls newline
//
// All variants: no queue, no thread, no lock. Just like DX11 LogInfo.
