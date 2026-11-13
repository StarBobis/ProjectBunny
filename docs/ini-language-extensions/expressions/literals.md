# Literals

## Binary Literals

Binary integer literals use the `0b` prefix.

```ini
global $var = 0b01010111
```

The parser accepts **1 to 64 binary digits** after `0b`. Missing digits and literals exceeding 64 digits are rejected.

The resulting integer is converted to an IEEE-754 `float32` INI value. Integers through `2^24` are exactly representable; above that, not every integer is preserved. A 64-digit literal can therefore parse successfully while losing low bits. Use bits 0 through 23 when storing flags that must survive arithmetic and assignment exactly.

Decimal literals may omit the leading zero: `.5` and `-.5` are accepted.

## D3D11_BIND_FLAGS Enum Literals

`D3D11_BIND_*` literals evaluate to the numeric [D3D11_BIND_FLAG value](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_bind_flag). Combine them with a resource's [BindFlags getter](../resources/resource-metadata.md#bindflags):

```ini
$is_render_target = ps-t0->BindFlags & D3D11_BIND_RENDER_TARGET
```

Use numeric `0` for no bind flags; there is no `D3D11_BIND_NONE` enum literal. Unknown names are rejected. `D3D11_BIND_` and `DXGI_FORMAT_` require the full enum prefix and name.

## DXGI_FORMAT Enum Literals

`DXGI_FORMAT_*` literals evaluate to the corresponding numeric value from the [DXGI_FORMAT enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format). They can be compared with a resource's [Format getter](../resources/resource-metadata.md#format):

```ini
$format_matches = ps-t0->Format == DXGI_FORMAT_R32G32B32A32_FLOAT
```

An unknown format name is rejected; the defined `DXGI_FORMAT_UNKNOWN` constant remains a valid enum value.
