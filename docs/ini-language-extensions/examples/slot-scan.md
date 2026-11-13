# Slot Scan

Scan bindings when a material's slot is not fixed. Replace the illustrative hash and format with the game's actual match. The empty resource receives the first matching reference.

```ini
[Constants]
global $slot = -1
global $i = 0
[ResourceNormals]

[TextureOverrideFindNormals]
hash = 1234abcd
$slot = -1
$i = 0
run = CommandListFindSlot

[CommandListFindSlot]
if $i < 16
    if ps-t[$i]->Format == DXGI_FORMAT_R8G8B8A8_UNORM
        $slot = $i
        ResourceNormals = ref ps-t[$i]
    else
        $i = $i + 1
        run = CommandListFindSlot
    endif
endif
```

A format match is only a heuristic, not proof of semantic content. If no match is found, `$slot` stays `-1`; this example retains the prior `ResourceNormals`, so explicitly clear it before scanning if that is unwanted. Resource-identity comparisons also carry hash collision/reuse risks.

Each recursive call uses the command-list recursion budget (256, including callers); choose a bound that leaves space for surrounding calls. Do not dereference `$slot` after a failed search. Metadata queries may query D3D resources; no timing result is claimed. This configuration has not been run against a target game.
