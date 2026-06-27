# DX12 Hunting IA Selection

DX12 hunting IA selection should mirror the DX11 hunting model.

DX11 does not treat a whole index buffer binding as the hunting identity when region hashing is active. During `BeforeDraw`, it combines the current IA binding with the draw arguments, then computes a buffer region hash from `FirstIndex`/`IndexCount` for IB and `FirstVertex`/estimated vertex count for VB. The overlay and matching path use the same hash, so the copied value and selected green/skip behavior refer to the same draw-visible region.

DX12 follows that shape:

1. `IASetIndexBuffer` and `IASetVertexBuffers` store lightweight view metadata on the command list.
2. `DrawInstanced` and `DrawIndexedInstanced` pass draw counts and start locations into hunting.
3. Hunting derives an IA region hash from the stored view plus draw arguments.
4. Selection, overlay display, copy, and skip checks use that same region hash.
5. Present purges stale IA hashes on a short frame lifetime, matching DX11's `overlay_buffer_hash_lifetime` behavior instead of growing an unbounded history.

This avoids a giant historical IB list and prevents mismatches where the overlay advances to one hash while draw skipping checks a different hash.

The hunting lock should only protect copied command-list state and visited-selection containers. PSO metadata lookup and IA hash calculation happen after the command-list state is copied out, so hunting does not hold its own lock while entering shader dump or resource tracker locks.

Hot-path hook activation must also include command lists that were created before hunting was toggled. DX12 games commonly reuse command lists, so activating only swapchain and command queue hooks is insufficient. When hunting is enabled, command lists submitted through `ExecuteCommandLists` should be offered to the command-list hook installer even if their lifecycle state was already registered earlier.

The D3D12 API shape supports this design: `DrawIndexedInstanced` provides the first index location and index count for the draw, while `D3D12_INDEX_BUFFER_VIEW` and `D3D12_VERTEX_BUFFER_VIEW` provide the bound GPU address, size, format, and stride. A draw-region hunting identity must therefore combine both pieces of state.
