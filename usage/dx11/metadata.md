# 资源元数据

在命令中读取：

```ini
[Constants]
global $size = 0
global $present = 0

[CommandListInspect]
$present = @ps-t0
$size = vb0->Size
```

`@目标` 是底层资源地址的紧凑哈希；空资源为 0。它可能碰撞、可能在资源释放后复用，不是持久 ID。

## 属性

| 属性 | 返回值 |
| --- | --- |
| `Format` | 视图优先的 DXGI 格式值 |
| `OriginalFormat` | 自定义资源声明的参考格式，默认 -1 |
| `Width/Height` | 物理纹理尺寸，不随视图 mip 缩小 |
| `Array/Mips` | 物理数组层数、mip 数，不是视图范围 |
| `BindFlags` | D3D11 绑定标志；缺失时为 0 |
| `Offset` | VB/IB/CB 当前区域的绝对字节偏移 |
| `Size` | 缓冲字节大小；CB 槽报告活动窗口 |
| `Stride` | 字节步长 |
| `SourceStride` | 填充自定义资源时的源步长，不用于普通管线槽 |

绘制内 VB/IB 的 Offset 包含当前 draw 的首顶点/首索引贡献；绘制外只含绑定偏移。自定义资源显式覆盖字段可优先于实际分配信息。

| 错误值 | 意义 |
| --- | --- |
| -1 | 数据未知/不可用 |
| -2 | 缺少资源 |
| -3 | 需要缓冲却不是缓冲 |
| -4 | 需要纹理却不是纹理 |

不要把负值当作有效大小。不同 getter 有特定缺省行为，例如 BindFlags 为 0、OriginalFormat 为 -1。

## 内容与空间哈希

```ini
$key = vs-cb0->HashRegion(0, 64)
$cell = vs-cb0->SpatialHash(0, 1, 2, 0.25)
```

先声明 `$key/$cell`。`HashRegion` 的 offset/size 为相对当前区域的字节；`SpatialHash` 的前三项是相对当前区域的 float 索引，最后一项为正的网格尺寸。

需要支持的 CPU 缓存/跟踪数据：

```ini
[Rendering]
cache_resource_data = constant_buffer
```

可选 `none`、`all` 或 `vertex_buffer index_buffer constant_buffer` 的组合。GPU 写入等更新会使缓存失效；开启选项不代表已有新鲜数据，可能触发同步读回。

内容哈希会碰撞；空间网格以 X/Z 4096、Y 256 个单元循环。它们用于近似归类，不证明内容或对象完全相同。
