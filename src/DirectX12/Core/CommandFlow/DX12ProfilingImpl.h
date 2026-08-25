#pragma once

// DX12_PROFILE_* compatibility macros routing into DX12Profiling.
// Split out of Core/DX12CommandFlow.h; include that umbrella header from existing code.
#include "../DX12HotPath.h"

class DX12Profiler;

#define DX12_PROFILE_FUNCTION(counter) \
	DX12Profiler::Scope _profile_timer(DX12Profiler::Counter, #counter)

#define DX12_PROFILE_FAST_FORWARD() do { \
	DX12Profiling::Count("FastForward"); \
} while (0)
