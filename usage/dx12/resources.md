# 资源与引用

## 文件资源

```ini
[ResourceIndices]
type = Buffer
format = R32_UINT
filename = mesh.ib

[ResourceVertices]
type = Buffer
stride = 32
filename = mesh.buf

[ResourceDiffuse]
filename = diffuse.dds
```

文件相对声明它的 INI 查找。索引位宽、顶点步长必须与文件一致。

| 选项 | 用途 |
| --- | --- |
| `type` | 资源类型说明 |
| `filename` 或 `file` | 初始数据路径 |
| `format` | DXGI 格式 |
| `data` | 内联数据，受实际加载路径限制 |
| `stride` | 字节步长 |
| `byte_width` 或 `width` | 字节数配置；不是 DX11 纹理尺寸覆盖语义 |
| `array` 或 `array_size` | 数组大小配置 |

DDS 维度、格式和数组类型还需满足 DX12 加载器与目标视图要求。DX11 的 `mips`、`color_space`、`bind_flags`、`original_dhash` 等字段不能按同义参数使用。

## 资源别名

```ini
[ResourceSelected]
[ResourceVariantA]
filename = variant_a.dds

[CommandListSelect]
ResourceSelected = ref ResourceVariantA
```

别名指向源资源，不复制像素。可由条件选择不同文件资源，再让覆盖规则绑定别名。

## 捕获当前纹理

```ini
[ResourceCaptured]

[CommandListCapture]
ResourceCaptured = ref ps-t0
```

在能解析当前图形 SRV 的 draw 上下文调用。捕获的是活动资源引用，不是 GPU 内容快照；后续更新仍会影响它。

不能把捕获扩展为任意缓冲/槽位复制。通用 `copy/copy_desc`、DX11 Region、动态槽和范围并未在此实现。

## 绑定

```ini
[TextureOverrideDiffuse]
hash = 12345678
this = ResourceDiffuse
```

替换为目标贴图 Hash。`this` 指命中的贴图；用 `ps-tN` 则是特定阶段槽，需核对当前根签名与描述符绑定。
