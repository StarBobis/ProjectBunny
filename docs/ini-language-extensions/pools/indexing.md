# Pool Indexing

Pool indexing strategy controls how pool elements are accessed and assigned.

## Static and dynamic indices

Indices may be literal values or runtime expressions. Ring indexing requires finite numeric indices, truncates fractions and wraps them into the slot range. FIFO and spatial keys use float bit patterns, so they can distinguish keys that ordinary numeric equality would not. Empty pools and invalid Ring indices do not fall back to the template.

## Ring indexing

Ring indexing treats the pool as a circular array.

### Negative indices

Negative indices wrap from the end of the pool.

For a pool of size 4:

| Expression    | Pool Index |
| ------------- | ---------- |
| `PoolFoo[-1]` | `3`        |
| `PoolFoo[-2]` | `2`        |
| `PoolFoo[-3]` | `1`        |
| `PoolFoo[-4]` | `0`        |

### Overflow

Indices automatically wrap around the pool size.

For a pool of size 4:

| Expression   | Pool Index |
| ------------ | ---------- |
| `PoolFoo[0]` | `0`        |
| `PoolFoo[1]` | `1`        |
| `PoolFoo[2]` | `2`        |
| `PoolFoo[3]` | `3`        |
| `PoolFoo[4]` | `0`        |
| `PoolFoo[5]` | `1`        |
| `PoolFoo[8]` | `0`        |

This allows continuous cyclic access without manual modulo operations.

## FIFO indexing

Unlike Ring mode, the value inside brackets is treated as a user-defined key (UID), rather than a direct pool index.

Any floating point value or INI variable can be used as a UID:

```ini
PoolFooFIFO[123]
PoolFooFIFO[-987.654]
PoolFooFIFO[$object_id]
```

The same UID always resolves to the same pool element until its assignment slot is recycled.

### Read Access

When new UID is accessed:

1. If UID is found, returns previously assigned element.
2. If UID is not found, returns default element value (for resource, it's resource template).

This default read does not allocate or evict a slot. `pool_allocate_slot_on_missing = 1` enables the older allocating-read behavior. Assignments always resolve an actual element and never write the shared missing template.

### Assignment

When assignment happens for UID:

1. If UID already exists, assignment re-uses previously assigned element slot.
2. If UID is new, assignment uses the next non-assigned element slot.
3. When the pool is full, the oldest assigned UID is evicted and insertion writes emptied element slot.

### Recycling

For a pool of size 2, initial assignments:

```ini
PoolFooFIFO[1.0] = ref Resource10
PoolFooFIFO[2.0] = ref Resource20
```

produce:

| UID   | Resource   | Element Index |
| ----- | ---------- | ------------- |
| `1.0` | Resource10 | `0`           |
| `2.0` | Resource20 | `1`           |

Subsequent lookups return the same assignments:

```ini
PoolFooFIFO[1.0] ; index 0 ref Resource10
PoolFooFIFO[2.0] ; index 1 ref Resource20
```

Assigning another UID:

```ini
PoolFooFIFO[3.0] = ref Resource30
```

overwrites the oldest slot assignment:

| UID   | Resource   | Element Index |
| ----- | ---------- | ------------- |
| `3.0` | Resource30 | `0`           |
| `2.0` | Resource20 | `1`           |

UID `1.0` is no longer mapped.

## Spatial indexing

`pool_index_type = spatial` uses packed spatial keys and can reuse a nearby occupied cell rather than allocate a new slot. The grid wraps each axis: 4,096 cells in X/Z and 256 in Y. The default lookup radius is one cell. On a nearby hit, the mapping follows the requested key; a moving object's successive adjacent positions can therefore retain the same slot and contents. This is approximate association, not a globally unique object identity.

A valid spatial miss uses the same missing-template policy as FIFO. Reads can refresh existing timestamps when configured, and an assignment still allocates if no nearby slot is found. Check [Declaration options](declaration.md) and [Pool metadata](pool-metadata.md) before relying on expiration or last-update values.

### Frame-time scaling of spatial reuse

The configured spatial radius is a 120 FPS reference radius. Effective reuse distance scales by `FRAME_TIME * 120` and rounds upward: radius 2 gives distance 4/2/1 at 60/120/240 FPS. A zero or invalid frame interval gives radius zero. The local 12/8/12 toroidal grid bounds the useful Chebyshev radius at 2048 cells. This changes nearby matching, not exact-key lookup or the default policy that a missing read returns the template without allocating a slot.
