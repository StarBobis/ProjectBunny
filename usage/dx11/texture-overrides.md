# TextureOverride

名字虽是 Texture，目标也可以是 VB、IB、CB 等资源。Hash 为资源 Hash，不是着色器的 64 位 Hash。

```ini
[TextureOverridePart]
hash = 12345678
match_first_index = 0
match_index_count = 300
handling = skip
```

替换为实际 Hash 和 draw 参数。共享缓冲可对应多个部件，必须用绘制条件缩小范围。

## 匹配方式

| 字段 | 匹配对象 |
| --- | --- |
| `hash` | 资源 Hash |
| `match_first_vertex/first_index/first_instance` | 绘制起点 |
| `match_vertex_count/index_count/instance_count` | 绘制数量 |
| `match_priority` | 多个规则的匹配优先级 |
| `match_type/usage/format` | 资源类型、用途、格式 |
| `match_bind_flags/cpu_access_flags/misc_flags` | 描述标志 |
| `match_byte_width/stride` | 缓冲字节大小、步长 |
| `match_width/height/depth/array/mips` | 纹理尺寸、数组、mip |
| `match_msaa/msaa_quality` | 采样描述 |

表中 `match_` 为完整前缀，例如 `match_index_count`、`match_format`。

资源描述的模糊匹配字段不能和 `hash` 同用；绘制条件可以与 Hash 或模糊匹配同用。

```ini
[TextureOverrideScreen]
match_type = Texture2D
match_width = height * 16 / 9
match_height = res_height
match_bind_flags = +shader_resource -depth_stencil
match_priority = -1
```

模糊数值使用专用的简单比较/尺寸表达式，不是完整命令表达式。可用 `>`、`<=` 等比较；不要放任意函数和命名变量。

## 资源创建与匹配结果

| 选项 | 用途 |
| --- | --- |
| `filter_index` | 给匹配资源设置可用于表达式筛选的数值 |
| `format/width/height` | 覆盖资源描述 |
| `width_multiply/height_multiply` | 尺寸缩放 |
| `override_byte_stride/override_vertex_count` | 配套模型扩容信息 |
| `uav_byte_stride` | UAV 步长信息 |
| `iteration` | 限制匹配迭代 |
| `deny_cpu_read` | 限制相应 CPU 读路径 |
| `expand_region_copy` | 扩展区域复制行为 |

这类创建/跟踪选项可能需要重启重建资源。不要把增大顶点数当作自动修复模型布局。

## 触发贴图规则

需要在目标着色器绘制处检查指定槽时：

```ini
[ShaderOverrideCheck]
hash = 0123456789abcdef
CheckTextureOverride = ps-t0

[TextureOverrideDiffuse]
hash = 12345678
this = ResourceDiffuse

[ResourceDiffuse]
filename = diffuse.dds
```

替换两个 Hash。`this` 指被检查的那一项；不能在无匹配上下文时假设它代表 `ps-t0`。
