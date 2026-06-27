# ADR 0004: DX12 Frame Analysis Compute Resource Bindings

## Status

Accepted

## Context

FrameAnalysis could dump IA buffers, but `ResourceDump` reported `bindings=0` and `textures=0`. `BuffersDX12.csv` contained draw-time IA rows, while `BindIA` producer fields remained empty for GPU preskinning cases.

The immediate cause was missing descriptor heap metadata. Descriptor records can exist without usable heap records, but descriptor table resolution starts from the GPU descriptor handle and must map it back through a shader-visible descriptor heap to a CPU descriptor handle. Since F8 is requested after most heaps were created, gating heap metadata behind capture-time resource tracking loses that mapping.

The next DX11 comparison exposed the same issue for root signatures and descriptor copies. DX11 queries every SRV slot directly from the context, while DX12 must expand descriptor tables through root signature descriptor ranges. If root signature metadata is not retained from creation time, DX12 sees only the table base descriptor, commonly a CBV, and no texture SRV candidates are produced. If descriptor copies are not retained, expanded shader-visible heap slots cannot resolve back to the original SRV/UAV descriptor records.

DX12's binding model makes this unavoidable: command lists bind compute descriptor tables through root parameters and GPU descriptor handles, while root descriptors bind GPU virtual addresses directly. A useful dump must preserve enough creation-time metadata to resolve both forms later.

## Decision

DX12 records root signature metadata, CBV/SRV/UAV descriptor creation metadata, shader-visible descriptor heap metadata, and descriptor copy metadata from creation time. Descriptor copy tracking stores only metadata needed to preserve latest destination-handle contents.

FrameAnalysis writes `BindResource` events for descriptor resources and includes `hunt_hash` alongside `hash` and `file_hash`. These rows are emitted for skipped resources as well as copied resources, because compute SRV/UAV evidence is required for GPU preskinning even when a deduped file is not produced. It also writes `summary/ResourceBindingsDX12.csv` so tools can join dispatch SRV/UAV/CBV bindings to deduped files without parsing the full JSON log.

Texture2D SRV/UAV bindings are dump candidates. Successful copies are written as `.dds` files through the same deduped resource path as buffers; skipped texture copies keep their binding row and skip note.

Unbounded descriptor ranges are expanded against the descriptor heap instead of being collapsed to one descriptor. Expansion remains bounded by the frame-analysis per-range descriptor budget, so bindless tables can expose texture SRVs without turning F8 into an unbounded heap walk.

`FrameAnalysisDX12.csv` points at `summary/ResourceBindingsDX12.csv` instead of stale current-frame resource file names that are not produced by the resource dump path.

## Consequences

ZZMIDX12 can correlate draw-time GPU preskinning VBs with prior compute UAV producers and inspect the dispatch SRV inputs and UAV outputs.

Creation-time descriptor metadata increases long-lived metadata volume, so descriptor cache pruning remains important. The design avoids enabling descriptor copy tracking permanently and does not add driver calls to draw or dispatch hot paths. F8 logs `unbounded_ranges` and `unbounded_descriptors` in `BindingResourcesCollected` so bindless descriptor expansion can be validated from the capture log.

Remaining fallback-style paths in original function lookup and resource copy are unchanged and should be reviewed separately.
