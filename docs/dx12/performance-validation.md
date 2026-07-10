# DX12 Performance And Correctness Validation

Status: Build validated; updated runtime capture pending
Owner: DX12 Runtime
Last updated: 2026-07-10

## Purpose

Define one repeatable gate for every architecture phase. A change is accepted only when it builds, preserves Mod behavior, and improves or intentionally trades performance for a documented correctness fix.

## Build Gate

Run `build_debug_x64.ps1` after code changes. Documentation-only changes do not require a build. Record the build result and do not deploy a failed binary.

### 2026-07-10 Implementation Build

`build_debug_x64.ps1` completed successfully after the P0 correctness, descriptor synchronization, state deduplication, cache lifetime, and inactive async cleanup slices. The resulting `d3d12.dll` was copied to `D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12`. `LaunchGame=false`, so this build does not establish runtime Mod equivalence or measured performance.

## Runtime Log

Default path:

`D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl`

The current file is tab-separated despite its extension. Any reusable parser belongs under `tools/`.

## Baseline: 1,582 Frames

| Metric | Value |
| --- | ---: |
| Wall time | 85,014.826 ms |
| All-sample mean frame interval | 53.772 ms |
| All-sample expected rate | 18.60 FPS |
| Steady-state mean, intervals <= 100 ms | 19.317 ms |
| Steady-state expected rate | 51.77 FPS |

### Accumulated CPU Stages

| Stage | Total ms |
| --- | ---: |
| DrawHook | 19,215.189 |
| DrawOverride | 18,311.608 |
| ModPrepareOverride | 14,577.137 |
| SetPipelineState | 2,087.981 |
| BuildIaHash | 1,791.011 |
| RootDescTable | 828.000 |
| ResourceBarrier | 732.106 |
| ExecuteCL | 540.900 |
| DispatchHook | 439.978 |

```text
Accumulated stage time (each # is approximately 1,000 ms)
DrawHook           ###################  19,215.189 ms
DrawOverride       ##################   18,311.608 ms
ModPrepareOverride ###############      14,577.137 ms
SetPipelineState   ##                    2,087.981 ms
BuildIaHash        ##                    1,791.011 ms
RootDescTable      #                       828.000 ms
ResourceBarrier    #                       732.106 ms
ExecuteCL          #                       540.900 ms
DispatchHook       #                       439.978 ms
```

### Last Observed Frame: 1,582

| Metric | Total ms | Per-call us / Notes |
| --- | ---: | --- |
| Frame wall interval | 569.652 | Expected 1.76 FPS |
| Present-to-present interval | 569.359 | - |
| Work outside Present | 568.340 | - |
| DrawHook | 84.611 | Aggregate |
| ModPrepareOverride | 61.023 | Aggregate |
| IA hash build/store | 14.471 | Aggregate |
| PreSkin probe | 11.362 | Aggregate |
| Logging | 22.476 | Reported only; not treated as a hang root cause |

```text
Frame 1,582 stage time (each # is approximately 5 ms)
Frame wall         ################################################################################################################## 569.652 ms
DrawHook           #################                                                                                                  84.611 ms
ModPrepareOverride ############                                                                                                       61.023 ms
Logging            ####                                                                                                               22.476 ms
IA hash            ###                                                                                                                14.471 ms
PreSkin probe      ##                                                                                                                 11.362 ms
```

No post-change log exists yet because the build removed the previous file and did not launch the game.

## Required Per-Phase Report

For each measured run, report:

1. Total frame time in ms and per-event/call time in us.
2. Present-to-present interval and expected FPS.
3. Every instrumented function's total time and share of measured frame time.
4. Call count, mean, median, p95, and maximum where data is available.
5. Mod prepare hits, misses, fallbacks, replacements, skips, restores, and PreSkin hits.
6. Cache size, hit, miss, eviction, collision, and generation-invalidation counts.
7. A bar chart for stage totals and a frame-time trend chart.

## Correctness Comparison

Use the same scene, camera, configuration, capture duration, and warm-up. Compare:

- Mod-visible output and known replacement assets.
- Shader override candidate/match/action counts.
- Descriptor lookup and copy-resolution results.
- Hunting selection and skip counts when enabled.
- Dump resource and subresource counts when enabled.
- Reload behavior and command-list reset behavior.

Any decreased Mod match/action count blocks the phase unless the previous count is proven erroneous.

## Acceptance Thresholds

- Build succeeds with no new warnings in changed modules.
- No Mod behavior regression.
- No unbounded cache or ledger growth introduced.
- P0 correctness changes are accepted even if locally performance-neutral.
- Performance phases should improve the targeted stage by at least 5% outside noise, measured across comparable steady-state frames.

## Rollback Strategy

Keep old matching or lookup paths as synchronous fallback adapters during migration. Each architecture phase must be independently removable. Delete compatibility paths only after counters demonstrate equivalence and no fallback dependence.

## Log Cost Policy

High-frequency logging may slow execution but is not presumed to cause hangs, crashes, or non-responsiveness. When evidence is insufficient, add concise entry, exit, identity, generation, decision, and duration logs around the unresolved path.
