// ============================================================
// DX12Hooks.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Core.h"

#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

// Umbrella header: split into per-responsibility headers under Hooks/ and
// Proxy/. Include order matches the original declaration order.
//
//   Hooks/DX12CreateTimingTelemetry.h     - create-API timing telemetry
//   Hooks/DX12DescriptorShadowTelemetry.h - descriptor-shadow telemetry
//   Hooks/DX12HotPathTiming.h             - hot-path stage timing macros
//   Hooks/DX12Profiler.h                  - profiler scopes/counters + macros
//   Hooks/DX12DispatchHookFlow.h          - dispatch hook flow
//   Hooks/DX12IaReplacement.h             - IA replacement executors
//   Hooks/DX12DrawHookFlow.h              - draw hook flow
//   Hooks/DX12HookRegistry.h              - hook registry/manager
//   Proxy/D3D12ProcessGate.h              - process gate
//   Proxy/D3D12ProxyRuntime.h             - proxy runtime state

#include "Hooks/DX12CreateTimingTelemetry.h"
#include "Hooks/DX12DescriptorShadowTelemetry.h"
#include "Hooks/DX12HotPathTiming.h"
#include "Hooks/DX12Profiler.h"
#include "Hooks/DX12DispatchHookFlow.h"
#include "Hooks/DX12IaReplacement.h"
#include "Hooks/DX12DrawHookFlow.h"
#include "Hooks/DX12HookRegistry.h"
#include "Proxy/D3D12ProcessGate.h"
#include "Proxy/D3D12ProxyRuntime.h"
