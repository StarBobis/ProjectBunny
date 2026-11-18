# DX11 Mod 编写指南

面向对象：DX11 版 DLL 的使用者、Mod 作者、Mod 维护者。

适用范围：DX11 版（`d3d11.dll`）。DX12 版 Mod 的节名与语义不同，不能直接套用。

本文按当前源码的**注册 → 解析 → 实际消费**路径整理，目的是记录每个选项的**生效条件、默认值和失败路径**，并给出可直接改写的最小例子。面向最终用户的精简版说明在 `usage/` 目录下，本文与它们互补，不重复其排版。

**验证状态**：本文结论来自源码静态核对（见文末[源码索引](#9-源码索引)）。本轮没有启动游戏、没有运行 Mod、没有做实机或性能验证；文中出现的 Hash、尺寸、槽位、阈值一律是**占位示例**，必须换成目标游戏里的真实值。

---

## 目录

1. [Mod 基础生命周期](#1-mod-基础生命周期)
2. [ShaderOverride](#2-shaderoverride)
3. [TextureOverride](#3-textureoverride)
4. [ShaderRegex](#4-shaderregex)
5. [CustomShader](#5-customshader)
6. [着色器替换、编译与缓存](#6-着色器替换编译与缓存)
7. [失败路径速查](#7-失败路径速查)
8. [排错顺序](#8-排错顺序)
9. [源码索引](#9-源码索引)

---

## 1. Mod 基础生命周期

### 1.1 目录与加载顺序

`d3dx.ini` 是唯一入口，Mod 的 INI 由 `[Include]` 拉进来：

```ini
[Include]
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
```

- `include` / `include_recursive` / `exclude_recursive` / `user_config` 的语义见[INI 与命名空间](../usage/dx11/configuration.md)。
- 递归遍历的实际行为（`src/DirectX11/IniHandler.cpp:993-1045`）：进入一个目录后，先把该目录下**全部 `.ini` 文件**收进集合，按大小写不敏感排序后逐个加载，**然后**再按同样顺序进入各个子目录。也就是说同一层的 INI 先于子目录里的 INI。
- 排除模式在遍历开始前一次性收集（`IniHandler.cpp:1850`），按 glob 转成大小写不敏感的 PCRE2 正则（`IniHandler.cpp:916-938`）。模式同时作用于**文件名和目录名**（`IniHandler.cpp:1016-1023`）。
- 只处理 `.ini` 结尾的文件（`IniHandler.cpp:1024-1026`）；同名的其它文件只是资源，不参与加载。
- 同一个文件被重复包含时给出警告 `File included multiple times`（`IniHandler.cpp:1882-1885`）。

### 1.2 命名空间与节的作用域

递归加载的 Mod 会自动隔离自定义节名（`IniHandler.cpp:319-328`、`:1035`）：

- 命名空间 = 相对 DLL 目录的路径 + 文件名，例如 `Mods\Example\mod.ini`。
- 带前缀的节被改写为 `前缀\命名空间\原名`，即 `ShaderOverride\Mods\Example\mod.ini\Foo`。
- **不改写**的节（全局入口，所有 Mod 共享）：`[Constants]`、`[Present]`、`[ClearRenderTargetView]`、`[ClearDepthStencilView]`、`[ClearUnorderedAccessViewUint]`、`[ClearUnorderedAccessViewFloat]`，以及 `[Hunting]`、`[Logging]`、`[System]`、`[Input]`、`[Device]`、`[Rendering]`、`[Loader]`、`[Profile]`、`[Stereo]`、`[ConvergenceMap]`。判定依据是这些节在源码登记表里是**精确匹配**而不是前缀匹配（`IniHandler.cpp:96-133`、`:195-219`）。
- 会被改写的节（带前缀）：`TextureOverride`、`CommandList`、`ShaderOverride`、`CustomShader`、`ShaderRegex`、`BuiltInCommandList`、`BuiltInCustomShader`、`Pool`、`Resource`、`Key`、`Include`、`Preset`。
- 全局节允许被递归加载的文件**追加或覆盖**键值（`IniHandler.cpp:456-463`、`:603-606`）；带前缀的节重名会给出警告 `Duplicate section found`（`IniHandler.cpp:479-484`）。

需要稳定的跨 Mod 接口时，在文件**开头**显式改命名空间（`IniHandler.cpp:540-566`）：

```ini
namespace = Example

[Constants]
global $enabled = 1

[CommandListUpdate]
$enabled = 1
```

`namespace =` 与 `condition =` 属于**前导行**：解析器在遇到第一个 `[节]` 之前才接受它们（`IniHandler.cpp:653-690`）。放在第一个节之后就只会得到 `Entry outside of section` 警告。

其它 Mod 里这样引用：`run = CommandList\Example\Update`。

### 1.3 生效边界与重载

加载顺序是固定的（`IniHandler.cpp:5397-5440`）。规则是：**能被引用的节必须先解析**，所以后解析的节可以安全引用先解析的节，反之不行（例如命令列表引用 `[Resource*]`，资源就必须先解析，见 `IniHandler.cpp:5387-5396` 的说明）。

| 顺序 | 阶段 | 说明 |
| --- | --- | --- |
| 1 | `[Constants]` | 唯一允许分配全局变量的命令列表，必须最先 |
| 2 | `[Resource*]`、`[Pool*]` | 命令列表可以引用资源，资源必须先于命令列表 |
| 3 | `[Preset*]`、`[Key*]` | 依赖 `[Constants]` 分配的变量 |
| 4 | `[CustomShader*]` | 编译着色器 |
| 5 | `[CommandList*]` | 显式命令列表 |
| 6 | `[ShaderOverride*]` | |
| 7 | `[ShaderRegex*]` | |
| 8 | `[TextureOverride*]` | |
| 9 | `[Present]`、`[Clear*]` | |

重载：

- `F10` 默认绑定 `reload_config` 与 `reload_fixes`（`Dependencies/d3dx.ini:155`、`:166`）。
- `reload_config` 只是**置标志位**（`IniHandler.cpp:4704-4712`），真正的 `ReloadConfig` 在 Present 处理里执行（`src/DirectX11/HackerDXGI.cpp:249-250`）。重载会重建全部节、变量与资源。
- `reload_fixes` 单独重载 `ShaderFixes` 里的着色器补丁（`src/DirectX11/Hunting.cpp:1238-1284`）。
- **F10 不会替换 DLL 本身**，也不会重新加载已经创建的资源对象。

### 1.4 冲突与禁用

- 改目录名（例如 `Example` → `DISABLED Example`）+ 排除规则 `exclude_recursive = DISABLED*` 是官方推荐的禁用方式：排除在遍历前生效，目录名匹配就不会进入。
- 两个 Mod 同时替换同一目标时，行为由**优先级**决定，不是由文件夹顺序决定（见 [3.5](#35-优先级与冲突)）。
- 加载期就有冲突提示：重复 `ShaderOverride` 哈希、重复 `TextureOverride` 哈希、模糊匹配节永远匹配不上、ShaderRegex 子节缺失等，都会写入日志；只要本次加载出现过 INI 警告，加载结束时播放一次提示音（`IniHandler.cpp:302-317`、`:5472`）。提示音受 `[Logging] show_warnings` 控制，代码默认 **true**（`IniHandler.cpp:5111`），但 INI 模板里写的是 `show_warnings = 0`（`Dependencies/d3dx.ini:1091`），模板下不会有声音，**日志仍然照写**。

### 1.5 持久化选项

- `global persist $name = value` 声明的变量在保存时写入 `user_config` 指定的文件（默认 `d3dx_user.ini`），该文件在所有其它 INI 之后加载（`IniHandler.cpp:1918-1923`），因此它的值会覆盖同名默认值。
- 恢复默认用 `Ctrl + Alt + F10`（`wipe_user_config = ctrl alt no_shift VK_F10`，`Dependencies/d3dx.ini:169`），它清的是整份用户设置，不只当前 Mod。
- 不要把主配置写进 `d3dx_user.ini`：那是用户设置的落盘位置。

### 1.6 命令表与表达式速查

本文示例反复出现下列词汇，这里只做速查；完整说明见[命令列表与执行时机](../usage/dx11/command-lists.md)、[表达式](../usage/dx11/expressions.md)和[变量与持久设置](../usage/dx11/variables.md)。

| 词汇 | 含义 |
| --- | --- |
| `run = CommandList名称` | 在当前位置调用另一个命令列表；命令按出现顺序执行 |
| `post` | 原始绘制/操作之后执行的阶段，不是"下一行之后" |
| `handling = skip` | 跳过原始绘制，只在前置阶段有意义 |
| `handling = abort` | 中断当前命令执行路径 |
| `CheckTextureOverride = ps-t0` | 对该槽位上绑定的资源执行匹配的 `[TextureOverride*]` |
| `this = <Resource节名>` | 把资源写回当前被检查的那一项 |
| `ps-t0 = <Resource节名>` | 把资源绑定到指定槽位 |
| `x` / `y` / `z` / `w` | IniParams 的第 0 组分量，`x1`、`y1` 等对应后续组。参见 [`ParseIniParamName`](../src/common/util.h#L991) |
| `$name` | 变量。用 `global` 声明全局变量，用 `local` 在命令列表作用域内声明局部变量。全局变量可加 `persist` 保存到用户设置，或加 `locked` 禁止命令写入。参见[变量与持久设置](../usage/dx11/variables.md) |
| `if` / `else if` / `elif` / `else` / `endif` | 条件块，`elif` 和 `else if` 都可用。参见 [分支解析](../src/DirectX11/CommandList.cpp#L9870) |

INI 中以分号开头的行可作注释，见 [INI 行解析](../src/DirectX11/IniHandler.cpp#L673)。

INI 函数见[表达式函数表](ini-language-extensions/expressions/functions.md)。着色器可用 HLSL 的 `asint()` 读取浮点位模式，例如 `if (asint(IniParams[0].x) == asint(-0.0))` 可检查 IniParams 中的负零，区分未绑定资源与没有匹配候选。见 [`IniHandler.cpp` 中的同类示例](../src/DirectX11/IniHandler.cpp#L2885)。

---

## 2. ShaderOverride

`[ShaderOverride*]` 按**着色器**匹配，在每次绘制前执行其命令列表。名字里的 `Shader` 指顶点/几何/像素/计算等**着色器**，与资源 Hash 不是同一类值。

### 2.1 匹配条件

| 项 | 说明 |
| --- | --- |
| `hash` | 目标着色器的 64 位 Hash，最多 16 位十六进制（`IniHandler.cpp:1589-1611`） |

- 缺少 `hash=`：警告 `Section missing Hash=`，**整节被跳过**（`IniHandler.cpp:2939-2943`）。
- Hash 不是合法十六进制或超过 16 位：警告 `Hash parse error`，该节不再注册（`IniHandler.cpp:1599-1603`）。
- 着色器 Hash 由 Hunting 导出得到，见[定位与导出](../usage/dx11/hunting.md)；不要拿资源 Hash 填进来。

### 2.2 选项

`[ShaderOverride*]` 中被单独解析（非命令）的键只有这几个（`IniHandler.cpp:2906-2914`），**其余每一行都是命令**：

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `hash` | 无（必填） | 目标着色器 |
| `allow_duplicate_hash` | `false` | 允许多个同哈希的规则共存，见 [2.4](#24-重复哈希) |
| `depth_filter` | `none` | **已弃用**，`depth_active` / `depth_inactive`；仍会生效但会打印迁移提示（`IniHandler.cpp:2876-2901`、`HackerContext.cpp:534-556`） |
| `model` | 空 | 重编译该着色器时使用的模型，如 `vs_5_0`（`HackerDevice.cpp:1222-1227`） |
| `disable_scissor` | `false` | 关闭该路径的裁剪；被翻译成内置 CustomShader 调用（`IniHandler.cpp:2968-2977`） |
| `filter_index` | `FLT_MAX`（表示未设置） | 着色器筛选值，见 [2.5](#25-filter_index) |

常用命令：`handling = skip`（跳过这次绘制）、`run = CommandListX`、`CheckTextureOverride = ps-t0`、资源绑定、`x = 1` 等参数赋值。

### 2.3 生效时机

- 匹配发生在绘制前（`src/DirectX11/HackerContext.cpp:1063-1078`），按阶段依次查找：顶点 → 外壳 → 域 → 几何 → 像素。计算着色器走单独路径（`HackerContext.cpp:1871-1874`）。
- 命中后执行该节的命令列表；节内的 `post` 命令在绘制结束后执行（`HackerContext.cpp:1142-1146`）。
- `handling = skip` 只在绘制前有意义；`handling = abort` 会同时中断绘制前后的命令列表（`src/DirectX11/CommandList.cpp:1127-1137`）。

### 2.4 重复哈希

多个 Mod 用同一个着色器 Hash 时，默认会告警（`IniHandler.cpp:2828-2874`）：

```text
Possible Mod Conflict: Duplicate ShaderOverride hash=%16llx
```

两种豁免方式：

- `allow_duplicate_hash = true`：**协作式**。同一 Hash 的**所有**节都要声明，缺一个就仍然告警。适合脚本化生成的节。
- `allow_duplicate_hash = overrule`：**覆盖式**。任意一个节声明即可，整体不再告警。适合两个第三方 Mod 已确认可共存，但会放过其它真实冲突。

节名仍需唯一（或带命名空间），豁免只影响告警，不会合并两个节的设置。

### 2.5 filter_index

`filter_index` 是着色器的筛选值，被 `ShaderOverride` 的 `filter_index` 读取（`IniHandler.cpp:2955-2957`），也被 `ShaderRegex` 组在命中时写进对应的 ShaderOverride（`src/DirectX11/ShaderRegex.cpp:485-487`）。

- 原始值会备份为 `backup_filter_index`（`IniHandler.cpp:2957`），当 ShaderRegex 取消链接时恢复（`ShaderRegex.cpp:557-560`）。
- `filter_index` 不参与布尔判断：命中了 `filter_index = 0` 的规则仍然算“有候选”，详见[按目标查询候选](../usage/dx11/texture-overrides.md#按目标查询候选)。
- 未设置 `filter_index`（默认 `FLT_MAX`）的命中规则不会返回自己的索引：纹理过滤表达式对“命中了但都没设 `filter_index`”的情况返回 `1.0`（`CommandList.cpp:3748-3754`，见 [3.6](#36-过滤filter_index-与-iteration)）。

### 2.6 最小示例

```ini
[ShaderOverrideExample]
hash = 0123456789abcdef
CheckTextureOverride = ps-t0
x = 1
```

命中该着色器时检查 `ps-t0` 上的贴图替换，并把 `x` 设为 1。Hash 必须替换成真实值。

---

## 3. TextureOverride

名字里虽然写着 `Texture`，目标可以是贴图，也可以是 VB / IB / CB / UAV 等任意资源。匹配用**资源 Hash**（32 位），不是着色器的 64 位 Hash。

### 3.1 三类匹配

| 类别 | 依据 | 存放位置 | 备注 |
| --- | --- | --- | --- |
| 精确匹配 | `hash` | `G->mTextureOverrideMap`（Hash → 规则列表） | 命中后**不再评估模糊匹配**（`src/DirectX11/ResourceHash.cpp:1942-1953`） |
| 模糊匹配 | 资源描述字段 | `G->mFuzzyTextureOverrides` | 没有 `hash` 时才生效 |
| 绘制上下文匹配 | `match_*` 绘制参数 | 与上面两类的规则合并 | 可与 `hash` 或模糊匹配**同时**使用 |

### 3.2 精确匹配

```ini
[TextureOverridePart]
hash = 12345678
```

- `hash` 解析本身允许最多 16 位十六进制（`GetIniHash` 用 `%16llx`），但 TextureOverride 的结果会被截断成 32 位（`IniHandler.cpp:3876`），所以通常写成 8 位十六进制。把 16 位着色器 Hash 填进来只会保留低 32 位：它**不会**"匹配不上"，而是变成一个**真实存在但与原资源无关**的 32 位资源 Hash，可能命中并静默替换另一个资源。务必写成 8 位资源 Hash。
- 缺少 `hash=` 且也没有任何模糊字段：警告 `Section missing Hash= or valid match options`，整节跳过（`IniHandler.cpp:3876-3885`）。
- 同一节同时写了 `hash` 和模糊字段：警告 `Cannot use hash= and match options together!`，但该节**仍按 Hash 注册**（`IniHandler.cpp:3887-3895`）。不要指望模糊条件生效。

### 3.3 模糊匹配

```ini
[TextureOverrideScreen]
match_type = Texture2D
match_width = height * 16 / 9
match_height = res_height
match_bind_flags = +shader_resource -depth_stencil
; 数值越大优先级越高；10 表示高于默认的 0
match_priority = 10
```

字段全集（`IniHandler.cpp:3312-3327`）：

| 字段 | 匹配对象 |
| --- | --- |
| `match_type` | 资源维度 |
| `match_usage` | 资源用途，默认 `D3D11_USAGE_DEFAULT`（`IniHandler.cpp:3714-3718`） |
| `match_bind_flags` / `match_cpu_access_flags` / `match_misc_flags` | 描述标志 |
| `match_format` | DXGI 格式 |
| `match_byte_width` / `match_stride` | 缓冲字节数 / 元素步长 |
| `match_width` / `match_height` / `match_depth` / `match_array` | 纹理尺寸、数组层数 |
| `match_mips` | mip 数 |
| `match_msaa` / `match_msaa_quality` | 采样描述 |

规则：

- 数值表达式只支持简化形式 `[op] value | field_name [* multiplier] [/ divider]`。运算符只有六个记号：`=`、`!`、`<`、`<=`、`>`、`>=`（`IniHandler.cpp:3426-3447`；源码注释 `:3418` 写的是 `operator = "=" | "!" | "<" | ">" | "<=" | ">="`）。**不支持 `!=`**——源码没有这个分支，写 `!=` 时 `!` 先被吃掉，剩下的 `=...` 既不是整数也不是字段名，于是解析失败。表达"不等于"请写 `!`，例如 `match_width = ! 1920`。`field_name` 只能是 `width`、`height`、`depth`、`array`、`res_width`、`res_height`（`IniHandler.cpp:3373-3407`、`:3409-3500`）。**不能**放任意函数或命名变量。
- 表达式解析失败**只打印警告** `Unable to parse expression - must be in the simple form`（`IniHandler.cpp:3366-3371`），规则**仍会注册**。此时该字段的运算符已经被设成解析到的比较符（没有前缀时是 `=`），而取值仍是 0（`IniHandler.cpp:3426-3447`、`src/DirectX11/ResourceHash.cpp:1547-1556`）。效果不是"被忽略"，而是**拿 0 去比**：`match_width = abc` 变成"宽度等于 0"，几乎匹配不到东西；写成 `!=` 时（`!` 已被吃掉、剩下的 `=...` 解析失败）留下的是"该字段 != 0"，几乎匹配一切。写错表达式时先看日志里的这条警告。
- 由于运算符不再是 `ALWAYS`，`update_types_matched` 会认为该字段受约束，从而排除一部分资源类型（`ResourceHash.cpp:1800-1832`）。各字段独立生效，多个字段同时设置时排除集合取并集：

  | 字段 | 被排除的资源类型 | 剩余可能命中的类型 |
  | --- | --- | --- |
  | `match_width` / `match_mips` / `match_format` | Buffer（`:1813-1816`） | 全部纹理 |
  | `match_height` | Buffer、Texture1D（`:1818-1819`） | Texture2D、Texture3D |
  | `match_depth` | Buffer、Texture1D、Texture2D（`:1821-1822`） | **只有 Texture3D** |
  | `match_array` | Buffer、Texture3D（`:1824-1825`） | Texture1D、Texture2D |
  | `match_msaa*` | Buffer、Texture1D、Texture3D（`:1827-1829`） | 只有 Texture2D |
  | `match_byte_width` / `match_stride` | 全部纹理（`:1809-1811`） | 只有 Buffer |

  注意 `match_depth` 与 `match_array` 的排除集合**并不相同**：写了 `match_depth` 的规则只可能匹配 Texture3D，永远不会匹配 Texture2D。
- 标志字段支持 `+flag` / `-flag` 组合，也支持 `0x%x / 0x%x` 掩码写法；非法 flag 会警告 `Invalid flag` 并放弃整个字段（`IniHandler.cpp:3619-3688`）。
- `match_type` 写错：警告 `Unrecognised Enum: match_type=...`，该约束被丢弃，退回"任意维度"（`IniHandler.cpp:1660-1681`、`:3699-3703`）。
- `match_format` 写错：警告 `Unknown format "..."`，该字段同样被丢弃（`IniHandler.cpp:3738-3744`）。
- 字段组合决定可匹配的资源类型（`ResourceHash.cpp:1800-1832`）。如果没有任何一种资源类型可能命中，警告 `Section can never match any resources` 并**丢弃该节**（`IniHandler.cpp:3766-3770`）。
- 涉及 `res_width` / `res_height` 的规则按动态分辨率处理，分辨率变化会重新求值（`ResourceHash.cpp:1676-1686`）。

### 3.4 绘制上下文匹配

```ini
[TextureOverridePart]
hash = 12345678
match_first_index = 0
match_index_count = 300
```

可用字段（`IniHandler.cpp:3331-3337`）：`match_first_vertex`、`match_first_index`、`match_first_instance`、`match_vertex_count`、`match_index_count`、`match_instance_count`。

- 这些字段比较的是**当前绘制**的起点与数量，不是资源自身属性（`ResourceHash.cpp:1834-1856`）。
- 一个共享缓冲对应多个部件时，必须用它们把范围缩小，否则规则会命中所有部件。
- 绘制上下文匹配写在**任何**一条规则上，都会抑制重复 Hash 告警（`IniHandler.cpp:3785-3786`、`:3800-3801`）。

### 3.5 优先级与冲突

`match_priority` 是整数，默认 `0`（`IniHandler.cpp:3510-3512`）。**数值越大，优先级越高**。同一 Hash 的多条规则在加载末尾排序（`IniHandler.cpp:3910-3920`），排序规则是：

1. `match_priority` **升序**；
2. 优先级相同时按**节名升序**，保证多次运行结果一致（`ResourceHash.cpp:2125-2136`）。

即：**优先级高的排在后面**。资源创建期的描述覆盖按列表顺序应用，所以高优先级会覆盖低优先级的结果（`src/DirectX11/HackerDevice.cpp:2026-2031`）。

因此 `match_priority = -1` 表示**最低**优先级：它排在默认的 `0` 之前，先被应用、也最容易被同 hash 的默认优先级规则盖掉。要表达"最高优先级"应写比 0 大的正数（例如 `match_priority = 10`）。这个符号约定**不会**产生任何日志提示，写错只能靠效果反推。

模糊匹配规则用同一个比较器排序（`ResourceHash.cpp:2138-2141`），所以跨精确/模糊两类时优先级语义一致。

重复 Hash 的告警（`IniHandler.cpp:3780-3809`）：

```text
Possible Mod Conflict: Duplicate TextureOverride hash=%08lx
```

只要下面任一条件成立就不告警：本节有绘制上下文匹配、本节有 `match_priority`、对方节有绘制上下文匹配、对方节有 `match_priority`。告警文本也直接给出解法：加 `match_priority=n` 既能消警告，又能明确顺序。

### 3.6 过滤：filter_index 与 iteration

**filter_index**（`IniHandler.cpp:3572`，默认 `FLT_MAX`，表示未设置）给命中规则打一个可被表达式读取的数值。纹理过滤表达式返回值的定义（`src/DirectX11/CommandList.cpp:3708-3754`）：

| 情况 | 返回值 |
| --- | --- |
| 目标槽位没有绑定资源 | `-0.0` |
| 已绑定资源，但没有命中任何规则 | `0.0` |
| 命中了规则，但都没有 `filter_index` | `1.0` |
| 命中了多条规则 | 列表中**最后一个**带 `filter_index` 的值（作用域见本节末尾） |

`-0.0` 与 `0.0` 用 `==` 比较是相等的，这是为兼容旧配置保留的行为。要区分"槽位没绑资源"和"没有候选"，得在着色器里用 `asint()` 读符号位——那是 HLSL 函数，不是 INI 表达式，见 [1.6](#16-命令表与表达式速查)。

顺序上要注意作用域，两条路径的追加顺序不同：

- **保留资源（绘制期）**路径：先追加精确命中、再追加模糊命中（`ResourceHash.cpp:2102-2115`），所以"最后一个带 `filter_index` 的值"可能来自模糊规则，即便精确规则的 `match_priority` 更高。
- **资源创建期**路径：一旦有精确 hash 命中就**完全跳过**模糊规则（`ResourceHash.cpp:1942-1953`，见 [3.1](#31-三类匹配)）。

因此同一批规则在创建期与绘制期可能得到不同的 `filter_index` 与覆盖顺序。

**iteration**（`IniHandler.cpp:3552-3570`）限制同一资源被重复创建时的覆盖次数：

```ini
[TextureOverridePart]
hash = 12345678
iteration = 1, 2
```

- 列表**必须以正数开头**：解析时遇到 `0` 或负数就中断（`IniHandler.cpp:3562-3569`），所以 `iteration = 0, 1, 2` 实际等价于"永不应用"，不是"从第 0 次开始"。
- 计数器初值是 0，每次该规则被检查时**先加一**再比对（`HackerDevice.cpp:1914-1930`），因此第一次检查的值是 1、第二次是 2，以此类推；只有落在列表里才应用覆盖，否则打印 `override skipped`。
- 最多支持 10 个编号。
- `iteration` 只影响**资源创建期**的描述覆盖，不影响绘制期的命令列表。

### 3.7 资源创建期覆盖

这些选项改变的是**资源创建时**的描述，资源已经存在就改不了了，通常需要重启游戏或触发资源重建（`IniHandler.cpp:3514-3575`、`HackerDevice.cpp:2003-2050`）：

| 选项 | 作用 |
| --- | --- |
| `format` | 覆盖格式 |
| `width` / `height` | 覆盖尺寸 |
| `width_multiply` / `height_multiply` | 尺寸倍率，默认 `1.0` |
| `override_byte_stride` / `override_vertex_count` | 缓冲扩容；需要 `allow_buffer_resize`（`[Rendering]`，默认 `true`，`IniHandler.cpp:5273`） |
| `uav_byte_stride` | UAV 元素步长，用于推算 UAV 元素数（`HackerDevice.cpp:1618-1644`） |
| `expand_region_copy` | 扩展区域复制行为 |
| `deny_cpu_read` | 限制 CPU 读路径 |

失败路径：

- `override_vertex_count` 大于 0 但没给 `override_byte_stride`：警告 `Failed to detect stride for override_vertex_count=...` 并放弃该节（`IniHandler.cpp:3523-3530`）。
- 节名里含 `VertexLimitRaise` 但没写 `override_vertex_count`：按约 8 MB 兜底（`IniHandler.cpp:3543-3546`）。
- 同一个 Hash 下有多条规则写了不同的 `override_byte_width`，取**最大**值并应用到该 Hash 的所有规则（`IniHandler.cpp:3811-3848`）。
- 尺寸倍率为负或溢出 `UINT` 会被拒绝（`HackerDevice.cpp:1950-1962`）。
- `width` / `height` 写成负数（且不是 `-1` 这个"未设置"哨兵）：警告 `ERROR: Invalid resource override [...]`，该次覆盖被放弃（`HackerDevice.cpp:1939-1946`）。
- `[Rendering] allow_buffer_resize = 0` 时，`override_byte_stride` / `override_vertex_count` / `uav_byte_stride` 以及节名含 `VertexLimitRaise` 的 8.8 MB 兜底**整组被静默忽略**，不产生任何警告（`IniHandler.cpp:3520-3550`）。

### 3.8 dHash 感知匹配

除了按 Hash 和描述匹配，还有按**感知哈希**匹配贴图的方式，只支持像素着色器（`src/DirectX11/CommandList.cpp:8693`、`:9630-9651`）：

```ini
[TextureOverrideExample]
hash = 01234567
run = SnapshotOriginalTextures
ps->MatchTextures(ResourceMatchFilter) = ref ResourceReplacement
run = RestoreOriginalTextures

[ResourceMatchFilter]
match_dhash = 1363895a02495a04
match_format = BC3_UNORM
match_confidence = 0.75

[ResourceReplacement]
filename = replacement.dds
```

- 过滤资源必须声明 `match_dhash`（也接受别名 `original_dhash`）；值为 1～16 位十六进制，不足 16 位按高位补零（`IniHandler.cpp:2425-2427`、`:1598-1599`）。未声明或解析失败则不参与匹配（`src/DirectX11/TextureMatch.cpp:115-117`）。
- `match_confidence` 默认 `0.75`，限制到 `[0,1]`，非法非有限值回退到默认（`IniHandler.cpp:2428-2433`）；允许距离为 `floor((1-confidence)*64)`，**含边界**（`TextureMatch.cpp:119-120`）。
- 比较始终使用**原始**贴图；同一次绘制内后来的候选只有距离更小才能覆盖已接受结果，同距离保留先到者（`TextureMatch.cpp:161-176`）。
- 必须保留 `run = RestoreOriginalTextures`，否则替换绑定会泄漏到后续绘制。
- 首次计算 dHash 可能触发同步 GPU 读回，不能当作零成本操作。

完整语义见[声明资源](../usage/dx11/resources.md#按-dhash-匹配像素着色器贴图槽位)。

### 3.9 声明与绑定资源（`[Resource*]`）

本文示例会用到 `[Resource*]` 节。这里只列本文用到的键，完整字段见[声明资源](../usage/dx11/resources.md)。

```ini
[ResourceDiffuse]
filename = diffuse.dds

[ResourceIndices]
type = Buffer
format = R32_UINT
stride = 4
filename = indices.ib
```

| 键 | 作用 |
| --- | --- |
| `filename` | 文件初始数据；相对所属 INI 查找，找不到再回退到 DLL 目录 |
| `type` | 资源类型，直接填 `Buffer`、`Texture2D` 等名称。合法值见[声明资源](../usage/dx11/resources.md)和 [`CustomResourceTypeNames`](../src/DirectX11/CommandList.h#L479) |
| `format` / `stride` / `width` / `height` | 格式、元素步长、尺寸 |
| `match_dhash` | 感知匹配参考 Hash；1～16 位十六进制，不足 16 位按高位补零；也接受别名 `original_dhash`（`IniHandler.cpp:2425-2427`） |
| `match_format` | 感知匹配要求的 SRV 格式；也接受别名 `original_format`（`IniHandler.cpp:2412-2414`） |
| `match_confidence` | 感知匹配阈值，默认 `0.75`，限制到 `[0,1]`（`IniHandler.cpp:2428-2433`） |

绑定关系：

- `this = <Resource节名>`：把该资源写回**当前被检查的那一项**（`CheckTextureOverride = ps-t0` 时是 `ps-t0` 槽位）。
- `ps-t0 = <Resource节名>`：把该资源绑定到指定槽位。

`[Resource*]` 节必须先于引用它的命令列表解析（见 [1.3](#13-生效边界与重载)）。

### 3.10 最小示例

隐藏某个部件的绘制：

```ini
[TextureOverridePart]
hash = 12345678
match_first_index = 0
match_index_count = 300
handling = skip
```

替换贴图：

```ini
[ResourceDiffuse]
filename = diffuse.dds

[TextureOverrideDiffuse]
hash = 12345678
this = ResourceDiffuse
```

`this` 指**当前被检查的那一项**（`CheckTextureOverride = ps-t0` 时就是 `ps-t0` 槽位），不是"任意贴图"（`CommandList.cpp:9077`、`:10707-10721`）。`[Resource*]` 的键与资源类型见[声明资源](../usage/dx11/resources.md)。

---

## 4. ShaderRegex

`[ShaderRegex*]` 对着色器的**汇编**做模式匹配与替换，适合一批结构相近但 Hash 不同的着色器。

### 4.1 节结构

```ini
[ShaderRegexExample]
shader_model = ps_5_0

[ShaderRegexExample.Pattern]
^mov (?P<dst>r\d+\.xyzw), (?P<src>r\d+\.xyzw)\n

[ShaderRegexExample.Pattern.Replace]
mov ${dst}, ${src}\n
```

| 节 / 选项 | 必填 | 说明 |
| --- | --- | --- |
| 主节 `shader_model` | **是** | 空格分隔，可列多个模型；缺失则警告 `RegEx section missing shader_model` 并禁用整组（`IniHandler.cpp:3046-3049`） |
| 主节 `temps` | 否 | 命名临时寄存器，自动分配并调整 `dcl_temps`（`IniHandler.cpp:3060-3061`） |
| 主节 `filter_index` | 否 | 命中时写进对应 ShaderOverride（`IniHandler.cpp:3065`、`ShaderRegex.cpp:485-487`） |
| 主节 `exclude_hash` | 否 | 排除指定着色器，见 [4.4](#44-exclude_hash) |
| 主节命令 | 否 | 匹配后执行的 INI 命令 |
| `.Pattern` | 否 | PCRE2 模式 |
| `.Pattern.Replace` | 否 | 替换文本 |
| `.InsertDeclarations` | 否 | 插入尚不存在的声明 |

**当前一组只支持一个 `.Pattern` 及其对应的 `.Pattern.Replace`**（`IniHandler.cpp:3262-3271`）。需要多条连续规则时，用多个独立的组。省略 `.Pattern` 时，该组匹配指定模型的所有候选——范围很宽，必须配合 `exclude_hash` 或严格的 `shader_model` 使用。

### 4.2 匹配与替换语义

- 模式用 PCRE2 编译，选项固定为 `PCRE2_CASELESS | PCRE2_MULTILINE`（`src/DirectX11/ShaderRegex.cpp:220-223`）：**默认忽略大小写**，且 `^` / `$` 按行匹配。
- INI 会去掉每行的行首尾空白，跳过空行和以 `;` 开头的注释行，然后把各行**直接拼接、不插入换行**（`IniHandler.cpp:3080-3089`、`:3155-3164`）。所以换行必须自己写 `\n`；要匹配缩进写 `\s*`。日志会打印最终拼出的模式与替换串（`IniHandler.cpp:3096-3098`、`:3169-3171`），可以直接核对。
- 替换使用 `PCRE2_SUBSTITUTE_EXTENDED`（`ShaderRegex.cpp:347`），因此 `\n` 会被展开。
- `${0}` 表示完整匹配，`${name}` 引用命名捕获组；`$name` / `${name}` 也可以引用 `temps` 里的命名临时寄存器（`ShaderRegex.cpp:300-315`）。
- 替换必须保持合法的声明、寄存器分配与控制流，否则后面汇编会失败。
- `temps` 与命名捕获组重名会被拒绝：警告 `Named capture group overlaps with temp regs!` 并禁用整组（`IniHandler.cpp:3104-3107`）。
- 模式编译失败：警告 `WARNING: PCRE2 regex compilation failed at offset N`（`ShaderRegex.cpp:225`）。

### 4.3 temps 与 InsertDeclarations

- `temps` 声明的寄存器按现有 `dcl_temps` 之后的编号依次分配；补丁生效后 `dcl_temps` 会同步增加（`ShaderRegex.cpp:300-315`、`:168-195`）。
- 临时寄存器总数超过 `D3D11_COMMONSHADER_TEMP_REGISTER_COUNT` 时拒绝补丁（`ShaderRegex.cpp:327-331`、`:173-175`）。
- `.InsertDeclarations` 插入尚不存在的声明行；已存在的相同行不会重复插入（`ShaderRegex.cpp:104-127`）。即使没有做正则替换，只要所有模式都匹配成功，声明照样会插入（`ShaderRegex.cpp:433-434`）。

### 4.4 exclude_hash

```ini
[ShaderRegexEnableTextureOverrides]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
exclude_hash = 5927c81f6515eb11
exclude_hash = 0123456789abcdef
run = CommandListSkin
```

- 1～16 位十六进制，大小写均可，也接受 `0x` 前缀（`ShaderRegex.cpp:437-455`）。
- 空值、非法字符或超长值：警告 `Invalid ShaderRegex exclude_hash=...` 并**禁用整个组**（`IniHandler.cpp:3052-3056`）。
- 重复值会合并。
- 排除只作用于**所在组**：该组的模式匹配、汇编修改、命令列表和 `filter_index` 都不作用于这个着色器；其它组以及显式的 `[ShaderOverride*]` 不受影响（`ShaderRegex.cpp:852`、`:899`、`:459-460`）。
- 排除检查发生在着色器处理阶段，不增加逐次绘制的条件判断。

### 4.5 生效时机与缓存

- 处理发生在绘制前（`src/DirectX11/HackerContext.cpp:1060`、`:769-799`），对当前绑定的各阶段着色器逐个尝试。
- 如果**没有任何**带 `.Pattern` 的组匹配该着色器模型，整个反汇编过程会被跳过（`HackerContext.cpp:669-673`），这是主要性能来源。
- 缓存文件（`ShaderRegex.cpp:605`、`:667`、`:701`、`:736`、`:765`）：

| 文件 | 内容 |
| --- | --- |
| `%016llx-%ls_regex.dat` | 元数据：版本号、`shader_regex_hash`、是否打补丁、命中组 ID 列表 |
| `%016llx-%ls_regex.bin` | 补丁后的字节码 |
| `%016llx-%ls_regex.txt` | 补丁后的汇编，仅在 `export_fixed` 时输出 |

- 元数据头里的 `shader_regex_hash` 是**全部 `[ShaderRegex*]` 节的哈希**（`IniHandler.cpp:3229-3231`、`:3301`）。改动任何 Regex 配置都会改变它，旧的缓存元数据随即被拒绝并重新生成（`ShaderRegex.cpp:626-628`）。
- 缓存还会检查：命中组 ID 是否越界、该组是否已排除此 Hash——任一不满足就拒绝缓存（`ShaderRegex.cpp:643-647`）。
- 命中组后，组的命令列表与 `filter_index` 被链接进对应的 `[ShaderOverride*]`（`ShaderRegex.cpp:457-522`）；配置重载时解除链接并恢复 `backup_filter_index`（`ShaderRegex.cpp:524-563`）。

### 4.6 失败路径

| 情况 | 行为 |
| --- | --- |
| 主节缺 `shader_model` | 警告 `RegEx section missing shader_model`，禁用整组 |
| `.Pattern` 编译失败 | 警告 `WARNING: PCRE2 regex compilation failed at offset N`，禁用整组 |
| 命名捕获组与 `temps` 重名 | 警告 `Named capture group overlaps with temp regs!`，禁用整组 |
| `.Pattern.Replace` 没有对应的 `.Pattern` | 警告 `Missing corresponding pattern section`，禁用整组 |
| 任一子节缺失或解析失败 | 警告 `Disabling entire shader regex group`，删除整组（`IniHandler.cpp:3287-3291`） |
| `exclude_hash` 非法 | 警告 `Invalid ShaderRegex exclude_hash=...`，禁用整组 |
| 补丁后汇编失败 | 装配错误按**非致命**处理并打印 NOTICE，着色器保持原样（`HackerContext.cpp:705-722`） |

---

## 5. CustomShader

`[CustomShader*]` 声明自己的着色器、覆盖管线状态，并执行 draw / dispatch。**仅声明不会自动绘制**，必须通过 `run` 调用。

### 5.1 声明与编译

```ini
[CustomShaderProcess]
cs = process.hlsl
cs-t0 = ResourceInput
cs-u0 = ResourceOutput
dispatch = 1, 1, 1

[CommandListProcess]
run = CustomShaderProcess
```

| 选项 | 说明 |
| --- | --- |
| `vs` / `hs` / `ds` / `gs` / `ps` / `cs` | 各阶段的 HLSL 源文件 |
| `flags` | 编译标志，作用于链上所有着色器（`IniHandler.cpp:4605-4611`） |
| `max_executions_per_frame` | 每帧最多执行次数，默认 `0`（不限制） |

编译行为（`src/DirectX11/CommandList.cpp:2948-3113`）：

- **着色器模型固定为 `<阶段>_5_0`**（`CommandList.cpp:3045-3047`），例如 `cs_5_0`。要换模型需要改源码。
- 源文件先按**所属 INI 的命名空间路径**查找，找不到再回退到 DLL 目录（`CommandList.cpp:3005-3037`）。把 HLSL 和 INI 放在一起最省事。
- `cs = null` 表示解绑该阶段（`CommandList.cpp:3002-3003`）。
- 编译失败：警告 `Error compiling custom shader <路径>`，该节的命令列表**不再解析**（`IniHandler.cpp:4628-4639`），因此依赖它的 `run` 不会执行。
- 编译产物按需缓存为 `<源文件>.<模型>.<flags>.bin`（`CommandList.cpp:3052-3056`），**写在源文件旁边**，不是 `cache_directory`；时间戳比源文件旧则丢弃并重编，日志打印 `Discarding stale cached shader`（`CommandList.cpp:2909-2913`）。

### 5.2 管线状态

| 组 | 字段 |
| --- | --- |
| 混合 | `blend`、`alpha`、`mask`；支持 `[0]`–`[7]` 渲染目标后缀 |
| 混合公共项 | `alpha_to_coverage`、`sample_mask`、`blend_factor[0..3]`、`blend_state_merge` |
| 深度 | `depth_enable`、`depth_write_mask`、`depth_func` |
| 模板 | `stencil_enable`、`stencil_read_mask`、`stencil_write_mask`、`stencil_front`、`stencil_back`、`stencil_ref`；各项都要写完整的 `stencil_` 前缀 |
| 深度模板合并 | `depth_stencil_state_merge` |
| 光栅化 | `fill`、`cull`、`front`、`depth_bias`、`depth_bias_clamp`、`slope_scaled_depth_bias` |
| 光栅化开关 | `depth_clip_enable`、`scissor_enable`、`multisample_enable`、`antialiased_line_enable` |
| 光栅化合并 | `rasterizer_state_merge` |
| 拓扑 | `topology` |
| 采样器 | `sampler`（目前只实现过滤设置） |

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

混合项格式为 `操作 源因子 目标因子`，颜色与 alpha 分开配置。完整字段清单见 `src/DirectX11/IniHandler.cpp:4525-4556`。

### 5.3 调用与绘制

- `run = CustomShaderX` 触发一次执行（`CommandList.cpp:1114-1116`）。
- 执行时会保存并恢复原本绑定的着色器、混合/深度/光栅化状态、采样器与拓扑（`CommandList.cpp:3441-3520`）。对游戏绑定的临时替换要自己保存和恢复。
- 绘制命令：`draw`、`drawindexed`、`drawinstanced`、`drawindexedinstanced`、`dispatch` 以及对应的 `*indirect` 变体；`draw = from_caller` 表示沿用调用方的绘制参数（`CommandList.cpp:958-999`）。
- `max_executions_per_frame` 超限时跳过执行，并计入 `Profiling::max_executions_per_frame_exceeded`（`CommandList.cpp:3470-3479`）。
- 线程组数要按 HLSL 的 `numthreads` 和数据量算；HLSL、输出目标、资源槽和视口必须由调用场景提供。

---

## 6. 着色器替换、编译与缓存

这一节描述**文件级**的着色器补丁，与 INI 里的 `[ShaderOverride*]` 是两条独立通路。

### 6.1 目录与设置

```ini
[Rendering]
override_directory = ShaderFixes
cache_directory = ShaderCache
cache_shaders = 1
export_shaders = 0
export_hlsl = 0
export_fixed = 0
export_binary = 0
dump_usage = 0
```

- `override_directory` 是**生效**的补丁目录；`cache_directory` 只作参考与缓存（`IniHandler.cpp:5215-5242`、`:5279-5283`）。这两个路径在代码里**初始为空**（`src/DirectX11/globals.h:449-450`、`:816-817`），`ShaderFixes` / `ShaderCache` 的名字来自 INI 模板与游戏适配包（`Dependencies/d3dx.ini:657`、`:660`）。任一路径为空时，文件级着色器替换整体停用（`HackerDevice.cpp:1155-1156`）。
- 只有 `override_directory` 里的内容会替换游戏着色器；`cache_directory` 里的文件不会被应用（`src/DirectX11/HackerDevice.cpp:1108-1118` 的设计说明）。
- `cache_shaders` 的**代码默认值是 false**（`IniHandler.cpp:5244`），模板里也写作 `cache_shaders = 0`（`Dependencies/d3dx.ini:666`），即默认不写编译缓存。
- `export_hlsl` 是分级开关：1 输出 HLSL，2 额外附带原始 ASM，3 再附带重编译后的 ASM（`HackerDevice.cpp:954-957`、`:1003-1009`、`:1060-1073`）。
- `storage_directory` 由独立的 D3DCompiler 导出工具读取（`src/D3DCompiler/d3dcWrapper.cpp:72`），不属于 DX11 主进程的着色器替换路径。

### 6.2 查找顺序

游戏创建着色器时，按固定顺序查找替换文件（`HackerDevice.cpp:1146-1191`）：

| 顺序 | 文件 | 行为 |
| --- | --- | --- |
| 1 | `%016llx-%ls_replace.bin` | 直接使用已编译字节码 |
| 2 | `%016llx-%ls.bin` | 同上，备用命名 |
| 3 | `%016llx-%ls_replace.txt` | HLSL 源码 → `D3DCompile` 重新编译 |
| 4 | `%016llx-%ls.txt` | 汇编源码 → 重新汇编 |
| 5 | 反编译 + 自动修复 | 没有补丁文件时的兜底路径 |

其中 `%ls` 是阶段名（`vs`、`ps`、`gs`、`cs` 等）。完整命名规则见[ShaderOverride 与文件补丁](../usage/dx11/shaders.md#编辑着色器)。

- HLSL 编译用 `D3DCOMPILE_OPTIMIZATION_LEVEL3`，入口点是 `main`（`HackerDevice.cpp:1035-1036`、`:756-757`）。
- 可以在 `[ShaderOverride*]` 里用 `model = vs_5_0` 覆盖重编译时使用的着色器模型（`HackerDevice.cpp:1222-1227`）。
- 反编译失败不保证能拿到可用的 HLSL；此时应改用汇编补丁（`HackerDevice.cpp:980-985`）。

### 6.3 缓存与失效

- 缓存开启时，HLSL 编译结果写成 `%016llx-%ls_replace.bin`，并把它的修改时间**设为源 `.txt` 的时间戳**（`HackerDevice.cpp:780-796`）。
- 读取缓存时，`.bin` 与同名 `.txt` 的时间戳必须**完全一致**，否则丢弃缓存并重新编译，日志打印 `Discarding stale cached shader`（`HackerDevice.cpp:581-626`、`:638-641`）。只比"新不新"是不够的，因为补丁包里的 `.txt` 时间戳可能早于用户机器上生成的 `.bin`。
- `.bin` 存在但找不到对应 `.txt` 时，只给出警告并仍然使用（`HackerDevice.cpp:623-625`）。这是历史兼容行为，删了 `.txt` 却留着 `.bin` 属于用户操作问题。
- ShaderRegex 的缓存见 [4.5](#45-生效时机与缓存)。
- CustomShader 的缓存见 [5.1](#51-声明与编译)。

### 6.4 重载

- `F10` 的 `reload_fixes` 会遍历 `ShaderFixes`，重新编译时间戳变化的补丁并替换活跃着色器（`src/DirectX11/Hunting.cpp:1238-1284`）。
- ShaderFixes 里的补丁优先于 ShaderRegex 自动生成的替换：用户替换的着色器总是优先（`src/DirectX11/HackerContext.cpp:587-590`）。
- 装配失败在**实时重载**时是致命的，在创建期替换和 ShaderRegex 路径上按非致命处理（`HackerDevice.cpp:712-714`、`HackerContext.cpp:705-722`）。

---

## 7. 失败路径速查

| 症状 | 日志关键字 | 位置 |
| --- | --- | --- |
| `[ShaderOverride*]` 没有 `hash` | `Section missing Hash=` | `IniHandler.cpp:2941` |
| Hash 不是合法十六进制 | `Hash parse error` | `IniHandler.cpp:1601` |
| 两个 Mod 用了同一个着色器 Hash | `Possible Mod Conflict: Duplicate ShaderOverride hash=` | `IniHandler.cpp:2866` |
| 两个 Mod 用了同一个资源 Hash | `Possible Mod Conflict: Duplicate TextureOverride hash=` | `IniHandler.cpp:3803` |
| `hash` 和 `match_*` 混用 | `Cannot use hash= and match options together!` | `IniHandler.cpp:3888` |
| 模糊规则永远匹配不上 | `Section can never match any resources` | `IniHandler.cpp:3767` |
| 模糊数值表达式写法不对 | `Unable to parse expression - must be in the simple form` | `IniHandler.cpp:3368` |
| 模糊标志名非法 | `Invalid flag` | `IniHandler.cpp:3669` |
| 模糊枚举/格式名非法 | `Unrecognised Enum:` / `Unknown format` | `IniHandler.cpp:1676` / `:3741` |
| `override_vertex_count` 缺 stride | `Failed to detect stride for override_vertex_count=` | `IniHandler.cpp:3528` |
| 资源尺寸覆盖写成负数 | `ERROR: Invalid resource override` | `HackerDevice.cpp:1944` |
| 模糊匹配写 `!=`（源码不支持） | `Unable to parse expression - must be in the simple form` | `IniHandler.cpp:3368` |
| ShaderRegex 组被禁用 | `Disabling entire shader regex group` | `IniHandler.cpp:3290` |
| ShaderRegex 缺 `shader_model` | `RegEx section missing shader_model` | `IniHandler.cpp:3047` |
| ShaderRegex `exclude_hash` 非法 | `Invalid ShaderRegex exclude_hash=` | `IniHandler.cpp:3054` |
| ShaderRegex 命名组与 `temps` 冲突 | `Named capture group overlaps with temp regs!` | `IniHandler.cpp:3105` |
| 正则编译失败 | `WARNING: PCRE2 regex compilation failed at offset` | `ShaderRegex.cpp:225` |
| CustomShader 编译失败 | `Error compiling custom shader` | `CommandList.cpp:3090` |
| CustomShader 源文件找不到 | `Shader not found:` | `CommandList.cpp:3041` |
| 着色器补丁缓存过期 | `Discarding stale cached shader` | `HackerDevice.cpp:639` |
| 补丁只有 `.bin` 没有 `.txt` | `WARNING: Unable to validate timestamp of` | `HackerDevice.cpp:623` |
| 节名写错 | `Unknown section type` | `IniHandler.cpp:508` |
| 节里出现重复键 | `Duplicate key found:` | `IniHandler.cpp:617` |
| 重名节 | `Duplicate section found` | `IniHandler.cpp:481` |
| 键写在了节外 | `Entry outside of section` | `IniHandler.cpp:568` |
| INI 不是 UTF-8 | `INI file is UTF-16 encoded, not UTF-8` / `Invalid UTF-8 in` | `IniHandler.cpp:861`、`:872` |
| 同一个文件被重复包含 | `File included multiple times` | `IniHandler.cpp:1883` |

---

## 8. 排错顺序

1. **确认 DLL 真的加载了**：看 `d3d11_log.txt` 是否生成。模板默认 `hunting = 0`，没有定位面板不代表没加载（见[安装](../usage/dx11/index.md)）。
2. **确认节被解析**：日志里搜 `[节名]`。出现 `Unknown section type` 说明节名前缀拼错了。
3. **确认 Hash 命中**：用[定位与导出](../usage/dx11/hunting.md)拿到目标 Hash；`[ShaderOverride*]` 用 16 位、`[TextureOverride*]` 用 8 位，不要混用。
4. **确认匹配到了而不是解析到了**：`filter_index` 返回 `0` 说明"绑定了资源但没有命中规则"；返回 `-0.0` 说明槽位根本没绑资源。
5. **确认执行到了**：在命令列表里加一行 `x = 1`，再用日志确认。`x` 是 IniParams 分量，着色器侧读 `IniParams[0].x`；`asint()` 之类是 HLSL 函数，写在着色器里而不是 INI 里，见 [1.6](#16-命令表与表达式速查)。
6. **确认作用时机**：`handling = skip` 只在绘制前有效；`post` 命令在绘制后执行。
7. **确认冲突**：日志搜 `Possible Mod Conflict`。
8. **确认缓存**：改动 Regex 配置后旧 ShaderRegex 缓存会被自动拒绝；ShaderFixes 的 `.bin` 需要和 `.txt` 时间戳一致。不要为了排错删掉手写的 ShaderFixes 文件。

---

## 9. 源码索引

| 主题 | 文件 | 关键位置 |
| --- | --- | --- |
| 节登记表（哪些节是命令列表、哪些是精确匹配） | `src/DirectX11/IniHandler.cpp` | `:96-133`、`:195-219` |
| 命名空间与节名改写 | `src/DirectX11/IniHandler.cpp` | `:319-328`、`:422-513`、`:540-566` |
| 递归加载与排除模式 | `src/DirectX11/IniHandler.cpp` | `:916-938`、`:993-1045`、`:1850`、`:1888-1907` |
| 加载顺序 | `src/DirectX11/IniHandler.cpp` | `:5397-5440` |
| 配置重载 | `src/DirectX11/IniHandler.cpp` / `src/DirectX11/HackerDXGI.cpp` | `IniHandler.cpp:4704-4712`、`IniHandler.cpp:5697-5777` / `HackerDXGI.cpp:249-250` |
| Hash 解析 | `src/DirectX11/IniHandler.cpp` | `:1589-1611` |
| ShaderOverride 解析 | `src/DirectX11/IniHandler.cpp` | `:2828-2983` |
| ShaderOverride 消费 | `src/DirectX11/HackerContext.cpp` | `:871-883`、`:1063-1078`、`:524-576` |
| TextureOverride 解析 | `src/DirectX11/IniHandler.cpp` | `:3307-3364`、`:3502-3604`、`:3690-3938` |
| 模糊数值表达式 | `src/DirectX11/IniHandler.cpp` | `:3366-3407`、`:3409-3500` |
| TextureOverride 匹配与排序 | `src/DirectX11/ResourceHash.cpp` | `:1578-1658`、`:1660-1832`、`:1834-1953`、`:2125-2141` |
| 绘制上下文匹配 | `src/DirectX11/ResourceHash.cpp` | `:1834-1856` |
| 资源创建期覆盖 | `src/DirectX11/HackerDevice.cpp` | `:1939-2050`、`:2062-2099` |
| `iteration` 判定 | `src/DirectX11/HackerDevice.cpp` | `:1914-1930` |
| `filter_index` 求值 | `src/DirectX11/CommandList.cpp` | `:3708-3754` |
| `CheckTextureOverride` 执行 | `src/DirectX11/CommandList.cpp` | `:1894-1964` |
| dHash 感知匹配 | `src/DirectX11/TextureMatch.cpp` / `src/DirectX11/CommandList.cpp` | `:108-177` / `:8693` / `:9630-9651` |
| ShaderRegex 解析 | `src/DirectX11/IniHandler.cpp` | `:3030-3305` |
| ShaderRegex 运行时 | `src/DirectX11/ShaderRegex.cpp` | `:208-435`、`:437-563`、`:565-776`、`:887-935` |
| ShaderRegex 延迟替换 | `src/DirectX11/HackerContext.cpp` | `:591-767`、`:769-799` |
| CustomShader 解析 | `src/DirectX11/IniHandler.cpp` | `:4070-4520`、`:4523-4652` |
| CustomShader 编译与缓存 | `src/DirectX11/CommandList.cpp` | `:2899-2937`、`:2948-3113` |
| CustomShader 执行 | `src/DirectX11/CommandList.cpp` | `:3441-3520` |
| 着色器替换查找顺序 | `src/DirectX11/HackerDevice.cpp` | `:1146-1191`、`:1209-1279` |
| `.bin` / `.txt` 缓存与时间戳 | `src/DirectX11/HackerDevice.cpp` | `:568-686`、`:692-800` |
| ShaderFixes 实时重载 | `src/DirectX11/Hunting.cpp` | `:1238-1284` |
| 绘制期着色器 Override 结构 | `src/DirectX11/globals.h` | `:258-277` |
