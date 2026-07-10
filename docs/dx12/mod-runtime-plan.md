# DX12 Mod Runtime Architecture Plan

Status: In progress; M0 implemented, runtime equivalence pending
Owner: DX12 Mod Runtime
Last updated: 2026-07-10

## Purpose

Reduce per-draw CPU work without changing which Mods match or execute. The runtime must make the same decision as a full match for every draw, including after configuration reloads and when draw arguments, IA bindings, descriptor contents, or resource contents change.

## Baseline

The captured 1,582-frame log contains 180,305 `prepare_shader_override_miss` events totaling 14,245.290 ms and only 237 successful preparations. The current PSO-level decision is represented in several places:

- `gShaderOverridePsoMatchCache`
- `gPsoOverridePlans`
- `DX12PsoDecisionCacheUtils::DrawEntries`
- `DX12CommandListRuntimeState::drawDecision`

This duplication requires repeated snapshots, locks, target-plan reconstruction, and VM setup even when the immutable PSO/configuration relationship is already known.

## First-Principles Invariants

1. A cache hit may replace matching only when its key contains every input that can change the result.
2. A negative cache hit must never suppress a Mod that a full match would accept.
3. Configuration reloads publish a new generation; no decision from an older generation may be reused.
4. PSO facts may be immutable, while draw arguments, IA content, descriptor content, and resource content remain invocation facts.
5. A fast path must fall back to the existing matcher whenever it cannot prove equivalence.
6. Runtime work remains synchronous within the intercepted frame.

## Target Module

Introduce one deep `CompiledPsoModPlan` Module keyed by `(ID3D12PipelineState*, reloadGeneration)`. Its Interface exposes a stable plan reference and a small decision kind. Its Implementation owns shader-override candidates, skip behavior, IA target requirements, descriptor target requirements, and precompiled target actions.

The command-list state stores only a stable reference or generation-qualified handle to this plan. Draw-specific matching receives a complete invocation key and performs only the dynamic checks required by the plan.

## Implementation Phases

### Phase M0: Correct negative caching - implemented 2026-07-10

- Replace hash-only draw miss identity with a structured key.
- Include indexed state, vertex/index count, instance count, first vertex/index/instance, and base vertex.
- Compare the full key after hashing.
- Add regression tests in which two draws share a PSO and IA views but differ only in each draw argument.

Implementation note: `DX12PsoDrawFastRejectKey` now compares PSO, reload generation, target plan, IA identity kind, every direct draw argument, and full structured equality. `FirstInstance` is also propagated through IA and descriptor texture-override matching instead of being forced to zero. The Debug x64 build passes; an in-game Mod equivalence run remains required before M1.

### Phase M1: Publish one compiled PSO plan

- Move PSO/configuration matching results into `CompiledPsoModPlan`.
- Build once per `(PSO, reloadGeneration)` and publish only fully initialized immutable plans.
- Keep the current full matcher as the miss/fallback adapter.
- Record plan build, plan hit, dynamic match, fallback, and Mod execution counters.

### Phase M2: Remove redundant decision layers

- Route draw and dispatch decisions through the compiled plan.
- Remove `DX12PsoDecisionCacheUtils` entries that merely copy plan facts.
- Remove command-list copies of immutable plan data; retain only current PSO and plan reference.
- Delete old caches only after result-equivalence telemetry shows no divergence.

### Phase M3: Compile target actions

- Pre-resolve candidate action lists and required dynamic inputs.
- Avoid VM scope creation when the plan proves no candidate can execute.
- Preserve action order and all pre/post command semantics.

## Correctness Gates

- Existing Mods must produce identical replacement, skip, and restore counts.
- Reloading a Mod configuration must invalidate all old plans immediately.
- A draw-argument-only change must reach the full dynamic matcher when relevant.
- Descriptor-dependent plans must not use an IA-only negative result.
- Hash collisions must be harmless because equality compares structured keys.

## Risk And Alignment Granularity

Primary risk: over-classifying dynamic data as immutable and suppressing a valid Mod. Changes therefore align at PSO generation for immutable candidate selection and at individual draw invocation for final matching. Roll back each phase independently by routing the new Module's miss path to the existing matcher.

## Expected Effect

Expected Draw Hook hotspot reduction: 50%-67%. Expected steady-state saving: approximately 6.3-8.1 ms per frame in the captured workload. Theoretical CPU-bound steady-state rate may move from 51.8 FPS to roughly 77-89 FPS; these are hypotheses until validated with the process in `performance-validation.md`.

## References

- [Microsoft: Recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
- [Microsoft: ID3D12GraphicsCommandList::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset)
