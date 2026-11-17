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

## 按目标查询候选

普通表达式 `ps-t7` 已经会匹配该槽的 TextureOverride，并返回命中的 `filter_index`；未设置时返回 1，无匹配时返回 0。它不执行 TO 命令。需要区分“命中了 `filter_index = 0`”与“无候选”，或希望先使用候选缓存时，可以查询 `ps-t7->TOCandidate`。

`目标->TOCandidate` 只查询写出的目标，返回 0 或 1，不执行 TO 的 pre/post 命令。目标可以是任意支持的管线槽、动态槽或自定义资源。DLL 不选择槽位组合，也不会因为某个配置引用了该属性而在每次 draw 开始时扫描槽位。

`this->TOCandidate` 沿用当前选中目标的语义，包括 VB/IB 的区域处理。自定义资源沿用现有资源求值行为，可能触发惰性创建或过期处理；这些成本也属于查询成本。

| 情况 | 查询语义 |
| --- | --- |
| 精确匹配 | 使用资源当前已记录的完整哈希，并检查当前绘制的六项 `match_*` 条件；不重新计算内容哈希 |
| 模糊匹配 | 检查资源描述与当前绘制条件，包括动态分辨率；仅有模糊规则也可以返回 1 |
| `filter_index` | 不参与布尔结果；命中 `filter_index = 0` 也返回 1 |
| 区域哈希 | 开启 `track_region_hashes` 时，VB/IB 只要存在 TO 规则就保守返回候选；返回 1 不证明区域哈希命中，后续仍需正式检查 |
| 空目标、未跟踪资源或无规则 | 返回 0；区域哈希保守分支仍按上一行处理已绑定的 VB/IB |

INI 决定选哪些槽以及如何组合。下面只演示一个配置选择的两个入口，不是完整的绘制适用性判断：

```ini
[CommandListCheckSelected]
if $enabled
    if vb7->TOCandidate || vs-t37->TOCandidate
        checktextureoverride = vb7
        checktextureoverride = vs-t37
    endif
endif
```

先声明 `$enabled`。如果模组还依赖其他入口，必须在自己的 INI 中加入相应条件；一个目标返回 0 不代表整个 draw 没有适用的 TO。

查询在表达式执行的位置读取当前绑定与绘制参数，pre/post 会分别求值，没有隐式的跨阶段快照。前置命令若修改绑定或数量，后置结果可以不同；需要恢复的命令不能依赖这个条件仍为真。配置应明确安排保存、恢复和后置检查，并验证嵌套调用。

现有 `&&`、`||` 会求值两侧。需要避免禁用时查询或按顺序提前结束查询时，使用外层 `if` 或 `elif`，不要假设表达式短路。候选查询本身也有获取绑定、引用计数、锁和缓存查询成本；是否比直接检查更快，应测量包含这些成本的完整命令路径。

## 旧配置兼容

原有 `if $costume_mods`、`checktextureoverride`、`pre`/`post` 和资源筛选表达式继续按原有流程执行。没有使用 `->TOCandidate` 的旧配置不会自动增加候选查询，也不会同时启动另一套检查列表。

新配置只在执行到 `->TOCandidate` 时查询指定目标。候选为假时，由 INI 的 `if` 跳过后面的正式检查；为真时，仍调用原有 `checktextureoverride` 完成匹配并执行 TO 命令。候选查询自身不执行这些命令，复用执行逻辑不会让同一条命令额外运行一遍。

`DRAW_HAS_OVERRIDE` 不属于兼容的原版语法，使用该实验条件的配置需要迁移。新版 INI 中的 `->TOCandidate` 也需要支持该属性的 DLL；旧 DLL 不能解析新属性。
