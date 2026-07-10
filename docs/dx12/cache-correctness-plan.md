# DX12 Cache Correctness And Lifetime Plan

Status: In progress; K0 complete, K1/K2 partially implemented
Owner: DX12 Runtime Caches
Last updated: 2026-07-10

## Purpose

Make every cache explicitly correct, bounded where it is only an optimization, and generation-aware. Resource Dump completeness must not be bounded by cache policy.

## Cache Inventory And Findings

- Draw negative prefilter: key omits draw arguments and stores only a hash. This is a P0 Mod correctness defect.
- PreSkin abort cache: fingerprints only the first eight CBVs. This is a P0 Mod correctness defect.
- `DX12IaHashStateCacheUtils`: stores age/cursor fields but never evicts; command-list maps can grow without bound.
- Texture override lookup: clears all 8,192 entries at once, causing a cold-start cliff.
- PSO draw fast-reject cache: unbounded, globally locked, and hash-only.
- PreSkin UAV cache: lookup does not refresh order, so the claimed LRU behaves as FIFO.
- `gRecentComputeUavs`: retained for Debug diagnostic snapshots; it is not part of Mod matching. Its 1,145 baseline records cost 5.706 ms.

Current Dump policy is not artificially limited: single readback and subresource limits return maximum values, and the frame readback budget is zero. These policies must remain unlimited.

## First-Principles Invariants

1. Cache identity equals all inputs that affect the cached result.
2. Hashes select buckets; structured equality establishes identity.
3. Negative results require stronger proof than positive acceleration because false negatives disable Mods.
4. Configuration and command-list recording generations are explicit key fields where applicable.
5. Cache eviction may reduce performance but must never change behavior.
6. Bounded optimization caches must degrade to recomputation, not missing data.
7. Dump data structures are not optimization caches and cannot have completeness limits.

## Target Module

Create typed cache-key and cache-policy Utils classes in `.hpp` files. Each cache exposes lookup, store, invalidation, and telemetry through one Interface. Policies are local to each cache's reuse domain rather than a shared global clear.

## Implementation Phases

### Phase K0: Correct identity - implemented 2026-07-10

- Use a complete structured draw invocation key.
- Fingerprint all relevant PreSkin bindings, not a fixed prefix.
- Replace hash-only entries with hash plus equality.

### Phase K1: Correct lifetime

- Add bounded incremental eviction to IA and fast-reject optimization caches.
- Refresh true LRU order on hit where LRU is claimed.
- Use generation invalidation for reload-scoped facts.

Implemented portion: IA hash-state caches are bounded to 1,024 entries per command-list state, PSO decision caches to 4,096 entries per kind, and structured draw fast-reject entries to 16,384. Eviction removes one optimization entry and falls back to recomputation.

### Phase K2: Remove cache cliffs and dead state

- Replace whole-table texture cache clears with incremental replacement.
- Delete `gRecentComputeUavs` after confirming zero readers through CodeGraph and build/tests.
- Remove age/cursor fields that do not implement a real policy.

Implemented portion: the 8,192-entry texture-override lookup no longer clears the entire table; it replaces one entry incrementally. The diagnostic compute-UAV history remains because CodeGraph found a Debug reader.

### Phase K3: Consolidate with compiled PSO plans

- Delete caches that duplicate immutable `CompiledPsoModPlan` facts.
- Retain invocation caches only for expensive dynamic computations.

## Correctness Gates

- Collision-forcing tests must return the correct entry.
- Eviction tests must fall back to recomputation and preserve Mod results.
- Reload and command-list reset must invalidate the correct scope only.
- Long-running cache size must reach a stable bound.
- Dump resource/subresource counts must remain complete.

## Risk And Alignment Granularity

Primary risk is incomplete key design. Each key is aligned to the smallest domain in which the result is immutable: reload generation, PSO, command-list recording, binding epoch, resource-content epoch, and draw invocation as required. Cache changes are deployed individually with hit/miss/fallback telemetry.

## Expected Effect

K0 is correctness-first and may be neutral. K1-K3 should remove long-session growth and cache-clear spikes. Expected steady-state cache overhead reduction is 10%-30% in the affected paths; the large Mod Runtime gain is accounted for in `mod-runtime-plan.md`.
