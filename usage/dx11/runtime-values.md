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
