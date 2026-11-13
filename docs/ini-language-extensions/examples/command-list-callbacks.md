# Command-List Callbacks

Keep each block in a separate included INI. A library exposes an empty hook; a consumer supplies its implementation. The explicit namespaces make cross-file references unambiguous.

Library INI:

```ini
namespace = CharacterLib
[Constants]
global $character_drawn = 0
[TextureOverrideCharacter]
hash = 1234abcd
$character_drawn = FRAME_NUMBER
run = CommandListOnCharacterDraw
[CommandListOnCharacterDraw]
```

Consumer INI:

```ini
namespace = Outfit
[Constants]
global $outfit_drawn = 0
[Present]
CommandList\CharacterLib\OnCharacterDraw = ref CommandListOutfitHandler
[CommandListOutfitHandler]
$outfit_drawn = FRAME_NUMBER
; Add outfit commands and declare any assets they need.
```

The Present assignment installs the hook before later intercepted draws; it is not an initialization guarantee for earlier draws. Assign `CommandList\CharacterLib\OnCharacterDraw = null` to detach. Cross-namespace replacement is allowed for an empty destination (both pre/post considered), not an arbitrary nonempty library implementation. Reference assignment captures the resolved root; changing an intermediate alias need not retarget an earlier binding.

A third namespace can supply a handler which explicitly runs `CommandList\Outfit\OutfitHandler` before its own commands, then attach that handler to the empty library hook. Avoid cycles and account for the recursion limit. Reload invalidates old proxy links. See [command lists](../command-lists/README.md). These files have not been run as a complete game mod.
