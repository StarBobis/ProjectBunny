# DX12 Frame Analysis Resource Bindings

DX12 frame analysis must preserve compute resource binding evidence for GPU preskinning extraction.

IA dumps alone are not enough. A draw-visible VB can be produced by an earlier compute dispatch, so FrameAnalysis needs both sides of the relationship:

1. `BindResource` rows for dispatch SRV/UAV/CBV bindings.
2. `BindIA` rows whose producer fields point back to the latest overlapping UAV dispatch.
3. A summary CSV that maps dispatch resource bindings to the same deduped file and file hash used by `FileDump`.

Descriptor table bindings depend on descriptor heap metadata. F8 is requested after most descriptor heaps and descriptors already exist, so descriptor heap creation metadata and CBV/SRV/UAV descriptor creation metadata must be retained from creation time. Tracking only during the F8 capture window loses the GPU-handle-to-CPU-descriptor mapping and makes `BindingResourcesCollected` fall to zero even when descriptor records exist.

Descriptor heap metadata is lightweight and should include heap pointer, type, descriptor count, CPU start, GPU start, and increment. CBV/SRV/UAV descriptor creation metadata should include the resource pointer, view desc, GPU VA, resource offset, view size, stride, format, and current known resource state. Descriptor copy tracking can remain gated behind the frame-analysis or preskinning hot path because copies may occur frequently, but creation metadata must not depend on the capture window.

`summary/ResourceBindingsDX12.csv` is the stable machine-readable bridge for ZZMIDX12. It records call index, dispatch/draw type, PSO, CS hash, resource kind, root parameter, register, resource pointer, actual deduped file, file hash, binding hash, offset, size, stride, and format.
