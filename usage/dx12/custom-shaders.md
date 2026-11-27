# CustomShader

通过自定义元素定义图形着色器文件、状态、资源和绘制，再用 `run` 调用。

```ini
[CustomShaderDraw]
vs = draw_vs.hlsl
ps = draw_ps.hlsl
o0 = set_viewport bb
draw = 3, 0

[Present]
run = CustomShaderDraw
```

需提供实际 HLSL；两个文件入口均为 `main`，当前图形路径分别编译为 `vs_6_0`、`ps_6_0`，两阶段都必须可用。部署编译器时保留配套的 `dxcompiler.dll` 与 `dxil.dll`。

共享图形根签名提供 space 0 的 t0–t127 与 s0 线性 clamp 采样器，t120 用于 IniParams。默认深度和模板关闭，输出单个非 MSAA 颜色目标。不能把游戏的整套根签名或 DX11 管线合并规则作为隐含前提。

`cs` 等阶段名能被解析，但这里的文件编译与图形 PSO 路径只接入 VS/PS，不把 `cs = 文件` 当作已实现的独立计算着色器调用。

## 当前可直接配置的图形状态

```ini
[CustomShaderDraw]
vs = draw_vs.hlsl
ps = draw_ps.hlsl
cull = none
topology = triangle_list
blend = add src_alpha inv_src_alpha
draw = 3, 0
```

| 字段 | 值 |
| --- | --- |
| `vs/ps` | 当前图形路径接入的文件 |
| `blend` | `disable` 或 `操作 源因子 目标因子` |
| `cull` | none/front/back |
| `topology` | point_list、line_list/strip、triangle_list/strip 及支持的邻接/patch 形式 |
| `o0`–`o7` | 自定义元素识别的渲染目标槽。值含 `bb` 时使用后备缓冲 |

混合操作支持 add、subtract、rev_subtract、min、max；常用因子为 zero、one、src_alpha、inv_src_alpha、src_color、dest_color 及对应逆值。

当前 INI 配置会忽略 `alpha`、`fill`、`front` 和 `sample_mask`。光栅化使用实心填充，正面按顺时针判断。

深度、模板、采样器和逐目标混合的完整 DX11 状态集合不视为兼容。需要这些状态时，先核对当前执行路径，避免得到默认状态却误以为配置生效。

## draw / dispatch

| 命令 | 参数 |
| --- | --- |
| `draw` | 顶点数、首顶点 |
| `drawindexed` | 索引数、首索引、基础顶点 |
| `drawinstanced` | 每实例顶点数、实例数、首顶点、首实例 |
| `drawindexedinstanced` | 每实例索引数、实例数、首索引、有符号基顶点、首实例 |
| `dispatch` | X、Y、Z 线程组数 |
| `draw = from_caller` | 沿用调用者绘制参数 |
| `drawindexed = from_caller` | 沿用调用者索引绘制参数 |
| `drawindexed = auto` | 从替换索引缓冲推导数量 |

在原绘制的替换命令列表内，两条实例绘制命令都接受整数和运行时数值表达式。例如 `drawindexedinstanced = 12, $copies, 0, -2, 4` 使用当前索引绑定，从实例 4 开始绘制。参数数量错误或数值越界会跳过该条命令。

独立的 Present 图形元素可记录第一条非索引 `drawinstanced`。该路径尚缺索引缓冲绑定和间接绘制支持。

`dispatch` 作用于相应计算替换路径；它不使本页的图形元素自动创建独立计算 PSO。完整图形文件结构见[常用写法](./recipes)。
