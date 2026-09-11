#pragma once

// Runtime state, JSON diagnostics, and the unified diagnostics facade.
// Umbrella header: the implementation was split into per-responsibility
// headers. Existing code can keep including DX12Diagnostics.h; new code may
// include the narrower headers directly.
//
//   DX12RuntimeState.h   - runtime mode, module handle, frame counter shards
//   Diag/DX12Diag.h      - unified async logging + profiling (TIMING builds)
//   DX12JsonUtils.h      - JSON escaping / key=value field append helpers

#include "DX12RuntimeState.h"
#include "Diag/DX12Diag.h"
#include "DX12JsonUtils.h"
