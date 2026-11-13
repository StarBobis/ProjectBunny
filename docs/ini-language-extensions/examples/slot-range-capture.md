# Slot Range Capture

Capture the game's bindings before replacing them and restore them after the draw. Each side gives its own bounds: this pool has three elements for pipeline slots 3 through 5. Supply the three texture files and replace the illustrative hash with the actual draw's resource hash.

```ini
[PoolSavedTextures]
pool_size = 3

[ResourceDiffuse]
filename = Diffuse.dds
[ResourceNormalMap]
filename = NormalMap.dds
[ResourceLightMap]
filename = LightMap.dds

[TextureOverrideCharacter]
hash = 1234abcd
PoolSavedTextures[0:2] = ref ps-t[3:5]
ps-t3 = ref ResourceDiffuse
ps-t4 = ref ResourceNormalMap
ps-t5 = ref ResourceLightMap
post ps-t[3:5] = ref PoolSavedTextures[0:2]
post PoolSavedTextures[*] = null
```

`ref` retains actual resources and compatible source views instead of copying their GPU data. Restore **without `unless_null`**: a slot that was empty before the override must be unbound again. Skipping that null source would leave the mod's texture bound. Every slot the mod changes must be included in the saved range.

## Capture once for several draws

If a framework captures before several modified draws and restores at the end of that cycle, a guard prevents the second draw from saving the mod's bindings over the originals. These lists use the pool declared above:

```ini
[Constants]
global $captured = 0

[CommandListCaptureSlots]
if !$captured
    PoolSavedTextures[0:2] = ref ps-t[3:5]
    $captured = 1
endif

[CommandListReleaseSlots]
if $captured
    ps-t[3:5] = ref PoolSavedTextures[0:2]
    PoolSavedTextures[*] = null
    $captured = 0
endif
```

Call the capture list before the first replacement and the release list at the matching end of the cycle. The surrounding framework must supply those hooks; declarations do not schedule themselves. Clear the pool after restoring to drop its logical references and reset the guard for the next cycle.

Bounds may be runtime expressions. Both ranges must stay valid and contain the same number of elements; bare/inherited bounds are rejected. See [range semantics](../resources/slot-indexing.md).

For CB ranges, a plain reference binds the whole buffer, even when a captured element contains CB window metadata. Do not use this texture pattern as proof that arbitrary CB windows will be restored. Copying CB data is a separate operation with physical-buffer bounds.

The regression suite exercises this guarded pattern through production flow/assignment parsers and real WARP bindings. It does not load the external texture assets or run this complete configuration in a target game. INI has no exception/finally construct; the framework must ensure its release hook runs.
