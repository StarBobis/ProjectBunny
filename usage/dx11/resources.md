# 声明资源

空节可作为运行时捕获目标：

```ini
[ResourceSaved]
```

文件资源：

```ini
[ResourceTexture]
filename = texture.dds

[ResourceVertices]
type = Buffer
stride = 32
filename = mesh.buf

[ResourceIndices]
type = Buffer
format = R32_UINT
filename = mesh.ib
```

`stride` 单位为字节；索引格式应与文件中的整数宽度一致。文件相对所属 INI 查找，随后可回退到 DLL 目录。

## 选项

| 字段 | 意义 |
| --- | --- |
| `type` | Buffer、StructuredBuffer、ByteAddressBuffer、Texture1D/2D/3D、TextureCube；可用对应 RW 类型名 |
| `filename` | 文件初始数据 |
| `format` | DXGI 格式，资源字段可省略 `DXGI_FORMAT_` 前缀 |
| `data` | 内联初始数据，按指定格式编码 |
| `stride`、`byte_width` | 元素字节步长、总字节数 |
| `width`、`height`、`depth` | 纹理尺寸 |
| `width_multiply`、`height_multiply` | 尺寸倍率 |
| `mips`、`array` | mip 数、数组层数 |
| `msaa`、`msaa_quality` | 多重采样参数 |
| `bind_flags` | 用途，如 `shader_resource unordered_access` |
| `misc_flags` | D3D11 资源附加标志 |
| `max_copies_per_frame` | 每帧复制次数上限；0 不限制 |
| `color_space` | `srgb` 或 `linear` |
| `original_format`、`original_dhash` | 感知匹配的参考元数据 |

部分字段可由复制源推导。显式格式、步长与尺寸必须互相兼容；字段被解析不代表底层资源创建一定成功。

`bind_flags` 可组合 `vertex_buffer`、`index_buffer`、`constant_buffer`、`shader_resource`、`stream_output`、`render_target`、`depth_stencil`、`unordered_access`；`decoder/video_encoder` 仅用于相应视频用途。常量缓冲不能任意与其他用途组合。

```ini
[ResourceValues]
type = Buffer
format = R32_FLOAT
data = 0 1 2 3
```

## 色彩空间

WIC 图片使用 `srgb` 强制 sRGB 格式，`linear` 忽略 sRGB 元数据。DDS 的 `srgb` 可强制 sRGB，`linear` 不保证将已经是 sRGB 的 DDS 改成线性格式。

```ini
[ResourceColor]
filename = color.png
color_space = srgb
```

这是格式解释，不是把像素数值做颜色转换。自动检测 PNG 的额外 sRGB 扫描由 `[System] force_detect_color_space` 控制，默认关闭。

## 贴图匹配元数据命名兼容

推荐使用 `match_format`、`match_dhash` 和资源成员 `->getmatchformat`。旧 INI 的 `original_format`、`original_dhash` 及 `->originalformat` 仍可使用；同一资源同时声明新旧键时，新键优先。dHash 按完整 64 位十六进制解析，不经浮点数转换。

## 按 dHash 匹配像素着色器贴图槽位

```ini
[TextureOverrideExample]
hash = 01234567
run = SnapshotOriginalTextures
ps->MatchTextures(ResourceMatchFilter) = ref ResourceReplacement
run = RestoreOriginalTextures

[ResourceMatchFilter]
match_dhash = 1363895a02495a04
match_format = BC3_UNORM
match_confidence = 0.75

[ResourceReplacement]
filename = replacement.dds
```

`ps->MatchTextures(过滤资源)` 使用过滤资源声明的 `match_dhash` 与当前 PS 声明且绑定的原始贴图比较，将右侧资源绑定到全局最接近的槽位；多个槽位距离相同则一起替换。只支持 PS。过滤资源可与替换资源分开，也可使用同一个 Resource。

- `match_dhash` 必须声明，完整 64 位十六进制；显式的全零 Hash 有效，未声明或解析失败不参与匹配。
- `match_format` 可省略；声明后按原始 **SRV 格式**过滤。格式名无效时不匹配。
- `match_confidence` 默认 `0.75`，限制到 `[0,1]`；非法非有限值回退到默认值。允许距离为 `floor((1-confidence)*64)`，包括边界。`1` 要求完全一致；零与非零 Hash 的距离按 64 处理。
- 同一被拦截 draw 内，后来的候选只有距离更小时才能覆盖已接受结果；同距离保持先到者。比较始终使用原始贴图，而不是之前的替换结果。
- 支持资源复制选项，例如 `ref`、`copy`。原始贴图无法计算 dHash、替换源为空、创建 SRV 或绑定失败时，不占用该槽位的最佳匹配名额。

`run = SnapshotOriginalTextures` 默认放在 pre；省略时首次匹配自动捕获。重复捕获不会重置同次 draw 的候选竞争。`run = RestoreOriginalTextures` 默认放在 post，恢复原始 SRV（包括原来为空的已声明槽位），并重置竞争距离。可显式用 pre/post 调整执行位置。请保留恢复命令，避免替换绑定泄漏到后续绘制。

快照属于 Context 和当前 draw/dispatch，不复用跨帧计数；在调用结束、ClearState、Context 销毁时释放，配置重载使旧快照失效。dHash 匹配要求 immediate context，deferred context 安全跳过。调用可能触发首次 dHash 计算，配置开销取决于候选数量和贴图大小，不能视为零成本操作。
