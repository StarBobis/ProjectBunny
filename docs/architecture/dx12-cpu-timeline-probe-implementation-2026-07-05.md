# DX12 CPU Timeline Probe Implementation Plan

## Goal

The latest frame log proves the current telemetry does not cover the real hot
spot:

```text
present:              1267
frameWallUs:          316615
hotPathKnownUs:        ~72062
apiWallKnownUs:        ~39489
presentWallUs:            706
unattributedWallUs:   ~244553
```

The goal of this pass is diagnostic coverage only. Do not optimize behavior yet.
Add probes that can split the missing wall time into:

1. Time inside known D3D12/DXGI wrapper calls.
2. Time between wrapper calls while the game is outside our hooks.
3. Time in logging enqueue/formatting.
4. Time in worker tasks by queue and stage.
5. Time in IA replacement lookup/bind and buffer resolve maintenance.
6. Time in PreSkin probe preparation and apply phases.

## Official DX12 Grounding

Microsoft Direct3D 12 documentation describes command lists as CPU-recorded work
that is explicitly submitted through command queues. Queue execution, fences,
and synchronization are explicit in D3D12, so when queue and present waits are
small but frame wall time is large, the missing time must be CPU-side command
recording, wrapper work, application CPU work outside our hooks, or
instrumentation gaps.

References queried before code changes:

- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

## First-Principles Diagnosis

A frame wall interval is elapsed time between two present boundaries. It is not
equal to the sum of selected function probes unless every thread and every
callsite in the interval is covered.

The current log has three important facts:

1. `Present` and `ExecuteCommandLists` are below 1 ms, so GPU submit/present
   wait is not the 300 ms source.
2. `DX12HotPathTiming` covers only selected hot path scopes and has nested
   scopes, so summing it cannot explain frame wall.
3. `DX12ApiWallTiming` covers only selected wrapper calls, so time between
   wrapper calls is currently invisible.

Therefore the next correct move is to make the frame wall accounting closed:

```text
frameWall ~= presentWall + apiInsideKnown + interApiGaps + workerTaskWall + logCost + outsideHooks
```

The exact split can be imperfect because multiple threads overlap, but the log
must expose enough wall-clock ranges to tell whether the missing 200+ ms is in
our hooks, worker queues, logging, or the game outside D3D calls.

## Implementation Scope

### 1. API Coverage Probe

Files:

- `src/DirectX12/utils/DX12OuterTimingProbeUtils.hpp`

Add per-frame API coverage fields updated by every `ApiWallScope`:

- `apiScopeCalls`
- `apiScopeTotalUs`
- `apiScopeFirstBeginToPresentUs`
- `apiScopeLastEndToPresentUs`
- `apiScopeMaxGapUs`
- `apiScopeSpanUs`
- `apiScopeNestedCalls`

Reason:

This separates "inside our wrapped API calls" from "time between wrapped API
calls". If the missing wall time is application CPU outside our hooks, this
probe will show a large max gap or large before/after gap.

### 2. Logging Cost Probe

Files:

- `src/DirectX12/runtime/DX12LogManager.hpp`
- `src/DirectX12/utils/DX12OuterTimingProbeUtils.hpp`

Add lightweight timing around log enqueue/format paths and emit a per-frame
`DX12LogCost` line:

- `jsonCalls`
- `textCalls`
- `debugJsonCalls`
- `enqueueCalls`
- `formatUs`
- `enqueueUs`
- `droppedLines`

Reason:

Logging is allowed to be slow in this project, but it still needs to be
accounted for. The question is not whether logging is "safe"; it is whether
logging is part of the missing CPU wall time.

### 3. Worker Queue Stage Probe

Files:

- `src/DirectX12/utils/DX12OuterTimingProbeUtils.hpp`
- `src/DirectX12/resource/DX12ResourceCollectQueue.cpp`
- `src/DirectX12/descriptor/DX12DescriptorCollectPipeline.cpp`
- `src/DirectX12/binding/DX12BindingCollectQueue.cpp`

Existing worker telemetry aggregates all worker tasks. Add queue/stage buckets:

- queue name
- task stage
- calls
- total us
- max us

Reason:

The latest frame has `workerTaskUs=71872`. We need to know if that is resource,
descriptor, binding, or a specific stage inside those queues.

### 4. IA Replacement Detail Probe

Files:

- `src/DirectX12/ia/DX12IaReplacementExecutor.hpp`
- `src/DirectX12/mod/DX12ModRuntime.hpp`
- `src/DirectX12/mod/DX12ModRuntime.cpp`

Add scopes around:

- IA override lookup
- IA resource bind
- PSO IA negative cache
- replacement prepare

Reason:

The log contains many `DX12IaOverrideCheck` and `DX12IaResourceBind` lines. The
current `BuildIaHash` and `ModPrepareOverride` scopes are too coarse to prove
whether lookup, bind, or resource management is the real cost.

### 5. PreSkin Detail Probe

Files:

- `src/DirectX12/command/DX12DispatchHookFlow.hpp`
- `src/DirectX12/mod/DX12PreSkinRuntime.hpp`

Add scopes around:

- `PreparePreSkinProbe`
- `DX12ModApplyPreSkinningUavReplacement`
- match-cs path
- VLR fallback path
- descriptor table patch
- known PreSkin patch replay

Reason:

The visible `PreSkinQuery/Match/Apply` scopes dropped, but dispatch still has
spikes. The next probe must split the PreSkin path by decision stage.

### 6. Log Analyzer Fix

Files:

- `tools/Analyze-DX12FrameLog.ps1`

Fix the analyzer to parse the actual log format:

```text
timestamp<TAB>func<TAB>json-fragment
```

Reason:

The current script assumes each line is a complete JSON object. The runtime log
is not formatted that way, so the report tool must reconstruct objects before
printing the frame report.

## Safety Rules

- Do not change rendering behavior.
- Do not change resource states, descriptor writes, IA hash bytes, or override
  semantics.
- New C++ code and comments must be English.
- Do not use `auto` in new C++ code.
- New utility classes must end with `Utils`.
- All new class members and functions must be public.
- Build with `build_debug_x64.ps1` after code changes.

## Expected Output After Game Run

The next log must make this line possible:

```text
frameWallUs = presentWallUs + apiCoverageKnownUs + maxApiGap/outsideHookUs + workerStageUs + logCostUs + remainingUnknownUs
```

Expected diagnostic improvement:

- Missing CPU wall attribution should improve from roughly 20% coverage to
  70%-95% coverage.
- No direct performance improvement is expected from this pass.
- The next optimization pass should be based on the largest newly exposed
  bucket, not on current 10 ms-level functions.
