# DX11 立体渲染与 HLSL 反编译修正

这些 `[Rendering]` 设置用于维护依赖立体变换的 DX11 游戏适配。参数引用目标着色器里的资源、矩阵和常量名。适配时先导出原着色器，定位相应名称，再检查生成的 HLSL 和两眼画面。

## 触发条件

- 游戏创建着色器时，自动反编译路径需要 `export_hlsl`、`fix_sv_position` 或 `recompile_all_vs` 至少一项开启。三者默认分别为 `0`、`false` 和 `false`。已有对应的 `*_bad.txt` 会跳过自动反编译。
- Hunting 的 HLSL 导出也会传入同一组反编译设置。导出、生成的代码可编译、运行时替换成功和修正效果需要分别检查。
- 深度、位置和矩阵修正还需要匹配反编译器识别的指令与资源。换用游戏版本、渲染路径或 shader 后应重新核对名称及表达式。

相关入口见 [`HackerDevice.cpp`](../src/DirectX11/HackerDevice.cpp)、[`Hunting.cpp`](../src/DirectX11/Hunting.cpp) 和 [`DecompileHLSL.cpp`](../src/HLSLDecompiler/DecompileHLSL.cpp) 的 `WritePatches`。

## 深度与世界坐标

| 键 | 内容与适用条件 |
| --- | --- |
| `fix_ZRepair_DepthTexture1`、`fix_ZRepair_DepthTexture2` | 两条候选深度重建路径各用一个 `纹理名.分量`，如 `SceneDepthTexture.x`。分量支持小写 `x/y/z/w/r/g/b/a`。格式错误会警告并保留已有值。 |
| `fix_ZRepair_Dependencies1`、`fix_ZRepair_Dependencies2` | 各候选路径需要的着色器依赖项。多个名称用逗号紧邻分隔。 |
| `fix_ZRepair_ZPosCalc1`、`fix_ZRepair_ZPosCalc2` | 从对应路径的深度样本计算 `zpos` 的 HLSL 片段。生成代码使用 `1.0 / zpos` 得到 `wpos`。 |
| `fix_ZRepair_PositionTexture`、`fix_ZRepair_PositionCalc` | 游戏已有坐标纹理时使用的纹理名和世界位置计算片段。模板里的 `%s` 用于代入反编译器识别的表达式。 |
| `fix_ZRepair_DepthTextureHash` | 备用深度 SRV 使用的 DX11 资源哈希。当前解析使用最多八位十六进制数，非零值启用该路径。命中要求游戏成功创建该资源的 SRV 且 DLL 已跟踪到资源哈希。 |

解析见 [`IniHandler.cpp`](../src/DirectX11/IniHandler.cpp) 的 `ParseDepthTextureParameter` 及 `[Rendering]` 设置消费。备用 SRV 的捕获逻辑见 [`HackerDevice.cpp`](../src/DirectX11/HackerDevice.cpp)。

## 反投影与对象位置

| 键 | 内容 |
| --- | --- |
| `fix_InvTransform` | 修正涉及的逆变换名称。多个名称用逗号紧邻分隔。 |
| `fix_BackProjectionTransform1`、`fix_BackProjectionTransform2` | 反投影向量的备选分量表达式，依赖着色器里对应的矩阵或常量。 |
| `fix_ObjectPosition1`、`fix_ObjectPosition2` | 要匹配的对象位置名称。 |
| `fix_ObjectPosition1Multiplier`、`fix_ObjectPosition2Multiplier` | 对应对象位置的修正系数片段。 |
| `fix_MatrixOperand1` | 要修正的矩阵名称。 |
| `fix_MatrixOperand1Multiplier` | 对应矩阵的修正系数片段。 |

三分量修正系数的写法见 [DX11 模板](../Dependencies/d3dx.ini) 的 AUTOMATIC 段。模板中的常量名和数值来自特定适配。调试时通过 [DX11 诊断](DX11-Diagnostics.md) 导出目标着色器，检查实际生成的补丁和编译日志。
