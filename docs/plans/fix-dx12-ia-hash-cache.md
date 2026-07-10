# Fix DX12 IA Hash Cache

## Objective

Eliminate repeated IA hash-state reconstruction across command-list recordings while preserving Mod hash matching after resource, content, and GPU-write changes.

## Baseline

- Valid-frame interval: 43.006 ms (43,006 us), 23.25 FPS.
- IA hash-state cache: 1,884 lookups, 319 hits, 1,565 misses per 10 frames; 16.93% hit rate.
- BuildIaHash: 24.221 ms per 10 frames, 2,422.1 us per frame.
- Mod correctness: 414 IA matches, three replacement matches per active frame, zero failed HRESULT/status/device-loss signals.

## Root Cause

The outer IA hash-state cache key omits GPU virtual addresses, ignores existing IA content/resolve generations and the command-list GPU-write epoch, and includes PSO/reload fields that do not affect the resulting resource-region hash. Capacity eviction removes an arbitrary unordered-map entry. Cache hits also deep-copy dynamic vertex-buffer storage.

The deeper `DX12HashIaBufferView` module already provides content-aware thread-local L1/L2 caching across command lists. However, its L1/L2 hit path always acquires the resource lock even when the resolve generation is unchanged. The outer cache should reuse complete hash states during one command-list recording and delegate actual region hashing to the deeper module only on a dependency-aware miss. Keeping complete states scoped to one recording avoids unsafe sharing across independent GPU queue timelines.

## Plan

1. Refactor `DX12IaHashStateCacheKey` around actual hash dependencies: target-plan fingerprint, IA-view identity including GPU VAs, Draw region, IA content epoch, buffer-content generation, and GPUVA resolve generation. Remove PSO/reload identity.
2. Encapsulate map/clock/eviction ownership in one `DX12IaHashStateCache` module and return a pointer to cached state on hits, avoiding per-hit dynamic-vector copies.
3. Pass the current IA content epoch into key construction. Potential GPU writes already bump this epoch through Dispatch, Copy, Clear UAV, Resolve, and ExecuteIndirect hooks. Reset continues clearing the cache so independent queue timelines cannot share a stale complete state.
4. Add a zero-lock fast path to the deeper IA resource hash cache when content and resolve generations exactly match the cached generations. Acquire the resource lock only to validate a range across a changed resolve generation.
5. Replace arbitrary capacity eviction with least-recently-used entry selection. Keep the existing 1,024-entry bound because this is a runtime cache, not a Dump resource limit.
6. Add aggregate telemetry for evictions so capacity pressure is observable without changing Mod execution.
7. Sync CodeGraph, build Debug x64, inspect the fresh log, and compare hit rate, BuildIaHash time, frame time, Mod matches, and DX12 error signals. Delete this completed plan.

## Safety Conditions

- Different GPU VAs, view sizes, strides, formats, slots, target plans, or Draw regions cannot share an outer cache entry.
- CPU buffer-content updates, GPUVA resolution invalidation, or command-list GPU-write operations cannot reuse stale hash states.
- Runtime descriptor recording and complete Dump behavior remain unchanged.
- A cache miss executes the existing `BuildIaHashStateForTargets` implementation unchanged.
