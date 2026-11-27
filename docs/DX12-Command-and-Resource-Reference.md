# DX12 命令与资源参考

面向 `d3d12.dll` 的使用者与 Mod 作者。本文件按当前源码的**注册 → 解析 → 消费**路径逐条列出 DX12 支持的命令、变量、运行时值与资源语义，并标注每条结论的源码位置，便于逐项复核。

- 使用者侧说明见 [`usage/dx12/`](../usage/dx12/index.md)：[命令列表](../usage/dx12/command-lists.md)、[变量与表达式](../usage/dx12/expressions.md)、[运行时参数](../usage/dx12/runtime-values.md)、[资源与引用](../usage/dx12/resources.md)、[TextureOverride 与模型替换](../usage/dx12/texture-overrides.md)。
- DX11 的同名功能见 [`ini-language-extensions/`](ini-language-extensions/commands/README.md)。DX12 **不是** DX11 命令集的超集，本文逐条标注支持 / 不支持。
- 本文中的示例用于说明语法与调用链，**未在游戏中实测**，不构成"已验证可用"的结论。

---

## 1. 解析与执行总览

### 1.1 节前缀 → 解析器

所有 INI 段先按前缀分派，再由对应解析器生成配置对象。命令列表是其中之一。

| 节前缀 | 解析入口 | 产物 |
| --- | --- | --- |
| `CommandList`、`BuiltInCommandList`、`CustomShader`、`BuiltInCustomShader`；以及精确的 `Present`、`Constants` | `MigotoCommandListParser::ParseSections` | `CommandListConfig`（动作流） |
| `ShaderOverride` | `MigotoShaderOverrideParser::ParseSections` | `ShaderOverrideConfig` |
| `TextureOverride` | `MigotoTextureOverrideParser::ParseSections` | `TextureOverrideConfig` |
| `Resource` | `MigotoResourceParser::ParseSections` | `ResourceConfig` |
| `ShaderRegex` | `MigotoShaderRegexParser::ParseSections` | `ShaderRegexConfig` |

源码：`src/DirectX12/Config/DX12Config.h`；节前缀判定：`src/common/MigotoIniCommandListUtils.hpp:46`（`IsExplicitCommandListSection`）。

> **注意**：`CommandList` 族前缀是"命令列表"的判定，`CustomShader` 族前缀的段同时进入命令列表解析；只有当该段确实带阶段着色器文件或渲染状态行时，才被登记为"自定义着色器元素"（`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:160`，`BuildSpec`）。

### 1.2 动作流

解析结果是**动作流**（`std::vector<CommandListAction>`），每个动作有一个 `CommandListActionKind`（`src/DirectX12/Config/DX12Config.h:453`）。执行器按动作流逐条消费（`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1504`、`:1596`）。

同一段动作流有两个执行路径：

- **编译计划路径**：不含条件分支的命令列表会被预编译为 `DX12CompiledCommandListPlan`，走 `ExecuteCompiledCommandListPlan`（`:928`）。
- **VM 路径**：含 `if/else` 等分支时由 `MigotoCommandVmUtils::State` 逐条求值（`src/common/MigotoCommandVmUtils.hpp:300`）。

两条路径对同一动作的语义一致；VM 路径额外负责条件分支、变量赋值与 `post` 延迟写。

### 1.3 键名匹配顺序（重要）

`MigotoCommandListParser::ParseActionFromEntry`（`src/DirectX12/Config/DX12Config.h:815`）按固定顺序判定键名，**先匹配者胜出**：

```
run → checktextureoverride → handling(skip) → draw → drawindexed → dispatch
→ vs/hs/ds/gs/ps/cs → post $x|post xNN → local $x → $x | global $x | persist $x
→ Resource* → xNN/yNN/zNN/wNN → blend|cull|topology|depth|stencil|oN
→ this → ib | vbN | <stage>-cbN | <stage>-tN | <stage>-uN
```

键名统一转小写后比较，因此 `RUN`、`Run`、`run` 等价。值的大小写按各命令单独判定（`skip`、`auto`、`from_caller` 等均小写比较）。

**未被识别的键会被静默丢弃**（`ParseActionFromEntry` 返回 `false` 后该条目不入动作流）。拼写错误没有报错、也没有警告，只能靠逐条核对本文的键名表发现。

其中渲染状态键的集合由 `IsRenderStateKey` 固定（`src/common/MigotoIniCommandListUtils.hpp:170`）：只有 `blend`、`cull`、`topology`、`depth`、`stencil` 和 `o`+数字会变成 `SetRenderState` 动作。DX11 的 `fill`、`front`、`alpha`、`sample_mask` **不在该集合内**，写入后被静默丢弃（详见 §2.10）。

### 1.4 名称解析与命名空间

- `run = X`：`X` 以 `CommandList` / `BuiltInCommandList` / `CustomShader` / `BuiltInCustomShader` 开头，或精确等于 `Present` / `Constants` 时，按显式节名解析；否则按 `CommandList` + 名称在当前命名空间内解析（`src/DirectX12/Config/DX12Config.h:705`，`ResolveCommandListReference`）。
- 资源引用：`ref ` 前缀会被剥掉后按 `Resource` 命名空间解析（`:717`，`ResolveResourceReference`）。绑定值上也可直接写 `ref `：`ps-tN = ref ResourceX` 与 `ps-tN = ResourceX` 等价（`src/DirectX12/ModRuntime/DX12ResourceAliasStore.h:109`）。
- 变量名会**折叠命名空间**：`$\Example\foo` 与 `$foo` 指向同一个全局变量（`src/common/MigotoCommandVmUtils.hpp:469`，`NormalizeVariableName`）。

---

## 2. 命令一览

### 2.1 调用与阶段链接

| 语法 | 解析 | 说明 |
| --- | --- | --- |
| `run = CommandList名称` | `Run`（`DX12Config.h:824`） | 调用另一个命令列表。目标缺失时写日志 `run_target_missing`（`DX12CommandListExecutor.h:1517`）后跳过。 |
| `pre = 名称` / `pre run = 名称` / `prerun = 名称` | `links.pre`（`DX12Config.h:955`） | 前置列表，在主列表之前执行。 |
| `post = 名称` / `post run = 名称` / `postrun = 名称` | `links.post`（`DX12Config.h:959`） | 后置列表，在主列表之后、且只在 post 阶段执行。 |

`pre` / `post` / `run` 三个链接键可同时出现在同一段中，各自可重复（`AppendRunList` 按出现顺序追加，`DX12Config.h:696`）。

链接目标为 `CustomShader*` 时，执行器不解释其函数体，而是把它登记为一条"自定义着色器元素"记录，留到 Present 阶段回放（`DX12CommandListExecutor.h:1427`）。

### 2.2 控制流

原始行（无 `=`）被识别为控制流（`src/common/MigotoIniCommandListUtils.hpp:107`，`IsControlFlowRawLine`）：

| 语法 | 说明 |
| --- | --- |
| `if <表达式>` | 进入分支。父分支不激活时，条件不再求值。 |
| `else if <表达式>` / `elif <表达式>` | 两种拼写等价；仅在父分支激活且尚无分支命中时求值。 |
| `else` | 父分支激活且无分支命中时激活。 |
| `endif` | 关闭当前分支。 |

分支栈由 VM 维护（`MigotoCommandVmUtils.hpp:420`，`ApplyControlFlow`）。`if` / `elif` / `else` 在没有对应 `if` 时被忽略；`endif` 在没有分支时是空操作。**没有循环语法**，也不需要闭合检查——未闭合的 `if` 会一直保持到段末。

### 2.3 变量与延迟写

| 语法 | 动作 | 作用域 |
| --- | --- | --- |
| `$名 = 表达式` | `VariableAssignment` | 先在 local 作用域由内向外查找命中；未命中则写全局表。 |
| `global $名 = 表达式` | `VariableAssignment` | 同上（`global` 只是声明前缀）。 |
| `global persist $名 = 表达式` | `VariableAssignment` | 同上，并登记为持久变量。 |
| `persist $名 = 表达式` | `VariableAssignment` | 同上，并登记为持久变量。 |
| `local $名 = 表达式` | `LocalVariableAssignment` | 写入**当前最内层** local 作用域。 |
| `post $名 = 表达式` | `PostVariableAssignment` | 延后到 post 阶段写。 |
| `post x87 = 表达式` | `PostVariableAssignment` | 延后到 post 阶段写着色器常量。 |

判定规则见 `src/common/MigotoIniCommandListUtils.hpp:116`（`IsVariableAssignmentKey` / `IsVariableDeclarationKey` / `IsLocalVariableAssignmentKey` / `IsPostVariableAssignmentKey`）。

前缀剥离顺序为 `post ` → `local ` → `global persist ` → `global ` → `persist `（`src/common/MigotoCommandVmUtils.hpp:514`，`ExtractVariableName`）；`global persist ` 先于 `global ` 命中，所以 `global persist $x` 正确剥离为 `$x`。

**作用域规则**：每次进入一个命令列表或动作列表，执行器压入一个 local 作用域，退出时弹出（`DX12CommandListExecutor.h:74`，`CommandVmRunScope`）。因此：

- `local $x` 只在**声明它的那个列表**内可见；被调用列表返回后即失效。
- 读取同样由内向外查找 local，找不到才落到全局表（`MigotoCommandVmUtils.hpp:528`，`LookupVariable`）。

**`global` 不强制写全局**：`global $x = 1` 与 `$x = 1` 走同一条写入路径（`Assign` → `AssignResolvedName`），若当前作用域链上存在同名 local，写入的是那个 local。DX12 的 VM 没有"跳过 local 直写全局"的分支，需要全局语义时请确保作用域链上没有同名 local。

### 2.4 着色器常量（IniParams）

`x0` / `y0` / `z0` / `w0` 形式（首字母 `x|y|z|w`，其后全为数字）被识别为着色器常量（`src/common/MigotoIniCommandListUtils.hpp:156`，`IsShaderConstantKey`），赋值走 `AssignSymbol`，写入与变量同一张全局表。

打包规则（`src/DirectX12/ModRuntime/DX12CustomShaderRegisters.h`）：

- 常量 `xNN` 对应 IniParams 的第 `NN` 个 float4 的 `.x`；`x87/y87/z87/w87` 即 `IniParams[87].xyzw`。
- IniParams 是 1D float4 纹理，共 **512** 个槽位（`kIniParamsFloat4Count = 512`），在自定义着色器中位于寄存器 `t120`（`kIniParamsShaderRegister = 120`）。
- 打包在**元素登记时**对全局表做一次快照（`DX12CommandListExecutor.h:1480`），所以同一个元素里先写常量再 `run` 才看得到新值。

只有配套的自定义着色器实际读取该寄存器时，设值才影响画面。

### 2.5 绘制与调度

| 语法 | 参数 | 说明 |
| --- | --- | --- |
| `draw = 顶点数, 起始顶点` | 2 个无符号整数 | 非索引绘制。 |
| `drawindexed = 索引数, 起始索引, 基顶点` | 3 个无符号整数 | 索引绘制。基顶点在 DX12 中按有符号使用。 |
| `drawinstanced = 顶点数, 实例数, 首顶点, 首实例` | 4 个数值 | 每实例顶点数和实例范围均由该命令指定。 |
| `drawindexedinstanced = 索引数, 实例数, 首索引, 基顶点, 首实例` | 5 个数值 | 基顶点接受有符号 32 位整数，其余参数取无符号 32 位整数。 |
| `draw = auto` | — | 顶点数取当前 VB0 视图字节数除以步长，绘制一个实例。 |
| `drawindexed = auto` | — | 索引数按当前索引视图的格式和字节数计算，绘制一个实例。 |
| `drawindexedinstanced = auto` | — | 同上，实例数及首实例取自被拦截的绘制。缺少原调用时跳过。 |
| `draw = from_caller` | — | 顶点数与起始顶点取自被拦截的原始绘制。 |
| `drawindexed = from_caller` | — | 索引数、起始索引、基顶点取自被拦截的原始绘制。 |
| `dispatch = X组, Y组, Z组` | 3 个无符号整数 | 计算派发。 |

参数以逗号分隔，个数必须精确匹配。显式实例绘制可以混用整数字面量和运行时表达式。表达式结果向零截断，非法表达式、非有限值或整数越界都会跳过本条指令。普通 `draw`、`drawindexed` 和 `dispatch` 的数值形式使用整数字面量。

执行语义（`src/DirectX12/Hooks/DX12IaReplacement.h:118`，`ExecuteReplacementDraws`）：

- 整数参数形式的 `draw` 与 `drawindexed` 继承原调用的实例数与首实例。显式实例绘制将配置的值传给 DX12，包括零计数。
- `from_caller`：沿用原始绘制的顶点/索引数与起点；得到的计数为 0 时该次绘制被跳过。
- 自动数量使用绘制回放时实际绑定的视图。索引视图只接受 R16_UINT 和 R32_UINT。DX12 视图字节数已计入绑定偏移，除法只保留完整元素。零步长、空视图和零元素均跳过。自动绘制从顶点或索引 0 开始，基顶点为 0。
- 普通绘制：顶点数为 0 时跳过。
- `dispatch`：`X/Y/Z` 任一为 0 时该次派发被跳过（`DX12IaReplacement.h:131`）。

底层调用分别是 `DrawInstanced` / `DrawIndexedInstanced` / `Dispatch`。

### 2.6 绑定目标

`MigotoCommandListParser::ParseTarget`（`src/DirectX12/Config/DX12Config.h:647`）支持的键：

| 键 | 含义 | 约束 |
| --- | --- | --- |
| `ib` | 索引缓冲 | — |
| `vbN` | 顶点缓冲 | `N` 为十进制数字 |
| `vs-cbN` / `ps-cbN` / `cs-cbN` | 常量缓冲 | 阶段前缀仅 `vs` / `ps` / `cs` |
| `vs-tN` / `ps-tN` / `cs-tN` | 着色器资源视图（SRV） | 同上 |
| `ps-uN` / `cs-uN` | 无序访问视图（UAV） | **仅 `ps` 与 `cs`**，`vs-uN` 不被识别 |

槽位号由 `ParseUnsignedSuffix` 解析，只接受纯十进制数字（`src/common/MigotoIniCommandListUtils.hpp:56`）。

绑定值必须非空，否则该条命令被丢弃（`DX12Config.h:939`）。

绑定语义：绑定不是"写入某个固定寄存器"，而是解析**当前根签名与描述符**中该槽位的现有绑定，再把资源替换进去（`ApplySetDescriptorResourceAction`，`DX12CommandListExecutor.h:446`）。因此：

- 目标槽位必须在游戏当前的根签名里存在对应类型的描述符范围，否则无法解析出可替换的绑定。
- 计算阶段（`cs`）的绑定从计算根签名解析，与图形根签名分开。
- `ps-tN` 赋值会被额外记入"当前像素 SRV 绑定"，供后续自定义元素回放复现（`DX12CommandListExecutor.h:481`）。

`ib = null` / `vbN = null` 表示显式解绑：解析器记录 `explicitNullBindings` 标记，但**不**产生运行时动作（`DX12Config.h:1579`），实际语义仍由 `handling = skip` 承担。

### 2.7 `this` 与 `handling`

| 语法 | 位置 | 说明 |
| --- | --- | --- |
| `this = 资源名` | TextureOverride 段**顶层** | 静态快路径：替换本次命中贴图的内容（`drawReplaceThisResource`，`DX12Config.h:1725`）。 |
| `this = 资源名` | TextureOverride 段的 `if/else` 内 | 运行时分支：动作流中的 `SetThisResource`，由 VM 决定用哪个资源（`DX12Config.h:1730`）。 |
| `handling = skip` | 任意段 | 设置替换对象的 skip 标志，拦截到的原始绘制不再执行。 |

`SetThisResource` 需要一个**已按描述符命中**的目标；没有命中目标时是空操作，并写诊断日志 `DX12SetThisResourceDiag` / `skipped_no_target`（`DX12CommandListExecutor.h:507`）。因此按 IA 绑定命中的覆盖段里，`this` 没有可替换对象。

`handling` 只有取值 `skip` 时被识别；其它取值（如 `handling = force`）不被识别，该条目被静默丢弃（`DX12Config.h:835`）。

### 2.8 `CheckTextureOverride`

`CheckTextureOverride = <绑定目标>`（`DX12Config.h:830`）：在当前上下文查找该目标上是否存在可命中的 TextureOverride，若命中则执行该覆盖段的命令列表与动作（`DX12CommandListExecutor.h:894`，`RunCheckTextureOverrideTarget`）。

- 目标按 §2.6 的目标语法解析；常量缓冲 / SRV / UAV 三类目标会预热描述符覆盖缓存。
- 未命中时不做任何事，不报错。
- 命中时执行的是该覆盖段自己的 `pre` / `run` / `post` 链接与动作，递归深度同样受限。

### 2.9 资源赋值（`ref`）

`Resource名 = ...`（键以 `Resource` 开头，大小写不敏感）被识别为 `ResourceAssignment`（`DX12Config.h:891`）。值为空时整条被丢弃。

值分三类（`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1385`，`RegisterResourceAliasFromAction`）：

| 值 | 行为 |
| --- | --- |
| `ref Resource名` | 注册**资源别名**：该名字在后续解析时指向目标资源（不复制像素）。 |
| `ref ps-tN`（或 `ref vs-tN`） | **捕获**当前绑定在该槽位的纹理，存为该资源名。 |
| 其它 | 不支持，写一次诊断日志 `DX12ResourceAliasDiag` / `unsupported_copy` 后忽略。 |

别名解析规则（`src/DirectX12/ModRuntime/DX12ResourceAliasStore.h:52`）：

- 别名键统一转小写存储，目标保留原拼写（目标要按原样去加载资源）。
- 别名链最多跟随 **16** 跳；出现环或链过长时返回最初的查询名。
- 别名在**命令列表实际执行时**注册，因此别名可以由分支条件决定；别名表随每次配置重载清空。

捕获语义（`src/DirectX12/ModRuntime/DX12DrawTextureCaptureUtils.h:27`）：

- 只支持 `D3D12_RESOURCE_DIMENSION_TEXTURE2D` 的 SRV；缓冲 / CBV 没有可复现的视图契约。
- 保存的是**活动资源引用**（`AddRef`）+ 自建 SRV 描述符，**不是** GPU 内容快照；游戏后续对该资源的写入仍会影响捕获结果。
- 不发拷贝命令、不插屏障，游戏命令列表不被污染。
- 重复捕获时旧引用通过延迟销毁路径释放，在途的旧帧描述符保持有效。

### 2.10 自定义着色器元素专用键

以下键只有写在 `CustomShader*` 段里才有意义；写在普通命令列表里会被解析成动作但**在执行器中被忽略**（`DX12CommandListExecutor.h:1575`、`:1663`）。

| 键 | 取值 | 说明 |
| --- | --- | --- |
| `vs` / `hs` / `ds` / `gs` / `ps` / `cs` | 着色器文件路径 | 每个阶段一个文件，相对声明它的 INI 解析。 |
| `blend` | `disable` 或 `op src dst [alpha_op alpha_src alpha_dst]` | 渲染目标混合。`op` ∈ `add`/`subtract`/`rev_subtract`/`min`/`max`；因子见 `DX12CustomShaderElement.h:235`。 |
| `cull` | `none` / `front` / `back` | 剔除模式。 |
| `topology` | `point_list` / `line_list` / `line_strip` / `triangle_list` / `triangle_strip` / `*_adj` / `N_control_point_patch_list` | 拓扑。 |
| `depth` / `stencil` | — | 记录到 `stateLines`，由 PSO 构建阶段解释。 |
| `o0` – `o7` | — | 渲染目标设置路径。仅这 8 个槽位会被识别为渲染目标；值含 `bb` 时表示该元素写后备缓冲（`hasBackbufferTarget`）。`o8` / `o9` 仍是合法的渲染状态键，但只被记入 `stateLines`，不参与后备缓冲判定（`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:450`）。 |

解析见 `src/DirectX12/ModRuntime/DX12CustomShaderElement.h:377`（`ApplyRenderStateLine`）。

**当前无法由用户配置的键**：`alpha`、`fill`、`front`、`sample_mask` 在内部模型里确有对应字段（`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:121`、`:126`），但它们**不在 `IsRenderStateKey` 的键集合内**（`src/common/MigotoIniCommandListUtils.hpp:170` 只接受 `blend` / `cull` / `topology` / `depth` / `stencil` / `o`+数字），解析阶段就不会生成 `SetRenderState` 动作，`ApplyRenderStateLine` 的对应分支在当前源码下**不可达**——写进 `[CustomShaderX]` 会被**静默丢弃**，既不生效也不报错。只写了这些键的段甚至不会被登记为自定义着色器元素（`BuildSpec` 的 `anyState` 只能由 `SetShader` 或 `SetRenderState` 置真，`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:160`）。

spec 的默认值是 `fill=solid` / `cull=back` / `topology=triangle_list` / `front=clockwise`（`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:121`–`:128`）。其中 `cull` 与 `topology` 可由上表覆盖，`fill` 与 `front` **目前无法覆盖**；缺省值不回落到游戏状态。

元素回放的限制（`DX12CommandListExecutor.h:1464`）：

- 绘制参数取函数体中**第一条普通 `Draw` 动作**的 `count` / `start`；含条件分支或多条绘制的函数体退化为默认的 4 顶点 strip。
- 单帧最多回放 **64** 个元素（`kMaxCustomElementsPerFrame`，`src/DirectX12/ModRuntime/DX12CustomShaderRegisters.h:40`），超出的元素被跳过并写诊断。
- 元素在 Present 阶段回放（`src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:311`），早于回放会与后备缓冲的 PRESENT 状态竞争。

### 2.11 TextureOverride / ShaderOverride 匹配键

`TextureOverride` 段的键由 `MigotoTextureOverrideParser::ParseSections` 解析（`src/DirectX12/Config/DX12Config.h:1497`），`ShaderOverride` 段由 `MigotoShaderOverrideParser::ParseSections` 解析（`:1128`）。下表行号均指向 `DX12Config.h`。

**匹配类**

| 键 | 含义 | 默认值 | 位置 |
| --- | --- | --- | --- |
| `hash` | 目标资源 Hash（十六进制，可带 `0x` 前缀，最多 8 位十六进制字符） | 0 | 1557 |
| `match_vertex_count` | 顶点数量比较 | 未设置 | 1609 |
| `match_index_count` | 索引数量比较 | 未设置 | 1615 |
| `match_instance_count` | 实例数量比较 | 未设置 | 1621 |
| `match_first_vertex` | 起始顶点比较 | 未设置 | 1627 |
| `match_first_index` | 起始索引比较 | 未设置 | 1633 |
| `match_first_instance` | 起始实例比较 | 未设置 | 1639 |
| `match_priority` | 同 Hash 段的匹配优先级，整数越大越先 | 0 | 1645 |
| `cs_hash` | 计算着色器 Hash（64 位） | 未设置 | 1654 |
| `match_cs_tN_hash` | 计算输入 SRV 的 Hash | 未设置 | 1672 |
| `match_srvN_hash` | 同上（另一种拼写） | 未设置 | 1679 |
| `match_uav_bytes` | **仅记录存在标记**，解析出的数值被丢弃 | 未设置 | 1663 |

数值比较的写法由 `ParseNumericMatch`（`:1410`）解析：`N`、`=N`、`!N`、`<N`、`<=N`、`>N`、`>=N`；无法解析成无符号数时该项视为未设置。注意 `!=N` **不是**受支持的写法：解析器只剥离一个前导 `!`，剩下的 `=N` 无法解析成数字，于是按解析失败处理——该匹配条件被**禁用**，而不是"不等于"。

`match_uav_bytes` 的**数值不参与筛选**。解析器把数值解析出来后直接丢弃，只保留 `hasMatchUavBytes` 布尔标记（`:1663`）；该标记只影响段的执行路径：含它的段不会被当作"纯静态贴图替换"走快路径（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:84`），也不会进入静态 IA 替换表（`src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h:1873`）。换言之，写下这个键**不会**按字节数筛选 UAV。

**绑定 / 替换类**

| 键 | 含义 | 位置 |
| --- | --- | --- |
| `handling` | 仅 `skip` 生效 | 1566 |
| `ib` | 索引缓冲（`null` 为显式解绑） | 1579 |
| `vbN` | 顶点缓冲（`null` 为显式解绑） | 1596 |
| `ps-tN` / `vs-tN` | 图形阶段 SRV 替换 | 1699 / 1715 |
| `cs-tN` / `srvN` | 计算阶段输入资源 | 1686 / 1692 |
| `this` | 命中的贴图内容替换 | 1725 |
| `override_byte_stride` | 扩容相关顶点字节步长 | 1740 |
| `override_vertex_count` | 扩容相关顶点数 | 1749 |
| `uav_byte_stride` | 输出缓冲步长 | 1758 |

`ShaderOverride` 段支持 `hash`（64 位着色器 Hash，`:1160`）、`handling`（`:1169`），以及 §2.1–§2.10 的通用动作键。

**登记与排序规则**

- 缺省 `hash = 0`；没有 hash 的段只有在具备扩容参数、`cs_hash` 或绘制上下文匹配之一时才被登记（`:1788`）。
- 同一 Hash 下的多个段按 `match_priority` 降序排列，优先级相同时按节名（小写）升序（`:1792`）。
- 写在 `if/else` 分支内的 `ib`、`vbN`、`ps-tN`、`vs-tN`、`this` 不进入加载期的静态捕获，而是留在运行时动作流里由分支决定（`:1516`、`:1587`、`:1705`、`:1729`）。

---

## 3. pre / post 阶段模型

### 3.1 阶段划分

| 阶段 | 何时执行 | `post` 赋值是否生效 |
| --- | --- | --- |
| pre | 覆盖命中后、主绘制之前 | **不生效**（被丢弃，不排队） |
| main | 覆盖命中后 | **不生效** |
| post | 覆盖的 post 阶段 | 生效，在作用域结束时统一写入 |

`ApplyVmAction` 对 `PostVariableAssignment` 的判定是 `if (includePost && mVm.Active())`（`DX12CommandListExecutor.h:1683`）：只有处于 post 阶段时才排队。

### 3.2 `post` 赋值的写入时机

排队的 `post` 写在**作用域退出时**统一执行（`CommandVmRunScope` 析构函数 → `FlushPostAssignments`，`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:91`）。因此：

- 表达式在**写入时刻**求值，不是排队时刻。想恢复被覆盖前的值，必须先把旧值存进另一个变量。
- 延后写与立即写共用同一条写入路径（常量目标走 `AssignResolvedName`），所以 `post x87 = $bak` 与 `x87 = $bak` 落到同一个槽位。

### 3.3 `post` 的目标受限

`IsPostVariableAssignmentKey` 只接受两种目标（`src/common/MigotoIniCommandListUtils.hpp:135`）：`$` 开头的变量，或着色器常量名（`xNN` 形式）。

**`post ps-t0 = ResourceX` 不被识别为延后写**，它也不匹配任何绑定目标（`ParseTarget("post ps-t0")` 失败），结果是整条被**静默丢弃**。要延后恢复的是绑定而不是常量时，DX12 没有对应语法。

### 3.4 链接与阶段的组合

一个段的 `pre` / `run` / `post` 链接分别在 pre / main / post 阶段运行；`pre` 列表内部的 `post` 赋值不会生效（pre 阶段 `includePost=false`）。`post` 列表自身在 post 阶段运行，其内部的 `post` 赋值在列表结束时写入。

---

## 4. 表达式

表达式由 `src/common/MigotoCommandVmUtils.hpp` 中的 `ExpressionParser` 求值。

### 4.1 优先级（高 → 低）

| 级别 | 语法 |
| --- | --- |
| 1 | `(` `)`、数学函数调用、单目 `!` `~` `+` `-` |
| 2 | `**`，右结合 |
| 3 | `*` `/` `//` `%` |
| 4 | `+` `-` |
| 5 | `<<` `>>` |
| 6 | `<` `<=` `>` `>=` |
| 7 | `==` `!=` `===` `!==` |
| 8–10 | `&`、`^`、`\|`，依次降低 |
| 11–12 | `&&`、`\|\|` |

### 4.2 数值规则

| 规则 | 取值 | 源码 |
| --- | --- | --- |
| 算术精度 | 操作数与各步结果均为 float32 | `src/common/IniExpressionNumbers.h` |
| 数值相等 | 精确比较 float32 数值。正负零相等，NaN 与自身不相等 | `ParseEquality` |
| 真值判定 | 正负零为假，其余数值和 NaN 为真 | `Truthy` |
| 除法 / 整除 / 取模 | 非零除数正常参与运算。`//` 对商向下取整，`%` 使用 `fmod`。非零有限数除以零得到有符号无穷，零除以零或对零取模得到 NaN | `src/common/IniExpressionNumbers.h` |
| `&&` / `\|\|` | 两侧都求值 | `ParseAnd`、`ParseOr` |
| 数字字面量 | `0b` 或 `0B` 后可接 1–64 个二进制位，读入时舍入到 float32。其他数值使用 `wcstof` 解析 | `ReadExpressionNumber` |
| 一元数学函数 | 21 个纯数值函数与 DX11 共享实现和回归用例，函数的参数与结果使用 float32 | `src/common/IniUnaryMath.h` |
| `random(max)` | 两端共用有界采样器。正上限取 [0, max)，负上限取 (max, 0]。换帧更新种子并保留采样序号。语法错误、零或非有限上限均不消耗样本 | `src/common/ExpressionFunctions.cpp` |
| 位运算与移位 | `~ & ^ \| << >>` 与 DX11 共用操作数转换、算术右移和 float32 结果舍入规则。移位数取低 5 位 | `src/common/IniBitwise.h` |
| 标识符 | 首字符 `$` `_` 字母；后续可含 `\` 与数字（支持 `$\命名空间\变量`） | `:237`、`:243` |

**逻辑不短路**是有意设计：短路写法会让右操作数的记号未被消费，`Parse()` 的"文本必须读完"检查随之失败，整条表达式退化为 0。副作用表达式放在 `&&` 右侧要特别小心。

### 4.3 失败路径

- 表达式无法完整解析时，**整个表达式返回 0**（`ok=false` → `Parse()` 返回 `0.0`，`:91`），不会报错。
- 未定义的标识符读取为 **0**（`LookupVariable` 找不到即返回 `0.0`，`:551`）。这不是"功能探测"手段。
- **不带 `$` 的裸标识符不会命中变量**。local 表的键由 `ExtractVariableName` 保留 `$` 前缀，全局表的变量键同样带 `$`（`SetGlobal` → `WriteGlobalLocked` → `NormalizeVariableName`），因此 `foo` 查不到 `$foo`，读取结果为 0。裸标识符只能命中**着色器常量**（`x87` 等，由 `AssignSymbol` 写入同一张全局表）与**内置名**（如 `frame_time`、`fps`、`draw_type`）。引用变量必须写 `$变量名`。
- 除数为 0（或接近 0）时结果为 0，不会抛异常；脚本应自行检查除数。

---

## 5. 可见运行时值

### 5.1 帧变量

帧变量在**帧边界**（Present）采集并写入全局表（`src/DirectX12/Runtime/DX12VmFrameVariablesUtils.h:93`，`ApplyGlobals`）：

| 名称 | 含义 | 取值来源与回退 |
| --- | --- | --- |
| `window_width` / `window_height` | 客户区尺寸 | 客户区 < 640（宽）/480（高）时依次回退到 `res_*`、`rt_*`，最后用 `GetSystemMetrics` 屏幕尺寸（`:224`、`:235`） |
| `res_width` / `res_height` | 后备缓冲尺寸 | 交换链 `BufferDesc`；`IDXGISwapChain1::GetDesc1` 的非零值优先；≤0 时回退到窗口尺寸（`:68`、`:129`） |
| `rt_width` / `rt_height` | 帧快照中的缓冲尺寸 | 取 `res_*`，为 0 时取窗口尺寸（`:74`） |
| `cursor_screen_x` / `cursor_screen_y` | 屏幕坐标 | `GetCursorInfo` / `GetCursorPos` |
| `cursor_window_x` / `cursor_window_y` | 窗口内坐标 | `ScreenToClient`；失败时等于屏幕坐标 |
| `cursor_x` / `cursor_y` | 归一化位置 | 窗口坐标 ÷ 对应维度；窗口坐标比值接近 0（≤1e-6）时改用屏幕坐标 ÷ 维度；两者都接近 0 时才为 0。维度 ≤1e-6 时直接为 0（`src/DirectX12/Runtime/DX12VmFrameVariablesUtils.h:246`） |
| `cursor_showing` | 光标是否显示 | `CURSOR_SHOWING` 标志 |
| `cursor_visible` | 同 `cursor_showing` | 写入的是同一个值（`:111`） |
| `cursor_hotspot_x` / `cursor_hotspot_y` | 光标热点 | `GetIconInfo`，光标句柄不变时缓存复用（`:188`） |
| `effective_dpi` | 有效 DPI | `GetDpiForWindow` → 设备 DPI → 下限 96（`:261`） |

采集是**惰性**的：只有当某个表达式引用过帧变量时（`frameVariableReferenced` 标志），Present 才会做交换链查询、光标轮询与约 20 次全局写入（`DX12VmFrameVariablesUtils.h:39`）。配置重载会清除该标志（`src/common/MigotoCommandVmUtils.hpp:803`）。

因此**帧变量反映的是上一次帧边界采集到的值**，不是表达式求值瞬间的系统状态。

### 5.2 帧时间与兼容名

| 名称 | 返回 | 说明 |
| --- | --- | --- |
| `time` | 运行秒数 | 从 DLL 启动到最近一次主交换链进入正式 Present，以单调时钟计。 |
| `frame_time` | 帧间隔秒数 | 与上一次正式 Present 入口比较，第一次从 DLL 启动起算。回退的时钟间隔记为 0。 |
| `fps` | 平滑帧率 | 使用共享的 DX11 FPSCounter，以 0.1 为平滑系数；零间隔和长于一秒的间隔保持上次帧率。 |
| `frame_number` | 帧号 | 正式 Present 返回后递增。四个帧参数都按 float32 发布，同一条表达式读到同一份帧数据。 |
| `draw_type` | 调用类型 | DrawInstanced 为 3，DrawIndexedInstanced 为 4，直接 Dispatch 命中蒙皮匹配时为 8。无调用上下文时为 0。 |
| `thread_group_count_x`、`thread_group_count_y`、`thread_group_count_z` | 原派发的线程组数 | 在直接 Dispatch 命中的蒙皮覆盖里可读，post 赋值在原生派发返回后求值。 |
| `vertex_count`、`index_count`、`instance_count` | 当前直接绘制的数量 | 每实例的顶点数或索引数及实例数。 |
| `first_vertex`、`first_index`、`first_instance` | 当前直接绘制的起点 | 索引绘制的基顶点按 DX11 的 UINT 转 float32 规则取值。 |

以下名称有兼容写入但**恒为 0**，不能用于判断实际状态（`DX12VmFrameVariablesUtils.h:115`）：

| 名称 |
| --- |
| `hunting`、`frame_analysis`、`sli`、`stereo_active`、`stereo_available` |

### 5.3 不存在实时计数的名称

`draw_number` 和 `dispatch_number` 的逐调用计数仍待实现。

---

## 6. 资源

### 6.1 资源节与选项

`Resource` 前缀的段由 `MigotoResourceParser::ParseSections` 解析（`src/DirectX12/Config/DX12Config.h:1869`）：

| 键 | 含义 | 默认值 |
| --- | --- | --- |
| `type` | 资源类型说明 | 空 → **按缓冲处理**（`ResourceConfigLooksLikeBuffer`，`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1716`） |
| `filename` / `file` | 初始数据路径 | 空 → 只考虑内联 `data` |
| `format` | DXGI 格式 | 空 → `DXGI_FORMAT_UNKNOWN` |
| `data` | 内联十六进制数据 | 空 |
| `stride` | 顶点步长（字节） | 0 → 回退到游戏当前 IA 槽位步长 |
| `array` / `array_size` | 数组大小 | 0（**当前不参与资源创建**，见下） |
| `byte_width` / `width` | 字节数 | 0 → 取文件实际大小 |

未被识别的键被忽略。`byte_width` / `width` 在 DX12 是**字节数**配置，不是 DX11 的纹理尺寸覆盖语义。

`array` / `array_size` 只在解析器里被记录（`DX12Config.h:1906`），资源创建路径不读取它：缓冲固定以 `DepthOrArraySize = 1` 创建（`src/DirectX12/ModRuntime/DX12LoadedResources.cpp:135`）。

### 6.2 路径解析

`ResolveResourcePath`（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1673`）：

- 相对路径以**声明该资源的 INI 所在目录**为基准；读取失败时回退到配置根目录（`DX12FallbackPath` 日志）。
- 路径整体受 `MAX_PATH` 限制。文件名或拼接结果超长时**整条解析被拒绝**，写日志 `DX12ResourcePathRejected`（`filename_too_long` / `combined_path_too_long`），加载失败，**不会**退回内联数据。
- 内联 `data` 允许 `0x` 前缀，会去掉所有空白字符；长度必须为偶且非空，否则加载失败。

### 6.3 资源类型

| 类型判定 | 取值 | 加载路径 |
| --- | --- | --- |
| 缓冲类 | `type` 为空，或 `buffer` / `structuredbuffer` / `appendstructuredbuffer` / `consumestructuredbuffer` / `byteaddressbuffer` / `rwbuffer` / `rwstructuredbuffer` / `rwbyteaddressbuffer` | `BuildLoadedBufferResource`：D3D12 上传堆缓冲，`DXGI_FORMAT_UNKNOWN`，1 mip |
| 纹理类 | `type` 为 `texture` / `texture2d`，**或**文件扩展名是 `.dds` / `.png` / `.jpg` / `.jpeg` / `.bmp` / `.gif` / `.tif` / `.tiff` | DDS 走 `ParseDds`；其余走 WIC 解码（`src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h:100`） |

注意 `type` 为空时按缓冲处理，因此**纹理资源必须显式写 `type = texture` 或使用受支持的图片扩展名**。

缓冲加载的失败路径（`src/DirectX12/ModRuntime/DX12LoadedResources.cpp:81`，日志事件 `DX12ResourceLoad`）：

| 原因 | 日志 `reason` | 行为 |
| --- | --- | --- |
| 路径无法解析 | `path_rejected` | 加载失败，忽略 `data` |
| 读取失败 | `read_failed` | 失败，叠加 overlay warning |
| 数据为空 | `empty_resource` | 失败 |
| `CreateCommittedResource` 失败 | `create_failed` | 失败，叠加 overlay error |
| `Map` 失败 | `map_failed` | 失败，叠加 overlay error |

`byte_width` 大于文件实际大小时，剩余部分补零（`:163`）。

### 6.4 格式限制

**缓冲的 `format`**（`ParseDx12ResourceFormat`，`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:6`）只识别：

| 写法 | 结果 |
| --- | --- |
| 空 | `DXGI_FORMAT_UNKNOWN` |
| `r16_uint` / `r32_uint`（可带 `dxgi_format_` 前缀） | `DXGI_FORMAT_R16_UINT` / `DXGI_FORMAT_R32_UINT` |
| 纯十进制数字 | 直接当作 `DXGI_FORMAT` 枚举值 |
| 其它字符串 | `DXGI_FORMAT_UNKNOWN` |

解析出的格式主要影响**索引缓冲**的索引格式：替换索引缓冲时，格式为 `R16_UINT` / `R32_UINT` 则采用之，否则沿用游戏当前索引视图格式，再否则回退 `R32_UINT`（`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:380`）。因此 `format = r16_uint` 应当与实际索引位宽一致。

**纹理的格式由文件内容决定**，`format` 键不参与纹理格式选择。DDS 解析（`ParseDds`，`src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h:325`）的限制：

| 限制 | 失败 `reason` |
| --- | --- |
| 魔数必须是 `DDS `，头长 124，像素格式长 32，宽高非零 | `bad_magic` / `bad_header` / `too_small` |
| **不支持 CubeMap**（`caps2 & 0x200`），**不支持 3D 纹理**（`depth > 1`） | `unsupported_dimension` |
| DX10 头要求 `resourceDimension == 3`（TEXTURE2D）且 `arraySize == 1` | `unsupported_dx10_dimension` |
| 格式必须是已知格式（BC1~BC7、DXT1/3/5、ATI1/2、BC4/BC5、若干 fourCC、RGB 位掩码 32/16 位、Luminance/Alpha） | `unsupported_format` |
| 格式的位宽必须已知 | `surface_info_failed` |
| mip 链数据不得被截断 | `truncated_data` |

DX11 的 `mips`、`color_space`、`bind_flags`、`original_dhash` 等字段在 DX12 没有对应实现。

### 6.5 资源与绘制绑定

资源的实际接入点：

- **索引缓冲** `ib = ResourceX`：替换索引缓冲视图并重算索引内容 Hash（`DX12CommandListExecutor.h:370`）。索引格式按 §6.4 决定。
- **顶点缓冲** `vbN = ResourceX`：替换顶点缓冲视图；`stride` 缺省时沿用游戏当前该槽位步长（`:413`）。槽位号 ≥ 32 的绑定被忽略（`:411`）。
- **SRV / CBV / UAV** `ps-tN` 等：按 §2.6 解析当前描述符后替换。

顶点缓冲与索引缓冲替换同时会更新该次绘制的匹配 Hash，因此 `match_*` 条件与替换后的资源保持一致。

### 6.6 引用、别名与捕获

见 §2.9。要点回顾：

- `ref ResourceY` 是**名字级别名**，不复制任何数据。
- `ref ps-tN` 是**活动引用捕获**，不是快照。
- 别名链最多 16 跳，成环时回到最初的查询名。
- 别名随配置重载清空。
- 捕获只支持 2D 纹理 SRV。

### 6.7 不支持的资源操作

当前 DX12 **没有**实现以下 DX11 资源语义，出现在配置里会被静默忽略：

| 操作 | 状态 |
| --- | --- |
| `copy` / `copy_desc`（资源间 GPU 拷贝） | 未实现 |
| 资源 Region（`region`） | 未实现 |
| 动态槽（dynamic slot / `this` 的动态重定向表） | 未实现 |
| `store`（GPU→CPU 回读） | 未实现（DX11 语义见 [`ini-language-extensions/commands/README.md`](ini-language-extensions/commands/README.md)） |
| `dump` / `clear` / `analyse_options` | 未实现，导出走内置 F8/F9 |
| 池（Pool）与 `PoolX[...]` 访问 | 未实现 |
| `preset` / `exclude_preset` | 未实现 |
| 命令列表 `ref` 回调 | 未实现 |
| 自定义元素的 `fill` / `front` / `alpha` / `sample_mask` | 内部模型有字段，但键分类器不把它们送入状态路径，写入即被静默丢弃（见 §2.10） |

---

## 7. 重载与同步

### 7.1 配置重载

F10 触发 `DX12ModRuntimeReload`（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1625`）：

1. **先保存**持久变量（`DX12UserConfigSave::Save("reload")`），使重载读到的文件里已经包含最新值（DX11 的 `ReloadConfig` 同序）。
2. 重新解析配置、重建命令列表 / 覆盖 / 资源映射。
3. 重置自动保存计时器（`ResetAutosaveTimer`）。
4. 结果通过 overlay 状态提示：成功 / 有警告 / 有错误（`"F10 DX12 mod config reloaded with N error(s)"` 等）。

重载**不替换正在运行的 DLL**；DLL 与注入方式的变更需要重启游戏。

启动阶段的首个加载是延迟执行的：只有在 Present 计数 > 2 且距请求已超过 `kDeferredInitialModLoadDelayMs`（2000 ms）时才真正加载（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:277`）。

重载会清空全局变量表与帧变量惰性采集标志（`src/common/MigotoCommandVmUtils.hpp:803`），并重建持久变量登记表。

### 7.2 持久变量与自动保存

| 项 | 值 | 源码 |
| --- | --- | --- |
| 保存间隔 | 默认 **60 秒**；负值禁用周期写入 | `src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h:2488` |
| 首次写入时机 | 加载后第一次变化**几乎立即**写入（计时器初值为 0） | `:2558`（`AutosaveDue`） |
| 写入触发 | 只有持久变量的**值真的发生变化**才置脏并写盘 | `src/common/MigotoCommandVmUtils.hpp:828` |
| 写入时机 | 自动保存（帧边界）、重载前、DLL 退出时 | `DX12ModRuntimeLoad.cpp:1641`、`:1668` |
| 文件格式 | `; AUTOMATICALLY GENERATED FILE - DO NOT EDIT` + `[Constants]` + `$名 = 值`（`%.9g`） | `src/common/MigotoCommandVmUtils.hpp:686` |
| 路径 | `[Include] user_config`，默认 `d3dx_user.ini` | `src/DirectX12/Config/DX12ConfigLoader.h:369` |

写盘失败（打开 / 写 / flush / close 任一步）会写日志 `DX12UserConfig` / `save_failed` 并**保持脏标记**，下一个周期重试；不会因失败而截断文件。

写入的名字是**折叠命名空间后的扁平名**：`$\TestMod\panel_x` 写进文件的是 `$panel_x`（`src/DirectX12/Config/DX12ConstantsVmUtils.h:94`），这样重新读入后仍落到同一个变量槽位。

用户配置文件最后加载，因此其中的持久值**覆盖** Mod INI 里的默认值（`src/DirectX12/Config/DX12ConstantsVmUtils.h:25`）。

### 7.3 纹理上传的同步

DDS 纹理的像素上传走一条**私有队列 + 围栏**通道（`src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h:457`）：

- 上传命令在私有队列上执行，不进入游戏的命令列表（避免在录制中间插入 `CopyTextureRegion` 与屏障）。
- 游戏的队列在下次 `ExecuteCommandLists` 时**在 GPU 侧等待**上传围栏，不阻塞 CPU。
- 上传槽位是 4 环（`kUploadChannelSlots = 4`），每个槽位在围栏值完成后才复用。
- 未完成的上传由 `DX12UploadChannelWaitOnQueue`（`src/DirectX12/ModRuntime/DX12LoadedResources.cpp:16`）在队列侧等待。

### 7.4 并发保护

配置与资源表由 `gModLock`（SRWLOCK）保护：

- 只读快路径用**共享**锁（每次绘制的资源可用性检查走这条路），避免与录制线程争用独占锁（`src/DirectX12/ModRuntime/DX12LoadedResources.cpp:228`）。
- 加载 / 写入用**独占**锁；同一资源并发加载时后到者会重新检查并复用已加载结果（`:237`）。
- 资源对象通过延迟销毁路径释放，保证在途命令列表里的描述符仍然有效。

---

## 8. 与 DX11 的差异速查

| 主题 | DX11 | DX12 |
| --- | --- | --- |
| 表达式数值 | float32 算术、比较、位操作、数学函数、`**`、`===`、二进制字面量 | 数字解析、算术、位运算、移位和一元数学函数与 DX11 共享实现。`===`、`!==` 比较 float32 位模式 |
| 逻辑求值 | — | `&&` / `\|\|` **两侧都求值**（不短路） |
| 除零 | — | 结果为 0，不报错 |
| `global` 前缀 | 显式写全局变量 | 与 `$x` 同路径；作用域链上有同名 local 时写 local |
| 递归深度 | 256 层 | `depth > 16` 即停止（`DX12CommandListExecutor.h:1417`） |
| 自递归 | — | 同名列表已在调用栈上时跳过该次调用（`:1744`，`EnterCommandList`） |
| 帧计数器 | `frame_number`、`draw_number`、`dispatch_number` | 主交换链帧号已接入，逐绘制与逐派发计数待实现 |
| `store` / Region / 动态槽 / 池 | 支持 | 未实现 |
| CubeMap / 3D DDS | 支持 | 不支持（`unsupported_dimension`） |
| 资源 `array` | 数组纹理 | 只被解析，不参与资源创建 |
| `format` | 纹理格式字符串 | 只识别 `r16_uint` / `r32_uint` / 数字枚举 |

---

## 9. 示例

> 以下示例用于说明语法与调用链，**未在游戏中实测**。

### 9.1 常量、分支与每帧命令

```ini
[Constants]
global $enabled = 1
global persist $variant = 0

[CommandListNext]
local $next = ($variant + 1) % 3
$variant = $next

[Present]
if $enabled
    run = CommandListNext
endif
```

- `$variant` 是持久变量，会写进 `d3dx_user.ini`（写出的名字是 `$variant`）。
- `local $next` 只在 `CommandListNext` 这一层作用域内存在。
- `[Present]` 每帧执行一次。

### 9.2 延迟恢复着色器常量

```ini
[Constants]
global $saved_x = 0

[CommandListTemporary]
$saved_x = x87
x87 = 1
post x87 = $saved_x
```

`post x87 = $saved_x` 只在 post 阶段生效：先记下旧值，改写常量，在 post 阶段把旧值写回。注意表达式在**写入时刻**求值，所以必须在改写前先保存。

**前提**：该列表必须在 post 阶段被运行，例如由覆盖段用 `post = CommandListTemporary` 触发：

```ini
[TextureOverrideBody]
hash = 12345678
post = CommandListTemporary
```

在 pre / main 阶段调用该列表时，`post x87 = $saved_x` 会被**静默丢弃**（`PostVariableAssignment` 只在 `includePost` 为真时排队，`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1683`），既不生效也没有提示。

### 9.3 按分支替换纹理

```ini
[ResourceVariantA]
filename = variant_a.dds

[ResourceVariantB]
filename = variant_b.dds

[ResourceSelected]

[CommandListSelect]
if $variant == 0
    ResourceSelected = ref ResourceVariantA
else
    ResourceSelected = ref ResourceVariantB
endif
```

别名在命令列表执行时注册，因此别名目标由分支决定；别名表在配置重载时清空。

### 9.4 捕获当前绑定的纹理

```ini
[ResourceCaptured]

[CommandListCapture]
ResourceCaptured = ref ps-t0
```

只有在能解析出当前图形 SRV 的绘制上下文中才生效；捕获的是活动引用而非内容快照。只支持 2D 纹理。

### 9.5 模型替换片段

```ini
[ResourceIndices]
type = Buffer
format = R32_UINT
filename = mesh.ib

[ResourceVertices]
type = Buffer
stride = 32
filename = mesh.buf

[TextureOverrideBody]
hash = 12345678
match_index_count = 300
handling = skip
ib = ResourceIndices
vb0 = ResourceVertices
drawindexed = 300, 0, 0
```

- `format = R32_UINT` 必须与索引位宽一致，否则替换后的索引视图格式与实际数据不符。
- `stride = 32` 必须与顶点布局一致。
- `drawindexed = 300, 0, 0` 为索引数、起始索引、基顶点。
- `handling = skip` 跳过被拦截的原始绘制。

### 9.6 计算派发

```ini
[CommandListCompute]
dispatch = 64, 1, 1
```

`X/Y/Z` 任一为 0 时该派发会被跳过。派发在替换流的派发阶段执行，沿用原绘制的实例语义不适用（派发没有实例概念）。

---

## 10. 源码证据索引

| 主题 | 文件 | 位置 |
| --- | --- | --- |
| 动作种类枚举 | `src/DirectX12/Config/DX12Config.h` | 453 |
| 动作解析（键名与顺序） | `src/DirectX12/Config/DX12Config.h` | 815 |
| 绑定目标解析 | `src/DirectX12/Config/DX12Config.h` | 647 |
| 绘制参数解析 | `src/DirectX12/Config/DX12Config.h` | 750 |
| `run` / `pre` / `post` 链接解析 | `src/DirectX12/Config/DX12Config.h` | 944 |
| 资源节解析 | `src/DirectX12/Config/DX12Config.h` | 1869 |
| TextureOverride 解析（`this`、分支深度、`null`） | `src/DirectX12/Config/DX12Config.h` | 1497 |
| 键名/前缀判定工具 | `src/common/MigotoIniCommandListUtils.hpp` | 全文 177 行 |
| 渲染状态键集合（`fill`/`front`/`alpha`/`sample_mask` 不可达） | `src/common/MigotoIniCommandListUtils.hpp` | 170 |
| TextureOverride / ShaderOverride 匹配键 | `src/DirectX12/Config/DX12Config.h` | 1497、1128 |
| 表达式求值器 | `src/common/MigotoCommandVmUtils.hpp` | 72 |
| 变量作用域与延迟写 | `src/common/MigotoCommandVmUtils.hpp` | 300 |
| 持久变量写盘 | `src/common/MigotoCommandVmUtils.hpp` | 686 |
| `[Constants]` 回放与持久登记 | `src/DirectX12/Config/DX12ConstantsVmUtils.h` | 21 |
| 命令执行器（动作流、深度、别名、捕获） | `src/DirectX12/ModRuntime/DX12CommandListExecutor.h` | 49 |
| 递归深度与重入保护 | `src/DirectX12/ModRuntime/DX12CommandListExecutor.h` | 1417、1744 |
| 绘制/派发回放语义 | `src/DirectX12/Hooks/DX12IaReplacement.h` | 118 |
| 资源别名表 | `src/DirectX12/ModRuntime/DX12ResourceAliasStore.h` | 33 |
| 纹理捕获 | `src/DirectX12/ModRuntime/DX12DrawTextureCaptureUtils.h` | 27 |
| 缓冲资源加载与失败路径 | `src/DirectX12/ModRuntime/DX12LoadedResources.cpp` | 81 |
| 资源路径解析 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` | 1673 |
| 资源类型判定 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` | 1716 |
| 资源格式解析 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` | 6 |
| DDS 解析与格式限制 | `src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h` | 325 |
| 上传通道与围栏 | `src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h` | 457 |
| 自定义着色器元素 | `src/DirectX12/ModRuntime/DX12CustomShaderElement.h` | 105 |
| IniParams 打包 | `src/DirectX12/ModRuntime/DX12CustomShaderRegisters.h` | 22 |
| 帧变量采集与写入 | `src/DirectX12/Runtime/DX12VmFrameVariablesUtils.h` | 39 |
| 重载流程 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` | 1625 |
| 自动保存策略 | `src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h` | 2483 |

---

## 11. 待确认事项

已由源码判定、**不需实机确认**的事项（列出以免被误列为未知）：

- **`match_uav_bytes`**：数值被丢弃，只保留存在标记；该标记只影响段的执行路径，不参与筛选（§2.11）。
- **`fill` / `front` / `alpha` / `sample_mask`**：当前不可配置，写入即被静默丢弃（§2.10）。
- **`post` 的目标集合**：只接受 `$变量` 与 `xNN` 常量，其它目标（如 `post ps-t0`）被静默丢弃（§3.3）。

以下为写文档时**无法仅凭源码判定**、需要实机或运行日志确认的点：

1. **`checktextureoverride` 的实际命中面**：源码按绑定目标解析并查找覆盖，实际能命中哪些目标取决于游戏根签名与描述符跟踪结果，未实测。
2. **描述符绑定替换的槽位映射**：目标槽位（`ps-tN` 等）与游戏根签名寄存器槽的对应关系由描述符跟踪得出，替换是否落到预期寄存器需用帧分析确认，参见[计算输出替换](../usage/dx12/texture-overrides.md)。
3. **自定义元素的多绘制函数体**：源码注释说明会退化为默认 4 顶点 strip（`DX12CommandListExecutor.h:1464`），实际表现需实机观察。
4. **上传通道与游戏队列的等待时序**：源码为 GPU 侧围栏等待，跨队列的实际表现需在游戏中观察。
