# Pool Variables

A pool can hold floating-point variables as well as resources. Prefix the pool name with `$`:

```ini
[Constants]
global $id = 0
global $value = 0

[PoolState]
pool_size = 32
pool_index_type = fifo
pool_variable_default_value = -1

[CommandListState]
$value = $PoolState[$id]
$PoolState[$id] = $value + 1
```

Index selection follows [pool indexing](indexing.md). Missing FIFO/spatial reads normally return the variable template without allocating a slot; `pool_allocate_slot_on_missing = 1` enables allocating reads. Assignment allocates/selects an element. Resource and variable state share a slot; with `pool_element_type_switch_reset = 1`, assignment changes its active type and resets the previous type. Setting this option to `0` retains both types; reading does not switch types.

## All physical slots

```ini
[CommandListResetState]
$PoolState[*] = 7
$PoolState[*] = null
```

A full-range expression is evaluated once and applied to every physical variable slot, independent of FIFO/spatial keys. `null` resets variable state to its configured default. Resource state is retained; locked variables are unchanged. `$PoolState[first:last]` is not a variable range syntax. Resource full-range operations are [separate](resources.md).

## Persistence

`pool_persist_variables = 1` saves initialized Ring variable slots. FIFO/spatial variable keys are not persisted. Namespace and physical slot identify a saved Ring value. Locked variables are excluded. Unknown saved values are governed by `[System] clear_unknown_settings`: its parser default is `1`, while the supplied template explicitly sets `0`. Disabled cleanup preserves unknown names and values; enabled cleanup gives a changed unknown set a reload grace period before removing an unchanged set. See [variables](../variables/README.md) for the full persistence rules.
