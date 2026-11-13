# Resources

## Template Resource

For resource-type elements, the pool uses an internal **template resource** to initialize each element. By default, the template uses the same option defaults as a `[Resource]` section with no options specified.

When a pool is parsed:

1. An internal **template resource** is created.
2. It stores all resource options and (optional) initial data.
3. Individual pool resources are instantiated from this template when first used.

To override template defaults, specify any `[Resource]` section options directly in the `[Pool]` section:

```ini
[PoolFoo]
pool_size = 4

type = Buffer
data = "Hello World!"
```

Every resource created within the pool inherits the template configuration and initial data.

## Pool Resource Access

A pool element resource is accessed using its element index in square brackets.

Except for Static pools, the element index is evaluated as an INI expression at runtime:

```ini
PoolFoo[0]
PoolFoo[$index]
PoolFoo[$index + 1]
```

### Reading an Unknown FIFO UID

When an unassigned UID is requested for a [FIFO-indexed](indexing.md#fifo-indexing) pool, the **template resource** is returned by default without allocating a slot. Spatial misses follow the same policy. `pool_allocate_slot_on_missing = 1` restores allocating reads. Empty pools and invalid Ring indices do not return the missing template.

## Pool Resource Assignment

Pool element resources support assignment in the same way as resources declared in a `[Resource]` section.

Both copy types are supported:

```ini
PoolFoo[$index] = ref ResourceText  ; Reference Copy (cheap).
PoolFoo[$index] = copy ResourceText ; Full Copy (expensive).
```

> `ref` shares the underlying D3D resource. `copy` copies the selected source contents, including a selected buffer region when present; it need not copy the entire backing allocation.

## Pool Resource Usage

Pool element resource can be used anywhere a custom resource is accepted.

```ini
PoolFoo[$index] = copy cs-t0
vs-t0 = ref PoolFoo[$index]
```

Creating or initializing a pool element does not immediately instantiate its underlying D3D resource.

Like resources declared in a [Resource] section, a pool resource is instantiated only when first used.

As a result, initialized pool elements do not consume VRAM until their underlying resources are required at runtime, such as when bound to a pipeline slot.

## Resource and Variable Reset

Whole-pool element targets distinguish resources from variables:

```ini
PoolFoo[*] = null   ; Mark pool resources null; preserve variable values.
$PoolFoo[*] = null  ; Reset variable values to their configured default; preserve resources.
```

A mixed element keeps its other type's state when only one kind is reset. Locked variables remain unchanged. Resource reset clears region/CPU-cache metadata and marks the element null; the underlying D3D allocation and cached view may remain available for reuse. A subsequent resource read still returns null until it is populated again.

The `$` prefix is required for a variable-range expression. An invalid resource source is rejected instead of being retried as a variable assignment. For a normal pool, `PoolFoo = null` also resets indexing state. For a proxy, it detaches the source without resetting that source pool's elements.

## Physical ranges

`PoolFoo[first:last]` selects physical slots with Ring wrapping, regardless of the pool's keyed indexing mode. It can be paired with a matching pipeline slot range for resource assignment. It does not perform FIFO/spatial key lookup. See [slot ranges](../resources/slot-indexing.md).
