# ADR 0002: DX12 Hunting IA Region Hashes

## Status

Accepted

## Context

DX12 hunting could toggle on, but navigation keys such as Num7 and Num8 did not reliably switch visible green selections. The IB list also grew too large. DX11's hunting code uses draw-context region hashes when region hashing is active and purges visited IB/VB hashes from the overlay on a frame lifetime.

DX12 was recording IA hashes from the bound buffer view itself. That made large or reused buffers noisy, and it risked a mismatch between what the overlay selected and what the draw hook skipped.

There was also a hook activation issue: command lists created before hunting was toggled could already be registered, preventing later hot-path command-list hook installation when hunting became active.

## Decision

DX12 hunting IA selection now uses draw-region hashes derived from the IA view plus draw arguments. `DrawInstanced` and `DrawIndexedInstanced` pass counts and start locations into hunting, and both selection recording and skip checks use the same computed hash.

Visited IB/VB hashes are aged at Present with a short frame lifetime to keep the overlay focused on currently visible draw work instead of all historical bindings.

When command lists are submitted after hunting activation, the command-list hook installer runs even for command lists whose lifecycle metadata was already registered.

Hunting copies PSO and IA state while holding the hunting lock, then performs shader metadata lookup and IA hash computation outside that lock before writing back the resulting visited hashes.

## Consequences

Num7/Num8 should cycle a much smaller, visible-frame-oriented IB set. The copied hash, overlay text, and green/skip behavior now refer to the same IA region.

The hunting hot path still avoids COM ownership and driver calls while holding the hunting lock. It stores copied D3D12 view metadata and computes CRC-based view hashes from already-known values.

DX12 still has broader fallback-style paths in command-list original lookup, resource loading, dispatch probe state, overlay fallback, and resource dump copy. Those were not part of this fix and should be reviewed separately before being treated as final architecture.
