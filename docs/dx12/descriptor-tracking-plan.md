# DX12 Descriptor Tracking Architecture Plan

Status: In progress; D0 implemented, runtime equivalence pending
Owner: DX12 Descriptor Runtime
Last updated: 2026-07-10

## Purpose

Consolidate descriptor identity and synchronization while preserving immediate visibility required by Mods. This plan explicitly follows `docs/adr/descriptor-runtime-recording-is-not-optional.md`: `Create*View` and `CopyDescriptors*` recording cannot be skipped.

## Current Friction

`RecordCreated*View` writes both a registered heap slot and `gDescriptorByCpuHandle`. The captured run grew the CPU-handle ledger from 28,048 entries at present 370 to 185,703 entries at present 1,311, while registered heap slot capacity was 377,744. The same descriptor fact can therefore be maintained and queried through several structures.

There are also correctness defects:

- `MAX_HEAP_LOCKS = 128` silently leaves heap indices at or above 128 without per-heap synchronization.
- `ResolveHeapSlot` reads `gDescriptorHeapByCpuStart` and `gDescriptorHeaps` while heap creation/release can mutate or reallocate them.

No COM ownership leak has been established: captured resource pointers are non-owning unless explicitly marked otherwise.

## First-Principles Invariants

1. A descriptor's latest logical contents must be visible synchronously after every `Create*View` and `CopyDescriptors*` call.
2. A registered heap slot is the source of truth for handles that resolve into that heap.
3. The CPU ledger is the source of truth only for staging or otherwise unresolved CPU handles.
4. Copying a descriptor updates the destination source of truth before returning from the hook.
5. Heap registration, lookup, removal, and slot access must remain valid under concurrent command recording.
6. Descriptor heap switching invalidates root-table bindings as specified by DX12; runtime state must model this behavior.
7. Tracking must cover every descriptor in the frame; no resource-count limit is allowed for Dump behavior.

## Target Module

Create one deep `DX12DescriptorRegistryUtils` Module in a `.hpp`. Its Interface owns heap registration, heap removal, handle resolution, descriptor creation recording, descriptor copying, and lookup. Callers do not choose between heap slots and the CPU ledger.

Synchronization must scale with live heaps rather than a fixed lock array. A registry-level shared/exclusive lock protects structure publication and removal; stable dynamically allocated per-heap locks protect slot contents.

## Implementation Phases

### Phase D0: Remove the fixed synchronization limit - implemented 2026-07-10

- Store the lock with each stable heap record.
- Make heap lookup retain a valid record for the duration of slot access.
- Synchronize map/vector structure reads with creation and release.
- Add a test with more than 128 registered heaps and concurrent create/copy/lookup operations.

Implementation note: the fixed 128-lock array was replaced by dynamically allocated stable locks. Heap resolution and slot access now share one `gDescriptorLock` read-lock lifetime, so heap registration, release, and vector reallocation cannot race the access. Build validation passes; the stress/runtime test remains pending.

### Phase D1: Route each handle to one source of truth

- On `Create*View`, resolve the destination once.
- Write a registered heap slot when resolution succeeds.
- Otherwise write the CPU ledger for staging descriptors.
- On `CopyDescriptors*`, resolve source through the registry and synchronously write each destination through the same routing rule.

### Phase D2: Consolidate compatibility ledgers

- Measure fallback lookup counts before removing existing ledgers.
- Retain compatibility lookup only while unresolved legacy paths exist.
- Remove duplicate writes only after tests prove identical Mod descriptor matches.

### Phase D3: Heap binding correctness

- On `SetDescriptorHeaps`, update the command-list current state once.
- Invalidate affected graphics and compute descriptor-table bindings when the bound heap changes.
- Preserve non-table root arguments according to the DX12 specification.

## Correctness Gates

- Existing descriptor-dependent Mods must match before and after copies in the same command list.
- Staging-to-visible and visible-to-visible copies must resolve identical resource/view metadata.
- Heap release and pointer reuse must not expose old slots.
- More than 128 heaps must behave identically to the first 128.
- No descriptor event may be dropped because Capture, Hunting, or Dump is disabled.

## Risk And Alignment Granularity

Primary risks are stale pointer use during heap removal and incorrect routing of staging handles. Structural lifetime aligns to a heap registration generation; descriptor contents align to individual slots and individual `Create*View`/copy operations. Each phase retains the old lookup as a fallback until counters reach zero for the migrated path.

## Expected Effect

Expected benefit is lower ledger growth, fewer hash-map operations, and less duplicated descriptor metadata. Estimated descriptor-recording CPU reduction is 15%-35% after D2, but correctness phases D0-D1 may initially be performance-neutral. Validate rather than assuming gains.

## Official DX12 References

- [Microsoft: Descriptor heaps overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview)
- [Microsoft: ID3D12Device::CopyDescriptors](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-copydescriptors)
- [Microsoft: ID3D12GraphicsCommandList::SetDescriptorHeaps](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps)
