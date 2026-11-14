# 着色器覆盖与模型筛选

## ShaderOverride

```ini
[ShaderOverrideExample]
hash = 0123456789abcdef
CheckTextureOverride = ps-t0
```

Hash 使用 64 位着色器标识。也可用 `handling = skip` 跳过命中着色器的操作，或在命中时调用命令列表。

同一着色器可能被多个物体共用。要限定到部件，结合资源检查与 TextureOverride 的绘制条件。

## ShaderRegex 的当前范围

DX12 当前只运行**没有 Pattern 子节**的 shader-model 筛选路径：

```ini
[ShaderRegexPixel]
shader_model = ps_5_0
run = CommandListPixel

[CommandListPixel]
x0 = 1
```

它对匹配模型的候选着色器执行命令。不要在未经限定时使用 `handling = skip`，否则可能隐藏大量内容。

以下 DX11 语义不适用：

- `.Pattern` / `.Pattern.Replace` 的 PCRE2 汇编修补。
- `.InsertDeclarations` 插入声明。
- `temps` 自动临时寄存器与 `filter_index` 行为。

存在 Pattern 子节的组不会进入当前模型筛选执行路径，不能通过添加空 Pattern 规避。

## 导出与文件

F9 在 `ShaderFixes` 输出着色器与分析数据。二进制、汇编和使用摘要用于分析；不保证每个格式都能反编译成 HLSL。

## 二进制替换

DX12 在创建管线时，为已配置 ShaderOverride 的 Hash 查找：

```text
ShaderFixes/0123456789abcdef-ps.bin
```

Hash 与阶段要和目标一致；文件必须是可用于该阶段的有效编译字节码。先声明对应 `[ShaderOverride名称] hash = ...`，再放置替换二进制。

这是管线创建时的替换，修改后重启游戏以覆盖已经创建的管线；F10 不能证明所有旧管线已被重新创建。保持原字节码备份。

它不读取 DX11 的 `_replace.txt` HLSL 命名。需要独立图形绘制时使用[CustomShader](./custom-shaders)。
