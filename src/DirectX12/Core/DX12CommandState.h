#pragma once

#include "DX12Diagnostics.h"

// IA state, compiled draw decisions, command-list runtime state, and caches.
// Umbrella header: split into per-responsibility headers under
// Core/CommandState. Include order matches the original declaration order.
//
//   CommandState/DX12IaDecisionState.h     - active IA state + compiled decisions
//   CommandState/DX12IaHashCaches.h        - tracking + IA hash/fast-reject caches
//   CommandState/DX12SimulateContext.h     - simulate-context templates/runtime
//   CommandState/DX12CommandListRuntime.h  - command-list runtime shards/capture
//   CommandState/DX12FrameAnalysisState.h  - frame-analysis runtime state

class DX12ModIaReplacement;

#include "CommandState/DX12IaDecisionState.h"
#include "CommandState/DX12IaHashCaches.h"
#include "CommandState/DX12SimulateContext.h"
#include "CommandState/DX12CommandListRuntime.h"
#include "CommandState/DX12FrameAnalysisState.h"
