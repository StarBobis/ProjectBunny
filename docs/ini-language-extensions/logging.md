# DX11 Logging

```ini
[Logging]
log_level = warning
show_warnings = 0
```

`log_level` accepts `disabled`, `warning`, `info` or `debug`, case-insensitively. The default is disabled. Warning-level logging includes the warning/overlay-notice pathways; info adds ordinary information logs, and debug adds detailed debug logs. An unavailable log file suppresses file output safely.

A recognized level takes precedence over legacy `calls` and `debug`. In particular, explicit `log_level = disabled` keeps logging off even with `calls = 1` and `debug = 1`.

If `log_level` is absent, empty or invalid, the loader falls back to the legacy options: `calls = 1` selects info, and adding `debug = 1` raises it to debug. `debug = 1` by itself does not enable logging. For example, an unrecognized level with both legacy flags enabled selects debug. With neither legacy flag enabled, fallback leaves logging disabled.

Changes take effect when the configuration is loaded or reloaded, even if the same file stream remains open. The early logging setup reads the new configuration directly before the main INI structures are available.

`show_warnings` controls warning notices and tones independently of file verbosity. A warning can therefore reach the file while screen warnings are disabled. Early INI parsing uses the new configuration's warning-display setting. Queue tests do not establish actual overlay rendering or audio behavior in a game.

Persistent settings remain saved at normal safe points and during explicit reload transactions. DLL detach does not run persistent-setting saves or stdio cleanup while the loader lock is held. Actual dynamic DLL unload behavior remains unverified by the regression executable.
