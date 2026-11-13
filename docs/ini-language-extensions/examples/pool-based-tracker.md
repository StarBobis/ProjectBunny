### Pool-Based Tracker

Suppose we want to keep the shapekey calculation results for an object from the last two frames while the game processes all objects in a single batch. The hashes `0123456789abcdef` and `12345678` below are placeholders: replace them with the game's actual compute-shader and resource hashes. This complete game-specific example has not been executed as part of the documentation import.

```ini

[PoolHistory]
pool_size = 2
max_copies_per_frame = 1

[ShaderOverrideShapeKeyCS]
hash = 0123456789abcdef
allow_duplicate_hash = true
CheckTextureOverride = cs-u0

[TextureOverrideCSU0]
hash = 12345678
post PoolHistory[FRAME_NUMBER] = copy cs-u0
```
Because `PoolHistory` is a `Ring` pool and `FRAME_NUMBER` is used as the pool index, it automatically wraps to one of the two available slots.

With `pool_size = 2`, the resources are stored as follows:

```
Frame 0 -> PoolHistory[0]
Frame 1 -> PoolHistory[1]
Frame 2 -> PoolHistory[0]
Frame 3 -> PoolHistory[1]
```

On frame 2, `PoolHistory[0]` is overwritten with the new result. If the matching output is captured every frame, the pool contains the results from the two most recent frames; missing matching dispatches leave older contents.

Both `PoolHistory[0]` and `PoolHistory[1]` can then be used as inputs to a custom compute shader, for example to calculate per-vertex movement vectors.
