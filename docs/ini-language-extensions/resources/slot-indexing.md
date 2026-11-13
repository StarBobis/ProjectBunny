# Dynamic Slots and Slot Ranges

## Dynamic single slots

An expression in brackets selects a pipeline slot at execution time:

```ini
[Constants]
global $slot = 0
[ResourceSaved]
[CommandListCapture]
ResourceSaved = ref ps-t[$slot]
```

Dynamic slots support shader `t`/`cb`, vertex buffers `vb`, render targets `o`, stream outputs `so`, and pixel/compute UAV `u` slots. Ordinary D3D11 stage/slot limits apply. Invalid or nonfinite values are rejected before integer conversion. An invalid dynamic source is treated as an absent resource: ordinary assignment can therefore clear its destination, while `unless_null` preserves it. An invalid dynamic destination leaves the binding unchanged. This differs from an invalid range, which skips the range operation. Numeric fractions truncate after range validation. Dynamic slots are evaluated at runtime; input-layout `ElementFormat`/`ElementOffset` overrides still require fixed vertex-buffer slots.

## Inclusive ranges

Ranges are available for resource-copy assignments: shader `t` and `cb` slots in all six stages, and pixel/compute `u` slots. Endpoints are inclusive runtime expressions. Examples below belong in a command-list context with declared resources/pools:

```ini
ps-t[0:7] = ref PoolSaved[0:7]
PoolSaved[0:7] = copy ps-t[0:7]
ps-t[0:3] = ref ResourceTexture
ps-t[0:3] = null
```

A single source resource broadcasts to a destination slot range. Both explicit ranges must contain the same number of elements. Slot bounds must be finite, nonnegative, ordered and below the slot limit; invalid ranges skip the operation. Validation applies before truncation to integer indices.

Pool ranges always select **physical Ring-wrapped slots**, including for Static/FIFO/spatial pools. They do not perform keyed lookup or change key mappings. Their span/count cannot exceed capacity. This syntax selects resource elements; `$Pool[first:last]` is not supported.

## Inherited bounds

Either side can inherit its range from the other:

```ini
ps-t = ref PoolSaved[0:7]
ps-t[0:7] = ref PoolSaved
PoolSaved[0:7] = copy ps-t
PoolSaved = copy ps-t[0:7]
```

Bare stage `t`/`cb`/`u` targets are accepted only where the other side supplies pool bounds. A slot range inherited from a pool cannot wrap through the pool's end or exceed the pipeline slot limit; a pool range inherited from slots cannot exceed its capacity.

## Options and execution

Explicit `ref`, `copy` and other resource-copy options retain their ordinary meaning. Default slot binding uses references; fetching slots into a pool defaults to copying. `unless_null` preserves the old destination for an absent source, and a skipped pool fetch does not allocate or change its element's type/timestamp. `no_view_cache` retains its normal view-cache meaning.

The implementation can group compatible bindings, but ranges do not promise one native call for every option or target type. In particular, do not infer a constant-buffer copy's execution strategy from the compact syntax. A plain CB reference restores a whole-buffer binding; an explicit `Region` source is supported by scalar operations, not range operations.

`dump` accepts explicit slot ranges and distinguishes slot output filenames. Bare inherited targets and pool ranges are not dump sources. Other commands do not implicitly accept range syntax. See [slot capture](../examples/slot-range-capture.md) and [slot scan](../examples/slot-scan.md).
