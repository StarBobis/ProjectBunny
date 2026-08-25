#pragma once

#include "DX12CommandState.h"

// Hot-path feature gates, profiling state, and hang watchdog.
// Umbrella header: split into per-responsibility headers under Core/HotPath.
// Include order matches the original declaration order.
//
//   HotPath/DX12HotPathState.h - hot-path feature gate flags + shared state
//   HotPath/DX12HangWatchdog.h - in-process hang watchdog with self-minidump
//   HotPath/DX12Profiling.h    - frame/present profiling helpers

#include "HotPath/DX12HotPathState.h"
#include "HotPath/DX12HangWatchdog.h"
#include "HotPath/DX12Profiling.h"


class DX12Profiler;
