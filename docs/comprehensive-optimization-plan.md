# Comprehensive Optimization Plan — Frame #2586 Analysis

## Current State

| Metric | Value |
|--------|-------|
| Frame wall | 235,005 us (235ms) = 4.3 FPS |
| API scope total | 128,697 us (54.8%) — mostly GPU wait |
| Hook CPU (cpuUs_total) | 2,910 us (1.2%) |
| Log cost | 27,819 us (11.8%) |
| CpuStage sum (fine-grained) | 107,436 us |
| Draws/frame | 1,549 |
| Dispatches/frame | 209 (47 PreSkin probes) |

**Key insight**: The GPU is the bottleneck (Draw wall = 84% of frame). CPU optimization won't increase FPS directly, but it reduces hook overhead, freeing CPU for other work and reducing potential frame pacing jitter.

---

## Optimization Candidates (ranked by impact × feasibility)

### 🥇 P0: IaHash Pipeline — 26.3ms (11.7%)

**Code**: `DX12ResourceTrackerQueries.cpp:572-748`, `DX12IaHashStateCacheUtils.hpp:88-148`

**Root cause**: Every draw call builds a CRC32C hash of vertex/index buffer contents. The expensive parts are:
1. **GPU buffer content read** (`DX12TryGetBufferContent`) — reads mapped buffer memory, cache-miss-heavy
2. **L3 cache LRU eviction** (`store_ia_hash_cache`) — O(4096) scan on every insert
3. **Deep vector copies** — `DX12IaHashState` copies all vertex buffer hashes on cache store

**Proposed optimizations**:

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P0a | Add L0 cache: per-draw-call hash cache keyed by `(drawIndex, bindingSerial)` | Same draw call with same bindings → same hash. Skip GPU buffer read entirely. | **-60%** (~15.8ms) |
| P0b | Replace LRU O(n) scan with clock algorithm | Use a circular `age` increment instead of scanning 4096 entries | **-80%** of store_ia_hash_cache (~2.1ms) |
| P0c | Use `std::move` + emplace instead of deep copy on Store | Avoid `std::vector<DX12IaBufferHash>` copy | **-30%** of store time (~0.8ms) |
| **P0 total** | | | **~18.7ms saved** |

---

### 🥈 P1: PreSkin Dispatch Probe — 20.0ms (8.9%)

**Code**: `DX12DispatchHookFlow.hpp:256-581`

**Root cause**: Each of 47 dispatch probes does:
1. CollectPreSkinBindings (walk descriptor tables)
2. Build binding fingerprint
3. Check probe cache
4. On miss: deep copy 4 vectors to async queue
5. Apply UAV replacement

**Proposed optimizations**:

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P1a | Increase probe cache capacity + add generation-based invalidation | Current cache may be too small or evicting too aggressively | **-30%** misses → ~6ms |
| P1b | Use move semantics for async queue vectors | Instead of copying uavs/srvs/cbvs/constants, std::move them | **-15%** of miss cost (~3ms) |
| P1c | Pre-compute binding fingerprint incrementally | Update fingerprint on SetComputeRoot* instead of rebuilding every probe | **-20%** of fingerprint time (~0.1ms) |
| **P1 total** | | | **~9ms saved** |

---

### 🥉 P2: Log Overhead — 27.8ms (11.8%)

**Code**: `DX12LogManager.hpp`, various call sites

**Root cause**: 2,718 snprintf calls/frame. `formatUs` still 16.8ms even after JSON→key=value conversion. The bottleneck is call count, not format complexity.

**Proposed optimizations**:

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P2a | Batch adjacent log calls into single snprintf | Merge multiple `DebugJsonFunc` calls into one multi-key call per category | **-50%** formatUs (~8.4ms) |
| P2b | Convert remaining JSON call sites (DX12Config, MigotoShaderRegexParse, DX12ModRuntime*, DX12HotPathState) | Init-time only, one-time cost, no per-frame impact | negligible per-frame |
| P2c | Skip empty/zero-value fields | If `dispatch=0`, don't log it | **-15%** format bytes (~2.5ms) |
| **P2 total** | | | **~10.9ms saved** |

---

### 4️⃣ P3: Shader Override Cache Miss — 5.7ms (2.5%)

**Code**: `DX12ShaderOverrideRuntime.hpp:618-700`

**Root cause**: On cache miss, does full config scan + RunShaderOverride + ApplyPendingResourceViews before determining "no change". Lock contention on exclusive `gModLock`.

**Proposed optimizations**:

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P3a | Check negative cache BEFORE acquiring exclusive lock | Avoid lock contention for known-no-match PSOs | **-40%** (~2.3ms) |
| P3b | Pre-allocate TLS vectors instead of heap-alloc on miss | Eliminate `reserve()` + `push_back` per miss | **-20%** (~1.1ms) |
| **P3 total** | | | **~3.4ms saved** |

---

### 5️⃣ P4: RecordComputeUavs — 2.8ms (1.3%)

**Code**: `DX12ModRuntimeState.hpp:254-283`

**Root cause**: `gRecentComputeUavs` uses `std::vector` with `erase(begin(), begin()+excess)` — O(n) shift of all remaining elements per insert when buffer is full.

**Proposed optimizations**:

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P4 | Replace `std::vector` + front-erase with `std::deque` or fixed ring buffer | O(1) pop_front instead of O(n) erase | **-90%** (~2.5ms) |
| **P4 total** | | | **~2.5ms saved** |

---

### 6️⃣ P5: BindCollect P3 (Bulk Descriptor Lookup) — 1.9ms → ~0.5ms

**Code**: `DX12BindingTracker.cpp` (already partially optimized with P1, P2, P4)

**Root cause**: `DX12FindDescriptorSummariesByCpuHandleNoWait` called per-range — fragmented hash lookups.

**Proposed**: Two-pass bulk descriptor lookup (already planned, not yet implemented).

| # | Change | Mechanism | Est. Savings |
|---|--------|-----------|-------------|
| P5 | Single bulk `DX12FindDescriptorSummariesByCpuHandleNoWait` call for all ranges | N small hash lookups → 1 large lookup | **-75%** of remaining (~1.4ms saved) |

---

## Total Expected Impact

| Priority | Optimization | Current Cost | After | Savings |
|----------|-------------|-------------|-------|---------|
| **P0** | IaHash pipeline | 28.9ms | ~10.2ms | **-18.7ms** |
| **P1** | PreSkin probe | 20.0ms | ~11.0ms | **-9.0ms** |
| **P2** | Log overhead | 27.8ms | ~16.9ms | **-10.9ms** |
| **P3** | Shader override miss | 5.7ms | ~2.3ms | **-3.4ms** |
| **P4** | RecordComputeUavs | 2.8ms | ~0.3ms | **-2.5ms** |
| **P5** | BindCollect bulk lookup | 1.9ms | ~0.5ms | **-1.4ms** |
| **Total** | | **87.1ms** | **~41.2ms** | **-45.9ms (52.7%)** |

> Note: These are CpuStage cumulative timings, not wall-clock. Actual frame wall reduction depends on how much overlaps with GPU work. The hook CPU total (cpuUs_total) is only 2.9ms, but the fine-grained stages include work on multiple threads and overlapping operations.

---

## Execution Order

| Order | Task | Effort | Risk | Dependencies |
|-------|------|--------|------|-------------|
| 1 | P4: deque/ring buffer for RecordComputeUavs | Small | Low | None |
| 2 | P0b: Clock eviction for IaHash cache | Small | Medium | None |
| 3 | P0a: Per-draw-call L0 IaHash cache | Medium | Medium | P0b |
| 4 | P3a: Early negative cache check | Small | Low | None |
| 5 | P5: Bulk descriptor lookup | Medium | Medium | P1+P2+P4 already done |
| 6 | P1a: Increase probe cache capacity | Small | Low | None |
| 7 | P1b: Move semantics for async queue | Small | Medium | P1a |
| 8 | P2a: Batch log snprintf calls | Large | Low | None |
| 9 | P0c: Move semantics for IaHash cache store | Small | Low | None |
| 10 | P3b: TLS vectors for shader override | Small | Low | None |

**Quick wins (P4, P0b, P3a, P1a)**: ~4 tasks, small effort, ~7ms savings together. Can be done in one session.
