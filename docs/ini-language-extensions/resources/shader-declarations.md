# Shader Declarations

Runtime expressions can inspect cached declarations from the bound original shader bytecode. Stages are `vs`, `hs`, `ds`, `gs`, `ps` and `cs`:

```ini
$constants = vs->cb_size(0)
$resource_type = ps->srv_type(1)
```

| Getter | Result |
| --- | --- |
| `cb_type(slot)` | `0` undeclared, `1` immediate-indexed, `2` dynamic-indexed |
| `cb_size(slot)` | Declared count of 16-byte constants |
| `srv_type(slot)` | `0` undeclared, `1` typed, `2` structured, `3` raw |
| `srv_stride(slot)` | Declared structured-buffer stride in bytes |
| `srv_dimension(slot)` | D3D SRV dimension enumeration value |
| `cb_slot_count`, `srv_slot_count` | Number of distinct declared slots |
| `cb_first_slot`, `srv_first_slot` | Lowest declared slot, or `-1` for a cached shader with none |
| `cb_last_slot`, `srv_last_slot` | Highest declared slot, or `-1` for a cached shader with none |

The six summary members take no arguments. For example, declarations at CB slots 4 and 13 produce count `2`, first `4` and last `13`. Missing shader/cache behavior remains the same as for the per-slot members below.

CB slots are 0–13 and SRV slots are 0–127. With a cached shader available, fractional finite slot expressions truncate; negative, nonfinite or out-of-range slots return `-1`. An undeclared slot returns `-1` for unavailable properties, while its type is `0`. No bound shader returns negative zero; an unavailable shader mapping/cache entry returns positive zero before slot evaluation. This alone does not prove a shader declares no resources. For a declared typed/raw SRV, `srv_stride` returns `0`.

CB size comes from the DXBC declaration of used 16-byte registers. It can differ from the full allocation size reported by reflection for a buffer whose later members are unused.

The decoder handles SM4.0, SM4.1 and SM5.0 declarations. It does not decode SM5.1. Queries describe the original shader, not replacement/custom shader bytecode and not resources currently bound to its slots. Creation paths without a cached declaration record, including stream-output shader creation, can return the missing-cache fallback.

`cb_mask` and `srv_mask` are recognized names but mask evaluation is not implemented; do not use their current zero result as a declaration mask.

Frame-analysis PS SRV dumps skip slots absent from a cached original shader's declarations. A missing declaration cache retains the dump-all fallback. Other shader stages keep their existing dump behavior.
