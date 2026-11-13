# Whole-Pool Operations

These operations act on a pool, rather than one element.

## Reference

```ini
[PoolSource]
pool_size = 4
[PoolAlias]
pool_size = 4

[CommandListAlias]
PoolAlias = ref PoolSource
```

The destination proxies the source's resolved root at binding time. Binding to an already proxied source captures that root; changing the intermediate proxy later does not retarget this captured reference. Existing chains can still resolve through roots subsequently redirected elsewhere. Cycles are rejected without changing the previous binding; the same command can succeed after a temporary cycle is removed.

The destination's own elements are retained while it proxies another pool. Reference bind requirements propagate through the parsed reference graph, including pool templates and existing or later-created elements. A data `copy` does not create the same reference dependency. `@PoolAlias` is a compact pool-identity hash, not a collision-free proof that two pools are identical.

## Copy configuration

```ini
[CommandListConfigure]
PoolAlias = copy_desc PoolSource
```

Copies the source pool configuration and template resource metadata, rather than its element data or proxy reference. This includes capacity, index type, lazy initialization, missing-slot allocation, spatial radius, expiration timeout/reset/read refresh, type-switch reset, variable default and variable persistence. Configuration changes may rebuild indexing/element state; do not use `copy_desc` as an element-data backup.

## Clear

```ini
[CommandListDetach]
PoolAlias = null
```

For a proxy this detaches the reference and returns to the destination's own state. For a non-proxy this clears indexing/lifetime bookkeeping; it is not an immediate overwrite of every stored resource and variable. Use `PoolAlias[*] = null` to reset resource state, and `$PoolAlias[*] = null` to reset variable state. Mixed slots preserve the opposite type. See [resources](resources.md) and [variables](variables.md).
