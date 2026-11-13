# Slot Range Capture

Save every slot that your draw changes, then restore it. This example saves `ps-t0` through `ps-t8`, including the slot temporarily occupied by the mask. Supply `Mask.dds` and a compatible `Character.hlsl`; the illustrative hash must be replaced. The shader's vertex inputs, outputs and draw setup must match the intercepted draw.

```ini
[PoolSavedTextures]
pool_size = 9
[ResourceMask]
filename = Mask.dds

[TextureOverrideCharacter]
hash = 1234abcd
handling = skip
PoolSavedTextures[0:8] = ref ps-t[0:8]
ps-t8 = ref ResourceMask
run = CustomShaderCharacter
ps-t[0:8] = ref PoolSavedTextures[0:8]

[CustomShaderCharacter]
ps = Character.hlsl
draw = from_caller
```

The explicit custom draw replaces the skipped game draw. Restoring only `0:7` and clearing slot 8 would lose its previous resource. The INI has no exception/finally construct: verify the shader and surrounding command flow before relying on this restoration pattern.

Bounds may be expressions, provided both sides select equal counts and stay within limits. For CB ranges, distinguish a reference from a data copy and preserve any original windows that your consumer requires; a plain CB `ref` uses a whole-buffer binding. Compact range notation does not establish that CB copies use a single API call or a particular per-slot fallback. See [range semantics](../resources/slot-indexing.md). This example has not been run as a game mod.
