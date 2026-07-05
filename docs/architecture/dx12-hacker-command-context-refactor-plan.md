# DX12 Hacker Command Context Refactor Plan

Date: 2026-07-05

## Purpose

The current DX12 runtime is functionally close to the DX11 mod model, but its
hot-path architecture is not DX11-like. DX11 is elegant because
`HackerContext` owns the current graphics state. State-changing calls update the
context, and draw calls consume the already-known state.

The current DX12 path often does the inverse: draw, dispatch, and barrier hooks
ask multiple subsystems to rediscover state, resolve resources, compute hashes,
probe PreSkin candidates, and prepare mod decisions on every command. This
creates CPU-heavy frames where the GPU waits for CPU-side command analysis.

The refactor goal is to build the DX12 equivalent of DX11 `HackerContext`:

```text
State-changing command -> update/compile command context
Draw/Dispatch command  -> consume compiled context decision
```

## Official D3D12 Constraints

These constraints must guide the refactor:

- Resource barriers describe resource/subresource usage-state transitions. They
  are synchronization/state facts, not buffer identity or buffer content facts.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
- Buffer GPU virtual address is a resource identity/address fact exposed by
  `ID3D12Resource::GetGPUVirtualAddress`.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getgpuvirtualaddress
- Pipeline state objects are immutable bundles of shader and fixed-function
  state after creation. PSO-derived mod metadata should be compiled once and
  reused while the PSO remains current.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

## Current Baseline From Latest Log

Log file:

```text
D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl
```

Last complete frame observed on 2026-07-05:

```text
present:              1526
frame interval:        290.772 ms / 290772 us
expected FPS:          3.44
PresentWait:           0.236 ms / 236 us
measured hot path:     106.106 ms / 106106 us
commands/bindings:     3378 / 3378
fastForwardHits:       2586
```

Top current hotspots:

```text
DispatchHook:          22.079 ms / 22079 us
DrawHook:              15.325 ms / 15325 us
PreSkinMatch:          14.685 ms / 14685 us
PreSkinApply:          14.683 ms / 14683 us
DrawOverride:          12.746 ms / 12746 us
SetPipelineState:       5.442 ms /  5442 us
ModPrepareOverride:     5.006 ms /  5006 us
PreSkinQuery:           2.904 ms /  2904 us
BuildIaHash:            2.652 ms /  2652 us
```

The first ResourceBarrier policy fix is already effective:

```text
BarrierCapture:                 0.070 ms / 70 us
barrier captureCalls:           261
barrier captureSkippedPolicy:   261
barrier recordedCalls:          0
```

This proves the remaining problem is not ResourceBarrier capture. The remaining
architecture problem is repeated draw/dispatch state interpretation.

## Non-Negotiable Hash Invariants

The mod system depends on stable hashes. Refactoring must not change hash
meaning.

Do not change:

- IA hash algorithm.
- IA hash input identity:
  - GPU virtual address.
  - content when CPU-visible content is available.
  - resource/view metadata fallback.
  - stride.
  - format.
  - slot.
  - draw-region offset and size.
- Shader hash algorithm.
- Mod INI semantics.
- `[TextureOverride] hash=` matching behavior.
- `[ShaderOverride]` matching behavior.

Allowed changes:

- Move hash calculation earlier or later if the same inputs are used.
- Cache hash results by stronger lifetime/epoch keys.
- Split state epoch from identity/content epoch.
- Compile per-PSO and per-dispatch decision data.
- Skip work when the compiled decision proves that no mod can match.

## Root Cause

DX11 has one central command context. DX12 currently has several local trackers
without a single orchestration object:

```text
DX12CommandListRuntimeState
DX12BindingTracker
DX12ResourceTracker
DX12ModRuntime
DX12PreSkinRuntime
DX12IaReplacementExecutor
DX12DrawHookFlow
DX12DispatchHookFlow
```

Because there is no DX12 `HackerContext` equivalent, hook paths perform
business logic directly:

```text
Draw/Dispatch -> query state -> resolve resource -> compute hash -> match mod -> patch
```

The desired model is:

```text
Set* commands -> update context and compile decisions
Draw/Dispatch -> consume precompiled decision
```

## Target Architecture

Introduce a DX12 command-context layer that owns current command-list state and
compiled mod decisions.

Primary classes:

```text
DX12HackerCommandContext
DX12CompiledPsoState
DX12CompiledDrawDecision
DX12CompiledDispatchDecision
DX12IaBindingState
DX12IaHashDecisionCache
DX12DescriptorBindingEpoch
DX12PreSkinPlan
DX12ModDecisionCache
DX12CommandContextRegistry
```

Implementation rule:

- Classes must expose public fields and public methods only, matching current
  project preference.
- Utility helpers should live in `*Utils.hpp`.
- No `auto` in new C++ code.
- C++17 only.
- Hook functions should become thin routing functions.

## Target Data Flow

### SetPipelineState

Current behavior:

```text
SetPipelineState -> update runtime state
Draw/Dispatch later rediscover what this PSO means
```

Target behavior:

```text
SetPipelineState
  -> update current PSO
  -> lookup or build DX12CompiledPsoState
  -> store shader hashes
  -> store root signature metadata
  -> store shader override candidates
  -> store IA target plan
  -> store PreSkin CS candidate plan if compute PSO
```

`DX12CompiledPsoState` should answer:

```text
hasAnyShaderOverride
hasDrawOverrideCandidate
hasDescriptorOverrideCandidate
hasIaHashTarget
hasPreSkinCandidate
canNeverMatchDraw
canNeverMatchDispatch
requiredIaSlots
requiredDescriptorRanges
computeShaderHash
graphicsShaderHashes
```

### Root Signature, Descriptor Heap, Descriptor Table

Current behavior:

```text
Dispatch/PreSkin collects current bindings repeatedly.
```

Target behavior:

```text
SetRootSignature / SetDescriptorHeaps / SetRootDescriptorTable
  -> update descriptor binding state
  -> bump descriptor epoch only when effective state changes
  -> keep a compact binding fingerprint for PreSkin and descriptor triggers
```

### IA Bindings

Current behavior:

```text
Draw -> build IA hash state -> resolve GPUVA -> query resource tracker
```

Target behavior:

```text
IASetIndexBuffer / IASetVertexBuffers
  -> update IA binding state
  -> bump IA epoch when effective binding changes
  -> resolve stable resource identity once where possible
  -> prepare cheap binding fingerprint

Draw
  -> compute only the required region hash when compiled PSO says it is needed
  -> reuse cache by PSO + IA epoch + draw region key
```

### Draw

Current behavior:

```text
DrawHook
  -> HandleDrawOverrides
  -> collect target plan
  -> BuildIaHash
  -> ModPrepareOverride
  -> maybe ApplyAndExecute
```

Target behavior:

```text
DrawHook
  -> context.GetCompiledDrawDecision(draw args)
  -> if decision is Noop: original draw
  -> if decision needs IA hash: calculate only required hashes
  -> if decision matches: execute compiled replacement plan
```

### Dispatch

Current behavior:

```text
DispatchHook
  -> often enters PreSkin path
  -> many "cs not configured" exits
  -> PreSkin probe misses repeatedly
```

Target behavior:

```text
DispatchHook
  -> read compiled compute PSO decision
  -> if CS is not a PreSkin candidate: original dispatch
  -> if candidate: lookup PreSkinPlan by CS hash + descriptor epoch
  -> apply already-compiled patch plan
```

The latest log showed:

```text
PreSkinProbeCalls:         25
PreSkinProbeMiss:          25
PreSkinProbeNegativeHit:   0
PreSkin cs_not_configured: 95
```

This means Dispatch currently asks known-noop questions too often.

### ResourceBarrier

Current behavior after first fix:

```text
Normal Mod mode skips resource state recording.
```

Target behavior:

```text
ResourceBarrier
  -> never invalidate identity/content hash caches for state-only changes
  -> track state only for FrameAnalysis, ShaderDump, and Hunting consumers
```

This must remain true. Resource state is not IA content identity.

## Implementation Phases

### Phase 0: Guard Rails And Metrics

Goal: make regressions visible before changing hot-path behavior.

Tasks:

- Keep current per-frame timing output.
- Add explicit per-frame counters:
  - draw decision cache hits/misses.
  - dispatch decision cache hits/misses.
  - PreSkin candidate reject count.
  - PreSkin plan positive hits.
  - PreSkin plan negative hits.
  - IA hash cache hits/misses.
  - IA hash full recalculations.
- Keep `captureSkippedStatePolicy` for barrier tracking.
- Add one log line per frame for decision-cache effectiveness.

Expected result:

```text
No behavior change.
No hash change.
Only better diagnosis.
```

### Phase 1: Add DX12HackerCommandContext

Goal: centralize per-command-list state without changing behavior.

Tasks:

- Add `DX12HackerCommandContext.hpp`.
- Add `DX12CommandContextRegistry.hpp`.
- Context owns:
  - command list pointer.
  - current runtime state pointer.
  - current PSO.
  - current root signatures.
  - current IA state.
  - current descriptor epochs.
  - cached compiled PSO pointer.
- Existing hooks still call existing executor paths.
- Context is updated in existing state-change hooks.

Acceptance:

```text
Build passes.
Frame output unchanged within noise.
No mod behavior change.
```

### Phase 2: Compile Compute Dispatch Decisions

Goal: remove repeated PreSkin no-op checks.

Tasks:

- Add `DX12CompiledDispatchDecision`.
- Build decision when compute PSO changes.
- Decision states:
  - `NoComputeShader`.
  - `NoPreSkinCandidate`.
  - `NeedsPreSkinProbe`.
  - `NeedsShaderOverrideWork`.
- DispatchHook should fast-exit before `PreparePreSkinProbe` when decision is
  `NoPreSkinCandidate`.
- Cache `cs_not_configured` by CS hash at PSO compilation time.

Acceptance metrics:

```text
DispatchHook:       reduce by 40% to 70%
cs_not_configured:  near zero on hot frames
PreSkinProbeCalls:  only candidate CS dispatches
```

Expected performance:

```text
DispatchHook 22 ms -> 6 ms to 12 ms
Hot path 106 ms    -> 90 ms to 96 ms
```

### Phase 3: Compile PreSkin Plans

Goal: stop repeating PreSkin match/apply work when bindings repeat.

Tasks:

- Add `DX12PreSkinPlan`.
- Key plan by:
  - compute shader hash.
  - descriptor binding fingerprint.
  - root constants fingerprint when used.
  - mod reload generation.
- Plan states:
  - `NoopNegative`.
  - `PatchDescriptorTable`.
  - `PatchCbvSrvUav`.
  - `ResizeDispatch`.
- Positive cache hits must re-emit required D3D12 commands, but must not repeat
  expensive matching.
- Negative cache hits skip matching entirely.

Acceptance metrics:

```text
PreSkinMatch:       reduce by 70% to 90%
PreSkinApply:       reduce by 60% to 85%
PreSkinQuery:       reduce by 40% to 70%
PreSkin negative hits visible in log
PreSkin positive hits visible in log
```

Expected performance:

```text
PreSkinMatch+Apply 29 ms -> 4 ms to 9 ms
Hot path 90 ms           -> 65 ms to 75 ms
```

### Phase 4: Compile Draw Decisions Per PSO

Goal: make draws consume compiled PSO override metadata.

Tasks:

- Add `DX12CompiledPsoState`.
- Build it on PSO creation or first PSO bind.
- Extract:
  - shader hashes.
  - shader override candidate flags.
  - descriptor trigger requirements.
  - IA hash target requirements.
- Add `DX12CompiledDrawDecision`.
- Draw decision states:
  - `NoopNeverMatches`.
  - `NeedsIaHash`.
  - `NeedsDescriptorMatch`.
  - `NeedsReplacement`.
  - `SkipOriginal`.
- DrawHook should fast-exit if current compiled PSO cannot match.

Acceptance metrics:

```text
DrawOverride:       reduce by 50% to 75%
ModPrepareOverride: reduce by 40% to 70%
BuildIaHash calls:  only when current PSO requires IA hash
```

Expected performance:

```text
DrawOverride 12 ms -> 3 ms to 6 ms
Hot path 65 ms     -> 50 ms to 60 ms
```

### Phase 5: IA Hash Cache Rework

Goal: keep hash stability while avoiding global resource queries on repeated
draws.

Tasks:

- Add `DX12IaBindingState`.
- Add `DX12IaHashDecisionCache`.
- Cache by:
  - PSO identity or compiled PSO id.
  - IA binding epoch.
  - draw region key.
  - target slot.
  - content generation.
  - resource identity generation.
- Do not include resource state transition generation.
- Split cache invalidation:
  - resource lifetime and GPUVA changes invalidate identity cache.
  - CPU-visible content writes invalidate content cache.
  - ResourceBarrier state transitions do not invalidate IA hash cache.

Acceptance metrics:

```text
BuildIaHash:       reduce by 50% to 80%
IA hash full calc: only for new draw regions or changed content
No hash mismatch in Hunting or FrameAnalysis
```

Expected performance:

```text
BuildIaHash 2.6 ms -> 0.5 ms to 1.2 ms
Hot path 50 ms     -> 45 ms to 55 ms
```

### Phase 6: Slim HookFlow

Goal: make hooks route commands, not own policy.

Tasks:

- Move business decisions out of:
  - `DX12DrawHookFlow`.
  - `DX12DispatchHookFlow`.
  - `DX12WrapperRuntimeUtils`.
- Hook layer responsibilities:
  - unwrap arguments if needed.
  - fetch context.
  - update context for state-changing calls.
  - call context draw/dispatch executor.
  - call original when decision is noop.
- Policy layer responsibilities:
  - mod decision.
  - PreSkin plan.
  - IA hash target selection.
  - descriptor trigger matching.

Acceptance:

```text
Hook code becomes small and mechanical.
All mod semantics live in context/decision classes.
```

## File Map

Likely new files:

```text
src/DirectX12/command/DX12HackerCommandContext.hpp
src/DirectX12/command/DX12CommandContextRegistry.hpp
src/DirectX12/command/DX12CompiledPsoState.hpp
src/DirectX12/command/DX12CompiledDrawDecision.hpp
src/DirectX12/command/DX12CompiledDispatchDecision.hpp
src/DirectX12/ia/DX12IaBindingState.hpp
src/DirectX12/ia/DX12IaHashDecisionCache.hpp
src/DirectX12/mod/DX12PreSkinPlan.hpp
src/DirectX12/mod/DX12ModDecisionCache.hpp
src/DirectX12/utils/DX12CommandContextUtils.hpp
```

Likely touched files:

```text
src/DirectX12/command/DX12CommandListRuntimeState.hpp
src/DirectX12/command/DX12CommandListRuntime.cpp
src/DirectX12/command/DX12DrawHookFlow.hpp
src/DirectX12/command/DX12DispatchHookFlow.hpp
src/DirectX12/command/DX12CommandListStateCapture.cpp
src/DirectX12/ia/DX12IaReplacementExecutor.hpp
src/DirectX12/mod/DX12ShaderOverrideRuntime.hpp
src/DirectX12/mod/DX12PreSkinRuntime.hpp
src/DirectX12/resource/DX12ResourceTrackerQueries.cpp
```

## Risk List

### Command List Reset

Risk:

```text
Context state can survive past command-list Reset incorrectly.
```

Mitigation:

```text
Reset clears current state and compiled decision pointers.
Reset bumps context generation.
```

### Bundles

Risk:

```text
ExecuteBundle can replay state recorded in a secondary command list.
```

Mitigation:

```text
Keep bundle context separate.
Merge only explicitly supported state into parent context.
Do not assume bundle state mutates parent state unless D3D12 semantics require it.
```

### ExecuteIndirect

Risk:

```text
Draw/dispatch arguments are GPU-generated, so CPU-side draw-region hash may be incomplete.
```

Mitigation:

```text
Keep existing conservative ExecuteIndirect behavior until a dedicated command
signature parser exists.
```

### Descriptor Heap Copies

Risk:

```text
Runtime descriptor heap copies can change descriptor content without changing the root table pointer.
```

Mitigation:

```text
Descriptor epoch must bump on descriptor write/copy that affects tracked heap ranges.
```

### Multithreaded Command Recording

Risk:

```text
TLS caches can read stale context state across command-list migration.
```

Mitigation:

```text
Context registry key is command-list pointer plus generation.
TLS cache must validate generation.
```

### Internal Replay

Risk:

```text
Replacement execution can recursively trigger hooks.
```

Mitigation:

```text
Respect existing internal replay guards.
Compiled decisions must fast-exit during internal replay.
```

## Validation Plan

Every implementation phase must run:

```text
.\build_debug_x64.ps1
```

Then capture:

```text
D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl
```

Required report fields:

```text
frame total ms/us
frame interval ms/us
expected FPS
PresentWait ms/us
per-function total us
per-function percentage
top hotspot chart
decision cache hit/miss counters
PreSkin candidate reject/hit counters
IA hash cache hit/miss counters
Barrier policy counters
```

Regression checks:

```text
Mod still applies.
IA replacement resources still bind.
Hunting copied IA hash remains usable in TextureOverride hash.
FrameAnalysis hunt_hash stays stable for same resource/draw region.
No increase in PresentWait.
No missing shader override replacement.
No repeated resource state capture in normal Mod mode.
```

## Definition Of Done

The refactor is complete when:

```text
DX12 has a central command context equivalent to DX11 HackerContext.
DrawHook does not query global resource/mod state unless a compiled decision requires it.
DispatchHook rejects non-candidate compute shaders before PreSkin probing.
PreSkin positive and negative plan caches are visible in logs.
IA hash semantics are unchanged.
Normal Mod mode does not record ResourceBarrier state.
Measured hot path is below 55 ms on the same scene.
No Mod functionality regresses.
```

Stretch target:

```text
Measured hot path below 45 ms.
Expected FPS improves from 3.4 FPS to 6 FPS or better on the same scene.
```

## Next Concrete Step

Start with Phase 2 before Phase 4.

Reason:

```text
The latest log shows DispatchHook + PreSkinMatch + PreSkinApply costs about
51.4 ms in the last complete frame. Most of that is caused by repeated
non-candidate CS exits and repeated PreSkin probe misses. This is the highest
leverage change after ResourceBarrier capture was removed.
```

First implementation target:

```text
Add DX12CompiledDispatchDecision and make DispatchHook fast-exit before
PreparePreSkinProbe when the current compute shader is not a configured
PreSkin candidate.
```

## Implementation Progress

### 2026-07-05 Phase 2 Slice: Compiled Dispatch Decision

Implemented files:

```text
src/DirectX12/command/DX12CompiledDispatchDecision.hpp
src/DirectX12/command/DX12CommandListRuntimeState.hpp
src/DirectX12/command/DX12CommandListRuntime.hpp
src/DirectX12/command/DX12CommandListRuntime.cpp
src/DirectX12/command/DX12DispatchHookFlow.hpp
src/DirectX12/utils/DX12ResidentMaintenanceTelemetryUtils.hpp
```

Design:

```text
SetPipelineState / Reset
  -> DX12CommandListRuntime::RememberPipelineState
  -> compile DX12CompiledDispatchDecision

Dispatch
  -> EnsureDispatchDecision
  -> if no PreSkin candidate and no shader-override skip:
       call original Dispatch directly
  -> if shader-override skip:
       preserve existing skip behavior
  -> if PreSkin candidate:
       enter existing PreparePreSkinProbe path
```

Preserved invariants:

```text
IA hash algorithm unchanged.
IA hash inputs unchanged.
Shader hash algorithm unchanged.
TextureOverride hash matching unchanged.
ShaderOverride bytecode replacement unchanged.
PreSkin replacement path unchanged for candidate compute shaders.
Internal replay guard still exits before decision work.
```

New per-frame log category:

```text
DX12ResidentMaintenance category=dispatch_decision
  cacheHits
  cacheMisses
  builds
  noCompute
  noPreSkinCandidate
  preSkinCandidateBuilds
  preSkinCandidateDispatches
  shaderOverrideWork
  fastRejects
```

Expected effect:

```text
DispatchHook: reduce by 40% to 70% on frames dominated by cs_not_configured.
cs_not_configured: should approach zero for non-candidate CS dispatches.
PreSkinProbeCalls: should remain only for compiled candidate CS dispatches.
Measured hot path: expected 10 ms to 16 ms reduction on the previous 106 ms frame.
```
