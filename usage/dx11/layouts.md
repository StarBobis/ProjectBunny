# 输入布局与流输出

## 修改输入布局

格式与偏移按语义名称、语义索引定位：

```ini
vb0->ElementFormat(BLENDINDICES, 0) = R16G16B16A16_UINT
vb0->ElementOffset(BLENDWEIGHTS, 0) = 8
```

放在适当的绘制命令上下文。`vb0` 表示输入槽，`0` 是语义索引，偏移单位为字节。这里只修改解释方式，不重排缓冲内容。

字段须匹配顶点数据与着色器输入签名。动态 VB 索引不适用于这两个设置器。

## 流输出

```ini
[ResourceOutput]
type = Buffer
byte_width = 4096
bind_flags = stream_output shader_resource

[CommandListBindOutput]
so0 = ref ResourceOutput->Region(64, 16)
```

SO 使用起始字节 offset；Region 长度不限制硬件写入量。缓冲必须容纳几何着色器真实输出。

SO 槽为 0–3，支持动态单槽，不支持范围。未指定槽按 append 重新绑定；原生 API 无法查询旧写入偏移。

`CreateGeometryShaderWithStreamOutput` 创建的几何着色器不进入普通 Hash/绑定缓存，因此不能假设 Hunting、ShaderOverride 或文件替换可用于该着色器；输出缓冲仍可访问。

## 查询原始着色器声明

```ini
$count = vs->cb_size(0)
$stride = ps->srv_stride(1)
```

先声明结果变量。

| getter | 返回 |
| --- | --- |
| `cb_type(N)` | 0 未声明、1 即时索引、2 动态索引 |
| `cb_size(N)` | 使用的 16 字节常量数量 |
| `srv_type(N)` | 0 未声明、1 typed、2 structured、3 raw |
| `srv_stride(N)` | 结构化步长；typed/raw 为 0 |
| `srv_dimension(N)` | SRV 维度枚举 |
| `cb_slot_count/srv_slot_count` | 已声明槽数量 |
| `cb_first_slot/srv_first_slot` | 最低槽，无声明为 -1 |
| `cb_last_slot/srv_last_slot` | 最高槽，无声明为 -1 |

可查六个阶段。支持 SM4.0/4.1/5.0 原始字节码；不是替换着色器，也不是槽上当前绑定资源。缺失缓存可返回零，不能据此认定无声明。`cb_mask/srv_mask` 尚未实现，不使用它们判定掩码。
