# Constant-Buffer Region

Use an explicit absolute backing-buffer region to pass a subrange to another shader stage. Here the game-visible CB region is assumed to contain a 256-byte header followed by 3,072 bytes of bone data. Replace the illustrative hash, verify the physical buffer bounds and supply a compatible `Skinning.hlsl` compute shader and game-specific dispatch dimensions.

```ini
[Constants]
global $base = 0
[ResourceBones]

[TextureOverrideBones]
hash = 1234abcd
$base = vs-cb1->Offset
ResourceBones = copy vs-cb1->Region($base + 256, 3072)

[CommandListBindBones]
; Reference the same backing data without copying:
$base = vs-cb1->Offset
cs-cb0 = ref vs-cb1->Region($base + 256, 3072)
; Integration hook: dispatch the external skinning consumer here.
; Restore the previous cs-cb0 resource and window afterwards.
```

`Region` arguments are absolute byte offsets and byte lengths. The addition to `$base` is intentional; Region does not add the source view offset itself. A copy must fit the physical buffer and places the selected bytes in the destination copy. Merely checking a CB's visible `Size` does not prove the physical allocation is that large.

A CB reference requires offset and length divisible by 256, with a maximum window of 65,536 bytes. The hardware-visible reference window may extend beyond the physical buffer (out-of-range reads return zero), whereas copying that same out-of-bounds range is rejected. A plain CB `ref` without Region restores whole-buffer binding, so preserve explicit windows when required. See [resource regions](../resources/resource-regions.md). This pattern has not been run against a target game.
