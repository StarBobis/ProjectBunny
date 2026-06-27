# 0001 DX12 Frame Analysis Uses Root-Signature Binding Evidence

Status: accepted

DX12 F8 resource dumping must not rely on descriptor-table-base fallback candidates. A descriptor table is only meaningful when interpreted through the active graphics or compute root signature. The frame analysis exporter must enumerate descriptor ranges from the root signature and resolve descriptors through the active descriptor heaps.

This decision follows the DX11 design intent rather than its API shape. DX11 can query bound SRV slots directly from the context. DX12 must reconstruct equivalent bound-resource evidence from root signatures, descriptor tables, descriptor heaps, and descriptor metadata.

Consequences:

- F8 writes full resource metadata and PSO root summaries every run.
- Missing root signature or descriptor range evidence is logged as a diagnostic count.
- Resource dump candidates come from resolved descriptor ranges or root descriptors.
- Descriptor-table-base-only candidates are not emitted as dump candidates.

