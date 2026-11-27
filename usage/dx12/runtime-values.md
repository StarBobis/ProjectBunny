# 运行时参数

| 参数 | DX12 含义 |
| --- | --- |
| `time` | 从 DLL 启动到最近一次主交换链进入 Present 的秒数 |
| `frame_number` | 帧号从 0 开始，每次主交换链正式 Present 返回后加 1 |
| `frame_time` | 相邻两次正式 Present 入口之间的秒数，第一次从 DLL 启动起算 |
| `fps` | 按 DX11 算法平滑后的帧率，收到两个有效时间点后开始有值 |
| `window_width/window_height` | 窗口尺寸 |
| `res_width/res_height` | 后备缓冲尺寸，必要时回退窗口尺寸 |
| `rt_width/rt_height` | 帧快照中的后备缓冲/窗口尺寸，并非每次 draw 的真实目标 |
| `cursor_screen_x/cursor_screen_y` | 屏幕坐标 |
| `cursor_window_x/cursor_window_y` | 窗口内坐标 |
| `cursor_x/cursor_y` | 归一化位置 |
| `cursor_showing/cursor_visible` | 光标显示状态 |
| `cursor_hotspot_x/cursor_hotspot_y` | 光标热点 |
| `effective_dpi` | 有效 DPI |

窗口、光标和分辨率值在帧边界更新，不是每次表达式都查询系统。

以下名称虽有兼容值，不能用于判断实际状态：

| 名称 | 当前返回 |
| --- | --- |
| `draw_type` | 固定为 1 |
| `hunting/frame_analysis` | 固定为 0 |
| `sli/stereo_active/stereo_available` | 固定为 0 |

四个帧参数在第一次正式 Present 前均为 0。读同一条表达式时使用同一份帧数据，重载 INI 后继续计数。`time` 已改为按帧采样，同一帧内反复读取会得到相同结果。`fps` 忽略零间隔和超过一秒的间隔，`frame_time` 仍反映长帧。

`draw_number` 和 `dispatch_number` 的逐绘制计数仍待实现。

```ini
[Constants]
global $wide = 0

[Present]
if res_height > 0
    $wide = res_width / res_height > 1.7
endif
```
