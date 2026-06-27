# DX12 Frame Analysis Resource Bindings

DX12 F8 texture dump must follow the DX11 frame analysis principle: enumerate resources bound at the draw or dispatch execution point, then let the resource copy stage decide whether a resource can be dumped.

DX11 can ask the immediate context for shader resource views per shader stage. DX12 cannot mirror that API shape directly because shaders see descriptor tables rooted in a root signature and backed by descriptor heaps. The DX12 equivalent must therefore preserve the full evidence chain for each event:

- command list event
- graphics or compute root signature
- root parameter
- descriptor range
- descriptor heap
- descriptor index
- descriptor metadata
- resolved resource

If a descriptor table cannot be matched to a root signature range, the exporter must record diagnostic counts instead of inventing a binding from the descriptor table start. Treating the table base as a single implicit descriptor hides root signature capture failures and can make F8 look partially successful while most textures are still missing.

F8 must always write the diagnostic files that let this chain be inspected:

- `summary/CurrentFrameResourcesDX12.txt`
- `summary/ResourceMetadataDX12.txt`
- `PsoResourceSummaryDX12.txt`
- `summary/ResourceBindingsDX12.csv`

The binding collection log should include counts by descriptor kind and missing-evidence category. A low `srv` or `texture` count with many root tables means the problem is in root range or descriptor metadata resolution, not in Mod ini syntax.

IA dumps alone are not enough. A draw-visible VB can be produced by an earlier compute dispatch, so frame analysis needs both sides of the relationship:

1. `BindResource` rows for dispatch SRV/UAV/CBV bindings.
2. `BindIA` rows whose producer fields point back to the latest overlapping UAV dispatch.
3. A summary CSV that maps dispatch resource bindings to the same deduped file and file hash used by `FileDump`.

Descriptor heap metadata is lightweight and should include heap pointer, type, descriptor count, CPU start, GPU start, and increment. CBV/SRV/UAV descriptor creation metadata should include the resource pointer, view desc, GPU VA, resource offset, view size, stride, format, and current known resource state. Descriptor copy metadata must preserve the latest descriptor record for destination handles, because shader-visible heap contents are commonly populated before the F8 capture window.

Unbounded descriptor ranges must be treated as real ranges during F8 export. `NumDescriptors == UINT_MAX` does not mean one descriptor; it means the table range is unbounded from its effective offset. DX12 should enumerate from the table base plus the effective range offset until the descriptor heap ends or the frame-analysis range budget is reached. This preserves the DX11 behavior of walking all current SRV slots while keeping bindless heaps bounded.

Shader-visible descriptor heap contents may be populated by `CopyDescriptors` or `CopyDescriptorsSimple` long before F8 is pressed. Descriptor copy tracking is metadata movement inside the tracker and must preserve the latest descriptor record for destination handles across the process lifetime. Otherwise descriptor table expansion can produce slot indices without resolving the copied SRV/UAV resource behind those handles.

`BindResource` is binding evidence, not only a file-dump receipt. SRV/UAV/CBV rows must be emitted even when the resource copy is skipped or fails, with `status` and `note` carrying the reason. GPU preskinning producer detection depends on the UAV binding evidence, so it must not disappear just because no deduped file was written.

`summary/ResourceBindingsDX12.csv` is the stable machine-readable bridge for ZZMIDX12. It records call index, dispatch/draw type, PSO, CS hash, resource kind, root parameter, register, resource pointer, actual deduped file, file hash, binding hash, offset, size, stride, format, status, and note.

Texture dumping follows the same resource binding path. Texture2D SRV/UAV bindings should enter the dump candidate set and write `.dds` files through the DX12 copyable-footprint path when state, format, sample count, and budget allow it. If a texture cannot be copied, the SRV/UAV binding row still remains visible with a skip reason.
