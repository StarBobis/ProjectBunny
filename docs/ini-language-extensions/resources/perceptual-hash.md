# Perceptual Texture Hashes

`TextureTarget->HammingDistance(ResourceReference)` compares the target texture's 64-bit difference hash (dHash) with the `original_dhash` stored in a custom resource reference. It returns a float containing an integer from `0` to `64`; the hash values themselves remain exact 64-bit integers. A smaller distance means fewer differing bits, not a guarantee that two images are identical.

The argument is a resource reference, such as `ResourceReference` or a resource-pool element with the appropriate metadata. It is not an inline hexadecimal hash or a numeric expression. Declare the reference using [`original_format` and `original_dhash`](resource-metadata.md#originalformat). The reference can contain metadata alone; it does not require a texture file or a native allocation.

## Results and prerequisites

| Result | Meaning |
| --- | --- |
| `0`–`64` | Available comparison; ordinary nonzero hashes use their differing-bit count |
| `-1` | Hash unavailable, unsupported target/context/format, failed computation, or unavailable reference |
| `-2` | Target resource or required runtime device/context is missing |

Zero has an explicit comparison rule: two zero hashes have distance `0`; exactly one zero has distance `64`. A successfully computed zero is valid. An omitted `original_dhash` is also stored as zero, so declare the intended reference value rather than treating an omitted value as an unavailable-reference marker. Failed computation returns `-1`, not a fabricated successful zero comparison.

The target must be a tracked, initialized **Texture2D** on the immediate context's device, with one array slice, no multisampling and shader-resource binding support. At least one eligible mip must have uploaded data recorded by the texture tracking path. A native texture created outside that tracking path is unavailable even if its pixels exist. Buffers, texture1D/3D, arrays, MSAA textures and deferred contexts do not produce a dHash through this getter.

The selected sampling format must support filtered shader sampling. A supplied view's format is used, including a typed interpretation of a typeless resource or an sRGB view. Generic SRV, RTV, UAV and DSV interfaces are identified safely; this does not make every depth or integer format sampleable. Without a view, the resource's own format is used, so a typeless resource can require a suitable typed view. The view must belong to the same resource.

## Mip selection and retained hashes

```ini
[Rendering]
dhash_mip_size = 256
dhash_allow_rehash = 0
```

These are the defaults, and configuration reload reads them again. A positive `dhash_mip_size` searches initialized mips from smallest to largest, requiring both sides to be at least that many texels; mip 0 is always a size fallback when initialized. `0` searches largest first. The selection uses the whole physical texture's mip chain, rather than restricting itself to a view's displayed mip window.

The cache distinguishes the sampling format and preferred-mip policy as well as the tracked resource lifetime and upload generation. A linear view and an sRGB view of the same texture therefore cannot silently reuse each other's cached interpretation.

With `dhash_allow_rehash = 0`, an existing hash for the same sampling format and mip policy is retained after later uploads. The first query after a change returns that retained value too. This is useful for matching an initially identified texture; it is not a promise that the result describes its current pixels. Changing the format or mip policy requires its own computation.

With `dhash_allow_rehash = 1`, a changed texture is recomputed using an eligible mip from the latest tracked upload generation. If that generation has no eligible initialized mip, the result is unavailable. An upload or resource-state reset during computation prevents that result from being published as a fresh cache entry; a later query can retry.

A cache miss performs GPU work and synchronous CPU readback. Keep queries at an appropriate frequency for the mod's needs. No game performance or FPS result is implied by the cache behavior.

## Independent command-list fragment

This example declares every INI variable it uses and requires no external custom shader. Call `CommandListCheckReferenceTexture` from a suitable hook where `ps-t0` holds the intended tracked, initialized, sampleable Texture2D. It is a fragment, not a complete game's texture override configuration. Replace the reference hash with one captured for the intended image and choose the threshold for the actual content.

```ini
[Rendering]
dhash_mip_size = 256
dhash_allow_rehash = 0

[ResourceReferenceImage]
original_format = R8G8B8A8_UNORM
original_dhash = 0123456789abcdef

[Constants]
global $reference_distance = -1
global $reference_matches = 0

[CommandListCheckReferenceTexture]
$reference_matches = 0
$reference_distance = ps-t0->HammingDistance(ResourceReferenceImage)
if $reference_distance >= 0
    if $reference_distance <= 8
        $reference_matches = 1
    endif
endif
```

The format declaration is reference metadata; it does not convert `ps-t0` or force its sampling format. A caller can separately compare `ps-t0->Format` with `ResourceReferenceImage->OriginalFormat` when its strategy requires that check.

Frame-analysis resource logs append `dhash=` followed by 16 hexadecimal digits when a hash is available. `dhash=0000000000000000` is a valid zero value; an unavailable hash has no such field. The log follows the same mip, format and retained-hash settings, so record the interpretation used when selecting reference values.
