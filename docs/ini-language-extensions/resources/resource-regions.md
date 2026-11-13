# Resource Regions

Resource regions represent byte ranges within an underlying D3D11 buffer.

Regions can be defined implicitly by pipeline state or explicitly using the `->Region(...)` specifier.

## Pipeline Resource Regions

Pipeline slots may reference only a portion of an underlying buffer. For constant buffers, this region is defined by `FirstConstant` and `NumConstants`.

Copying a constant-buffer slot uses its currently visible region. Referencing that slot into a custom resource preserves the region metadata.

For example:

```ini
ResourceFoo = ref vs-cb0
ResourceBar = copy vs-cb0
```

Both operations use the region currently configured for `vs-cb0`, rather than the entire underlying constant buffer.

The `->Offset` attribute can be used to retrieve the absolute byte offset of the currently configured region.

See [Resource Metadata → `Offset`](resource-metadata.md#offset) for details. Binding a plain reference directly to a CB slot can restore whole-buffer binding; use an explicit Region when the destination window matters.

## Explicit Resource Regions

### `Region($byte_offset, $byte_size)`

The `->Region($byte_offset, $byte_size)` specifier selects an explicit region of the underlying buffer.

Both arguments are evaluated as byte values:

* `$byte_offset` specifies the absolute byte offset into the underlying buffer.
* `$byte_size` specifies the size of the region in bytes.

The offset is absolute in the backing buffer; it is not added to an existing view offset. Arguments must be finite and within the unsigned 32-bit range; offset must be nonnegative and size at least one. Fractional values truncate to whole bytes before range/alignment checks. The failure path leaves the destination unchanged. Selected buffer operations have WARP regressions; target-game behavior still needs validation.

### Copying a Region

```ini
cs-cb0 = copy vs-cb0->Region($byte_offset, $byte_size)
```

Only the specified region of the underlying buffer is copied. The range must fit inside the physical buffer. Copy ranges do not require 256-byte alignment: `Region(20, 32)` is valid for a sufficiently large buffer. A constant-buffer destination's allocation is rounded to its required 16-byte size alignment.

### Referencing a Region

```ini
cs-cb0 = ref vs-cb0->Region($byte_offset, $byte_size)
```

The same underlying buffer is referenced using the specified region without copying its contents. For a CB destination, offset and size must both be multiples of 256 bytes, and size must be at most 65,536 bytes. Zero size is rejected rather than interpreted as a whole-buffer binding.

The CB reference window may extend beyond the physical allocation: for a 1,024-byte source, `Region(768, 512)` is a valid reference window, but is invalid for `copy`. Only the intersection has stored data; out-of-range shader reads return zero. These window rules come from [SetConstantBuffers1](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1).
