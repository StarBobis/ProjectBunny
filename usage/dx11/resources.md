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
