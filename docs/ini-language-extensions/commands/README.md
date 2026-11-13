# Commands

## `store`

Reads a 32-bit floating-point value from a buffer and stores it in an INI runtime variable or [pool variable](../pools/variables.md).

```ini
store = $out, ResourceFoo, $offset
```

The command takes three arguments:

1. **Output variable** — an INI variable or an explicit pool variable such as `$PoolObjects[$object_id]`.
2. **Resource target** — the pipeline slot or custom resource to read from.
3. **Offset expression** — a dynamically evaluated expression specifying which 32-bit value to read.

For example:

```ini
store = $out, ResourceFoo, 4
```

The offset is interpreted as a **32-bit value index**, so `4` reads the fifth 32-bit value from the resource.

The index is absolute in the backing buffer, even if the source CB/SRV view starts at a nonzero offset. Finite nonnegative indices within the unsigned 32-bit index range are truncated to integers. A valid index outside the physical buffer, or a failed readback, writes zero as upstream does. Missing/null or non-buffer sources, device mismatches, deferred contexts and invalid numeric indices leave the output unchanged. A locked variable cannot be the output.

### GPU Readback

`store` performs **GPU → CPU readback**. The requested data is copied from the GPU resource into a CPU-readable staging buffer and then read by the CPU.

Only the required data is read back whenever possible. For regular buffers, only the requested 4-byte value is copied. For structured buffers, the smallest range containing the required complete structures is copied.

Readback staging buffers are reused across `store` invocations rather than recreated for every call.

Despite these optimizations, GPU → CPU readback is **expensive** and `store` should therefore generally be used only when the value is actually needed by the CPU-side INI runtime, and there is no other way around it.

Each GPU-path `store` performs its own copy and blocking Map, even when staging allocations are reused. See [GPU readback](../examples/gpu-readback.md) for frame gating and offset conversion.

### Cached reads

```ini
[Rendering]
cache_resource_data = constant_buffer
```

`cache_resource_data` accepts `none`, `all` (supported VB/IB/CB types), or a space-separated selection of `vertex_buffer`, `index_buffer` and `constant_buffer`. A valid tracked CPU snapshot covering the requested absolute byte range lets `store` read that value without a staging copy/Map. Mode selection alone does not establish that a snapshot exists or is current. An uncached buffer, uncovered range or invalidated snapshot uses the GPU readback path.

`track_region_hashes = 1` also enables the VB/IB/CB cache types for compatibility. Custom resources with no tracked native snapshot use GPU readback; a captured region-hash snapshot is not automatically a Store cache entry. Physical bounds, device/context validation, locked-output protection and the output rules above apply to both paths.

### Pool output

```ini
store = $PoolObjects[$object_id], vs-cb1, 4
```

Declare the pool and index variable first. The destination follows ordinary pool-variable assignment semantics, including type switching, expiration and persistence. A physical read failure writes zero through those assignment semantics and can allocate a missing dynamic destination; a missing/null source performs no assignment. Static pool references retain their ordinary parse-time resolution. Whole-pool output and variable subranges are not supported.
