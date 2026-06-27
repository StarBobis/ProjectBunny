# ADR 0004: DX12 Frame Analysis Compute Resource Bindings

## Status

Accepted

## Context

FrameAnalysis could dump IA buffers, but `ResourceDump` reported `bindings=0` and `textures=0`. `BuffersDX12.csv` contained draw-time IA rows, while `BindIA` producer fields remained empty for GPU preskinning cases.

The immediate cause was missing descriptor heap metadata. Descriptor records can exist without usable heap records, but descriptor table resolution starts from the GPU descriptor handle and must map it back through a shader-visible descriptor heap to a CPU descriptor handle. Since F8 is requested after most heaps were created, gating heap metadata behind capture-time resource tracking loses that mapping.

DX12's binding model makes this unavoidable: command lists bind compute descriptor tables through root parameters and GPU descriptor handles, while root descriptors bind GPU virtual addresses directly. A useful dump must preserve enough creation-time metadata to resolve both forms later.

## Decision

DX12 records CBV/SRV/UAV descriptor creation metadata and shader-visible descriptor heap metadata from creation time. Descriptor copy tracking remains gated behind the frame-analysis or preskinning hot path because descriptor copies can be frequent.

FrameAnalysis writes `BindResource` events for copied descriptor resources and includes `hunt_hash` alongside `hash` and `file_hash`. It also writes `summary/ResourceBindingsDX12.csv` so tools can join dispatch SRV/UAV/CBV bindings to deduped files without parsing the full JSON log.

`FrameAnalysisDX12.csv` points at `summary/ResourceBindingsDX12.csv` instead of stale current-frame resource file names that are not produced by the resource dump path.

## Consequences

ZZMIDX12 can correlate draw-time GPU preskinning VBs with prior compute UAV producers and inspect the dispatch SRV inputs and UAV outputs.

Creation-time descriptor metadata increases long-lived metadata volume, so descriptor cache pruning remains important. The design avoids enabling descriptor copy tracking permanently and does not add driver calls to draw or dispatch hot paths.

Remaining fallback-style paths in original function lookup and resource copy are unchanged and should be reviewed separately.
