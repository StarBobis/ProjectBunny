# ADR 0001: DX12 Release Logging Lifecycle

## Status

Accepted

## Context

Release injection logs showed successful DX12 initialization, device and swapchain hooks, then repeated `D3D12SerializeRootSignature` entries. The runtime already had `DX12EndReleaseStartupLogging`, but the release predicate ignored that state and returned true for every function.

This made release logging effectively global for the process lifetime. Even with asynchronous file writes, each logged hook call still performs formatting, queue locking, string storage, and wakeups.

## Decision

Release builds log all functions only during startup. After startup ends, normal JSON function logging is limited to low-frequency structural events unless `MIGOTO_DX12_RELEASE_LOGS=1` is set.

Plain `Log` messages remain enabled after startup so critical one-off status and shutdown messages still reach the log, but repeated polling logs must be rate limited.

## Consequences

The normal release hot path avoids logging pressure from high-frequency D3D12 exports and hooks while preserving enough structure to locate startup crashes.

Targeted full release logging remains available through an explicit environment variable.

Future diagnostics should add precise logs or debug-only logs instead of restoring always-on release hook logging.

First-frame crash probes should prefer one-shot flushed boundary events around the real API call and hook return path. This keeps the release hot path quiet while still separating original API failure from wrapper-tail failure and post-return termination.

An empty runtime configuration should not install frame hot path hooks. If future diagnostics or hunting need hotkeys with no active mod, that should be represented as an explicit diagnostic or hunt mode that turns the hot path on deliberately.

Num0 hunting is that explicit mode. DX12 hotkeys are detected by a lightweight input thread, which shows the overlay immediately and then activates cached swapchain and command queue hot path hooks. Present-side polling is only a backup when the input thread is unavailable.
