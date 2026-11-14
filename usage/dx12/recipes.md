# 常用写法

各例独立使用，先替换 Hash 和资源文件。未适配具体游戏的数据不会自动生成正确模型。

## 条件贴图替换

```ini
[Constants]
global persist $variant = 0

[KeyVariant]
key = NO_MODIFIERS F6
type = cycle
$variant = 0, 1

[ResourceA]
filename = a.dds

[ResourceB]
filename = b.dds

[TextureOverrideDiffuse]
hash = 12345678
if $variant == 0
    this = ResourceA
else
    this = ResourceB
endif
```

INI、`a.dds`、`b.dds` 放在同一 Mod 目录。目标需是实际命中的贴图 Hash。

## 按部件替换模型

```ini
[ResourceIndices]
type = Buffer
format = R32_UINT
filename = body.ib

[ResourceVertices]
type = Buffer
stride = 32
filename = body.buf

[TextureOverrideBody]
hash = 12345678
match_first_index = 0
match_index_count = 300
handling = skip
ib = ResourceIndices
vb0 = ResourceVertices
drawindexed = 300, 0, 0
```

步长、索引格式、数量和基础顶点必须来自实际数据。如果顶点由计算着色器先行处理，继续核对计算输入/输出链，不能只替换这次 draw。

## 图形着色器最小结构

INI：

```ini
[CustomShaderTint]
vs = triangle_vs.hlsl
ps = triangle_ps.hlsl
o0 = set_viewport bb
cull = none
blend = add src_alpha inv_src_alpha
draw = 3, 0

[Present]
run = CustomShaderTint
```

`triangle_vs.hlsl`：

```hlsl
float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
```

`triangle_ps.hlsl`：

```hlsl
float4 main() : SV_Target
{
    return float4(1, 0, 0, 0.1);
}
```

该结构画一层半透明红色全屏三角形，不依赖顶点文件。三个文件放在同一 Mod 目录，按 F10 重载；关闭时禁用该目录再重载。

需要 SM6 编译器、可用后备缓冲与自定义图形执行路径。代码结构对应当前编译接口，仍需在目标游戏确认绘制结果。
