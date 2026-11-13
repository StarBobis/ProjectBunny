# Texture Color Space

Custom resources and resource pools accept `color_space = srgb` or `color_space = linear`. Names are normalized by the INI loader. An omitted or invalid value uses automatic/default handling; an invalid value also produces a configuration warning. Pool templates, lazy elements and `copy_desc` propagate the setting.

For WIC images, `srgb` forces an sRGB texture format and `linear` suppresses sRGB metadata handling. With `[System] force_detect_color_space = true`, automatic loading additionally checks for a complete sRGB chunk before the first PNG IDAT chunk. This optional extra scan defaults to false; without it, WIC retains its normal metadata handling. Explicit resource `srgb`/`linear` settings take priority over the switch. The bounded scanner does not validate CRC checksums or the whole image, and does not itself infer sRGB from iCCP. The image decoder still validates the image and can process its own metadata.

For DDS, `srgb` sets the loader's force-sRGB option. `linear` does not force an already-sRGB DDS format back to a linear format; it follows the existing DDS loader behavior. These options change format interpretation, rather than promising a CPU pixel-color conversion.

```ini
[ResourceExample]
filename = image.png
color_space = srgb
```

The regression uses actual PNG/DDS loads, format queries and raw pixel readback. It does not establish shader-sampling equivalence across target games.
