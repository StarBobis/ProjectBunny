# GPU Readback

Use `store` only when the CPU-side INI needs an actual GPU float. Buffer identity and size getters are alternatives when content is unnecessary. A blocking staging `Map` can wait for the required GPU copy and dependent work; its cost depends on the workload. No frame-rate measurement is supplied.

```ini
[Constants]
global $distance = 0
global $last_sample_frame = -30

[TextureOverrideMeasure]
hash = 1234abcd
if FRAME_NUMBER - $last_sample_frame >= 30
    ; Read float 4 of the currently visible CB region.
    store = $distance, vs-cb1, vs-cb1->Offset / 4 + 4
    $last_sample_frame = FRAME_NUMBER
endif
```

The backing buffer must contain the requested float. The explicit frame guard prevents repeated samples across many draws in one sampling frame. A valid index beyond that buffer, or a readback failure, writes zero; a missing/null source leaves `$distance` unchanged. This example still advances the sampling frame, so add a game-specific retry policy if needed. `store` has no success-result variable and a zero result alone cannot distinguish failure from a successfully read zero.

`store`'s offset is an **absolute backing-buffer float index**, not a byte offset and not relative to a CB/SRV view. The addition above converts a visible CB byte offset to that absolute index. `Region`/hash APIs use different units; do not interchange their arguments.

For GPU-computed results, supply an external compute shader, declare its output resource with the correct format/stride and bind flags, dispatch it, then read the result. Packing several outputs can simplify that shader interface, but each GPU-path `store` still performs its own copy and blocking `Map`. Reusing a staging allocation does not combine stores into one readback. Structured sources copy complete structures as required.

## Reading an available CPU snapshot

```ini
[Rendering]
cache_resource_data = constant_buffer
```

For selected tracked buffers, `store` first checks for a valid CPU snapshot covering the requested value. Such a hit avoids the staging copy/Map. Missing or invalidated cache data still requires readback; GPU-produced results are not made CPU-resident merely by listing their buffer type. Keep guards where readback remains possible. No frame-rate improvement has been measured here.

## Per-object output

```ini
[Constants]
global $object_id = 0

[PoolMeasurements]
pool_size = 32
pool_index_type = fifo
pool_variable_default_value = -1

[TextureOverrideMeasureObject]
hash = 5678abcd
$object_id = @vs-cb1
store = $PoolMeasurements[$object_id], vs-cb1, vs-cb1->Offset / 4 + 4
```

Replace the illustrative hash and choose a stable key for the actual workload. Resource identity can collide and does not distinguish objects sharing a buffer. Pool assignment happens after a successful read; missing or invalid source data leaves the prior value untouched. This example is not a complete tested game configuration.

Caching `@vs-cb1` can avoid repeated work only under an explicit stable-content assumption: an unchanged resource pointer can still contain changed data, and identity hashes can collide. Use a suitable update signal rather than calling identity equality a content comparison. See [Store](../commands/README.md#store). This example has not been run as a complete game configuration.
