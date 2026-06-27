# ADR 0002: DX12 Hunting IA Region Hashes

## Status

Accepted

## Context

DX12 hunting could toggle on, but navigation keys such as Num7 and Num8 did not reliably switch visible green selections. The IB list also grew too large. DX11's hunting code uses draw-context region hashes when region hashing is active and purges visited IB/VB hashes from the overlay on a frame lifetime.

DX12 was recording IA hashes from the bound buffer view itself. That made large or reused buffers noisy, and it risked a mismatch between what the overlay selected and what the draw hook skipped.

There was also a hook activation issue: command lists created before hunting was toggled could already be registered, preventing later hot-path command-list hook installation when hunting became active.

## Decision

DX12 hunting IA selection now uses draw-region hashes derived from the IA view plus draw arguments. `DrawInstanced` and `DrawIndexedInstanced` pass counts and start locations into hunting, and both selection recording and skip checks use the same computed hash.

The draw-region hash algorithm is centralized in `DX12IaRegionHash`. Hunting, F8 IA frame analysis dump, manifest `BindIA` hash fields, and Mod IA `[TextureOverride] hash=` matching all call this helper instead of maintaining separate hash calculations.

For indexed draws, the helper receives `BaseVertexLocation` for VB region offset calculation. This matches the DX11 `DrawCallInfo` shape, where indexed draw constructors store base vertex in `FirstVertex` and the VB region helpers consume `FirstVertex`.

When the IA GPU virtual address resolves to a tracked buffer resource, `DX12HashIaBufferView` includes the resolved resource offset and region size. This keeps regions in one packed buffer distinct while preserving resource metadata stability.

F8 IA dump tasks copy the same byte region that produced `hunt_hash`. Dump filenames include the region hash, and `BuffersDX12.csv` exposes both region and original view coordinates.

DX12 records lightweight buffer resource metadata at resource creation time even when full frame-analysis metadata tracking is disabled. Frame analysis can be requested after most IB/VB buffers already exist, and IA dump cannot produce deduped files unless the captured IA GPU virtual address can be resolved to its owning buffer resource. Non-buffer resource metadata remains gated by the hot-path tracking state.

The buffer metadata registry is indexed by GPU virtual address for ordered range lookup. Hunting IA hashes can resolve multiple buffer regions per draw, so the lookup must not scan the full resource history. DX11's region-hash path is cache-oriented around the current resource and region; DX12's equivalent must keep GPU VA resolution logarithmic and cache resolved buffer ranges.

DX12 buffer VA resolution uses a thread-local recent range cache in front of the global ordered range cache. The cache key is the owning buffer range, not each draw-region start address, because region hashing commonly walks many offsets inside the same IB/VB resource. Resource creation, cleanup, and resource-state updates bump the cache generation so stale thread-local entries are dropped before reuse.

Visited IB/VB hashes are aged at Present with a short frame lifetime to keep the overlay focused on currently visible draw work instead of all historical bindings.

When command lists are submitted after hunting activation, the command-list hook installer runs even for command lists whose lifecycle metadata was already registered.

Queue submission calls the original `ExecuteCommandLists` before submitted-command-list registration, command-list hook installation, and hunt-summary publishing. These are bookkeeping operations around submitted work and can wait until after the game has entered the driver. This avoids pre-submit stalls from COM queries, hook installation, and Hunting container work during rapid Hunting toggles.

Hunting copies PSO and IA state while holding the hunting lock, then performs shader metadata lookup and IA hash computation outside that lock before writing back the resulting visited hashes.

## Consequences

Num7/Num8 should cycle a much smaller, visible-frame-oriented IB set. The copied hash, overlay text, green/skip behavior, F8 dump hash, and Mod matching hash now refer to the same IA region.

The hunting hot path still avoids COM ownership and driver calls while holding the hunting lock. It stores copied D3D12 view metadata under the hunting lock, then computes region hashes after releasing it.

DX12 still has broader fallback-style paths in command-list original lookup, resource loading, dispatch probe state, overlay fallback, and resource dump copy. Those were not part of this fix and should be reviewed separately before being treated as final architecture.
