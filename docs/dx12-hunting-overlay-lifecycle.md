# DX12 Hunting Overlay Lifecycle

DX12 hunting overlay state must be stable across Num0 toggles.

The overlay window is owned by its window thread. Hunting enable and disable should only change visibility and text state. Runtime shutdown is the only normal path that requests window destruction and message-loop exit.

This avoids a crash-prone lifetime pattern where the input thread disables hunting, posts window close, and hooks or later input events still try to refresh, position, or recreate the overlay while the previous window is being destroyed.

Window visibility, positioning, and repaint requests must be marshalled to the overlay window thread with private `WM_APP` messages. Input and Present threads may flip atomics, copy `HWND`/`RECT` target metadata under a short lock, and call `PostMessageW`, but they should not call `ShowWindow`, `SetWindowPos`, or `InvalidateRect` directly on the overlay window. A Release hang that stops immediately after `DX12 hunting toggled off` should first be checked for a synchronous cross-thread window call in the Hunting disable path.

The Hunting disable path should not refresh the overlay text after switching to disabled. It should record the state change and post a hide request. This mirrors DX11's toggle shape more closely: the hotkey changes the hunting mode, while overlay presentation is handled by the normal rendering/window owner path.

Hunting disable should not bump command-list tracking generation. The hot path already checks the hunting atomic before recording IA hunt data, and Present-side hot-path gating will settle after the disabled state is visible. Repeated Num0 off/on stress should not force all command-list runtime caches to refresh on every off edge.

Hunting enable and disable should call `DX12HotPathUpdate` immediately after the hunting atomic changes. The gate must not wait for the next Present, because quick Num0 stress can otherwise leave command queue and command list hooks in the full hot path after Hunting has already been disabled.

When Hunting toggles, stale IA containers should be swapped out under the Hunting lock and destroyed after the lock has been released. The lock protects only container ownership changes and selection state, not large container destruction.

Present skip paths still need to update the overlay while hunting is active. A first Present can still be in the hot-path skip mode immediately after Num0 enables hunting, so the skip branch must run the same lightweight overlay positioning step before it updates hot-path gates.

Hunting text should be rendered through the overlay window, not by drawing directly into the game window HDC. Direct HDC text is overwritten by normal frame presentation and reads as flicker. The overlay window can stay hidden until a swapchain target is known, then be positioned over the target client rect.

DX12 input polling should follow DX11's default hunting repeat cadence. DX11's default `repeat_rate` is 8 operations per second, so DX12 hunting navigation repeat uses a 125 ms interval after the initial delay. Num0 itself remains an edge-triggered toggle and resets hunting repeat-key state after toggling.
