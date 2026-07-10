# DX12 Hunting And Capture Architecture Plan

Status: In progress; inactive async residue removed, event compaction pending
Owner: DX12 Hunting/Capture
Last updated: 2026-07-10

## Purpose

Record enough ordered information for Hunting, Capture, and Dump without copying full command-list state on every event or introducing deferred worker queues.

## Current Friction

- `BindingEvent` copies complete root-parameter arrays and IA slots for each event.
- `CommandListRecordedEvent` embeds both binding and output payloads although only one is active.
- `DX12CaptureLedger::GetSnapshot` copies the accumulated collection again.
- `TryStoreOutputEvent` performs linear duplicate lookup, producing quadratic frame behavior.
- `DX12BindingCollectQueue` retains queue-shaped interfaces, but its current `EnqueueTask` is a synchronous fallback that returns `false`; it has no active worker.
- Hunting `ShouldSkipDraw` and `RecordDraw` can separately construct and hash the same IA state.

## First-Principles Invariants

1. Command-list events are ordered, append-only facts within one recording generation.
2. A state-setting event needs only the fields changed by that call.
3. A full snapshot can be reconstructed by replaying deltas from a known reset state.
4. ExecuteCommandLists publishes recorded events in submission order.
5. Dump must include every resource in the frame; compaction cannot impose a resource limit.
6. No deferred queue may postpone work beyond the intercepted frame.
7. Hunting and Mod Runtime may share computed IA identity, but Hunting must never consume or mutate Mod decisions.

## Target Module

Introduce a compact tagged `DX12CommandEvent` stream owned by each command-list recording. Each tag carries only its delta payload. The stream's Interface supports append, ordered replay, and submission publication. Full snapshots are materialized only for consumers that request them.

Draw evaluation creates one invocation context containing IA identity and hashes. Mod Runtime and Hunting read the same immutable context.

## Implementation Phases

### Phase H0: Remove quadratic duplicate checks

- Add a per-recording hash set for output-event identity.
- Preserve first-seen ordering in the event vector.
- Add collision-safe equality and counters for probes/collisions.

### Phase H1: Replace wide events with tagged deltas

- Define compact payload classes for root table, root descriptor, root constants, IA, PSO, output, draw, and dispatch transitions.
- Replace dual embedded payloads with a tagged variant or equivalent discriminated storage.
- Preserve exact event order.

### Phase H2: Replay instead of copying current state

- Reconstruct full binding state only at the consumer seam.
- Replace `GetSnapshot` bulk copies with stable read iteration or move-at-publication ownership.
- Keep command-list recording memory valid until all synchronous frame consumers finish.

### Phase H3: Unify draw identity work

- Build IA draw invocation identity once.
- Pass it to Mod matching, Hunting skip evaluation, and Hunting recording.
- Remove `DX12BindingCollectQueue`; publish synchronously at ExecuteCommandLists.

Implementation note: the unreachable `DX12PreSkinAsyncQueue` Module was deleted after CodeGraph confirmed there were no `Enqueue` callers and its `Start` method never created a worker. Active PreSkin work remains synchronous. The binding queue interface and compact event stream are still planned.

## Correctness Gates

- Event replay must reconstruct byte-identical binding state for representative command lists.
- Submission order across multiple command lists must match queue order.
- Hunting hashes, skip decisions, and selected resources must remain identical.
- Dump counts must never decrease because of event compaction.
- Bundles and command-list reset/re-record cycles require independent recording generations.

## Risk And Alignment Granularity

Primary risks are event lifetime and order. Alignment is per command-list recording generation, per intercepted state transition, and per queue submission. The old snapshot path remains available as a comparison adapter until replay equivalence is proven.

## Expected Effect

Expected Capture/Hunting allocation and memory-copy reduction is 30%-60% when those modes are active. Normal Mod-only impact is expected to be smaller; eliminating duplicate IA hashing should reduce the relevant Hunting draw cost by 20%-40%.

## References

- [Microsoft: Recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
