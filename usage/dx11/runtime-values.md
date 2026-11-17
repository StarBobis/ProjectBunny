# 运行时参数

这些值在命令执行时读取；不要拿绘制上下文参数初始化全局静态选项。

| 参数 | 含义 |
| --- | --- |
| `TIME` | 启动后秒数，单调时钟 |
| `FRAME_TIME` | 上次测得的帧间隔，秒 |
| `FPS` | 平滑帧率估计，初始可为 0 |
| `FRAME_NUMBER` | 当前帧号，边界为 Present |
| `DRAW_NUMBER` | 上下文当前帧绘制计数，首个 draw 为 1 |
| `DISPATCH_NUMBER` | 独立的 dispatch 计数，首个为 1 |
| `vertex_count/index_count/instance_count` | 当前绘制数量 |
| `first_vertex/first_index/first_instance` | 当前绘制起点 |
| `thread_group_count_x/y/z` | 当前 dispatch 线程组数，各项写完整前缀 |
| `indirect_offset` | 间接调用参数偏移 |
| `draw_type` | 当前调用类型 |
| `DRAW_HAS_OVERRIDE` | draw 开始时，IB、VB0–VB2 或 PS-t0 是否命中已加载的精确 TO 哈希与绘制范围 |
| `scissor_left/top/right/bottom` | 裁剪矩形边界，各项写完整前缀 |
| `rt_width`、`rt_height` | 当前渲染目标尺寸 |
| `res_width`、`res_height` | 配置所用分辨率 |
| `window_width`、`window_height` | 窗口尺寸 |
| `cursor_x`、`cursor_y` | 归一化光标位置 |
| `cursor_window_x`、`cursor_window_y` | 窗口内光标位置 |
| `cursor_screen_x`、`cursor_screen_y` | 屏幕光标坐标 |
| `cursor_showing` | 光标显示状态 |
| `cursor_hotspot_x`、`cursor_hotspot_y` | 光标热点 |
| `effective_dpi` | 有效 DPI |
| `hunting`、`frame_analysis` | 工具状态 |
| `stereo_active`、`stereo_available`、`sli` | 立体/设备状态 |

跳过的 draw 仍计数；Present 重置 draw/dispatch 计数。长时间运行后，float 帧号与时间的精度会降低。

## 每帧只执行一次

在会重复触发的绘制钩子中：

```ini
[Constants]
global $last_frame = -1
global $sample = 0

[CommandListOnce]
if $last_frame != FRAME_NUMBER
    $last_frame = FRAME_NUMBER
    $sample = TIME
endif
```

计数用于限定执行频率，不是跨运行的对象标识。资源信息见[元数据](./metadata)。

## 先按模组哈希筛选绘制

全局 ShaderRegex 调用的检查列表可以先用 `DRAW_HAS_OVERRIDE` 筛选，再执行逐槽检查：

```ini
[CommandListSkin]
if $costume_mods && DRAW_HAS_OVERRIDE
    checktextureoverride = ps-t0
    checktextureoverride = vb0
    checktextureoverride = vb1
    checktextureoverride = vb2
    checktextureoverride = ib
endif
```

条件直接检查已记录的资源哈希与 TO 的 `match_*` 绘制条件，不执行 TO 命令、不扫描模糊描述规则，也不重新计算资源哈希。IB/VB 入口保留几何模组，PS-t0 入口保留从该槽触发的纯贴图模组。其他槽位或仅有模糊规则的模组需要自己的入口，不应仅依赖这个条件。

pre/post 使用同一个 draw 开始时的判断，模组随后替换 IB/VB 或修改绘制数量不会改变本次 post 的判断。每次配置重载读取新的 TO 表。开启区域哈希时，有相同绘制数量候选的 draw 会保守放行，再由原有检查确认区域哈希。
