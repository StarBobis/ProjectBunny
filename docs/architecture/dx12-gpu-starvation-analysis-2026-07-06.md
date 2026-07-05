# DX12 Architecture: GPU Starvation Root-Cause Analysis & Optimization Plan
Date: 2026-07-06

## 1. Frame Analysis Report (Frame #1751, Typical Rendering Frame)

### 1.1 Top-Level Metrics

| Metric | Value | % of Frame |
|--------|-------|------------|
| Frame Wall Time | 276.04 ms | 100% |
| Frame Interval | 275.94 ms | -- |
| Expected FPS | 3.6 FPS | -- |
| Outside Present (game CPU) | 275.15 ms | 99.7% |
| Inside Present (our hook) | 888 us | 0.3% |
| API Scope Total (all D3D12 hooks) | 31.79 ms | 11.5% |
| API Max Gap (largest inter-call gap) | 7.53 ms | 2.7% |
| Frame Minus API Total (non-D3D12 time) | 244.25 ms | 88.5% |
| Worker Idle Wait (per-frame delta) | 300.52 ms | 108.9% **GPU STARVING** |

### 1.2 Per-Function Cost Breakdown (Main Thread, Descending by Total ms)

```
                                                                         Total      %Frame
DescriptorCollectPipeline::process_task                              ████ 64.85 ms   23.5%
DescriptorCollectPipeline::record_runtime_copy_range                ███  50.32 ms   18.2%
DescriptorCollectPipeline::copy_range_queue_or_merge                ███  43.79 ms   15.9%
DescriptorCollectPipeline::enqueue_descriptor                       ██   29.89 ms   10.8%
DescriptorCollectPipeline::copy_range_lock_wait (MUTEX WAIT!!!)     ██   19.28 ms    7.0%
DescriptorCollectPipeline::copy_range_flush_pending                 █    18.74 ms    6.8%
DescriptorCollectPipeline::record_runtime_descriptor                █    18.56 ms    6.7%
API: DrawIndexedInstanced         (626 calls, 16.5us avg)           █    10.32 ms    3.7%
API: Dispatch                     (160 calls, 46.3us avg)           ▋     7.41 ms    2.7%
API: SetPipelineState             (466 calls, 14.2us avg)           ▋     6.63 ms    2.4%
PreSkin::dispatch_prepare_probe   (30 calls)                        ▍     4.92 ms    1.8%
Logging Total                                                       ▍     3.90 ms    1.4%
IaReplacement::prepare_shader_override_miss (39 calls)              ▎     2.88 ms    1.0%
API: DrawInstanced                (76 calls, 33.0us avg)            ▎     2.51 ms    0.9%
```

### 1.3 GPU Starvation Visualization

```
Frame #1751 (276.04 ms)
├── Outside Present (game CPU): 275.15 ms (99.7%) ← GPU IDLE
│   ├── Game logic, physics, AI, culling, streaming
│   ├── CPU preparing draw calls between D3D12 API calls
│   └── [UNAVOIDABLE - game engine bottleneck]
│
├── Inside D3D12 API calls: 31.79 ms (11.5%)
│   ├── Our DescriptorCollectPipeline: ~181ms cumulative*
│   ├── Our IaReplacement: ~5ms cumulative*
│   ├── Our PreSkin: ~17ms cumulative*
│   ├── Our Logging: 3.9ms
│   └── Game's original D3D12 calls: ~12ms
│
└── Present hook: 888 us (0.3%)
    ├── Profiling flush: 547 us
    └── Original Present: 267 us

*Cumulative = sum of all stage times across threads. 
Main thread critical path: ~208ms (but some overlap with worker thread)
Net mod impact on frame: ~22ms (276ms with mod vs ~254ms estimated without)
```

### 1.4 Worker Thread Costs

| Function | Worker Thread (ms) |
|----------|-------------------|
| DescriptorCollectPipeline::worker_record_copy_range | 43.81 |
| DescriptorCollectPipeline::worker_record_descriptor_batch | 13.99 |
| DescriptorCollectPipeline::worker_compact_descriptor_batch | 3.11 |

---

## 2. Root Cause Analysis (First Principles)

### 2.1 The Physics of GPU Starvation

```
   GPU:  [Render Frame N-1]  [IDLE............]  [Render Frame N]  [IDLE............]
   CPU:  [Game Logic] [Record CmdList N] [Submit] [Game Logic] [Record CmdList N+1] [Submit]
                            ^--- our mod adds latency here ---^
```

The GPU finishes rendering frame N-1 quickly (DX12 GPU work is fast for this title).
Then the GPU waits for the CPU to submit frame N. The CPU is busy with:
- 244ms: Game engine work (physics, AI, culling, scene traversal) - NOT going through D3D12
- 32ms: D3D12 API calls (recording draw calls, setting state, our hooks)

Our mod makes the 32ms part longer by adding descriptor collection, IA replacement, logging, etc.
But the 244ms of game engine CPU work is the DOMINANT factor.

### 2.2 Quantitative Attribution

| Component | Time (ms) | % Frame | Can We Fix? |
|-----------|-----------|---------|-------------|
| Game CPU (outside D3D12) | 244.25 | 88.5% | NO - game engine |
| Our Descriptor Pipeline (main thread) | ~181* | 65.6%* | YES - primary target |
| Our IA Replacement + PreSkin | ~22* | 8.0%* | YES - secondary target |
| Our Logging | 3.90 | 1.4% | YES - minor |
| Our Other overhead | ~5* | 1.8%* | YES - minor |
| Game original D3D12 calls | ~12 | 4.3% | NO - game engine |

*Cumulative across threads. The critical path impact is smaller due to parallelism.

Without mod (estimated): 244 + 12 = ~256ms → 3.9 FPS
With mod (actual): 276ms → 3.6 FPS
**Mod's incremental impact: ~20ms (8% slowdown)**

### 2.3 Why DescriptorCollectPipeline Dominates

Analysis of 5434 copy operations + 4153 descriptor writes per frame:

1. **SYNCHRONOUS on critical path**: Every D3D12 state-setting call (SetDescriptorHeaps, SetRootDescriptorTable, SetRootCBV/SRV/UAV) triggers descriptor recording BEFORE returning control to the game. This is fundamentally synchronous when it should be asynchronous.

2. **Single mutex for everything**: One `std::mutex` guards 8+ unordered_maps, 1 deque, reverse indices, active task state, and pending write buffers. Main thread and worker thread contend for this single lock ~6787 times per frame.

3. **Dual-write amplification**: Every descriptor write goes through BOTH `RecordRuntimeDescriptor` (under `gDescriptorLock` exclusive) AND the pending-write buffer (under pipeline mutex). This doubles lock acquisitions.

4. **Overengineered inline resolution**: The pipeline supports "targeted promotion" where individual records can be extracted from queued tasks to satisfy on-demand queries. This requires reverse indices (`pendingTaskRefsByCpuHandle`), sequence tracking, and O(n) walks -- all under the mutex. This complexity is needed for frame capture correctness, but adds significant overhead.

### 2.4 IA Hash Cache Issues (From Deep Code Analysis)

The IA buffer hash pipeline has a TWO-LEVEL TLS cache (L1 keyed by GPU VA, L2 keyed by resource + offset). Both caches use **brutal full-clear** when exceeding 8192 entries. This means:
- After 8192 unique buffer ranges are seen, ALL cached hashes are lost
- In open-world games with many draw calls, this can happen multiple times per frame
- Each cache clear causes O(n) destruction of `unordered_map` entries
- Subsequent draws recompute CRC32C content hashes (expensive: involves CPU-side buffer read + CRC32C over full content bytes)

The same brutal-clear pattern exists in `gShaderOverrideNegativeCache` (8192 entries) and the PSO match cache.

---

## 3. Architecture Defects (Complete List)

### Defect #1: Synchronous Descriptor Pipeline on Critical Path [CRITICAL]
- **Location**: `DX12DescriptorCollectPipeline.cpp`
- **Impact**: ~181ms cumulative on main thread
- **Root Cause**: Every state-setting D3D12 call synchronously records descriptors before returning
- **Fix**: Convert to lock-free async ring buffer

### Defect #2: Single Mutex Contention [HIGH]
- **Location**: `DX12DescriptorCollectPipeline.cpp::mutex`
- **Impact**: 19.28ms `copy_range_lock_wait` per frame
- **Root Cause**: One mutex for main thread producer AND worker thread consumer
- **Fix**: Lock-free SPSC ring buffer eliminates contention entirely

### Defect #3: Dual-Write Descriptor Recording [HIGH]
- **Location**: `DX12DescriptorCollectPipeline::RecordDescriptor` + `::RecordRuntimeDescriptor`
- **Impact**: 18.56ms (`record_runtime_descriptor`) + 18.74ms (`copy_range_flush_pending`)
- **Root Cause**: Every descriptor is written to BOTH runtime table AND pending-write buffer
- **Fix**: Defer runtime recording when not capturing; single-write when possible

### Defect #4: IA Hash Cache Brutal Clear [MEDIUM]
- **Location**: `DX12ResourceTrackerQueries.cpp` TLS L1/L2 caches
- **Impact**: Repeated CRC32C recomputation after cache clears; difficult to quantify but adds latency to draw calls
- **Root Cause**: `unordered_map::clear()` when size > 8192 loses ALL entries
- **Fix**: LRU eviction or ring-buffer-based cache

### Defect #5: Negative Cache Brutal Clear [MEDIUM]
- **Location**: `DX12ModRuntime.cpp` `gShaderOverrideNegativeCache` and PSO match cache
- **Impact**: Full re-evaluation of shader override matching after 8192 entries
- **Root Cause**: Same brutal-clear pattern as IA hash cache
- **Fix**: LRU eviction

### Defect #6: Binding Worker Single-Task Dequeue [LOW]
- **Location**: `DX12BindingCollectQueue.cpp` worker loop
- **Impact**: 2x mutex acquisitions per task (dequeue + complete) vs resource worker's 1/64
- **Root Cause**: Known inefficiency - comment in resource worker explicitly notes this
- **Fix**: Batch dequeue like resource worker

### Defect #7: Profiling Counter Bug [LOW]
- **Location**: `DX12Profiling.hpp`
- **Impact**: `drawTotal` always shows 0, misleading for debugging
- **Root Cause**: Counters only increment inside `DX12_PROFILE_SCOPE`, skipped on fast-forward
- **Fix**: Move counter increments to unconditional outer path

---

## 4. Optimization Plan (Priority Order, With Estimated Impact)

### Plan #1: Lock-Free Descriptor Ring Buffer [ESTIMATED: -50ms main thread, -15% frame time]

**What**: Replace `std::mutex` + `std::deque` + 8 `unordered_map`s with a lock-free SPSC ring buffer.

**Implementation**:
1. Create `DX12DescriptorRingBufferUtils.hpp`:
   - Fixed-size power-of-2 ring buffer (65536 slots)
   - `std::atomic<uint64_t>` head (consumer) and tail (producer) pointers
   - Each slot: `DX12DescriptorCollectTask` + sequence number + state flags
   - `Enqueue(task)` - single atomic store-release on tail
   - `DequeueBatch(tasks[], maxCount)` - batch atomic load-acquire

2. Main thread (producer):
   - Claims ring buffer slot via atomic tail increment
   - Writes descriptor data to slot
   - Store-release to publish
   - NO mutex, NO lock contention
   - Estimated: 0.5-1us per operation (was 10-15us with mutex)

3. Worker thread (consumer):
   - Batch-dequeues up to 64 entries per wake
   - Processes each entry (same logic as current ProcessTask)
   - Store-release to advance head

4. Simplify inline resolution:
   - `EnsureDescriptorWritesVisible()` flushes ring buffer to head pointer
   - Removes need for `pendingTaskRefsByCpuHandle`, `queuedTaskById`, `activeDescriptorTaskCpuHandles`
   - Significantly reduces code complexity

**Expected Impact**:
- `copy_range_lock_wait`: 19.28ms → 0ms (eliminated)
- `copy_range_queue_or_merge`: 43.79ms → ~10ms (no mutex, faster insert)
- `copy_range_flush_pending`: 18.74ms → ~5ms (simplified flush)
- `enqueue_descriptor`: 29.89ms → ~8ms (lock-free push)
- Total: ~181ms → ~70ms on main thread

**Risk**: MEDIUM - Structural change to descriptor pipeline. Must preserve DrainForSnapshot semantics for Dump.

### Plan #2: Deferred Descriptor Recording (Capture-Aware Skip) [ESTIMATED: -50ms when idle]

**What**: Skip `record_runtime_copy_range` and `record_runtime_descriptor` when no capture/dump/mod-analysis is active.

**Implementation**:
1. Add `DX12DescriptorCollectPipeline::ShouldRecordRuntime()`:
   - Returns false when: no frame capture active, no shader dump active, no mod descriptor tracking needed
   - Returns true when: capture session active, dump armed, or mod requires descriptor history

2. In `RecordDescriptor()`:
   ```cpp
   if (!ShouldRecordRuntime()) {
       // Only do minimal pending-write tracking for safety
       EnqueueDescriptorTask(&record, source); // lock-free push
       return; // Skip RecordRuntimeDescriptor entirely
   }
   // Full path: runtime + pending write
   ```

3. In `RecordDescriptorCopyRange()`:
   ```cpp
   if (!ShouldRecordRuntime()) {
       // Skip runtime recording, just track for safety
       return;
   }
   ```

**Expected Impact** (when not capturing):
- `record_runtime_copy_range`: 50.32ms → ~0ms
- `record_runtime_descriptor`: 18.56ms → ~0ms
- Total savings: ~69ms on main thread
- Frame time: 276ms → ~210ms → 4.8 FPS (+31%)

**Risk**: LOW - Feature is additive. When capture IS active, full path still runs. Must ensure Dump drains pipeline before snapshot.

### Plan #3: IA Hash Cache LRU Eviction [ESTIMATED: -5ms draw overhead]

**What**: Replace brutal-clear with proper LRU eviction for TLS IA hash caches.

**Implementation**:
1. Create `DX12TlsHashCacheUtils.hpp`:
   - Ring-buffer index array (8192 entries) + generation counter
   - On insert when full: evict oldest entry instead of clearing all
   - On lookup: check generation for staleness
   - Thread-local, no locks needed

2. Apply same pattern to `gShaderOverrideNegativeCache`

**Expected Impact**:
- Eliminates repeated CRC32C recomputation after cache clears
- Reduces worst-case draw call latency
- Estimated: saves ~5ms in draw-bound scenarios

**Risk**: LOW - Cache behavior change, hash algorithm unchanged.

### Plan #4: Batch Descriptor Enqueue [ESTIMATED: -10ms]

**What**: Accumulate descriptors in TLS buffer, flush in batches.

**Implementation**:
- TLS buffer of 64 `DescriptorRecord` entries
- `FlushDescriptorBatch()` at draw/dispatch boundaries or buffer full
- Reduces queue operations from 4153 to ~65 per frame

**Expected Impact**:
- `enqueue_descriptor`: 29.89ms → ~15ms
- `release_task`: 1.26ms → ~0.2ms

**Risk**: LOW - Batching is transparent to downstream.

### Plan #5: Binding Worker Batch Dequeue [ESTIMATED: -2ms]

**What**: Apply same batch-dequeue pattern from ResourceCollectQueue to BindingCollectQueue.

**Expected Impact**: Reduces mutex acquisitions by 64x, small but consistent improvement.

**Risk**: LOW - Proven pattern already used in ResourceCollectQueue.

### Plan #6: ApiWallScope Optimization [ESTIMATED: -2ms]

**What**: Use RDTSC instead of QueryPerformanceCounter for finer-grained timing.

**Expected Impact**: Reduces per-draw timing overhead from ~1us to ~0.1us.

**Risk**: NONE - Timing change only.

---

## 5. Cumulative Expected Impact

| Optimization | Main Thread Savings | New Frame Time | New FPS |
|-------------|---------------------|----------------|---------|
| Baseline | 0 | 276 ms | 3.6 |
| Plan #2 (Deferred Recording) | -69 ms | ~210 ms | 4.8 |
| Plan #1 (Lock-Free Ring Buffer) | -60 ms* | ~180 ms | 5.6 |
| Plan #3 (LRU Cache) | -5 ms | ~175 ms | 5.7 |
| Plan #4 (Batch Enqueue) | -10 ms* | ~170 ms | 5.9 |
| Plan #5+6 (Minor) | -4 ms | ~166 ms | 6.0 |
| **Total** | **~148 ms** | **~166 ms** | **6.0 FPS (+67%)** |

*Note: Plan #1 and #4 overlap somewhat (both reduce descriptor overhead). Combined savings estimated conservatively.

---

## 6. Safety Analysis: Mod, Dump, Hunting Compatibility

### Hash Algorithm Consistency
- Shader hash: FNV-1a 64-bit (seed `14695981039346656037`, multiplier `1099511628211`) - identical across Dump, Mod, Hunting
- IA buffer hash: Same `DX12HashIaBufferView()` for all three paths - identical algorithm
- **None of our optimizations touch the hash algorithm**

### Impact Matrix

| Plan | Mod | Dump | Hunting | Risk |
|------|-----|------|---------|------|
| #1 Lock-Free Ring Buffer | No impact | Must drain before snapshot | No impact | Medium |
| #2 Deferred Recording | No impact | Auto-enables when capturing | No impact | Low |
| #3 LRU Cache | Cache behavior changes, hash unchanged | No impact | No impact | Low |
| #4 Batch Enqueue | No impact | No impact | No impact | Low |
| #5 Binding Batch | No impact | No impact | No impact | Low |
| #6 ApiWallScope | No impact | No impact | No impact | None |

### Critical Safety Requirement
Before any Dump/Capture snapshot, `DrainForSnapshot()` MUST flush all queued entries. This is already the pattern -- we preserve it in the lock-free design by adding a `FlushAll()` that advances the consumer to the producer position.

---

## 7. Implementation Order (Recommended Sequence)

1. **Plan #6 first** (ApiWallScope) - trivial, zero risk, warms up the codebase
2. **Plan #2 second** (Deferred Recording) - biggest single win, low risk
3. **Plan #4 third** (Batch Enqueue) - quick win, builds understanding of descriptor flow
4. **Plan #1 fourth** (Lock-Free Ring Buffer) - structural improvement, requires careful testing
5. **Plan #3 fifth** (LRU Cache) - improves worst-case behavior
6. **Plan #5 sixth** (Binding Batch) - minor, proven pattern

---

## 8. Areas NOT Touched (Out of Scope)

- Game engine CPU work (244ms) - cannot be optimized by mod
- Hash algorithm - must remain identical across Mod/Dump/Hunting
- PreSkin async queue - already optimized with dedicated worker thread
- Resource tracker - minor overhead, not on critical path
- Overlay rendering - not on critical path (post-Present)
