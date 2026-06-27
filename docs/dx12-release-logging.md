# DX12 Release Logging

Release builds must not keep ordinary hook logging enabled after runtime startup.

Root signature serialization can be called many times while a game builds pipeline state. That path is a normal D3D12 loading path, not an error signal. Logging every call after startup adds string formatting, queue locking, allocation, and log-thread pressure to a path that should mostly forward to the real D3D12 export.

The release log lifecycle is:

1. Enable startup logging when the log file opens.
2. Record initialization, config, hook install, and mod runtime load status.
3. Disable startup logging at the end of `BunnyDX12RuntimeInitialize`.
4. Keep low-frequency structure logs after startup for hook install, device, swapchain, direct command queue selection, first command submission, and first present.
5. Re-enable full release JSON logs only with `MIGOTO_DX12_RELEASE_LOGS=1` for targeted diagnosis.

If a future hang log ends at a repeated D3D12 export such as `D3D12SerializeRootSignature`, first verify release logging did not stay globally enabled before investigating mod ini syntax.

Repeated polling logs, such as deferred DXGI hook attempts before `dxgi.dll` is loaded, must be rate limited. A repeated plain `Log` message can corrupt the usefulness of a crash log by drowning out the last structural event.

For first-present crashes, `DX12PresentFirst` stages split the boundary:

1. `after_original_skip` means the real `Present` or `Present1` returned successfully.
2. `before_hotpath_update_skip` means present count increment completed.
3. `return_skip` means the hook reached its return statement and any later termination happened after control returned to the game.
