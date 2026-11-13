# Per-Object State

This pattern associates a variable and a previous buffer copy with an object key. Replace the illustrative hash with a real match. The key below assumes `vs-cb0` identifies one object for its lifetime; pointer-hash collisions, resource reuse and packed multi-object CBs violate that assumption. A suitable content `HashRegion` or spatial key can be used instead, with its own collision and tracking limits.

```ini
[Constants]
global $object_id = 0

[PoolObjects]
pool_size = 128
pool_index_type = fifo
pool_variable_default_value = -1
pool_element_type_switch_reset = 0
pool_expiration_timeout_frames = 1

[TextureOverrideObject]
hash = 1234abcd
$object_id = @vs-cb0
if $PoolObjects[$object_id] != FRAME_NUMBER
    if $PoolObjects[$object_id] == FRAME_NUMBER - 1
        cs-cb1 = ref PoolObjects[$object_id]
        run = CommandListConsumePrevious
    endif
    PoolObjects[$object_id] = copy vs-cb0
    $PoolObjects[$object_id] = FRAME_NUMBER
endif

[CommandListConsumePrevious]
; Integration hook: add a consumer of the previous object's CB here.
; Save/restore any pipeline bindings changed by your consumer.
```

The first draw of a new frame consumes the previous copy before replacing it; later draws do not replace it again. Mixed state is retained by disabling type-switch reset. Expiration limits stale key reuse but does not make hashes unique. Supply a game-specific consumer before expecting a visual effect. This complete configuration has not been run in a target game.
