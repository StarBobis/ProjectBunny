# CustomShader

自定义着色器节组合代码、状态和实际 draw/dispatch。通过 `run` 调用；仅声明着色器文件不会自动绘制。

```ini
[CustomShaderProcess]
cs = process.hlsl
cs-t0 = ResourceInput
cs-u0 = ResourceOutput
dispatch = 1, 1, 1

[CommandListProcess]
run = CustomShaderProcess
```

需先声明兼容的输入、输出资源，并提供真实 `process.hlsl`。线程组数应按 HLSL 的 `numthreads` 和数据量计算。

## 代码与限制

| 选项 | 用途 |
| --- | --- |
| `vs/hs/ds/gs/ps/cs` | 对应阶段代码文件 |
| `flags` | 编译标志 |
| `max_executions_per_frame` | 每帧最大执行次数 |
| `topology` | 点、线、三角形等图元拓扑 |
| `sampler` | 自定义采样过滤设置 |

## 管线状态

| 选项组 | 字段 |
| --- | --- |
| 混合 | `blend`、`alpha`、`mask`；支持 `[0]`–`[7]` 渲染目标后缀 |
| 混合公共项 | `alpha_to_coverage`、`sample_mask`、`blend_factor[0..3]`、`blend_state_merge` |
| 深度 | `depth_enable`、`depth_write_mask`、`depth_func` |
| 模板 | `stencil_enable/read_mask/write_mask/front/back/ref`，各项需写完整 `stencil_` 前缀 |
| 深度模板合并 | `depth_stencil_state_merge` |
| 光栅化 | `fill`、`cull`、`front`、`depth_bias`、`depth_bias_clamp`、`slope_scaled_depth_bias` |
| 光栅化开关 | `depth_clip_enable`、`scissor_enable`、`multisample_enable`、`antialiased_line_enable` |
| 光栅化合并 | `rasterizer_state_merge` |

例如：

```ini
[CustomShaderDraw]
vs = draw_vs.hlsl
ps = draw_ps.hlsl
cull = none
topology = triangle_list
blend = add src_alpha inv_src_alpha
draw = 3, 0
```

HLSL、输出目标、资源槽和视口须由调用场景提供。混合项格式为 `操作 源因子 目标因子`；颜色和 alpha 分开配置。

仅在需要的范围覆盖状态；对游戏绑定的临时替换要保存并恢复。模型输入布局见[布局与流输出](./layouts)。
