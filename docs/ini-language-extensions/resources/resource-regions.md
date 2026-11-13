# Resource Regions

Resource regions represent byte ranges within an underlying D3D11 buffer.

Regions can be defined implicitly by pipeline state or explicitly using the `->Region(...)` specifier.

## Pipeline Resource Regions

Pipeline slots may reference only a portion of an underlying buffer. For constant buffers, this region is defined by `FirstConstant` and `NumConstants`.

Copying a constant-buffer slot normally uses its currently visible region. Referencing it retains the underlying resource and available offset metadata; a saved plain reference is not a complete snapshot of the native CB binding window.

For example:

```ini
ResourceFoo = ref vs-cb0
ResourceBar = copy vs-cb0
```

The copy reads the configured visible region. A plain scalar reference can have unknown size metadata and later fall back to the backing-buffer size. Use an explicit Region when the intended reference window must be stated; binding a plain reference directly to another CB slot selects the whole buffer.

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

Saving an explicit Region in a custom resource or pool element retains its absolute offset and byte count. Later SRV/UAV references use that selected window, and a later copy reads that window into independent storage starting at offset zero. For example, a typed four-byte buffer selected with `Region(16, 32)` exposes eight elements starting at element four; it does not treat `32` as the end offset. A plain CB reference still selects the whole buffer; specify `->Region(...)` again when binding a CB window.

The CB reference window may extend beyond the physical allocation: for a 1,024-byte source, `Region(768, 512)` is a valid reference window, but is invalid for `copy`. Only the intersection has stored data; out-of-range shader reads return zero. These window rules come from [SetConstantBuffers1](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1).

See [constant-buffer region](../examples/constant-buffer-region.md) for a worked pattern.

### Binding a stream-output target

Assume `ResourceOutput` is a sufficiently large buffer with stream-output binding support. In a command-list context:

```ini
[Constants]
global $output_offset = 64
global $output_bytes = 16

[CommandListBindOutput]
so0 = ref ResourceOutput->Region($output_offset, $output_bytes)
```

A geometry shader created with a matching stream-output declaration writes from the selected byte offset. The INI Region still validates its arguments, but the native SO API consumes **only the offset**. `$output_bytes` is not a hardware write limit: emitted records can continue beyond it within the backing buffer's capacity. The shader output layout and buffer allocation must match the actual draw.

This revision rebinds the other SO slots at offset `0`. D3D11's `SOGetTargets` returns buffers, not their bound offsets, so their previous offsets cannot be queried and restored by this command. A source such as `ResourceSaved = ref so0` reports offset `0`; an explicit `so0->Region(...)` source measures from the backing buffer's start, not from the unknown SO write position.

This is a binding pattern, not a complete game configuration. The caller must supply the resource and actual stream-output shader/draw. Referencing a Region does not create a stream-output shader.
