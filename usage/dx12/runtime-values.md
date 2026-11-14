# 运行时参数

| 参数 | DX12 含义 |
| --- | --- |
| `time` | 单调时钟秒数 |
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

DX11 的 `FRAME_NUMBER`、`DRAW_NUMBER`、`DISPATCH_NUMBER`、`FRAME_TIME`、`FPS` 不属于这里可依赖的实时计数。未定义名称可能读取为零，不是功能检测方式。

```ini
[Constants]
global $wide = 0

[Present]
if res_height > 0
    $wide = res_width / res_height > 1.7
endif
```
