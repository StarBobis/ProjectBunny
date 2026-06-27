# ADR 0003: DX12 Hunting Overlay And Selection Stability

## Status

Accepted

## Context

Release Hunting showed three linked symptoms: Num0 could crash when closing green text, Num7/Num8 did not switch selections reliably, and the green overlay flickered instead of staying on screen.

The old DX12 path treated Hunting disable as overlay close, and the close message destroyed the overlay window. The overlay was owned by its own window thread while hotkeys and hooks could still refresh overlay text or request positioning. The Present skip branch also did not position the overlay during the first frames after Hunting was enabled.

DX12 IA selection also stored only a position in the current `std::set`. With the short visible-frame IA lifetime, purging or adding hashes could move the same position to a different hash. DX11 stores the selected IB/VB hash as the durable identity and treats the position as display metadata.

## Decision

Num0 Hunting disable hides the overlay window instead of destroying it. Runtime shutdown remains the path that posts a dedicated overlay destroy message so the window thread can destroy its own window and exit the message loop.

The HDC text drawing path into the game window is removed from Hunting overlay display. Hunting display uses the overlay window, and both full Present and first Present skip paths update overlay positioning while Hunting is active.

DX12 IA selection stores `selectedHash` for IB and VB. The current position is derived from the visible set when possible. If the selected hash is not visible, Num7/Num8 advance through the current visible set using the nearest lower-bound entry, matching DX11's overlay buffer lifetime behavior.

Hunting toggle clears old command-list IA state and bumps command-list tracking generation. This prevents command lists recorded before Hunting from contributing stale IA metadata to the first visible hunt set.

Draw hooks record per-command-list IA hunt summaries. `ExecuteCommandLists` publishes those summaries into the global visible overlay sets when command lists are submitted. This aligns DX12 Hunting with DX11's effective execution boundary better than adding every recorded draw directly to the overlay sets.

DX12 hunting navigation repeat uses 125 ms after the initial delay, matching DX11's default 8 repeats per second.

## Consequences

Closing Hunting no longer tears down the overlay window while hooks and input can still reference overlay state.

The green text should stay stable because it is no longer drawn directly into the swapchain target and overwritten by subsequent presents.

The IB/VB selection should be stable across visible-set churn, and the count should better represent hashes observed after Hunting activation instead of pre-toggle command-list history.

Remaining DX12 fallback-style paths still exist outside this decision, including original-function lookup fallback paths, resource path root fallback, and resource dump copy fallback. They need separate architectural review before being considered final.
