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
| `oN` | 自定义元素的渲染目标设置路径 |

混合操作支持 add、subtract、rev_subtract、min、max；常用因子为 zero、one、src_alpha、inv_src_alpha、src_color、dest_color 及对应逆值。

内部模型还含 alpha、fill、front、sample_mask 等状态描述，但当前命令分类器不会把所有同名 DX11 字段送入状态路径；不能仅按内部结构名配置它们。

深度、模板、采样器和逐目标混合的完整 DX11 状态集合不视为兼容。需要这些状态时，先核对当前执行路径，避免得到默认状态却误以为配置生效。

## draw / dispatch

| 命令 | 参数 |
| --- | --- |
| `draw` | 顶点数、首顶点 |
| `drawindexed` | 索引数、首索引、基础顶点 |
| `dispatch` | X、Y、Z 线程组数 |
| `draw = from_caller` | 沿用调用者绘制参数 |
| `drawindexed = from_caller` | 沿用调用者索引绘制参数 |
| `drawindexed = auto` | 从替换索引缓冲推导数量 |

DX11 的显式 instanced/indirect 命令集合没有在此完整提供。选择 from_caller 时仍需核对原始调用类型与替换资源容量。

`dispatch` 作用于相应计算替换路径；它不使本页的图形元素自动创建独立计算 PSO。完整图形文件结构见[常用写法](./recipes)。
