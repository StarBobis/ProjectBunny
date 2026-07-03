# [TextureOverride*] — 纹理 / IA Buffer 替换

最核心的 mod 配置节。根据 IA 资源 hash（IB/VB/纹理）匹配 DrawCall，替换 buffer 或执行 pre-skinning。

**Section 命名**: `[TextureOverride<名称>]`，如 `[TextureOverrideFatalFrameIB]`

**索引键**: `hash` — 32-bit 十六进制 IA 资源 hash

## Core Keys

### 身份匹配

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `hash` | uint32 (hex) | `0` | **必需** 资源 hash。32-bit 十六进制（可带 `0x` 前缀） |

### 处理模式

| Key | 值 | 说明 |
|---|---|---|
| `handling` | `skip` | 跳过原始 DrawCall，使用替换 buffer/view 替代 |

### Buffer 替换

| Key | 类型 | 说明 |
|---|---|---|
| `ib` | 资源引用 | 替换索引缓冲区。值为 `[Resource*]` section 名称（自动命名空间解析） |
| `vb<N>` | 资源引用 | 替换顶点缓冲区 slot `<N>`。如 `vb0 = MyVertexBuffer`。`<N>` 为非负整数 |

## Draw Context 匹配

所有匹配键使用 `NumericMatch` 语法（见总览）。不设置则不参与匹配。

| Key | 匹配目标 |
|---|---|
| `match_vertex_count` | DrawCall 顶点数 |
| `match_index_count` | DrawCall 索引数 |
| `match_instance_count` | DrawCall 实例数 |
| `match_first_vertex` | DrawCall 起始顶点偏移 |
| `match_first_index` | DrawCall 起始索引偏移 |
| `match_first_instance` | DrawCall 起始实例偏移 |

## 优先级

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `match_priority` | int | `0` | 多匹配时的优先级。值越大越优先 |

## Pre-Skinning (CS UAV → VB)

通过 Compute Shader 的 UAV 输出替换顶点缓冲。CS 的 UAV 按指定 stride 解释为 VB 数据。

| Key | 类型 | 说明 |
|---|---|---|
| `cs_hash` | uint64 (hex) | 产生顶点数据的 Compute Shader 字节码 hash |
| `match_uav_bytes` | uint64 | 期望的 UAV 输出字节数 |
| `match_cs_t<N>_hash` | uint32 (hex) | CS 在寄存器 `t<N>` 使用的 SRV 资源 hash（别名: `match_srv<N>_hash`） |
| `cs-t<N>` | 资源引用 | 替换 CS 寄存器 `t<N>` 的 SRV 资源（别名: `srv<N>`） |

## Vertex Limit Raise (VLR)

动态放大 UAV buffer 以容纳超出游戏原始分配的顶点数。

| Key | 类型 | 说明 |
|---|---|---|
| `override_byte_stride` | uint32 | 每顶点元素的字节跨度 |
| `override_vertex_count` | uint32 | 目标顶点数 |
| `uav_byte_stride` | uint32 | UAV 中每元素字节数 |

VLR 自动计算：
- `overrideByteWidth = overrideByteStride × overrideVertexCount`
- `overrideNumElements = overrideByteWidth ÷ uavByteStride`

## Command List 链接

未识别的 key 作为 Command List 操作解析：

| Key | 说明 |
|---|---|
| `run` | 主命令列表链接 |
| `pre` / `pre run` / `prerun` | 前置命令列表（Draw 前执行） |
| `post` / `post run` / `postrun` | 后置命令列表（Draw 后执行） |

## 案例

### 基础 IB+VB 替换

```ini
[ResourceFatalIB]
type = Buffer
filename = fatal_frame_ib.bin
format = R16_UINT

[ResourceFatalVB0]
type = Buffer
filename = fatal_frame_vb0.bin
stride = 32

[TextureOverrideFatalFrame]
hash = a1b2c3d4
handling = skip
ib = ResourceFatalIB
vb0 = ResourceFatalVB0
match_priority = 10
```

### 带 Draw Context 匹配的替换

```ini
[TextureOverrideSpecificDraw]
hash = deadbeef
handling = skip
ib = ResourceReplacementIB
vb0 = ResourceReplacementVB0
match_index_count = >=1000
match_instance_count = 1
match_priority = 20
```

### Pre-Skinning（CS 顶点替换）

```ini
[ResourceBoneBuffer]
type = StructuredBuffer
filename = bones.bin
stride = 64

[TextureOverrideSkinning]
hash = feedface
cs_hash = cafebabedeadbeef
cs-t0 = ResourceBoneBuffer
match_cs_t0_hash = aabbccdd
match_uav_bytes = 262144
override_byte_stride = 32
override_vertex_count = 8192
uav_byte_stride = 32
```

### 带前后命令列表

```ini
[CommandListBeforeDraw]
checktextureoverride = ib
checktextureoverride = vb0

[CommandListAfterDraw]
dispatch = 1, 1, 1

[TextureOverrideWithCommands]
hash = 1234abcd
handling = skip
ib = ResourceCustomIB
pre = CommandListBeforeDraw
post = CommandListAfterDraw
```
