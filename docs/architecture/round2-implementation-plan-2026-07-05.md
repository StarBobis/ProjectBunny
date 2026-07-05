# Round 2 Implementation Plan — 2026-07-05

## Summary

Implement all three phases of the architecture refactoring:
- **Phase A**: Async PreSkinProbe (eliminates 13-43ms Dispatch blocking)
- **Phase B**: PreSkinProbe LRU Cache (eliminates cold-start storms)
- **Phase C**: Worker Thread Batching (reduces WorkerTask 57ms→15ms)

## Invariant: Hash Algorithm Consistency

All three subsystems (Mod/Dump/Hunting) depend on identical hash outputs:
- `crc32c_append` / `HashBytes` — UNCHANGED
- `DX12HashBufferView` / `DX12HashIaBufferView` — UNCHANGED
- `BuildStableBindingFingerprint` — UNCHANGED (cache key for PreSkinProbe)
- `DX12ResolveBufferResourceByGpuVa` — UNCHANGED

Async PreSkinProbe computes the SAME result as the sync path, just on a worker thread.

## Phase A: Async PreSkinProbe

### Files to CREATE:
1. `src/DirectX12/mod/DX12PreSkinAsyncQueue.hpp` — Lock-free SPSC queue for PreSkinProbe requests + worker thread

### Files to MODIFY:
2. `src/DirectX12/command/DX12DispatchHookFlow.hpp` — Add async fallback in PreparePreSkinProbe when cache misses
3. `src/DirectX12/mod/DX12ModRuntime.cpp` — Start/stop async worker, wire up queue

### Design:
- Render thread: cache HIT → apply fast (~100us). Cache MISS → enqueue request, skip probe.
- Worker thread: dequeue → compute PreSkinProbe result → populate cache.
- Queue: bounded SPSC (Single Producer Single Consumer) using std::atomic.
- At most 1 worker thread for PreSkinProbe (compute shader matching is CPU-heavy but serial).

## Phase B: PreSkinProbe LRU Cache

### Files to MODIFY:
1. `src/DirectX12/mod/DX12ModRuntime.cpp` — Replace `.clear()` with LRU eviction for gPreSkinUavMatchCache

### Design:
- Use linked hash map pattern (std::list<entry> + std::unordered_map<key, list::iterator>)
- On insert: if size >= 4096, evict LRU entry. Never full clear.
- On mod reload: clear ALL entries (generation check already handles this)

## Phase C: Worker Thread Batching

### Files to MODIFY:
1. `src/DirectX12/resource/DX12ResourceCollectQueue.cpp` — Batch dequeue 64 events per lock cycle

### Design:
- In WorkerThreadProc, dequeue up to 64 events at once under a single lock.
- Process all 64 events before re-acquiring lock.
- Amortizes lock overhead 64x.

## Implementation Order

1. Phase B first (simplest, isolated change)
2. Phase C second (simple, isolated change)
3. Phase A last (most complex, depends on understanding of B)

## Verification Checklist

- [ ] Mod: IA buffer override matching works (unchanged hash algorithms)
- [ ] Mod: PreSkinProbe produces identical results (async computes same as sync)
- [ ] Dump: Resource resolution unchanged
- [ ] Hunting: Shader hunting unchanged
- [ ] No deadlocks (async queue + LRU cache + worker thread)
- [ ] build_debug_x64.ps1 succeeds
- [ ] No new warnings
