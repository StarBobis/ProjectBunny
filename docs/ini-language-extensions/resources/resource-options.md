# Resource Options

## `color_space`

Sets the color-space interpretation when loading a custom texture resource. The actual INI key is `color_space`; `override_color_space` is an internal field name and is not the configuration option.

```ini
[ResourceTexture]
filename = Texture.png
color_space = srgb
```

Values are case-insensitive: `srgb` forces an sRGB format, while `linear` suppresses sRGB metadata handling for WIC images. Omitted or invalid values use automatic handling; invalid values also produce a configuration warning. Pools inherit the option through their resource templates and metadata copies.

For DDS, `srgb` enables force-sRGB loading. `linear` does not strip an existing sRGB DDS format. See [Texture Color Space](loading.md) for WIC metadata, optional PNG detection, loader differences and validation boundaries.
