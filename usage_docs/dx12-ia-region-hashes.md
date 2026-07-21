# DX12 IA Region Hashes

DX12 IA hashes shown by Hunting, written by F8 frame analysis dumps, and consumed by `[TextureOverride] hash=` are the same region hash.

For index buffers, the region is selected from the bound `D3D12_INDEX_BUFFER_VIEW` using `StartIndexLocation` and `IndexCountPerInstance`.

For vertex buffers, the region is selected from the bound `D3D12_VERTEX_BUFFER_VIEW` using `StartVertexLocation` for non-indexed draws. Indexed draws use `BaseVertexLocation` and the same DX11 vertex-count estimate when there is no explicit vertex count.

In `summary/BuffersDX12.csv`, `hunt_hash` is the Mod-ready IA hash. The `gpu_va`, `offset`, and `size` columns describe the dumped region. The `view_gpu_va`, `view_size`, and `view_offset` columns describe the original IA view that contained that region.

Use the Hunting copied hash or the F8 `hunt_hash` value directly in `[TextureOverride] hash=...` for IA buffer matching.
