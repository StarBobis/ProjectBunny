# DX12 Hunting Overlay Lifecycle

DX12 hunting overlay state must be stable across Num0 toggles.

The overlay window is owned by its window thread. Hunting enable and disable should only change visibility and text state. Runtime shutdown is the only normal path that requests window destruction and message-loop exit.

This avoids a crash-prone lifetime pattern where the input thread disables hunting, posts window close, and hooks or later input events still try to refresh, position, or recreate the overlay while the previous window is being destroyed.

Present skip paths still need to update the overlay while hunting is active. A first Present can still be in the hot-path skip mode immediately after Num0 enables hunting, so the skip branch must run the same lightweight overlay positioning step before it updates hot-path gates.

Hunting text should be rendered through the overlay window, not by drawing directly into the game window HDC. Direct HDC text is overwritten by normal frame presentation and reads as flicker. The overlay window can stay hidden until a swapchain target is known, then be positioned over the target client rect.

DX12 input polling should follow DX11's default hunting repeat cadence. DX11's default `repeat_rate` is 8 operations per second, so DX12 hunting navigation repeat uses a 125 ms interval after the initial delay. Num0 itself remains an edge-triggered toggle and resets hunting repeat-key state after toggling.
