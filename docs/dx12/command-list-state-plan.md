# DX12 Command-List State Architecture Plan

Status: In progress; C1 IA branch implemented, broader merge pending
Owner: DX12 Command Runtime
Last updated: 2026-07-10

## Purpose

Maintain one authoritative current state per command list and derive Capture/Hunting history from state transitions. Avoid duplicate current-state updates in `DX12CommandListRuntimeState` and `CommandListBindingState`.

## Current Friction

Pipeline state, descriptor heaps, compute serials, and IA bindings are represented in multiple state Modules. Hook flows sometimes update runtime state once for Hunting and again for active Mods. Capture maintains another binding representation even though its events originate from the same intercepted calls.

## First-Principles Invariants

1. A DX12 command list is a state machine recorded in call order.
2. Each intercepted state-setting call performs one authoritative current-state transition.
3. Draw and dispatch observe exactly the state established by preceding calls on that command list.
4. `Reset` discards recorded state and starts from the supplied initial PSO.
5. Capture history is an ordered consequence of transitions, not a second current state.
6. State needed for Mod correctness is recorded even when Hunting and Capture are disabled.

## Target Module

Deepen `DX12CommandListRuntimeState` into the single current-state Module. Its Interface accepts typed transitions such as PSO, IA, descriptor heap, root binding, output merger, resource state, draw, and dispatch. Its Implementation updates current state, epochs, compiled-plan references, and optional event deltas exactly once.

Capture and Hunting become consumers of typed transition deltas. They do not independently maintain current bindings.

## Implementation Phases

### Phase C0: State ownership map

- Document every duplicated field and all readers/writers.
- Assign one authoritative owner for each field.
- Add transition counters that detect multiple writes for one hook invocation.

### Phase C1: Merge IA and PSO transitions

- Replace separate Hunting and Mod update branches with one state update.
- Derive IA/content epochs once.
- Store only the current PSO and generation-qualified compiled plan reference.

Implementation note: the hot-path IA topology, index-buffer, and vertex-buffer hooks now perform one runtime-state transition when Hunting and Mods are both active. Hunting telemetry remains independent. PSO ownership and the broader `CommandListBindingState` merge remain behind the runtime equivalence gate.

### Phase C2: Merge descriptor and root transitions

- Update bound heaps and root arguments once.
- Emit compact deltas only when Capture/Hunting needs history.
- Apply DX12 heap-switch invalidation rules in the authoritative transition.

### Phase C3: Reset and lifetime consolidation

- Centralize command-list registration, `Reset`, close, and release transitions.
- Ensure TLS accelerators reference current state generations and cannot survive unregister/pointer reuse.
- Remove duplicate `CommandListBindingState` fields after result-equivalence validation.

## Correctness Gates

- Replaying a captured transition sequence must reconstruct the same draw/dispatch inputs.
- `Reset` tests must show no state from a previous recording.
- Mod matching must remain active with Capture and Hunting disabled.
- Hunting enable/disable must not alter Mod decisions.
- Same-PSO and same-binding fast paths must still update any required generation or content epochs.

## Risk And Alignment Granularity

The primary risk is changing update order. Alignment is per intercepted DX12 call and per command-list recording generation. Implementation proceeds by field family so each family can temporarily mirror old state and compare it before deletion.

## Expected Effect

Normal Mod binding work totaled about 911.645 ms in the baseline and is not the top hotspot. Expected direct improvement is 5%-15% in binding hooks, with larger maintainability gains from locality and a smaller test surface.

## Official DX12 References

- [Microsoft: Recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
- [Microsoft: ID3D12GraphicsCommandList::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset)
- [Microsoft: ID3D12GraphicsCommandList::SetDescriptorHeaps](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps)
