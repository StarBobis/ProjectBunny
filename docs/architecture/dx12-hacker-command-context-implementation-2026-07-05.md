# DX12 Hacker Command Context Implementation Plan

## Goal

Move the DX12 hot path closer to the DX11 `HackerContext` architecture:

1. State-setting calls compile stable context decisions.
2. Draw and Dispatch consume already-known decisions.
3. IA hash and PreSkin binding caches use stable state identity instead of per-frame or per-call identity.
4. Frame logs expose the remaining unaccounted CPU time.

The immediate log target is `D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl`.
The latest analysed frame was `present=1459`:

```text
Frame wall:              313.242 ms / 313242 us
Frame interval:          312.978 ms / 312978 us
Expected FPS:              3.19
Present original wait:     0.454 ms /    454 us
ExecuteCommandLists:       0.334 ms /    334 us
Worker task time:         63.745 ms /  63745 us
Worker drain wait:        36.728 ms /  36728 us

SetPipelineState:         10.568 ms /  10568 us
DrawHook:                 10.546 ms /  10546 us
DrawOverride:              7.827 ms /   7827 us
DispatchHook:              5.999 ms /   5999 us
PreSkinQuery:              3.016 ms /   3016 us
ModPrepareOverride:        2.649 ms /   2649 us
BuildIaHash:               0.992 ms /    992 us
```

## Official DX12 Grounding

Microsoft's Direct3D 12 documentation supports moving work out of draw-time
rediscovery:

- Pipeline State Objects bundle most graphics pipeline state into immutable PSO
  objects. The runtime design expects applications to create or select these
  state objects, then bind them cheaply during command recording.
- Command lists record commands explicitly and are later submitted through
  `ID3D12CommandQueue::ExecuteCommandLists`; submit batching has fixed costs,
  so injected work should avoid avoidable command-list churn.
- Fence and queue synchronization are explicit in D3D12. If `Present` and queue
  waits are small while frame wall time is large, the bottleneck is CPU command
  recording or uninstrumented CPU work, not GPU wait.

References queried before implementation:

- https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12
- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization

## First-Principles Diagnosis

The game frame is a pipeline of CPU command recording, queue submission, GPU
execution, and presentation. The log shows GPU-facing waits are small:

```text
Present original wait: 0.454 ms
ExecuteCommandLists:  0.334 ms
Queue wait/signal:    0.045 ms
```

Therefore the 313 ms frame cannot be explained by GPU wait. The CPU-side hot
path is spending time on repeated state interpretation:

1. `SetPipelineState` compiles draw and dispatch decisions repeatedly.
2. Draw still enters `DrawOverride` for every draw under mod mode.
3. IA hash state cache uses per-present and IA epoch identity, so it misses
   even when the stable resource/view region repeats.
4. PreSkin plan cache hits, but PreSkin binding cache misses; this means the
   matching plan is stable while the binding snapshot key is still too volatile.
5. The frame timeline lacks an explicit outside-Present bucket, so the largest
   CPU interval is visible only as frame wall minus known slices.

## DX11 Alignment

DX11 `HackerContext` keeps a current context:

- Shader hash is updated at shader bind time.
- Draw consumes current shader state and draw arguments.
- Resource region hashes use stable `(buffer, offset, size)` cache keys.
- Texture override lookup first does cheap keyed lookup, then optional draw
  context filters.

DX12 must mirror the same design with DX12-native identities:

- PSO pointer + reload generation represents shader/mod decision identity.
- IA resource + offset + view size represents buffer-region identity.
- Root signature + descriptor heap version + table position represents binding
  identity.
- Draw/Dispatch should consume these identities, not rediscover them.

## Deepened Modules

### 1. `DX12PsoDecisionCacheUtils`

Files:

- `src/DirectX12/command/DX12PsoDecisionCacheUtils.hpp`
- `src/DirectX12/command/DX12CommandListRuntime.cpp`
- `src/DirectX12/command/DX12CommandListRuntime.hpp`
- `src/DirectX12/utils/DX12ResidentMaintenanceTelemetryUtils.hpp`

Interface:

- `GetDispatchDecision(pipelineState, hasShaderOverrides, needsPreSkinProbe)`
- `GetDrawDecision(pipelineState, hasShaderOverrides)`
- `ClearIfReloadChanged()`

Implementation:

- Cache compiled dispatch and draw decisions globally.
- Key by PSO pointer, mod reload generation, and relevant global mode flags.
- Keep LRU-ish bounded arrays to avoid unbounded map growth.
- Store decisions by value; command-list runtime copies them into current state.

Why:

- PSO-derived decisions are not command-list-local.
- Recompiling the same decision on every `SetPipelineState` is shallow module
  behaviour: every caller pays for the same policy discovery.

Expected effect:

- `SetPipelineState` hot path: 35% to 70% lower.
- `draw_decision.builds` and `dispatch_decision.builds`: should drop below
  unique PSO count per reload generation.
- Last-frame estimated gain: 4 ms to 7 ms.

### 2. Stable IA Hash State Cache

Files:

- `src/DirectX12/ia/DX12IaHashStateCacheUtils.hpp`
- `src/DirectX12/ia/DX12IaReplacementExecutor.hpp`
- `src/DirectX12/command/DX12CommandListRuntime.cpp`
- `src/DirectX12/command/DX12CommandListRuntime.hpp`

Interface:

- Build an IA cache key from the actual target plan, IA view values, draw
  region arguments, content generation, and resolve generation.
- Remove `presentGeneration`, `iaEpoch`, and `iaContentEpoch` from equality.

Implementation:

- Add `DX12CommandListRuntime::BuildIaHashStateCacheKeyForTargets`.
- Include only target-plan-required IA views.
- Preserve content and resolve generation invalidation.
- Keep command-list local storage, but make entries reusable across frames when
  the same IA region repeats.

Why:

- DX11 region hash cache survives within a frame by stable resource/offset/size.
- Current DX12 key includes per-present and IA epoch values, forcing misses.

Expected effect:

- IA hash cache hit rate should move from `0/22` to a visible nonzero rate on
  repeated draws.
- `BuildIaHash` hot path: 40% to 75% lower on repeated IA regions.
- Last-frame estimated gain: 0.4 ms to 0.8 ms; recent-frame gain can reach
  1 ms to 3 ms.

### 3. Stable PreSkin Binding Cache

Files:

- `src/DirectX12/binding/DX12PreSkinBindingCacheUtils.hpp`
- `src/DirectX12/binding/DX12BindingTracker.cpp`

Interface:

- Cache key should describe the effective compute binding state, not the command
  list pointer or a volatile query serial.

Implementation:

- Remove command-list pointer from the PreSkin binding cache key.
- Do not include descriptor query serial in the key.
- Include compute root signature, heap pointer/version, table base indices,
  root descriptor values, root constants, and root table runtime versions.
- Keep descriptor query serial only as a post-hit safety check when needed.

Why:

- Latest frame has `preskin_plan hits=25` but `preskin_bind_cache hits=0`.
- The PreSkin plan is stable; the binding snapshot key is too volatile.

Expected effect:

- `preskin_bind_cache` hit rate should become nonzero across repeated dispatch
  states.
- `PreSkinQuery + BindCollect`: 30% to 65% lower on repeated compute bindings.
- Last-frame estimated gain: 1 ms to 2 ms.

### 4. Frame Timeline Outside CPU Attribution

Files:

- `src/DirectX12/utils/DX12OuterTimingProbeUtils.hpp`
- `src/DirectX12/command/DX12WrapperRuntimeUtils.hpp`

Interface:

- Extend `DX12FrameTimeline` with an explicit outside-Present field:
  `outsidePresentUs = frameWallUs - presentWallUs`.

Implementation:

- If the timeline already has enough data, emit computed outside time in the
  existing `DX12FrameTimeline` line.
- Do not change command recording behaviour.

Why:

- The current report can prove the frame is CPU-bound, but cannot attribute the
  largest interval precisely.

Expected effect:

- No performance improvement expected.
- Diagnostic quality improves by making unaccounted CPU time explicit.

## Implementation Order

1. Add `DX12PsoDecisionCacheUtils`.
2. Wire `DX12CommandListRuntime::Build*Decision` through the global cache while
   preserving current telemetry.
3. Add stable IA hash-state cache key building and use it in
   `DX12IaReplacementExecutor`.
4. Stabilize PreSkin binding cache key.
5. Add `outsidePresentUs` to timeline output if the current output path permits
   a small local edit.
6. Build with `build_debug_x64.ps1`.

## Continuation Implementation Order

The first implementation pass built successfully, but the PreSkin binding cache
is still declared inside `DX12BindingTracker.cpp`. That keeps cache identity,
LRU storage, and equality rules hidden in a large implementation file, which is
not modular enough for continued DX11-style context deepening.

Continue with these concrete changes:

1. Move PreSkin binding cache key, entry, TLS cache, and equality/LRU helpers to
   `src/DirectX12/binding/DX12PreSkinBindingCacheUtils.hpp`.
2. Keep heap/table resolution in `DX12BindingTracker.cpp` because it depends on
   tracker-local query helpers. The new header should accept already resolved
   table identity and root state fields instead of owning descriptor lookup.
3. Add explicit global PSO cache telemetry calls in `DX12PsoDecisionCacheUtils`
   so the frame log can distinguish command-list-local decision reuse from
   global PSO decision reuse.
4. Add a repository-local log analysis script under `tools/` that reads
   `d3d12_log.jsonl` and prints frame wall time, frame interval, expected FPS,
   function totals, function percentages, cache hit rates, and ASCII bars.
5. Build with `build_debug_x64.ps1` because code changes are made.

Expected additional effect:

- PreSkin cache module depth improves: cache policy is now reusable and reviewable
  from one header instead of buried inside the binding tracker implementation.
- PSO cache telemetry will expose global decision-cache hit rate directly.
- Log report generation time should drop by more than 90% versus manual parsing
  for repeated analysis cycles.

## Safety Rules

- Do not change IA hash algorithm.
- Do not change IA hash input bytes.
- Do not change shader override semantics.
- Do not change PreSkin positive reapply semantics.
- Do not remove existing logs.
- New code and comments must be English.
- No `auto` in new code.
- New utilities live in `.hpp` and end with `Utils`.
- New classes expose public members and functions only.

## Verification

Run:

```powershell
.\build_debug_x64.ps1
```

Expected log deltas after a game run:

```text
SetPipelineState:           35% to 70% lower
DrawOverride:               15% to 35% lower immediately
BuildIaHash:                40% to 75% lower on repeated IA regions
PreSkinQuery + BindCollect: 30% to 65% lower on repeated dispatch bindings
IA hash cache hit rate:     from 0% to nonzero
PreSkin binding hit rate:   from 0% to nonzero
FPS estimate:               3.19 -> 3.5 to 4.3 if worker drain remains similar
```

If cache hit rates remain zero, the next required step is more logging around
the key fields, not another local patch.
