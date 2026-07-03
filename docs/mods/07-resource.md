# [Resource*] — 资源数据定义

定义可被 `[TextureOverride*]` 和 `[CommandList*]` 引用的缓冲区资源。数据可从文件或内联十六进制加载。

**Section 命名**: `[Resource<名称>]`

**引用方式**: 在 `ib`、`vb<N>`、`cs-t<N>` 等字段中使用 section 名称（自动命名空间解析）。

## Keys

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `type` | 字符串 | `Buffer` | 资源类型。支持：`Buffer`, `StructuredBuffer`, `AppendStructuredBuffer`, `ConsumeStructuredBuffer`, `ByteAddressBuffer`, `RWBuffer`, `RWStructuredBuffer`, `RWByteAddressBuffer` |
| `filename` | 路径 | *(空)* | 二进制数据文件路径。相对路径优先解析到源 INI 目录，其次为 baseDir（别名: `file`） |
| `format` | 字符串 | *(空)* | DXGI 格式名。如 `R16_UINT`, `R32_UINT`, `DXGI_FORMAT_R32_UINT` 或数值 |
| `data` | 十六进制字符串 | *(空)* | 内联十六进制二进制数据（`filename` 的替代方案）。空格自动去除 |
| `stride` | uint32 | `0` | 结构体字节跨度 |
| `array` | uint32 | `0` | 数组大小（别名: `array_size`） |
| `byte_width` | uint32 | `0` | 显式字节宽度。大于文件大小时覆盖文件大小（别名: `width`） |

## 资源类型说明

| type 值 | DX12 对应 |
|---|---|
| `Buffer` | 普通缓冲区 (D3D12_RESOURCE_DIMENSION_BUFFER) |
| `StructuredBuffer` | 结构化缓冲区 |
| `ByteAddressBuffer` | 字节地址缓冲区 |
| `RWBuffer` | 可读写缓冲区 |
| `RWStructuredBuffer` | 可读写结构化缓冲区 |
| `RWByteAddressBuffer` | 可读写字节地址缓冲区 |
| `AppendStructuredBuffer` | 追加结构化缓冲区 |
| `ConsumeStructuredBuffer` | 消耗结构化缓冲区 |

资源以 `D3D12_HEAP_TYPE_UPLOAD` + `D3D12_RESOURCE_STATE_GENERIC_READ` 创建。

## 案例

### 文件加载的索引缓冲区 (IB)

```ini
[ResourceMyIB]
type = Buffer
filename = my_index_buffer.bin
format = R16_UINT
byte_width = 65536
```

### 文件加载的顶点缓冲区 (VB)

```ini
[ResourceMyVB0]
type = Buffer
filename = my_vertex_buffer.bin
stride = 32
byte_width = 262144
```

### 内联数据的缓冲区

```ini
[ResourceInlineData]
type = Buffer
format = R32_UINT
data = 01000000 02000000 03000000 04000000
byte_width = 16
```

### 结构化缓冲区

```ini
[ResourceBoneData]
type = StructuredBuffer
filename = bones.bin
stride = 64
array = 256
```

### 字节地址缓冲区

```ini
[ResourceRawData]
type = ByteAddressBuffer
filename = raw_data.bin
```
