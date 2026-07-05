# DX12 Compiled Draw Decision Phase 4 Plan

Date: 2026-07-05

## Current Evidence

Latest post-Phase-3 frame:

```text
present:                       2879
frame interval:                282.000 ms / 282000 us
expected FPS:                    3.55
PresentWait:                     0.244 ms /    244 us
measured hot path:              42.983 ms /  42983 us

DrawHook:                        9.503 ms /   9503 us
DrawOverride:                    6.707 ms /   6707 us
SetPipelineState:                6.611 ms /   6611 us
DispatchHook:                    5.498 ms /   5498 us
PreSkinQuery:                    2.761 ms /   2761 us
RootDescTable:                   2.039 ms /   2039 us
ResourceBarrier:                 1.761 ms /   1761 us
BindCollect:                     1.367 ms /   1367 us
ModPrepareOverride:              1.302 ms /   1302 us
BuildIaHash:                     0.312 ms /    312 us
```

Phase 3 removed most repeated PreSkin miss work:

```text
PreSkinPlan lookups:             25
PreSkinPlan hits:                24
PreSkinPlan negativeHits:        23
PreSkinPlan positiveHits:         1
PreSkinPlan misses:               1
PreSkinMatch + PreSkinApply:     31.623 ms -> 0.596 ms
```

The new largest draw-side cluster is:

```text
DrawHook + DrawOverride + SetPipelineState + ModPrepareOverride
  = 24.123 ms / 24123 us
  = 56.1% of measured hot path
```

## Official D3D12 Facts Used

References:

```text
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawindexedinstanced
https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
```

Relevant constraints:

```text
DrawInstanced and DrawIndexedInstanced submit work using the current command-list
pipeline state.

D3D12 PSOs contain most graphics pipeline state, including shader bytecode, input
vertex format, primitive topology type, blend/rasterizer/depth state, render
target formats, multisampling state, stream-output state, and root signature.

Some state is outside the PSO and remains command-list state: resource bindings,
descriptor heaps, root parameters, IA index/vertex buffers, render targets,
viewport/scissor, blend factor, stencil ref, and IA primitive topology order.

IASetVertexBuffers sets vertex buffer views as command-list state. Therefore IA
hash inputs must still come from current IA bindings at draw time, but the
decision about whether a PSO can ever need those IA hashes belongs to PSO
compilation time.
```

## Root Cause

DX11 is elegant because `HackerContext` owns current state. State-changing calls
update the context, and Draw consumes the already-known state.

DX12 Phase 2 and Phase 3 moved compute decisions toward that model. Draw is
still behind. `DX12IaReplacementExecutor::HandleDrawOverrides` performs
state-discovery work inside every Draw:

```text
Draw
  -> check active shader overrides
  -> test whether current PSO has shader overrides
  -> collect shader override IA target plan for current PSO
  -> decide whether descriptor targets are possible
  -> maybe build IA hash state
  -> prepare shader override replacement
  -> maybe apply replacement
```

The architectural defect is not that any single check is expensive. The defect
is that Draw owns policy. That is the opposite of the DX11 `HackerContext`
model.

Current local cache defect:

```text
runtimeState.psoHasOverridesGeneration uses TrackingGeneration.
TrackingGeneration describes command-list tracking policy, not mod reload
semantic validity. This can invalidate PSO override answers for reasons
unrelated to the mod configuration.
```

Correct lifetime:

```text
PSO draw decision validity = PSO pointer + mod reload generation + active mod
feature flags.
```

## Target Architecture

Introduce a compiled draw decision as the draw-side peer of
`DX12CompiledDispatchDecision`.

New module:

```text
src/DirectX12/command/DX12CompiledDrawDecision.hpp
```

Primary class:

```text
DX12CompiledDrawDecision
```

Decision states:

```text
NoopNeverMatches
  Current PSO cannot match any draw ShaderOverride. DrawHook can call original
  draw without entering DrawOverride policy.

NeedsIaHash
  Current PSO has draw ShaderOverride candidates that can depend on IA hashes.
  Draw must build only the required IA hash state described by the compiled
  target plan.

NeedsDescriptorMatch
  Current PSO has descriptor-triggered ShaderOverride/TextureOverride work.
  Draw must still enter ModPrepareOverride even if no IA hash is required.

NeedsReplacement
  Current PSO has draw ShaderOverride candidates that do not require IA hash
  before ModPrepareOverride can run.

SkipOriginal
  Current PSO has ShaderOverride skip handling. Draw must preserve the existing
  skip behavior.
```

Compiled fields:

```text
pipelineState
reloadGeneration
hasActiveShaderOverrides
hasShaderOverrides
hasShaderOverrideSkip
targetPlan
targetPlanHasIaTargets
targetPlanHasDescriptorTargets
negativeCacheSafe
resolved
```

Target data flow:

```text
SetPipelineState / Reset
  -> DX12CommandListRuntime::RememberPipelineState
  -> compile dispatch decision
  -> compile draw decision

Draw
  -> EnsureDrawDecision
  -> if NoopNeverMatches:
       call original draw directly
  -> if SkipOriginal:
       preserve existing skip behavior
  -> if NeedsIaHash:
       build IA hash only for compiled target plan
  -> if NeedsDescriptorMatch or NeedsReplacement:
       call ModPrepareOverride with compiled metadata
```

## First Implementation Slice

This slice is intentionally broad enough to align the draw path with the Phase
2 dispatch architecture, but conservative enough to preserve Mod behavior.

Tasks:

```text
1. Add DX12CompiledDrawDecision.
2. Add mod runtime API to compile a draw decision for a PSO in one place.
3. Store the compiled decision in DX12CommandListRuntimeState.
4. Compile draw decision from RememberPipelineState and Reset.
5. Add EnsureDrawDecision to recompile when reload generation or active feature
   flags change.
6. Replace per-Draw DX12ModHasShaderOverridesForPso and
   DX12ModCollectShaderOverrideIaHashTargets calls with the compiled decision.
7. Keep DX12ModPrepareShaderOverrideReplacement as the authority for actual
   replacement execution.
8. Add per-frame telemetry for draw decision effectiveness.
```

Out of scope for this slice:

```text
Do not change IA hash algorithm.
Do not change IA hash input identity.
Do not change shader hash algorithm.
Do not change TextureOverride hash matching.
Do not change ShaderOverride command-list semantics.
Do not cache positive replacement command emission yet.
Do not remove DX12ModPrepareShaderOverrideReplacement.
```

## Guard Rails

Hash stability:

```text
DX12CompiledDrawDecision may decide whether IA hash work is required.
DX12CompiledDrawDecision may decide which IA slots are required using the same
DX12IaHashTargetPlan that already exists.
DX12CompiledDrawDecision must not compute new hashes or change hash inputs.
DX12CommandListRuntime::BuildIaHashStateForTargets remains the IA hash authority.
```

Mod behavior:

```text
Positive replacement execution remains in DX12ModPrepareShaderOverrideReplacement
and DX12IaReplacementExecutor::ApplyAndExecute.

Present replacement remains independent of draw decision fast rejects.

Hunting and FrameAnalysis paths keep their current DrawHook behavior before mod
override handling.

Internal replay guard still exits before replacement work.
```

Reload correctness:

```text
Compiled draw decision includes DX12ModGetReloadGeneration().
EnsureDrawDecision recompiles when reload generation changes.
Mod runtime loader already clears gShaderOverridePsoMatchCache on reload.
```

Descriptor correctness:

```text
If the compiled target plan has descriptor targets, negative PSO+IA cache is not
safe. Existing descriptor-aware behavior is preserved by entering
DX12ModPrepareShaderOverrideReplacement.
```

## Telemetry

Add a new per-frame log:

```text
DX12ResidentMaintenance category=draw_decision
```

Counters:

```text
cacheHits
cacheMisses
builds
noPso
noShaderOverride
noDrawOverride
needsIaHash
needsDescriptorMatch
needsReplacement
skipOriginal
fastRejects
iaHashDraws
descriptorDraws
```

Interpretation:

```text
fastRejects high:
  DrawHook is consuming compiled no-op decisions correctly.

builds near SetPipelineState changed count:
  PSO bind phase is doing the intended compile work.

cacheMisses much lower than Draw count:
  Draw is not repeatedly rediscovering PSO override metadata.

iaHashDraws lower than draw override checks:
  BuildIaHash is gated by compiled target requirements.
```

## Expected Effect

Current draw-side hotspot cluster:

```text
DrawHook + DrawOverride + SetPipelineState + ModPrepareOverride
  = 24.123 ms / 24123 us
```

Expected first-slice result:

```text
DrawOverride reduction:              35% to 60%
ModPrepareOverride reduction:        15% to 35%
BuildIaHash avoided on no-op PSOs:   40% to 70%
Draw-side cluster reduction:         25% to 45%
Total hot path reduction:            14% to 25%
Expected hot path:                   32 ms to 37 ms
Expected FPS from same frame:        3.55 -> 4.0 to 4.7 FPS
```

This is not yet "fully DX11 elegant." It is the draw-side compilation layer
needed before the deeper DX12 `HackerContext` consolidation can be completed.

## Deeper Follow-Up

After this slice:

```text
1. Move replacement preparation behind a DX12ModDecisionCache keyed by:
   PSO + reload generation + PreSkin generation + IA hash state + draw region.

2. Promote IA bindings into DX12IaBindingState with an explicit IA epoch.

3. Replace cheap fingerprint maps in DX12IaReplacementExecutor with
   DX12IaHashDecisionCache.

4. Slim DX12DrawHookFlow so it only routes:
   draw args -> context decision -> original or replacement executor.
```

Definition of done for Phase 4:

```text
Draw no-op PSOs do not enter the old override discovery path.
Draw decision validity is based on mod reload generation, not tracking
generation.
Existing Mod hashes remain stable.
Existing replacement behavior remains intact.
Per-frame draw_decision logs prove cache hits and fast rejects.
build_debug_x64.ps1 succeeds.
```

## Implementation Progress

### 2026-07-05 Phase 4 Slice: Compiled Draw Decision

Implemented files:

```text
src/DirectX12/command/DX12CompiledDrawDecision.hpp
src/DirectX12/command/DX12CommandListRuntimeState.hpp
src/DirectX12/command/DX12CommandListRuntime.hpp
src/DirectX12/command/DX12CommandListRuntime.cpp
src/DirectX12/mod/DX12ModRuntime.hpp
src/DirectX12/mod/DX12ShaderOverrideRuntime.hpp
src/DirectX12/ia/DX12IaReplacementExecutor.hpp
src/DirectX12/utils/DX12ResidentMaintenanceTelemetryUtils.hpp
```

Design landed:

```text
SetPipelineState / Reset
  -> DX12CommandListRuntime::RememberPipelineState
  -> compile DX12CompiledDispatchDecision
  -> compile DX12CompiledDrawDecision

Draw
  -> EnsureDrawDecision
  -> NoopNeverMatches fast reject
  -> otherwise reuse compiled targetPlan and skip state
  -> build IA hash only when compiled targetPlan requires it
  -> keep DX12ModPrepareShaderOverrideReplacement as execution authority
```

Hot-path lifetime fix:

```text
Old draw-side PSO cache lifetime:
  TrackingGeneration

New draw-side PSO cache lifetime:
  pipelineState + DX12ModGetReloadGenerationFast() + active shader override flag
```

Preserved invariants:

```text
IA hash algorithm unchanged.
IA hash inputs unchanged.
Shader hash algorithm unchanged.
TextureOverride hash matching unchanged.
ShaderOverride command-list execution unchanged.
Present replacement behavior unchanged.
Hunting and FrameAnalysis pre-mod draw paths unchanged.
```

New per-frame log category:

```text
DX12ResidentMaintenance category=draw_decision
  cacheHits
  cacheMisses
  builds
  noPso
  noShaderOverride
  noDrawOverride
  needsIaHash
  needsDescriptorMatch
  needsReplacement
  skipOriginal
  fastRejects
  iaHashDraws
  descriptorDraws
```

Expected effect from this implementation:

```text
DrawOverride:              expected 35% to 60% lower
ModPrepareOverride:        expected 15% to 35% lower
BuildIaHash avoid rate:    expected 40% to 70% on no-op PSOs
Total hot path:            expected 14% to 25% lower
Previous hot path:         42.983 ms / 42983 us
Expected hot path:         32 ms to 37 ms
Expected FPS:              3.55 -> 4.0 to 4.7 FPS
```

Verification:

```text
.\build_debug_x64.ps1
Result: All projects built successfully.
DX12 DLL copied to D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll.
```
