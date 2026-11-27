# 运行时参数

| 参数 | DX12 含义 |
| --- | --- |
| `time` | 从 DLL 启动到最近一次主交换链进入 Present 的秒数 |
| `frame_number` | 帧号从 0 开始，每次主交换链正式 Present 返回后加 1 |
| `frame_time` | 相邻两次正式 Present 入口之间的秒数，第一次从 DLL 启动起算 |
| `fps` | 按 DX11 算法平滑后的帧率，收到两个有效时间点后开始有值 |
| `draw_type` | `DrawInstanced` 为 3，`DrawIndexedInstanced` 为 4，直接 `Dispatch` 命中的计算蒙皮覆盖为 8 |
| `thread_group_count_x`、`thread_group_count_y`、`thread_group_count_z` | 当前直接派发在三个方向上的原始线程组数 |
| `vertex_count`、`index_count` | 当前绘制每个实例的顶点数或索引数，另一项为 0 |
| `instance_count`、`first_instance` | 当前绘制的实例数和起始实例 |
| `first_vertex`、`first_index` | 普通绘制的起始顶点，或索引绘制的基顶点与起始索引 |
| `window_width/window_height` | 窗口尺寸 |
| `res_width/res_height` | 后备缓冲尺寸，必要时回退窗口尺寸 |
| `rt_width/rt_height` | 帧快照中的后备缓冲/窗口尺寸，并非每次 draw 的真实目标 |
| `cursor_screen_x/cursor_screen_y` | 屏幕坐标 |
| `cursor_window_x/cursor_window_y` | 窗口内坐标 |
| `cursor_x/cursor_y` | 归一化位置 |
| `cursor_showing/cursor_visible` | 光标显示状态 |
| `cursor_hotspot_x/cursor_hotspot_y` | 光标热点 |
| `effective_dpi` | 有效 DPI |
| `frame_analysis` | 帧分析会话开启时为 1，关闭时为 0 |
| `hunting` | 定位开启时为 1，正常关闭时为 2。切换热键生效后立刻更新 |

窗口、光标和分辨率值在帧边界更新，不是每次表达式都查询系统。

`frame_analysis` 在命令列表求值时读取当前状态。例如，帧分析期间重载 INI，`[Constants]` 中的 `global $capture_on_load = frame_analysis` 会记下 1。后续命令直接读取 `frame_analysis` 时会随会话结束变为 0。文件头 `condition` 和普通数值配置只接受各自作用域内的操作数，使用此名称会报无效表达式或回退到默认值。

以下名称返回固定兼容值。

| 名称 | 当前返回 |
| --- | --- |
| `sli/stereo_active/stereo_available` | 固定为 0 |

四个帧参数在第一次正式 Present 前均为 0。读同一条表达式时使用同一份帧数据，重载 INI 后继续计数。`time` 已改为按帧采样，同一帧内反复读取会得到相同结果。`fps` 忽略零间隔和超过一秒的间隔，`frame_time` 仍反映长帧。

直接绘制的 ShaderOverride 和它调用的命令列表在 pre、post 阶段都能读到原绘制参数。即使顶点数或实例数为 0，`draw_type` 仍按实际入口取值。在 Constants、Present 和 Key 中，上表的绘制参数均为 0。和 DX11 一样，索引绘制的 `first_vertex` 先按 32 位无符号数解释，再转换成 float32，例如基顶点 −1 在表达式中读作 4294967296。

在通过 `cs_hash` 和 `match_cs_tN_hash` 命中的蒙皮覆盖段内，三个线程组参数始终取自游戏的原始 `Dispatch`。即使覆盖段用 `dispatch` 改写实际组数，`post` 赋值也能在派发返回后读到原始组数。其他上下文中的线程组数为 0。

普通计算 ShaderOverride 的命令列表、间接命令参数，以及 `draw_number` 和 `dispatch_number` 的逐调用计数仍待对齐。

```ini
[Constants]
global $wide = 0

[Present]
if res_height > 0
    $wide = res_width / res_height > 1.7
endif
```
