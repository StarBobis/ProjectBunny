# Input Control

DX11 bindings can be disabled without changing the game's own input handling:

```ini
[Input]
input = 1
toggle_input = ctrl alt shift VK_END
input_disable_mode = mods
```

`input` accepts the usual boolean values, including `on` and `off`. Its initial value is read once; runtime toggles survive configuration reloads. The supplied configuration template sets `input = 1`. If the key or entire `[Input]` section is missing, the parser defaults to **on**. Invalid input values use that same default and produce a configuration warning.

`input_disable_mode` is read on each configuration load/reload and accepts `mods` (default) or `all`. Invalid mode values fall back to `mods` with a warning. It changes which bindings are paused while runtime input is off:

| Runtime state | Mode | Recursively included mod keys | Other XXMI keys | `toggle_input` |
| --- | --- | --- | --- | --- |
| On | Either | Enabled | Enabled | Enabled |
| Off | `mods` | Paused | Enabled | Enabled |
| Off | `all` | Paused | Paused | Enabled |

The toggle has an explicit privileged classification; registration order does not grant that privilege. A missing or invalid toggle binding does not allow the first ordinary action through. The usual foreground-window and input-backend conditions still apply. Replace the older `[System] disable_input`/`toggle_input` settings with the `[Input]` options shown above.

Mod classification follows the actual INI file loaded by `include_recursive`, including nested recursive includes. Windows case, path separators and `.`/`..` are normalized with a directory boundary, so `ModsOther` does not match a `Mods` root. An explicit `namespace` alias does not change the source classification. Root and directly included INIs are ordinary/tool bindings; a section first loaded directly retains that provenance if the same file is encountered recursively later. A mod cycle's forward and Back bindings share its classification.

Actions selected by the current disable mode are paused before their input buttons are sampled. For example, if an affected HOLD is active when bindings are disabled, releasing its button does not dispatch Up until that binding becomes enabled again and the release is sampled. Disabling does not instantly restore active overrides.

If multiple bindings invoke the same HOLD, the first Down saves the original values. A repeated Down keeps that saved snapshot while still executing the activation command list and its queued post behavior. The first Up restores once; subsequent Up events do not restore again.

Regression coverage uses real registered actions and override logic with an in-memory button backend. It does not send operating-system keyboard events or verify a target game's foreground behavior.
