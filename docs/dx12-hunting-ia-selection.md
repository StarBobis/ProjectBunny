# DX12 Hunting IA Selection

DX12 hunting IA selection should mirror the DX11 hunting model.

DX11 does not treat a whole index buffer binding as the hunting identity when region hashing is active. During `BeforeDraw`, it combines the current IA binding with the draw arguments, then computes a buffer region hash from `FirstIndex`/`IndexCount` for IB and `FirstVertex`/estimated vertex count for VB. The overlay and matching path use the same hash, so the copied value and selected green/skip behavior refer to the same draw-visible region.

DX12 follows that shape through one shared IA region hash helper:

1. `IASetIndexBuffer` and `IASetVertexBuffers` store lightweight view metadata on the command list.
2. `DrawInstanced` and `DrawIndexedInstanced` pass draw counts and start locations into hunting.
3. Hunting derives an IA region hash from the stored view plus draw arguments.
4. F8 frame analysis IA dumps derive filenames, `hunt_hash`, manifest `hash`, and copied byte ranges from the same region.
5. Mod `[TextureOverride] hash=` IA matching builds its draw-time hash state from that same region helper.
6. Selection, overlay display, copy, skip checks, dump output, and Mod matching use that same region hash.
7. Present purges stale IA hashes on a short frame lifetime, matching DX11's `overlay_buffer_hash_lifetime` behavior instead of growing an unbounded history.

This avoids a giant historical IB list and prevents mismatches where the overlay advances to one hash while draw skipping checks a different hash.

IB/VB selection state should store the selected hash, not only a positional index into the current set. DX11 keeps `mSelectedIndexBuffer` and `mSelectedVertexBuffer` as the durable selection identity, while position is display metadata. When the visible set is purged by the short frame lifetime, DX12 must keep the selected hash stable and only use set position to display or advance. If the selected hash is not currently visible, Num7/Num8 should use the current visible set to find the nearest next or previous hash with `lower_bound` semantics.

When Hunting toggles on, DX12 clears old command-list IA state and bumps the command-list tracking generation. Command lists can be recorded before hunting becomes active, and carrying their stale IA metadata into the first hunt frame makes the IB list look like historical command-list contents instead of the currently hunted scene.

DX12 command-list hooks observe draw recording, while DX11's immediate context hunting observes work as it is issued. To keep IB/VB counts closer to DX11, DX12 draw hooks should record per-command-list hunt summaries, and `ExecuteCommandLists` should publish those summaries into the global visible overlay sets at submission time. This avoids adding IA hashes from command lists that were recorded but not submitted in the hunted frame.

`ExecuteCommandLists` should submit to the original queue before DX12 bookkeeping such as submitted-list registration, command-list hook installation, and hunt summary publishing. Those operations can involve COM `QueryInterface`, hook-manager locks, and Hunting containers. Doing them before the original queue submit can delay the game's GPU submission enough to look like a hang during rapid Num0 stress. Post-submit bookkeeping keeps the game submission moving and does not lose observability for already recorded command lists, because pre-submit hook installation cannot retroactively observe their past recording calls.

The hunting lock should only protect copied command-list state and visited-selection containers. PSO metadata lookup and IA hash calculation happen after the command-list state is copied out, so hunting does not hold its own lock while entering shader dump or resource tracker locks.

Hot-path hook activation must also include command lists that were created before hunting was toggled. DX12 games commonly reuse command lists, so activating only swapchain and command queue hooks is insufficient. When hunting is enabled, command lists submitted through `ExecuteCommandLists` should be offered to the command-list hook installer even if their lifecycle state was already registered earlier.

The D3D12 API shape supports this design: `DrawIndexedInstanced` provides the first index location and index count for the draw, while `D3D12_INDEX_BUFFER_VIEW` and `D3D12_VERTEX_BUFFER_VIEW` provide the bound GPU address, size, format, and stride. A draw-region hunting identity must therefore combine both pieces of state.

The shared helper mirrors the DX11 region rules: IB offset is `indexStride * FirstIndex`, IB size is `indexStride * IndexCount`, VB offset is `stride * FirstVertex`, and VB size uses the explicit vertex count or the DX11 indexed-draw estimate `(3 * IndexCount + 10) / 20 + 3`. For indexed draws DX12 passes `BaseVertexLocation` as the VB first vertex because DX11 stores that value in `DrawCallInfo::FirstVertex`.

`DX12HashIaBufferView` must include the resolved resource offset and region size when a GPU address resolves to a known buffer resource. Otherwise two regions inside one buffer collapse to the same hash and the region mechanism becomes indistinguishable from whole-resource matching.

F8 IA dump rows intentionally keep both region fields and original view fields. `gpu_va`, `offset`, and `size` describe the region that was hashed and dumped. `view_gpu_va`, `view_size`, and `view_offset` describe where that region came from inside the original IA view.

F8 IA deduped buffer files require a resource-creation-time buffer VA registry. Most IB/VB resources are created long before F8 is pressed, so enabling resource metadata only during frame analysis cannot resolve IA GPU virtual addresses back to `ID3D12Resource`. DX12 should keep lightweight buffer resource metadata from creation hooks for the process lifetime and use the captured IA region to resolve and dump only the selected frame's byte ranges.

The buffer VA registry is a hot-path structure because Hunting region hashes resolve IA GPU virtual addresses during draw recording. It must support ordered range lookup instead of scanning all recorded resources per draw. A linear resource-history scan turns Hunting activation into frame-time collapse once F8-compatible creation-time buffer metadata is enabled.

Resolved buffer ranges should also be cached at buffer granularity, not at individual region start addresses. Region hashing can touch many offsets inside the same IB/VB resource in one frame, so a region-keyed cache still misses repeatedly and forces shared resource-lock traffic. A small thread-local range cache plus a global ordered range cache keeps repeated draw-region hashes on the lock-free path once the owning buffer has been resolved.
