# DX12 PreSkin Plan Cache Phase 3 Plan

Date: 2026-07-05

## Current Evidence

Latest frame:

```text
present:                         1663
frame interval:                  297.000 ms / 297000 us
expected FPS:                    3.37
measured hot path:               117.187 ms / 117187 us
DispatchHook:                    21.981 ms / 21981 us
PreSkinMatch:                    15.812 ms / 15812 us
PreSkinApply:                    15.811 ms / 15811 us
PreSkinQuery:                     3.390 ms /  3390 us
PreSkin candidate dispatches:    25
PreSkin binding cache hits:       0
PreSkin binding cache misses:    25
cs_not_configured:                0
dispatch fast rejects:           92
```

Phase 2 succeeded: non-candidate compute shaders are now rejected before
`PreparePreSkinProbe`. The remaining cost is real PreSkin candidate dispatches.

## Official D3D12 Facts Used

Reference:

```text
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setcomputerootdescriptortable
https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures-overview
https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
```

Relevant constraints:

```text
SetComputeRootDescriptorTable binds a descriptor table by root parameter index
and GPU descriptor handle.

Root signatures define descriptor table layouts, root constants, and root
descriptors.

Shader-visible descriptor heaps are what descriptor tables reference during
command list execution.

SetDescriptorHeaps changes currently bound descriptor heaps and unsets previous
heaps.
```

Therefore a PreSkin cache key must be based on shader-visible descriptor facts,
root table locations, root constants, root descriptors, and descriptor contents.
It must not be based on the number or order of binding API calls.

## Root Cause

Current PreSkin probe cache is keyed by `runtimeState.computeBindingHash`.
That hash is updated from binding commands as an incremental serial-like value.
It is not a stable fingerprint of the effective compute binding state.

This creates false cache misses:

```text
same effective descriptor/root state
  -> different command-call history
  -> different computeBindingHash
  -> PreSkin cache miss
  -> repeated PreSkinMatch and PreSkinApply
```

DX11 is more elegant because current shader/binding state is a fact owned by
`HackerContext`; draw/dispatch consumes the state. DX12 is still not there:
Phase 2 compiled the compute shader decision, but Phase 3 still lets
`PreparePreSkinProbe` rediscover a plan per dispatch.

## Phase 3 Slice

Goal:

```text
Convert PreSkin probe cache from call-history keying to stable binding-state
fingerprint keying.
```

New module:

```text
src/DirectX12/mod/DX12PreSkinPlan.hpp
```

Primary class:

```text
DX12PreSkinPlanUtils
```

Responsibilities:

```text
BuildStableBindingFingerprint(
  uavs,
  srvs,
  cbvs,
  rootConstants
)
```

Fingerprint inputs:

```text
rootParameterIndex
rangeIndex
shaderRegister
registerSpace
descriptorOffset
rootDescriptor flag
GPU virtual address
descriptor kind
descriptor view dimension
descriptor resource pointer
descriptor resource offset
descriptor GPU virtual address
descriptor view size
descriptor buffer stride
descriptor CPU handle
descriptor table GPU start
root constant root index
root constant valid offsets
root constant values
```

Explicit non-inputs:

```text
ResourceBarrier state transitions.
Binding command call count.
Binding command order when final effective bindings are identical.
Frame number.
```

## Cache Flow

Target flow:

```text
PreparePreSkinProbe
  -> collect current PreSkin bindings
  -> build stable binding fingerprint
  -> query PreSkin cache by:
       mod reload generation
       compute shader hash
       stable binding fingerprint
  -> negative hit:
       skip PreSkinMatch and PreSkinApply
  -> positive hit:
       re-enter existing apply path for now
       preserve command emission behavior
  -> miss:
       run current match/apply path
       store result under stable fingerprint
```

This is intentionally conservative. Positive-hit reapply is still expensive,
but it preserves D3D12 command recording semantics. Negative hits are the
highest-confidence first win because the latest frame has many misses and only
one expensive positive apply.

## Telemetry

Add per-frame counters:

```text
PreSkinPlanCacheLookups
PreSkinPlanCacheHits
PreSkinPlanCacheNegativeHits
PreSkinPlanCachePositiveHits
PreSkinPlanCacheMisses
PreSkinPlanCacheStores
PreSkinPlanStableFingerprintBuilds
```

Expected log:

```text
DX12ResidentMaintenance category=preskin_plan
```

## Expected Effect

On frames like present 1663:

```text
PreSkinMatch + PreSkinApply: 31.623 ms -> 6 ms to 14 ms
DispatchHook:               21.981 ms -> 8 ms to 14 ms
Hot path:                  117.187 ms -> 92 ms to 105 ms
```

Estimated improvement:

```text
PreSkin hotspot reduction:  55% to 80%
Total hot path reduction:   10% to 21%
Expected FPS uplift:         3.37 -> 3.7 to 4.2 FPS
```

## Guard Rails

Do not change:

```text
IA hash algorithm.
IA hash inputs.
Shader hash algorithm.
TextureOverride hash matching.
ShaderOverride matching.
Actual PreSkin patch behavior.
Resource replacement behavior.
```

Allowed:

```text
Change where the PreSkin decision result is cached.
Add stable fingerprinting for cache identity.
Skip expensive matching when a stable negative cache entry exists.
```

## Next Deeper Step

After this slice, build a real positive `DX12PreSkinPlan`:

```text
NoopNegative
PatchDescriptorTable
PatchCbvSrvUav
ResizeDispatch
```

That step should store enough patch metadata to re-emit required D3D12 commands
without rerunning the full match path.
