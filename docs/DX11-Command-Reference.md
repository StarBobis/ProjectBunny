# DX11 命令参考

本文档面向 ProjectBunny 的 **DX11**（`d3d11.dll`）使用者、Mod 作者与维护者，汇总 DX11 INI 语言里可用的命令、绘制/分派调用、管线阶段目标、运行时变量、表达式、`Key`/`Preset`/输入绑定、持久化与回调行为。

范围与约束：

- **仅适用于 DX11。** DX12 有独立的接口与实现标准，本文档中的语法、节名与限制不适用于 DX12，也不要把 DX12 的写法套用到 DX11。
- 内容对照当前 DX11 源码的**注册 → 解析 → 消费**路径整理，正文中的行为均以 `src/DirectX11/` 下的实现为准。
- 本文是**参考手册**，不是已验证的教程。文中示例用于说明语法，未经完整游戏实测；上游同步与实测结论见 [DX11 上游同步记录](./DX11-Upstream-Sync.md)。
- 与既有扩展文档重复的部分只给出要点并链接过去，避免两处描述漂移。

配套阅读：

- [INI 语言扩展总览](./ini-language-extensions/README.md)
- [命令](./ini-language-extensions/commands/README.md)、[命令列表](./ini-language-extensions/command-lists/README.md)
- [变量](./ini-language-extensions/variables/README.md)、[表达式](./ini-language-extensions/expressions/README.md)
- [动态槽与槽范围](./ini-language-extensions/resources/slot-indexing.md)、[池](./ini-language-extensions/pools/README.md)
- [输入控制](./ini-language-extensions/input/README.md)、[回调示例](./ini-language-extensions/examples/command-list-callbacks.md)
- 面向终端用户的简明说明：[命令列表与执行时机](../usage/dx11/command-lists.md)、[绘制与 Dispatch](../usage/dx11/draw.md)、[按键与变量](../usage/dx11/keys.md)、[运行时参数](../usage/dx11/runtime-values.md)

---

## 1. INI 解析模型

理解这一节有助于判断“某行为什么没生效”。

### 1.1 节类型

DX11 在解析期把 INI 分为两类（`src/DirectX11/IniHandler.cpp:96-133`）：

**命令列表节**（`CommandListSections`）：节内容被解析为命令列表，允许重复键（命令列表里同名键是合法的多次调用）。

| 节前缀 | 精确节名 |
| --- | --- |
| `TextureOverride` | — |
| `CommandList` | — |
| `ShaderOverride` | — |
| `CustomShader` | — |
| `ShaderRegex` | — |
| `BuiltInCommandList` | — |
| `BuiltInCustomShader` | — |
| — | `Constants` |
| — | `Present` |
| — | `ClearRenderTargetView` |
| — | `ClearDepthStencilView` |
| — | `ClearUnorderedAccessViewUint` |
| — | `ClearUnorderedAccessViewFloat` |

**普通节**（`RegularSections`）：`Pool*`、`Resource*`、`Key*`、`Include*`、`Preset*`（前缀）以及 `Hunting`、`Logging`、`System`、`Input`、`Device`、`Rendering`、`Loader`、`Profile`、`Stereo`、`ConvergenceMap`（精确节名）。

### 1.2 大小写

- 命令列表内的**键、值、原始行在解析前一律转为小写**（`IniHandler.cpp:2645-2647`）。因此 `DrawIndexed`、`drawindexed`、`DRAWINDEXED` 等价。
- 命令列表外的节用不区分大小写的映射保存（`WStringInsensitiveHash` / `WStringInsensitiveEquality`），节名与键名同样不区分大小写。
- **变量名统一小写**：`[Constants]` 中的全局变量名在声明时被逐字符小写化（`IniHandler.cpp:2778`），命令列表里的 `$var` 引用也按小写查找。引用时不要依赖原始大小写。
- `Preset`/`Key` 的**枚举型取值**（如 `type`、`transition_type`）通过不区分大小写的枚举表匹配（`lookup_enum_val` 使用 `_autoicmp`，`src/common/util.h:239`），因此 `LINEAR` 与 `linear` 等价；`ParsePreset` 在查预设表前还会自行把节名小写。

### 1.3 命名空间

`namespace = <名字>`（节前的前导行）给同一 INI 文件里的节加命名空间前缀。跨命名空间引用有独立语法（`Namespace\SectionName`），详见 [ini-language-extensions/SYNC-NOTES.md](./ini-language-extensions/SYNC-NOTES.md)。命名空间**不改变**输入开关对 Mod 的分类（依据是实际递归加载的 INI 路径）。

### 1.4 键值切分与原始行

每一行按**第一个** `=` 切成键和值：

```ini
if $variant == 0
```

切分结果是键 `if $variant `、值 `= 0`，而**原始行** `if $variant == 0` 会被完整保留。命令解析按如下顺序尝试（`IniHandler.cpp:2560-2585`）：

1. 变量赋值（`$var = ...` / `local $var = ...`）
2. 流程控制（`if` / `else if` / `elif` / `else` / `endif`）—— **只看原始行**
3. 一般命令（`run`、`draw`、`clear`、`store` 等）
4. IniParam 覆盖（`x` / `y2` / `z3` / `w4`）
5. 资源/池目标赋值（`ps-t0 = ...`）

五项都不匹配时，该行被记为 `Unrecognised entry` 并输出警告。

因为流程控制只看原始行，`if $a == 1` 里的 `==` 不会被误当作键值分隔。

### 1.5 没有 `=` 的行

只有命令列表节（以及 `ShaderRegex`、`Profile`）允许出现没有 `=` 的行（`IniHandler.cpp:189-193`）。这正是 `if` / `else` / `endif` 能写成独立行的原因；在其它节里写这类行会得到 `Malformed line` 警告。

### 1.6 注释

分号 `;` **只能**作为整行注释：它是该行第一个非空白字符时整行被忽略。行内分号不是注释——源码明确保留了这种行为，因为分号可能是合法语法的一部分（`IniHandler.cpp:663-674`）。

因此 `ps-t0 = ResourceTexture ; 注释` 会被解析成“源目标 `resourcetexture` + 一个无法识别的选项 `;`”，整行被拒绝。写注释请单独占一行。

---

## 2. `pre` / `post` 阶段

每个命令列表节都有一份**前置（pre）**列表和一份**后置（post）**列表。键前缀决定命令进入哪一份（`IniHandler.cpp:2673-2682`）：

| 写法 | 归属 |
| --- | --- |
| `post <命令>` | 只进入后置列表 |
| `pre <命令>` | 只进入前置列表 |
| 无前缀 | 按命令自身的默认归属（见下） |

默认归属由 `AddCommandToList` 决定（`src/DirectX11/CommandList.cpp:453-493`）：

- 命令给出“合理默认列表”时只进入那一份（例如 `handling = skip` 只进前置列表，因为跳过原始绘制必须在原始 draw 之前执行；`special = ...` 也只进前置列表）。
- 没有默认列表时**同时进入前置与后置列表**（例如 `handling = abort`、`checktextureoverride`，源码注释说明这是为了让被调用方的前置与后置列表都被触发）。
- **显式前缀优先于默认归属**：只要写了 `post ` / `pre `，命令就只进入指定的那一份列表，默认归属被忽略。

变量赋值、IniParam 覆盖与资源槽赋值不走 `AddCommandToList`，它们的列表完全由前缀决定（无前缀时进入前置列表，`post ` 进入后置列表）。

`post` 不是“下一行之后”，而是被拦截操作的**后置阶段**。源码注释说明了为什么两种阶段必须分开：跳过原始绘制要在原始 draw **之前**执行，因此 `handling = skip` 只在前置列表生效；而 `handling = abort` 需要在绘制前后都能中止，所以默认同时进入两份列表。各节中前后置阶段相对于宿主操作的时机，见 [usage/dx11/command-lists.md](../usage/dx11/command-lists.md)。

> **限制：** 流程控制（`if` / `else` / `endif`）**不能**加 `post ` / `pre ` 前缀。带前缀的行会被当作显式列表行，流程控制分支被跳过，最终报 `Unrecognised entry`。

---

## 3. 命令一览

下表是 DX11 命令列表中可用的全部命令键。命令键在命令列表内大小写不敏感。

「归属」列的含义：

- **前置 + 后置**：不加前缀时命令同时进入前置与后置列表；写 `post ` / `pre ` 前缀则只进入指定的那一份。
- **前置**：不加前缀时只进入前置列表；写 `post ` 前缀可改到后置列表（`run = restoreoriginaltextures` 相反，默认后置）。

| 命令 | 归属 | 说明 |
| --- | --- | --- |
| `if` / `else if` / `elif` / `else` / `endif` | 前置 + 后置 | 流程控制，见 §4 |
| `$var = <表达式>` | 前置 | 变量赋值，见 §5 |
| `local $var = <表达式>` | 前置 | 声明并赋值局部变量，见 §5 |
| `x` / `y2` / `z3` / `w4` = `<表达式>` | 前置 | IniParam 覆盖，见 §5 |
| `draw` / `drawauto` / `drawindexed` / `drawinstanced` / `drawindexedinstanced` / `dispatch` | 前置 | 绘制与分派，见 §6 |
| `drawinstancedindirect` / `drawindexedinstancedindirect` / `dispatchindirect` | 前置 | 间接绘制与分派，见 §6 |
| `ps-t0 = ...` 等 | 前置 | 管线槽赋值，见 §7、§8 |
| `run = <CommandList>` | 前置 + 后置 | 调用命令列表，见 §9 |
| `run = <CustomShader>` | 前置 | 运行自定义着色器，见 §9 |
| `run = snapshotoriginaltextures` | 前置 | 快照当前原始纹理，见 §9 |
| `run = restoreoriginaltextures` | 后置 | 恢复快照的原始纹理，见 §9 |
| `handling = skip` | 前置 | 跳过原始绘制，见 §9 |
| `handling = abort` | 前置 + 后置 | 中止当前命令执行路径，见 §9 |
| `preset = <Preset>` | 前置 | 触发预设，见 §9、§15 |
| `exclude_preset = <Preset>` | 前置 | 排除预设，见 §9、§15 |
| `checktextureoverride = <目标>` | 前置 + 后置 | 对目标执行匹配的 TextureOverride，见 §9 |
| `reset_per_frame_limits = <名称>` | 前置 | 重置每帧执行/复制上限，见 §9 |
| `clear = <目标> [选项] <数值...>` | 前置 | 清理渲染目标/UAV，见 §9 |
| `analyse_options = <选项...>` | 前置 | 修改帧分析选项，见 §10 |
| `dump = <选项...> <槽范围或池范围>` | 前置 | 导出资源，见 §10 |
| `store = <变量>, <源>, <偏移>` | 前置 | GPU 读回，见 §11 |
| `special = upscaling_switch_bb` | 前置 | 上采样时切换后备缓冲，见 §9 |
| `special = draw_3dmigoto_overlay` | 前置 | 绘制 3DMigoto 叠加层，见 §9 |
| `commandlist<名称> = ref <名称>` / `= null` | 前置 + 后置 | 命令列表代理，见 §18 |

---

## 4. 流程控制

实现见 `src/DirectX11/CommandList.cpp:9734-9880`。

```ini
[CommandListApply]
if $enabled
    $variant = 1
else if $quality == 2
    $variant = 2
elif $quality == 1
    $variant = 1
else
    $variant = 0
endif
```

| 语法 | 行为 |
| --- | --- |
| `if <表达式>` | 表达式为真时执行分支体 |
| `else if <表达式>` | 前面的分支都不成立时的嵌套条件 |
| `elif <表达式>` | `else if` 的别名 |
| `else` | 兜底分支 |
| `endif` | 结束条件块 |

规则与失败路径：

- 关键字后必须跟**一个半角空格**：`if` 匹配前缀 `"if "`（3 字符），`else if ` 为 8 字符，`elif ` 为 5 字符。`if\t$x` 不会被识别。
- 表达式为空（例如 `if` 后只有空格）时解析失败，该行按未识别处理。
- `if` / `endif` 必须配对。两个方向各有诊断：缺少 `endif` 的 `if` 在运行期输出 `Statement "if" missing "endif"`（`CommandList.cpp:9956`）；多出来的 `endif` 在解析期输出 `Statement "endif" missing "if"`（`CommandList.cpp:9845`）。作用域不平衡时另外输出 `Scope unbalanced`（`IniHandler.cpp:2707`）。
- `else` / `endif` 之后不允许再写内容；`else if` 的实现是“在 else 分支里再嵌一个 if”，所以 `else if` 链共用一个 `endif`。
- `if` 块内声明的局部变量**不会**泄漏到父作用域或兄弟作用域；父作用域的变量在 `if` 块内可见（作用域实现见 `CommandList.h:181-190`）。
- `else` / `else if` 会清空当前最内层作用域的局部变量声明（与 `else` 分支互斥的语义一致）。

> **不支持循环。** INI 语言没有 `while` / `for`。需要遍历请用[池范围](./ini-language-extensions/pools/README.md)或有界的命令调用。

---

## 5. 变量、常量与 IniParam

### 5.1 全局变量声明（`[Constants]`）

```ini
[Constants]
global $base = 1
global persist $saved = 0
global locked $fixed = 1.23
```

| 标志 | 作用 |
| --- | --- |
| `global` | 声明为全局变量（`[Constants]` 中不带 `global` 的行会作为命令列表行处理，而不是声明） |
| `persist` | 值写入用户配置文件 `d3dx_user.ini`，见 §17 |
| `locked` | 只读：不能作为赋值目标，见 [变量文档](./ini-language-extensions/variables/README.md) |

- 标志可以组合，顺序不限，解析器按前缀匹配提取标志（`VariableFlagNames`，`CommandList.h:151-157`）。
- 初始值是**可选**的表达式，在声明被解析时求值；表达式可引用同批次已声明的变量（`global $bar = $foo + 1`）。
- 变量名非法（不以 `$` 开头、含非法字符）时输出 `Illegal global variable name`；同名重复声明输出 `Redeclaration of`。
- `[Constants]` 的命令列表在**立即上下文创建与配置重载**时运行，因此可用于一次性初始化。

### 5.2 变量赋值

```ini
$variant = 1
$variant = $base + 1
local $tmp = FRAME_NUMBER
```

- 赋值的右值是完整表达式（支持 §13 的全部运算符与函数）。
- 目标必须是已声明的全局变量，或用 `local` 声明的局部变量。给不存在的变量赋值会失败并告警；如果该行位于用户配置文件命名空间，会被当作待保留的持久化赋值处理。
- `local $name = ...` 在当前命令列表作用域声明局部变量。同一作用域内重复声明会被拒绝；遮蔽父作用域的局部变量也会被拒绝；仅与同名全局变量冲突时只记一条提示（`Local "%ls" masks a global variable with the same name`），不影响运行（`CommandList.cpp:516-547`）。
- 局部变量的生命周期属于该命令列表实例；`if` 块内声明的变量在 `endif` 处离开作用域。

### 5.3 IniParam 覆盖

```ini
x = 0.3
y2 = rt_width / rt_height
```

- IniParam 名字格式为 `x` / `y` / `z` / `w` 加可选数字后缀（`x`、`x1`、`x2` …），解析规则见 `src/common/util.h:991`。
- 数字后缀本身按任意非负整数解析，只要小于 `INT_MAX` 就会被接受（`util.h:1012-1022`）。源码里的 `225` 上限写在 `#if MIGOTO_DX == 9` 分支内，**DX11 不受它限制**。
- 但**能被着色器读到**的只有索引小于 `[Rendering] ini_params` 预留寄存器数的那些：该值默认 `120`，必须小于 `128`（`D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT`），超出时输出 `Option ini_params=%i out of range` 并禁用（`IniHandler.cpp:5285-5290`）。也就是说在默认配置下 `x120`–`x127` 名字能解析通过，但着色器侧没有为它们预留寄存器。
- 赋值写入 `G->iniParams`，CPU 侧数组会按实际用到的最大索引自动扩容（超过 `256` 时输出 `NOTICE: %d requested IniParams exceeds the recommended %d`），随后上传到着色器可读的常量缓冲。
- IniParam 可在 `[ShaderOverride*]`、`[TextureOverride*]`、`[CustomShader*]`、`[Key*]`、`[Preset*]` 等节里赋值，用途不同（见 §14、§15）。

---

## 6. 绘制与分派

实现见 `src/DirectX11/CommandList.cpp:903-1010`。参数用逗号分隔，顺序与 D3D11 原生 API 一致。

| 命令 | 参数 | 说明 |
| --- | --- | --- |
| `draw` | `顶点数, 首顶点` | `Draw` |
| `drawauto` | 无 | 使用流输出计数 |
| `drawindexed` | `索引数, 首索引, 基础顶点` | `DrawIndexed` |
| `drawinstanced` | `每实例顶点数, 实例数, 首顶点, 首实例` | `DrawInstanced` |
| `drawindexedinstanced` | `每实例索引数, 实例数, 首索引, 基础顶点, 首实例` | `DrawIndexedInstanced` |
| `dispatch` | `X, Y, Z` 线程组数 | `Dispatch` |
| `drawinstancedindirect` | `参数缓冲目标` | `DrawInstancedIndirect` |
| `drawindexedinstancedindirect` | `参数缓冲目标` | `DrawIndexedInstancedIndirect` |
| `dispatchindirect` | `参数缓冲目标` | `DispatchIndirect` |

### 6.1 自动数量

```ini
draw = auto
drawindexed = auto
drawindexedinstanced = auto
```

| 写法 | 语义 |
| --- | --- |
| `draw = from_caller` | 复用调用者的绘制参数 |
| `draw = auto` | 从当前顶点缓冲推导顶点数 |
| `drawindexed = auto` | 从当前索引缓冲推导索引数 |
| `drawindexedinstanced = auto` | 索引实例绘制的自动数量路径 |

自动数量依赖当前绑定的缓冲与字节偏移：未绑定资源、偏移越界或剩余数据不足一个元素时会跳过这次自动绘制。`draw = auto` 使用顶点缓冲槽 0，该槽步长为 0 时无法推导数量；索引数按当前 R16/R32 格式计算，与顶点步长无关。源码注释明确指出这里**假设槽 0 已绑定且是逐顶点数据**（不是逐实例），不满足这个假设时应手工指定绘制数量。自动数量**不会**替你挑选正确的子网格或修正布局。

### 6.2 间接调用的限制

三个 `*indirect` 命令的参数是**一个**间接参数缓冲目标（不是数量）。目标可以是管线槽或自定义资源；若目标是自定义资源，其绑定标志必须允许 `D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS`，否则解析时告警并拒绝该命令，并提示显式加 `copy` 关键字。

---

## 7. 管线阶段与槽目标

所有资源相关命令的“目标 / 源”都用同一套槽语法（`ResourceCopyTarget::ParseTargetPipelineSlot`，`CommandList.cpp:9046-9150`）。

| 目标 | 含义 | 限制 |
| --- | --- | --- |
| `ib` | 索引缓冲 | — |
| `o0` … `o7` | 渲染目标 | 最多 8 个 |
| `vb0` … `vb31` | 顶点缓冲 | 最多 32 个 |
| `so0` … `so3` | 流输出 | 最多 4 个 |
| `od` | 深度/模板目标 | — |
| `bb` / `r_bb` / `f_bb` | 后备缓冲（真实 / 虚拟） | 仅可作为**源**；后备缓冲视图不允许缓存 |
| `this` | 当前被覆盖的原始资源 | 可作源；作目标仅限 `checktextureoverride` 与间接绘制/分派上下文 |
| `null` | 空 | 仅可作为**源** |
| `iniparams` | IniParams 常量缓冲 | 仅可作为**源** |
| `cursor_mask` / `cursor_color` | 光标掩码 / 颜色 | 仅可作为**源** |
| `<阶段>-t<槽>` | 着色器资源视图（SRV） | 阶段见下 |
| `<阶段>-u<槽>` | 无序访问视图（UAV） | **只允许 `ps` 与 `cs`** |
| `<阶段>-cb<槽>` | 常量缓冲（CB） | 阶段见下 |

阶段前缀（`<阶段>`）为单字符：`v`=vertex、`p`=pixel、`c`=compute、`g`=geometry、`d`=domain、`h`=hull。槽号上限分别是 SRV `128`、UAV `8`、CB `14`。

```ini
ps-t0 = ResourceTexture
vs-cb0 = ResourceConstants
cs-u0 = ResourceRWBuffer
```

注意：

- 槽名**不能包含空白**；解析器遇到空白直接判为语法错误。
- `this` 作为**源**表示当前被覆盖的原始资源；作为**目标**则只在 `checktextureoverride` 或间接绘制/分派上下文里有效，在普通命令列表里给它赋值会被运行期拒绝，并输出 `"this" target cannot be set outside of a checktextureoverride or indirect draw call context`（`CommandList.cpp:10720`）。
- `-u`（UAV）用于 `vs`/`gs` 等阶段时返回语法错误。
- 采样器槽 `*-s*` 在源码中尚未启用，不要使用。
- 目标可以带前缀 `@` / `#` / `$`，也可以带成员后缀（如 `->HashRegion(0, 16)`、`->Length`），详见 [资源文档](./ini-language-extensions/resources/README.md)。
- 池与池范围目标的写法见 [池文档](./ini-language-extensions/pools/README.md)。

### 7.1 槽索引与槽范围

槽名后面可以跟一对方括号，里面有两种写法（`CommandList.cpp:8985-9044`）：

| 写法 | 含义 | 支持的槽 |
| --- | --- | --- |
| `<槽>[<表达式>]` | 动态槽索引，例如 `ps-t[$slot]` | 全部槽 |
| `<槽>[<起始>:<结束>]` | 槽范围，例如 `ps-t[0:3]`，起止都是表达式 | 仅 `*-t*`、`*-u*`、`*-cb*` |

- 动态索引的表达式在运行期求值；范围写法会把目标标记为 `SLOT_RANGE`。
- 范围写法只对着色器资源、UAV 与常量缓冲开放；`o*` / `vb*` / `so*` 写范围会返回语法错误。
- 槽范围与池范围配对时两端都要写显式范围；单个资源可以广播到目标槽范围（见 §8）。

---

## 8. 资源绑定与复制

```ini
[CommandListBind]
; 复制内容
ps-t0 = ResourceTexture
; 引用（共享同一资源）
ps-t1 = ref ResourceTexture
; 解绑定
ps-t2 = null
```

值的第一个 token 是源目标，其后可以跟复制选项。下表是**受支持**的选项；完整表见 `ResourceCopyOptionNames`（`CommandList.h:1227-1247`）。上游表里还有一个 `resolve_msaa`，但上游注释明确说明它对缺少 `D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE` 的格式没有回退路径、**不应被使用**，因此这里不把它列为可用选项：

| 选项 | 作用 |
| --- | --- |
| `copy` | 复制内容（默认语义） |
| `ref` / `reference` | 引用同一资源，不复制 |
| `copy_desc` / `copy_description` | 同时复制资源描述 |
| `unless_null` | 源为空时不执行 |
| `mono` | 单色复制 |
| `set_viewport` | 复制后同步设置视口 |
| `no_view_cache` | 不缓存视图 |
| `raw` | 使用原始（Raw）视图 |

失败路径：

- 源不是合法目标、选项无法识别时整行被拒绝并输出 `Unrecognised entry`。
- 池目标只能与池目标互拷；`$pool[...]` 形式由池变量解析器接管，不走普通复制路径。
- 槽范围与池范围配对使用时，**两侧都必须写显式范围**，且两边的元素个数必须相同（起始索引可以不同）；写成 `ps-t[3:5] = ref PoolSaved`（源是整个池）会被拒绝。
- 单个自定义资源或池元素可以广播到整个目标槽范围，例如 `ps-t[0:3] = ref ResourceTexture`。

槽范围的完整规则见 [动态槽与槽范围](./ini-language-extensions/resources/slot-indexing.md)。

---

## 9. 调用与控制类命令

### `run`

```ini
run = CommandListApply
run = CustomShaderEffect
run = BuiltInCommandListFoo
run = snapshotoriginaltextures
```

| 值 | 行为 |
| --- | --- |
| 以 `commandlist` / `builtincommandlist` 开头 | 调用显式命令列表节（`[CommandList*]` / `[BuiltInCommandList*]`） |
| 以 `customshader` / `builtincustomshader` 开头 | 运行自定义着色器 |
| `snapshotoriginaltextures` | 快照当前绑定的原始纹理（仅前置列表） |
| `restoreoriginaltextures` | 恢复快照的原始纹理（仅后置列表） |

目标节不存在时命令被拒绝（找不到 `CommandList`/`CustomShader` 时输出告警）。命名空间解析顺序为“带命名空间 → 不带命名空间”，命令列表与自定义着色器各自查找。

### `handling`

```ini
handling = skip
handling = abort
```

- `skip`：跳过原始绘制，**只在前置列表**生效。
- `abort`：中止当前命令执行路径，前置与后置列表都会生效。
- 除这两个取值外的值既不匹配 `skip` 也不匹配 `abort`，该行最终按未识别处理并输出 `Unrecognised entry`。

### `preset` / `exclude_preset`

```ini
preset = PresetNightMode
exclude_preset = PresetNightMode
```

查找顺序共四种组合：带命名空间并加 `Preset` 前缀 → 带命名空间不加前缀 → 不带命名空间加前缀 → 不带命名空间不加前缀。因此 `preset = NightMode` 与 `preset = PresetNightMode` 都能命中 `[PresetNightMode]`。找不到时命令被拒绝。

### `checktextureoverride`

```ini
checktextureoverride = ps-t0
```

对指定槽上的资源执行匹配的 `[TextureOverride*]`。目标按“源”解析，可以是单个槽、槽范围（`ps-t[0:3]`）、池范围（`PoolSaved[0:2]`）或单个池元素（`PoolSaved[$index]`）；目标是池但不是范围时命令被拒绝，整个池没有可检查的资源。该命令同时进入前置与后置列表，因此被触发方的两份列表都会被执行。命中时 `this` 指向当前检查到的那个槽或池元素。

### `reset_per_frame_limits`

```ini
reset_per_frame_limits = ResourceTexture
reset_per_frame_limits = CustomShaderEffect
```

清除指定资源或着色器的每帧执行/复制计数。值按前缀区分种类：`resource*`、`customshader*`、`builtincustomshader*`；找不到对应节时命令被拒绝。

### `clear`

```ini
clear = cs-u0 0 0 0 0
clear = od depth 1
clear = od stencil 0
clear = o0 int 0x00000000
```

- 第一个 token 是目标；其后最多 4 个数值，缺省时用最后一个值补齐各通道。
- 数值写成 `0x...` 十六进制时按整数解释（用于超过 24 位有效精度的整数值）；按浮点解析成功时同时保留浮点与整数两种解释，实际使用哪种取决于目标类型。
- 关键字：`int` 强制整数清理，`depth` 只清深度，`stencil` 只清模板。
- 深度/模板目标的取值规则：第一个值作为深度、第二个作为模板；只写 `stencil` 时第一个值作为模板。
- **视图类型由实际视图决定，而不是由目标名推断**：清理时对拿到的视图做 `QueryInterface`，能查到渲染目标视图就按渲染目标清理，查到深度/模板视图就按深度/模板清理，查到无序访问视图就按 UAV 清理（`CommandList.cpp:12794-12858`）。
  - 渲染目标视图使用浮点四分量清理。
  - 深度/模板视图：既没写 `depth` 也没写 `stencil` 时同时清深度与模板；只写 `depth` 只清深度；只写 `stencil` 只清模板。
  - 无序访问视图：写了 `int` 关键字或用了十六进制字面量时按无符号整数清理；否则当该 UAV 格式不支持浮点清理时也退回整数清理；两种情况都不满足时按浮点清理。
- 失败路径：取不到资源时输出 `No resource to clear` 并跳过本次清理；取不到可用视图时输出 `No view and unable to create view to clear resource`。

### `special`

```ini
special = upscaling_switch_bb
special = draw_3dmigoto_overlay
```

分别用于上采样时切换后备缓冲、在自定义绘制位置绘制 3DMigoto 叠加层。两条都只在前置列表生效。

---

## 10. 帧分析与导出

```ini
[CommandListDump]
analyse_options = dump_rt dump_tex
dump = dump_cb PoolTextures[0:4]
dump = ps-t[0:3]
```

- `analyse_options = <选项...>`：设置帧分析的导出选项，选项名见 `FrameAnalysisOptionNames`（`src/DirectX11/globals.h:192-243`），包括 `dump_rt`、`dump_depth`、`dump_tex`、`dump_cb`、`dump_vb`、`dump_ib`、`jps`/`jpg`/`jpeg`/`dds`、`jps_dds`/`jpg_dds`/`jpeg_dds`、`buf`/`txt`、`desc`、`clear_rt`、`persist`、`mono`、`filename_reg`、`filename_handle`、`hold`、`dump_on_unmap`、`dump_on_update`、`deferred_ctx_immediate`、`deferred_ctx_accurate`、`share_dupes`、`symlink`，以及历史组合名 `dump_rt_jps`、`dump_rt_dds`、`dump_depth_jps`、`dump_depth_dds`、`dump_tex_jps`、`dump_tex_dds`、`dump_cb_txt`、`dump_vb_txt`、`dump_ib_txt`。该命令只在前置列表生效。
- 两个注意事项：`log` 仍被接受但已废弃（帧分析日志现在始终开启，源码把它标为 `DEPRECATED`）；源码在 `dump_depth_jps` 上标注了 `Doesn't work yet`，该组合目前不能正常工作。
- `dump = <选项...> <目标>`：导出资源。目标可以是**显式槽范围**（`ps-t[0:3]`）或**池物理范围**（`PoolSaved[0:2]`）；目标是池但不是范围时命令被拒绝，整个池没有可导出的资源。选项可以在目标之前或之后出现，无法识别的选项会输出 `Unknown option` 警告。导出会把选中的槽 / 物理元素序号追加到输出文件名里。
- 池范围始终按**物理 Ring 槽位**选择，不做键查找，也不改变键映射；池元素为 `null` 时不产生输出。

帧分析的用户侧说明见 [usage/dx11/frame-analysis.md](../usage/dx11/frame-analysis.md)。

---

## 11. `store`：GPU 读回

```ini
store = $out, ResourceFoo, 4
store = $PoolObjects[$object_id], vs-cb1, 4
```

语法为三个逗号分隔的参数：

1. **输出变量** —— INI 变量或池变量（`$PoolX[$i]`）。
2. **源目标** —— 管线槽或自定义资源。
3. **偏移表达式** —— 动态求值，指定要读取第几个 32 位值。

行为与限制（详见 [store 命令文档](./ini-language-extensions/commands/README.md)）：

- 偏移按**32 位值索引**解释；索引相对后备缓冲绝对位置，即使 SRV/CB 视图有非零偏移也一样。
- 有效索引越界或读回失败时写入 0（与上游一致）。
- 源缺失/为空、不是缓冲、设备不匹配、延迟上下文、索引非法时**不改变**输出。
- 输出变量被 `locked` 标记时拒绝写入。
- `store` 走 GPU → CPU 读回，代价高；只有确实需要 CPU 侧数值、且没有替代方案时才使用。

---

## 12. 运行时变量（表达式中的只读量）

表达式与条件里可以读取下列运行时量（`ParamOverrideTypeNames`，`src/DirectX11/CommandList.h:1609-1653`）：

| 分类 | 名称 |
| --- | --- |
| 时间 | `time`、`frame_time`、`fps` |
| 帧/调用计数 | `frame_number`、`draw_number`、`dispatch_number` |
| 绘制参数 | `vertex_count`、`index_count`、`instance_count`、`first_vertex`、`first_index`、`first_instance`、`draw_type`、`draw_has_override`、`indirect_offset` |
| 分派参数 | `thread_group_count_x`、`thread_group_count_y`、`thread_group_count_z` |
| 屏幕 | `rt_width`、`rt_height`、`res_width`、`res_height`、`window_width`、`window_height`、`scissor_left`、`scissor_top`、`scissor_right`、`scissor_bottom` |
| 光标 | `cursor_x`、`cursor_y`、`cursor_window_x`、`cursor_window_y`、`cursor_screen_x`、`cursor_screen_y`、`cursor_showing`、`cursor_hotspot_x`、`cursor_hotspot_y` |
| 设备/工具 | `effective_dpi`、`sli`、`stereo_active`、`stereo_available`、`hunting`、`frame_analysis` |

说明：

- `time` 是启动后的秒数（单调时钟）；`frame_time` 是上次测得的帧间隔（秒）；`fps` 是平滑帧率估计，初值可能为 0。
- `frame_number` 以 Present 为边界；`draw_number` / `dispatch_number` 分别是绘制与分派计数，首个为 1。被跳过的绘制仍然计数。
- `rt_width` / `rt_height` 是当前渲染目标尺寸，`res_width` / `res_height` 是配置所用分辨率，两者不是一回事。
- `stereo_active`、`stereo_available` 在 DX11 路径下恒为 0。
- 长时间运行后，浮点形式的帧号与时间会损失精度。

典型用法（每帧只执行一次）：

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

---

## 13. 表达式

表达式用于变量赋值、`if` 条件、IniParam 覆盖、部分数值型节选项。

### 13.1 运算符

算术 `+ - * / % //`、比较 `== != < > <= >=`、逻辑 `&& || !`、位运算 `~ & | ^`、位移 `<< >>`、幂 `**`，以及严格相等 `===` / `!==`。

要点（详见 [运算符文档](./ini-language-extensions/expressions/operators.md)）：

- 表达式操作数内部是浮点数。参与位运算/位移前会被隐式转换为**有符号整数**，小数部分被截断（`1.1 → 1`，`-1.9 → -1`）。
- 整数精度受 IEEE-754 单精度限制，`2^24` 以内的整数精确；超出后可能丢精度。单变量可靠容纳 24 个二进制标志（位 `0`–`23`）。
- 位运算/位移的优先级与 C++ 相同。
- `//` 是**向下取整除法**，语义为 `floor(lhs / rhs)`（`CommandList.cpp:5762`）。它**不是注释引导符**：本 INI 方言只有行首的 `;` 才是注释（见 §1.6），`//` 是会被求值的运算符。

### 13.2 函数

`countbits`、`sin`、`cos`、`tan`、`asin`、`acos`、`atan`、`abs`、`sign`、`ceil`、`floor`、`trunc`、`round`、`frac`、`sqrt`、`rsqrt`、`exp`、`exp2`、`log`、`log2`、`saturate`、`random`。

- 函数必须带显式括号调用。
- `random($max)`：`$max` 为正时结果落在 `[0, $max)`，为负时落在 `($max, 0]`；有限非零上界不会被返回；上界为 0 时返回 0；非有限上界返回 NaN。
- `random` 基于当前帧号、系统 tick 与调用计数器的哈希，同一帧内多次调用可能得到不同值，也可能碰撞，**不保证跨运行可复现**。
- `noise` 未实现，会被表达式解析器拒绝。

完整列表见 [函数文档](./ini-language-extensions/expressions/functions.md)。

### 13.3 字面量

二进制字面量、D3D11 绑定标志与 DXGI 格式字面量见 [字面量文档](./ini-language-extensions/expressions/literals.md)。

---

## 14. `Key` 绑定

`[Key*]` 节把按键绑定到变量/IniParam 覆盖或命令列表。

```ini
[KeyVariant]
key = F6
type = cycle
$variant = 0, 1
```

| 字段 | 含义 | 默认 |
| --- | --- | --- |
| `key` | 正向按键，可写多次（同名键被允许） | 无（与 `back` 都为空时告警 `Missing Key=`） |
| `back` | `cycle` 的反向键 | 无 |
| `type` | `activate` / `hold` / `toggle` / `cycle` | `activate` |
| `condition` | 触发条件（表达式） | 无 |
| `run` | 激活时调用的命令列表 | 无 |
| `delay` / `release_delay` | 按下 / 释放延时（毫秒） | `0` |
| `transition` / `release_transition` | 按下 / 释放过渡时长（毫秒） | `0` |
| `transition_type` / `release_transition_type` | `linear` / `cosine` | `linear` |
| `wrap` | `cycle` 是否首尾循环 | `true` |
| `smart` | `cycle` 是否按当前值重新同步 | `true` |

按键名规则：

- 虚拟键名（如 `F6`、`K`、`VK_NUMPAD1`），可用 `ctrl` / `alt` / `shift` 等修饰键前缀；`NO_ALT` 等 `NO_` 前缀表示“必须未按下”。
- `NO_MODIFIERS` 展开为 `NO_CTRL`、`NO_ALT`、`NO_SHIFT`、`NO_LWIN`、`NO_RWIN` 五个条件。
- 以 `no_` 开头的键名会被取反（例如 `no_F1` 表示 F1 未按下）。
- 手柄键的完整写法是 `XB` + 可选的控制器序号（`1`–`4`）+ `_` + 名称：
  - 按钮名：`DPAD_UP`、`DPAD_DOWN`、`DPAD_LEFT`、`DPAD_RIGHT`、`START`、`BACK`、`LEFT_THUMB`、`RIGHT_THUMB`、`LEFT_SHOULDER`、`RIGHT_SHOULDER`、`A`、`B`、`X`、`Y`、`GUIDE`。
  - 扳机：`LEFT_TRIGGER`、`RIGHT_TRIGGER`，可跟 `> N` 指定触发阈值（例如 `XB2_LEFT_TRIGGER > 128` 表示 2 号手柄左扳机值必须大于 128 才算按下；不写 `> N` 时使用 `XINPUT_GAMEPAD_TRIGGER_THRESHOLD`）。
  - **省略控制器序号表示任意手柄都匹配**（`XB_A` 等价于 `XB1_A`–`XB4_A` 中的任一）。`XB_GUIDE` 需要加载 xinput 1.3/1.4，加载不到时该按键不可用。
- 无法解析的按键绑定会输出 `WARNING: UNABLE TO PARSE KEY BINDING`，该绑定被跳过。

类型语义：

| `type` | 行为 |
| --- | --- |
| `activate`（默认） | 按下即应用一次 |
| `hold` | 按住时应用，松开时恢复原值 |
| `toggle` | 首次按下应用，再次按下恢复原值 |
| `cycle` | 每次按下切换到列表下一项 |

`cycle` 的取值按位置配对：所有 `$var` 与 IniParam 的逗号列表、`transition`、`condition`、`run` 等都按第几轮配对。`wrap = 0` 时到末尾停在最后一项；`back` 键反向移动。

`condition` 为假时不会激活（输出 `Skipping override activation: condition not met`）。`transition` 为 0 表示立即生效；`cosine` 过渡使用余弦缓动。

`locked` 变量不能被按键覆盖（输出 `Cannot override locked variable`）。

按键、预设与输入开关的完整行为见 [输入控制文档](./ini-language-extensions/input/README.md) 与 [usage/dx11/keys.md](../usage/dx11/keys.md)。

---

## 15. `Preset`

`[Preset*]` 节定义一组变量/IniParam 覆盖，供 `preset =` 触发。

```ini
[PresetNightMode]
x = 0.5
$brightness = 0.2
transition = 100
transition_type = linear
release_transition = 200
release_transition_type = cosine
condition = FRAME_NUMBER > 0
unique_triggers_required = 1
```

| 字段 | 含义 | 默认 |
| --- | --- | --- |
| `<ini param>` / `$var` | 要覆盖的值 | — |
| `transition` / `release_transition` | 过渡时长（毫秒） | `0` |
| `transition_type` / `release_transition_type` | `linear` / `cosine` | `linear` |
| `condition` | 生效条件（表达式） | 无 |
| `run` | 激活 / 释放时调用的命令列表 | 无 |
| `unique_triggers_required` | 本帧至少需要多少个不同触发源才生效 | `0` |

生命周期：

- 预设在**帧边界**结算：本帧被 `preset =` 触发则保持激活，未被触发则在 Present 时自动释放（`PresetOverride::Update`）。
- `exclude_preset =` 把本帧的预设标记为排除，激活与释放都以此为准。
- `condition` 为假时跳过激活。
- 激活时会保存被覆盖的原始值，释放时恢复；多个预设覆盖同一参数时用引用计数管理，最后一个释放者才真正恢复原值（`OverrideGlobalSave`）。
- 过渡期间保存的是**目标值**而不是中间值，避免快速按下/松开时把中间值写回。
- `locked` 变量不能被预设覆盖。

预设与按键的用户侧说明见 [usage/dx11/presets-input.md](../usage/dx11/presets-input.md)。

---

## 16. 输入开关

```ini
[Input]
input = 1
toggle_input = ctrl alt shift VK_END
input_disable_mode = mods
```

| 选项 | 取值 | 默认 |
| --- | --- | --- |
| `input` | 布尔 | 打开（缺失或非法时按打开处理并给出配置告警） |
| `toggle_input` | 按键绑定 | 无 |
| `input_disable_mode` | `mods` / `all` | `mods` |

| 运行状态 | 模式 | 递归加载的 Mod 按键 | 其他 XXMI 按键 | `toggle_input` |
| --- | --- | --- | --- | --- |
| 开启 | 任意 | 启用 | 启用 | 启用 |
| 关闭 | `mods` | 暂停 | 启用 | 启用 |
| 关闭 | `all` | 暂停 | 暂停 | 启用 |

- 只影响 DLL 自己的按键绑定，不影响游戏输入。
- 关闭输入时，正在 HOLD 的值**不会**立即恢复；恢复后重新采样释放事件。
- Mod 分类依据实际递归加载的 INI 路径，改 `namespace` 不改变分类。
- 旧的 `[System] disable_input` / `toggle_input` 应迁移到 `[Input]`。
- 其它内置按键：`[Device] toggle_full_screen`、`[Device] force_full_screen_on_key`；`[Hunting]` 下有 `reload_config`、`wipe_user_config`、`analyse_frame`、`toggle_hunting` 等（源码 `src/DirectX11/Hunting.cpp:2081-2164`）。

细节见 [输入控制文档](./ini-language-extensions/input/README.md)。

---

## 17. 持久化

```ini
[Constants]
global persist $variant = 0
```

- 带 `persist` 的全局变量（以及池变量模板上的 `persist`）会在值变化后写入用户配置文件 `d3dx_user.ini` 的 `[Constants]` 节。
- 文件内容形如 `name = value`（浮点按 `%.9g` 输出）。写入采用“临时文件 + 原子替换”，失败时保留原文件并标记为脏，不会报告成功。
- 自动保存间隔由 `[System] settings_auto_save_interval` 控制，单位秒，默认 `60`；负数表示关闭自动保存。
- `locked` 变量不写入持久化文件，也不从文件恢复；它们的值由声明本身初始化。
- `[System] clear_unknown_settings` 控制“未知持久化项”的清理，默认 `1`；关闭时未知项会被保留。清理需要一次宽限期，并且要求名称与值的位模式在下一次重载时不变。
- 持久化值归属于归一化后的用户文件路径；改变路径或成功擦除设置不会让旧路径的值复活。
- `[Hunting] wipe_user_config` 绑定的按键会擦除用户配置文件。

详细规则（含失败路径）见 [变量文档](./ini-language-extensions/variables/README.md) 与 [持久化校验记录](./DX11-Persistence-Validation.md)。

---

## 18. 回调与代理

### 18.1 命令列表代理

```ini
[Constants]
global $counter = 0

; 钩子必须先声明为一个空的 [CommandList*] 节
[CommandListCallback]

; 代理赋值要写在一个会被执行到的命令列表节里
[Present]
CommandListCallback = ref CommandListImplementation

[CommandListImplementation]
$counter = $counter + 1
```

```ini
; 解除代理（同样要求 [CommandListCallback] 已经存在）
[Present]
CommandListCallback = null
```

- `commandlist<名称> = ref <名称>`（或 `= reference`）把目标命令列表**按引用**代理给另一个命令列表；代理**不复制**命令。
- **键名指定的是目标（destination）**：键 `commandlist<名称>` 必须对应一个**已经声明**的 `[CommandList<名称>]` 节——目标查找只覆盖 `explicitCommandListSections`，也就是 `[CommandList*]` 与 `[BuiltInCommandList*]` 节。目标节不存在时整行被拒绝并输出 `Unrecognised entry`；`= null` 也一样要求目标节存在。
- 赋值命令本身会被登记进**所在节**的前置与后置列表（`CommandList.cpp:838-839`），所以要把它写进一个真的会执行到的节里（例如 `[Present]`），而不是随便哪个节。
- 赋值捕获的是源当时的**代理根**，中间的别名变化不会自动重定向已有绑定。
- 成环赋值被忽略（变成 no-op），运行期解析会在链上安全停止。
- 跨命名空间只能覆盖**空**的回调（前置与后置都为空）；覆盖其它命名空间已有的非空实现会被拒绝并告警。解除代理（`= null`）始终允许。
- 配置重载会使旧引用失效，而不是保留指向旧命令列表的指针。
- 命令列表嵌套执行上限为 **256**（原 3DMigoto 为 64）。

详见 [命令列表文档](./ini-language-extensions/command-lists/README.md) 与 [回调示例](./ini-language-extensions/examples/command-list-callbacks.md)。

### 18.2 其它回调点

| 回调点 | 说明 |
| --- | --- |
| `run = <CommandList>` | 命令列表调用；被调用方的前置与后置列表按其自身阶段执行 |
| `checktextureoverride = <目标>` | 触发匹配到的 `[TextureOverride*]` |
| `[Key*] run = <CommandList>` | 按键激活时调用（`Override::Activate` 里执行激活列表） |
| `[Preset*] run = <CommandList>` | 预设激活 / 释放时调用激活列表与释放列表 |
| `special = draw_3dmigoto_overlay` | 绘制叠加层 |

> `[Key*]` / `[Preset*]` 里的 `run` 只解析为显式命令列表调用（`ParseRunExplicitCommandList`），不是完整命令列表语法。

---

## 19. 限制与失败路径速查

| 限制 | 说明 |
| --- | --- |
| 无循环语法 | INI 只有条件分支，没有 `while` / `for` |
| 嵌套深度 256 | 命令列表嵌套执行上限 |
| 流程控制不能带 `post ` / `pre ` | 带前缀后被当作显式列表行，报 `Unrecognised entry` |
| `if` / `endif` 必须配对 | 缺 `endif` 运行期告警 `Statement "if" missing "endif"`；多 `endif` 解析期告警 `Statement "endif" missing "if"` |
| 关键字后必须是半角空格 | `if ` / `else if ` / `elif ` |
| 命令列表内键值不区分大小写 | 键、值、原始行全部小写化 |
| 行内分号不是注释 | `;` 只有在行首才是注释；行内 `;` 会被当作值的一部分导致该行被拒绝 |
| 变量名全部小写 | 声明与引用统一小写 |
| 表达式内部是浮点 | 位运算前截断为整数；`2^24` 以上整数可能丢精度 |
| `*-u*` 只对 `ps`/`cs` 有效 | 其它阶段返回语法错误 |
| `this` 作目标受限 | 只在 `checktextureoverride` 或间接绘制/分派上下文中有效，否则运行期拒绝 |
| `commandlist<名称>` 的目标节必须存在 | 目标不是已声明的 `[CommandList*]` / `[BuiltInCommandList*]` 节时整行被拒绝 |
| `//` 不是注释 | 它是向下取整除法 `floor(lhs / rhs)`；注释只有行首 `;` |
| 槽范围只对 `*-t*`/`*-u*`/`*-cb*` 开放 | `o*`/`vb*`/`so*` 写范围返回语法错误 |
| 槽范围与池范围必须成对 | 两边都要写显式范围且元素个数相同；整个池不能作为槽范围的一侧源 |
| `dump` 目标是池时必须是范围 | 整个池没有可导出的资源；槽范围同样可以作为 `dump` 目标 |
| `store` 代价高 | 走 GPU → CPU 读回；源缺失/非法时不改变输出 |
| 锁定变量只读 | 赋值、`store`、按键、预设都不能写入 |
| 未识别行只告警 | 命令列表内未匹配的行输出 `Unrecognised entry`，其余行继续执行 |
| 持久化写入是原子的 | 临时文件 + 替换；失败保留原文件并标记脏状态 |

---

## 20. 与其他文档的关系

- 本文覆盖**基础命令与限制**；池、资源、表达式字面量、输入布局、日志等扩展特性在 [ini-language-extensions](./ini-language-extensions/README.md) 中有专门章节，本文按需链接而不重复。
- 本文所有行为描述以 DX11 源码为准；上游同步状态、逐提交状态与实测结论见 [DX11-Upstream-Sync.md](./DX11-Upstream-Sync.md) 与 [ini-language-extensions/SYNC-NOTES.md](./ini-language-extensions/SYNC-NOTES.md)。
- DX12 有自己的接口与标准，**不要**把 DX12 的语法、节名或限制套用到本文描述的 DX11 语言上。

## 21. 未验证事项

- 本文中的所有 INI 片段都是**语法示例**，未作为完整 Mod 在游戏中运行过。
- 未进行游戏内功能验证：按键绑定、预设过渡、`store` 读回、帧分析导出、`checktextureoverride` 触发等行为的实际游戏表现仍待实测。
- 未做性能基准；文中不含任何帧率或耗时结论。
- 上游 XXMI-Libs-Package 的对应文档在本地已有适配副本，但本文未逐条与上游原文做差异比对。
