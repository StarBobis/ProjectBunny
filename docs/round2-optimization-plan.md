# Round 2 Optimization Plan — Deep Root-Cause Analysis

## Current State (Frame #2709, after Round 1)

| Metric | Value | vs Round 1 |
|--------|-------|-----------|
| Frame wall | 196ms | -16.6% |
| Expected FPS | 5.1 | +18.6% |
| Hook CPU | 2,226 us | -23.5% |
| Log cost | 20,628 us | -25.9% |
| CpuStage total | 125,574 us | — |

### Remaining TOP 3 Hotspots

| # | Hotspot | Cost | % of CpuStage | Root Cause Found |
|---|---------|------|:---:|---|
| 1 | `build_ia_hash_state` | 51.3ms | 40.9% | ✅ (see below) |
| 2 | `dispatch_prepare_probe` | 21.8ms | 17.3% | ✅ (see below) |
| 3 | `apply_replacement_match` | 13.8ms | 11.0% | GPU-side, not CPU |

---

## 🔬 Root Cause #1: IaHash Cross-Frame Cache Always Misses

### Finding
`DX12IaHashStateCacheKey` (at `DX12IaHashStateCacheUtils.hpp:11-36`) includes:
- `contentGeneration` — global atomic counter, **bumps on every CPU buffer upload**
- `resolveGeneration` — global atomic counter, **bumps on any GPU VA change**

Both are monotonically increasing. On frame N+1, even with identical buffer content, the cache key is different → 100% cross-frame miss rate. The 240 cache hits (21%) are ALL **intra-frame** (same draw call within the same frame).

### The Fix: Strip generations from cache key, verify via inner caches
The per-buffer content hash caches (L1/L2 TLS at `DX12ResourceTrackerQueries.cpp:627`) already independently track invalidation via their own generation checks. They protect against stale content.

**Plan**: Add a second cache store that keys WITHOUT `contentGeneration`/`resolveGeneration`. On hit, verify the inner L1/L2 TLS caches are still valid. If valid, reuse. If invalidated, recompute.

```cpp
// Current key (cross-frame never hits):
key = (PSO, reloadGen, contentGen, resolveGen, targetPlan, iaView, drawArgs)

// New key (cross-frame CAN hit):
key = (PSO, targetPlan, iaView, drawArgs)  // omit generations
```

### Expected Impact
- Hit rate: 21% → **~75%** (most static geometry reuses buffers across frames)
- `build_ia_hash_state`: 51.3ms → **~13ms** (-38ms, **-75%**)

---

## 🔬 Root Cause #2: PreSkin Fingerprint Is NOT Cross-Frame Stable

### Finding
`BuildStableBindingFingerprint` at `DX12PreSkinPlan.hpp:30-52` includes:
- `descriptor.resource` — **ID3D12Resource pointer**
- `descriptor.gpuVirtualAddress` — **GPU VA**
- `descriptor.resourceOffset`, `descriptor.viewSize`

GPU VAs and resource pointers change every frame when games use ring-buffer/sub-allocation patterns. The function name says "stable" but it's **intra-frame stable only**. Cross-frame: 2% hit rate.

### The Fix: Strip volatile fields from fingerprint
Create a truly cross-frame stable fingerprint:
```cpp
UINT64 BuildCrossFrameStableFingerprint(
    shaderRegister,      // stable
    registerSpace,       // stable  
    rootParameterIndex,  // stable
    rangeType,           // stable (UAV/SRV/CBV)
    viewFormat,          // stable if same resource type
    viewDimension,       // stable
    // OMIT: resource pointer, GPU VA, resourceOffset
)
```

When a fingerprint match hits, verify the actual GPU binding is still valid (quick pointer comparison). If invalidated, fall through to full probe.

### Expected Impact
- Hit rate: 2% → **~60%** (most dispatches reuse same shader+binding pattern)
- `dispatch_prepare_probe`: 21.8ms → **~9ms** (-12.8ms, **-59%**)
- Reduces `match_cs_miss` (612us × 46 = indirect savings)

---

## 🔬 Opportunities #3-5: Smaller Wins

### #3: Batch log calls (formatUs: 12.4ms → 3ms)
`DX12CpuStageTiming` and `DX12HotPathTiming` each emit one `JsonFunc` per bucket/stage → ~250 individual `vsnprintf` calls per frame at ~5.5µs each. Batch into single calls.

### #4: IaHash key hashing optimization
The `DX12IaHashStateCacheKeyHash` mixes 11 fields through multiple multiply/xor rounds. Could cache a pre-computed hash or simplify.

### #5: Remaining JSON log call sites
Some init-time log calls (DX12Config, MigotoShaderRegexParse) still use JSON format — negligible per-frame impact but ugly.

---

## Execution Plan

| # | Task | Files | Effort | Risk | Est. Savings |
|---|------|-------|--------|------|-------------|
| **P0** | IaHash cross-frame cache (strip generations) | `DX12IaReplacementExecutor.hpp`, `DX12IaHashStateCacheUtils.hpp` | Medium | Medium | **-38ms** (75%) |
| **P1** | PreSkin stable fingerprint (strip GPU VA) | `DX12PreSkinPlan.hpp`, `DX12ModRuntime.cpp` | Medium | Medium | **-13ms** (59%) |
| **P2** | Batch log timing calls | `DX12OuterTimingProbeUtils.hpp`, `DX12HotPathTimingUtils.hpp` | Small | Low | **-9ms** (73%) |
| **P3** | Simplify IaHash key hash | `DX12IaHashStateCacheUtils.hpp` | Small | Low | **-2ms** (4%) |

### Run Order
1. **P1 first** — PreSkin stable fingerprint (independent, clear root cause)
2. **P0 second** — IaHash cross-frame cache (depends on understanding of inner cache invalidation)
3. **P2 third** — Batch log calls (mechanical, low risk)
4. **P3 last** — Hash simplification

### Total Expected
```
CpuStage:  125ms → ~58ms  (-67ms, -54%)
Hook CPU:  2.2ms → ~1.5ms (minor)
Log:       20.6ms → ~11ms (-9.6ms)
Frame:     196ms → ~196ms (GPU-bound, CPU gains don't increase FPS directly)
```
