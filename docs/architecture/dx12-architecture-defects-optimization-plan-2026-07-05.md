# DX12 Architecture Defect Analysis & Optimization Plan — 2026-07-05 (Round 2)

## 0. First-Principles Root Cause Analysis

### Why GPU Waits for CPU

In a properly pipelined DX12 application:

```
CPU Frame N: [record CLs] → Submit → [record CLs for N+1] → Present
GPU Frame N:                [exec N] → idle waiting for N+1 →

Result: CPU is 1 frame ahead, GPU never idles (pipeline depth = 1 frame)
```

In our hooked application:

```
CPU Frame N: [game record + HOOK 30-75ms] → Submit → [game logic] → Present [HOOK 0.7ms]
GPU Frame N:                                                 [exec N, ~8ms] → IDLE 342ms waiting for N+1
```

**The GPU is idle because:**
1. Game's own CPU work between API calls (physics, AI, etc.) takes ~275ms — this is NOT our problem
2. Our hook adds 30-75ms of processing DURING recording (per-draw/dispatch hooks) — this IS our problem
3. The Present hook work is only 0.7ms — negligible

**The root cause of GPU wait is NOT the Present hook (0.65ms) but the per-draw and per-dispatch hook overhead (30-75ms accumulated across 7000+ API calls per frame).**

### Why Hook Overhead Grows Over Time

The feedback loop:
1. Resources accumulate (16818 live) → data structures grow
2. Per-draw GPU VA lookups scan growing structures → each draw is slower
3. Worker thread processes more events per frame → WorkerTask grows (1.5ms → 57ms)
4. PreSkinProbe cache fully clears → burst of cold-start misses → 14-43ms per miss
5. Total API time per frame grows (1.7ms → 75ms)
6. Longer frames → more resources created per frame → goto 1

## 1. Defect Priority Ranking

### DEFECT-0 [CRITICAL - NEW]: PreSkinProbe Cache-Miss Blocks Render Thread (13-43ms)

**Evidence**: 45 spikes, ALL cache=miss, max 43ms single call, Dispatch per-frame cost 5-27ms

**Root cause**: `PreparePreSkinProbe()` runs synchronously on the render thread. Cache miss path does:
1. CollectPreSkinBindings (DX12BindingTracker.cpp:571-975) — deep struct copy + descriptor scan
2. DX12ModApplyPreSkinningUavReplacement (DX12PreSkinRuntime.hpp:1714-1912) — exclusive gModLock + SRV matching
3. Descriptor ring heap allocation (DX12ModRuntime.cpp:1327) — CreateDescriptorHeap driver call (10+ms)
4. CBV Map/Read/Patch (DX12PreSkinRuntime.hpp:182-189) — Map() on CBV resource

**Fix**: **Async PreSkinProbe**. Move computation off the critical path:
- Render thread: check cache → HIT: apply fast → MISS: skip probe, queue async compute
- Worker thread: compute PreSkinProbe result → populate cache
- Render thread next Dispatch: cache HIT → apply fast

**Impact**: Dispatch per-frame cost drops from 5-27ms to <1ms. Eliminates ALL 13-43ms spikes.
**Mod safety**: First encounter of new binding skips probe (same as negative cache today). Applies on next frame. Acceptable trade-off — Rendering is correct, mod takes effect with 1-frame delay.

### DEFECT-1 [CRITICAL - P0 DONE]: O(n) Fallback Linear Scan in GPU VA Resolution

Fixed in previous round. See [p0-fix-implementation-plan-2026-07-05.md](p0-fix-implementation-plan-2026-07-05.md).

### DEFECT-2 [CRITICAL - P0 DONE]: gBufferResolveCache PurgeRangeLocked Walks Entire Cache

Fixed in previous round.

### DEFECT-3 [HIGH]: PreSkinProbe Cache Full-Clear Causes Cold-Start Storms

**File**: DX12ModRuntime.cpp, line 1224 (full cache clear on mod reload)

**Problem**: Cache capacity is 4096. When full, ALL entries are cleared. Every subsequent Dispatch with unique binding triggers the full miss path (13-43ms). This is the "cold start storm" — 45 consecutive frames hit 13-43ms spikes.

**Fix**: Replace `.clear()` with LRU eviction. Keep the 4096 most recently used entries. Never fully clear the cache except on actual mod config change (reload generation bump).

**Impact**: Eliminates cold-start storms. Cache hit rate stays >95% instead of oscillating 0% → 100%.

### DEFECT-4 [HIGH]: Exclusive gModLock During PreSkinProbe Serializes Dispatch

**File**: DX12PreSkinRuntime.hpp, line 1768

**Problem**: The ENTIRE match+apply phase holds `gModLock` exclusive. If two threads record command lists with Dispatch calls, they serialize on this lock. Each waits for the other to finish 14-43ms of work.

**Fix**: Split the lock into read phase (shared) and write phase (exclusive):
- Read phase: look up caches (shared lock) — 99% of calls
- Write phase: populate caches, allocate descriptor heaps (exclusive lock) — 1% of calls

### DEFECT-5 [MEDIUM]: Worker Thread Processes Events One-at-a-Time

**File**: DX12ResourceCollectQueue.cpp

**Problem**: Each resource creation/update event is dequeued and processed individually. With 7000+ API calls per frame, the worker processes 7000+ individual events. Each event acquires/releases locks, invalidates caches, etc.

**Fix**: Batch dequeue — process up to 64 events per lock acquisition. This amortizes lock overhead and cache invalidation cost across events.

**Impact**: WorkerTask drops 3-5x for same event volume. Expected: 57ms → 15ms.

### DEFECT-6 [MEDIUM]: gResourceLock Contention on Multi-Threaded Recording

**File**: DX12ResourceTracker.cpp:20

**Problem**: Single SRWLOCK for all resource data structures. Every GPU VA resolution, resource create, and cache update contends.

**Fix**: Shard by GPU VA high bits (64 shards). Each shard has independent lock. Lookups only lock one shard.

## 2. Implementation Plan

### Phase A: Async PreSkinProbe (P0, highest impact)

**Goal**: Eliminate 13-43ms blocking Dispatch calls.

**Design**:

```
// In DX12DispatchHookFlow::PreparePreSkinProbe

// Step 1: Check cache (fast path, shared lock)
if (DX12ModGetCachedPreSkinProbeResult(...)) {
    // Cache HIT — apply patches synchronously (fast, ~100us)
    probe->applied = true;
    return;
}

// Step 2: Cache MISS — queue async computation, skip probe this frame
DX12PreSkinProbeAsyncRequest request;
request.csHash = probe->computeShaderHash;
request.bindingFingerprint = bindingFingerprint;
request.uavs = probe->uavs;  // Move, don't copy
request.srvs = probe->srvs;
request.cbvs = probe->cbvs;
request.rootConstants = probe->rootConstants;
DX12PreSkinAsyncQueue::Enqueue(std::move(request));

// Step 3: Return immediately — probe not applied this frame
probe->applied = false;
return;
```

**Files to create**:
- `mod/DX12PreSkinAsyncQueue.hpp` — Lock-free SPSC queue for PreSkinProbe requests
- `mod/DX12PreSkinAsyncWorker.hpp` — Worker thread that processes requests

**Files to modify**:
- `command/DX12DispatchHookFlow.hpp` — Add async path in PreparePreSkinProbe
- `mod/DX12ModRuntime.cpp` — Add async worker startup/shutdown

### Phase B: PreSkinProbe LRU Cache (P0)

**Goal**: Eliminate cold-start storms from full cache clear.

**Design**: Replace `std::unordered_map` full clear with LRU eviction using `std::list` + `std::unordered_map` (linked hash map pattern).

**Files to modify**:
- `mod/DX12ModRuntime.cpp` — Replace cache clear with LRU eviction

### Phase C: Worker Batching (P1)

**Goal**: Reduce WorkerTask from 57ms to ~15ms.

**Design**: Batch dequeue in `DX12ResourceCollectQueue::WorkerThreadProc`. Process groups of 64 events per lock cycle.

**Files to modify**:
- `resource/DX12ResourceCollectQueue.cpp`

## 3. Hash Algorithm Invariance Guarantee

All changes above do NOT modify:
- `crc32c_append` / `HashBytes` — unchanged
- `DX12HashBufferView` / `DX12HashIaBufferView` — unchanged
- `DX12ResolveBufferResourceByGpuVa` — unchanged
- `DX12FindDescriptorSummaryByCpuHandle` — unchanged
- PreSkinProbe cache KEY generation (`BuildStableBindingFingerprint`) — unchanged

**The async PreSkinProbe computes the SAME result as the sync path**, it just computes it on a different thread. The cache population uses the same key and value types. Mod/Dump/Hunting see identical data.

## 4. Expected Performance Improvement

| Phase | Change | Current | Target | Improvement |
|-------|--------|---------|--------|-------------|
| A | Async PreSkinProbe | Dispatch 5-27ms/frame | <1ms/frame | **5-27x** |
| B | LRU cache | 45 cold-start spikes | 0 spikes | **Eliminates spikes** |
| C | Worker batching | WorkerTask 57ms | ~15ms | **3.8x** |
| P0 (done) | Data structures | Draw 9-35ms/frame | 2-7ms/frame | **3-5x** |

**Combined expected improvement**: 
- Good frames: 10ms → 8ms (20% faster)
- Bad frames: 350ms → 50ms (7x faster)
- PreSkinProbe spikes: eliminated entirely
- WorkerTask degradation: stabilized at 15ms instead of growing to 57ms

## 5. Why This Is Not a Patch — It's a Fundamental Fix

Current architecture: **Synchronous interception** — every API call blocks on hook processing.
Proposed architecture: **Lazy evaluation** — record intent synchronously, compute result asynchronously.

This shifts the architecture from:
```
Intercept → Process → Forward (serial, blocking)
```
to:
```
Intercept → Record intent → Forward immediately (fast, non-blocking)
                              ↓
                    Worker: Process intent → Cache result
```

The same pattern can be extended to other heavy operations in the future (IA hash pre-computation, binding analysis, etc.).

## 6. Safety: Why Mod Still Works

The async PreSkinProbe trades a **1-frame delay in mod application** for **eliminating 13-43ms pipeline stalls**.

- Frame N: New binding encountered → cache miss → skip probe → mod NOT applied this frame
- Frame N+1: Same binding → cache HIT → apply patches → mod applied

This is acceptable because:
1. The game renders correctly regardless (PreSkinProbe is an optimization pass, not a correctness requirement)
2. The previous frame didn't have the mod either, so there's no visual pop
3. At 60 FPS, a 1-frame delay is imperceptible (16ms)
4. Current behavior: cache miss blocks the frame for 14-43ms anyway, which is FAR more visible
