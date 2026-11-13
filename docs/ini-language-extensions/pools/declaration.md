# Declaring Pools

Pools are declared using a dedicated `[Pool]`-type section. Each pool section must have a unique name, such as `[PoolFoo]`.

## Pool Size

Specifies the maximum number of elements that the pool can contain. The default maximum pool size is `1`.

Use `pool_size = X`:
```ini
[PoolFoo]
pool_size = 4 ; Increase the maximum pool size to 4.
```

## Element Initialization

By default, pool element objects are initialized lazily. A missing FIFO or spatial read returns its default template without allocating a slot; see [Indexing](indexing.md).

To force immediate initialization, set `pool_lazy_initialization = 0`:

```ini
[PoolFoo]
pool_lazy_initialization = 0 ; Initialize all elements at parse time.
```

Eager initialization creates the element objects during parsing. This consumes CPU memory; underlying GPU resources are still instantiated separately when needed. No fixed per-element memory cost is promised.

## Pool Index Types

### Ring

Provides array-like access with support for negative indices and index wrapping.

Ring is the default index type; `pool_index_type = ring` can be omitted.

Use `pool_index_type = ring`:

```ini
[PoolFoo]
pool_size = 4 ; Index -1 maps to index 3, while index 4 wraps to index 0.
pool_index_type = ring
```

See [Ring Indexing](indexing.md#ring-indexing) for more details.

### FIFO

Provides map-like access with floating-point keys and evicts the oldest element when the pool is full.

Use `pool_index_type = fifo`:

```ini
[PoolFoo]
pool_size = 4 ; When a fifth element is inserted, the oldest element is evicted.
pool_index_type = fifo
```

See [FIFO Indexing](indexing.md#fifo-indexing) for more details.

## Default Element Values

### Resources

Resource-type pool elements use an internal **template resource** for their configuration and initial data. Any `[Resource]` section options specified in the `[Pool]` section override the template defaults.

```ini
[PoolFoo]
pool_size = 4

type = Buffer
data = "Hello World!"
```

See [Template Resource](resources.md#template-resource) for more details.

### Variables

`pool_variable_default_value` sets the initial/reset value of each variable (default `0`). Set `pool_persist_variables = 1` to persist Ring-indexed variables; persistence is not supported for FIFO or spatial variable keys.

### Lifetime and compatibility options

* `pool_element_type_switch_reset` defaults to `1`: changing an element between resource and variable usage resets the previous type. Setting `0` retains both kinds of state.
* `pool_allocate_slot_on_missing` defaults to `0`: a missing keyed read returns the template. Setting `1` restores allocating reads.
* `pool_expiration_timeout_frames` defaults to disabled. A nonnegative value expires an element when its last update is older than that many frames.
* `pool_expiration_reset_elements` defaults to `1`: expiration also resets stored contents.
* `pool_expiration_refresh_on_read` defaults to `0`: enable it to refresh an existing element's timestamp on reads as well as writes.

The older names `pool_keep_alive_frames` and `pool_reset_expired_elements` remain accepted. If both old and new names appear, the new name takes precedence.
