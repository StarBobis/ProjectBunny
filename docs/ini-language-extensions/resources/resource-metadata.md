# Resource Metadata

Resource metadata can be accessed using two operators:

- `@` returns a numeric identity for the underlying D3D11 resource.
- `->` retrieves resource attributes such as size and stride.

<a id="resource-identity"></a>

## Resource Identity (`@`)

Returns a numeric (floating point) identity value for the underlying D3D11 resource.
* Can be used with any `ResourceCopyTarget` entity, such as `@ResourceCustom` or `@vs-t0`.
* Returns `0` if no underlying D3D11 resource is available.

```ini
$resource_id_custom = @ResourceFoo
$resource_id_d3d11 = @vs-t0
```

The identity value is derived from the **C++ pointer** to the underlying D3D11 resource, not resource contents. It is a compact hash: collisions and reuse after destruction are possible. It is not a unique persistent ID or a replacement for a data hash.

> It's essentially a 30-bit hash of 48-bit virtual memory address (standard x86-64 processors typically don't use upper 16 bits). This allows the value to fit into INI variables, which use IEEE-754 floats, where upper 7 bits become the exponent, and lower 23 bits become the mantissa.

### Non-null Resource Check

Resource identity provides an efficient way to check for non-null resource:

```ini
if @ResourceFoo
    ; Do something
endif
```

This tests resource presence without comparing resource contents. No timing comparison is promised here.

## Resource Attributes (`->`)

Resource attributes can be accessed using the `->` syntax.

### `BindFlags`

Returns the numeric D3D11 bind flags. Custom resources expose their stored bind flags; pipeline targets use the underlying physical resource descriptor. Combine the result with [D3D11_BIND enum literals](../expressions/literals.md#d3d11_bind_flags-enum-literals). No resource or unavailable state yields `0`.

```ini
$is_shader_resource = ResourceFoo->BindFlags & D3D11_BIND_SHADER_RESOURCE
```

### `Array`

Returns the full array size of a 1D or 2D texture. The selected SRV array window does not change this value. Without a custom override, 3D textures and buffers return `NOT_A_TEXTURE` (`-4`).

### `Mips`

Returns the full mip count of a 1D, 2D or 3D texture, rather than the selected view's mip count. A buffer returns `NOT_A_TEXTURE` (`-4`).

For both `Array` and `Mips`, a custom resource's explicit override takes priority, including `0`. The default override `-1` requests actual resource metadata; an unavailable resource returns `RESOURCE_NOT_FOUND` (`-2`).

```ini
$array_size = ps-t0->Array
$mip_count = ResourceTexture->Mips
```

### `Format`

`Format` returns the numeric [DXGI format value](../expressions/literals.md#dxgi_format-enum-literals). Configured custom-resource overrides take priority over stored metadata; view format is preferred over the underlying texture format. Buffers can expose a typed view format or `DXGI_FORMAT_UNKNOWN`.

Named `DXGI_FORMAT_*` values can be used as numeric expression literals. An invalid format name is rejected rather than silently becoming `UNKNOWN`.

### `Width`

Returns the full texture resource's width in texels, rather than the width of the selected view mip or array slice. A configured custom width override may be returned before instantiation. Otherwise a buffer returns `NOT_A_TEXTURE` (`-4`), and an unavailable resource returns `-2`.

### `Height`

Returns the full texture resource's height in texels. Without an explicit custom height override, a texture1D reports `1`. As with width, a configured custom height override may be returned before instantiation; the selected view mip does not change the underlying resource extent.

```ini
$width = vs-t0->Width
$height = ResourceFoo->Height
if vs-t0->Format == DXGI_FORMAT_R8G8B8A8_UNORM
    ; Apply format-specific handling.
endif
```

### `Offset`

Returns the byte offset of the currently configured buffer region.

For constant buffers, index buffers, and vertex buffers, the offset is derived from the corresponding D3D11 region parameter:

* Constant buffers — `FirstConstant`, converted to bytes
* Index buffers — binding offset plus the active draw's first-index contribution
* Vertex buffers — binding offset plus the active draw's first-vertex contribution

```ini
$offset = vs-cb0->Offset
$offset = ib->Offset
$offset = vb0->Offset
```

### `Size`

Returns the buffer size in bytes. A constant-buffer slot reports its active window size; a custom resource can report its stored size metadata. Offset queries currently apply to VB, IB and CB slots; do not assume every custom resource exposes a meaningful `Offset`.

```ini
$size = ResourceFoo->Size
```

### `Stride`

Returns the resource stride in bytes.

```ini
$stride = ResourceFoo->Stride
```

### `SourceStride`

Returns the stride of the source resource used to populate the target **custom** resource.

```ini
[ResourceBar]
type = Buffer
stride = 42
filename = Bar.buf

[PoolFoo]
pool_size = 2
type = Buffer
format = R32_FLOAT

[CommandListStrideExample]
PoolFoo[0] = copy ResourceBar

$bar_stride = PoolFoo[0]->SourceStride ; 42
$foo_stride = PoolFoo[0]->Stride       ; 4
```

Pipeline slots, such as `vb0`, are not supported by `SourceStride`.

### Performance

Some custom-resource attributes use metadata stored directly with the custom resource.

```ini
ResourceFoo->Size
```

Pipeline-slot attributes may require querying the underlying D3D11 resource.

```ini
vb0->Size
```

No access-time measurements are supplied for this build.

### Error Values

Attribute getters return the following values when data retrieval fails:

| Value   | Meaning                                                   |
| ------- | --------------------------------------------------------- |
| `-1.0f` | `UNKNOWN` — all data sources returned `0`                 |
| `-2.0f` | `RESOURCE_NOT_FOUND` — no buffer was found for the target |
| `-3.0f` | `NOT_A_BUFFER` — the target is not a D3D11 buffer         |
| `-4.0f` | `NOT_A_TEXTURE` — a width/height query reached a non-texture resource |
