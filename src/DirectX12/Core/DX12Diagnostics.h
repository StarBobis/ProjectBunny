#pragma once

// Logging, runtime state, JSON diagnostics, and frame-flow telemetry.
// Umbrella header: the implementation was split into per-responsibility
// headers. Existing code can keep including DX12Diagnostics.h; new code may
// include the narrower headers directly.
//
//   DX12LogUtils.h     - raw d3d12 log file helpers + DX12LogInfo macros
//   DX12RuntimeState.h - runtime mode, module handle, frame counter shards
//   DX12LogManager.h   - queued async log writer + log mode constants
//   DX12JsonUtils.h    - JSON escaping / key=value field append helpers

#include "DX12LogUtils.h"
#include "DX12RuntimeState.h"
#include "DX12LogManager.h"
#include "DX12JsonUtils.h"
