# DX12 配置参考（`d3dx.ini`）

适用对象：使用 `d3d12.dll` 代理的 DX12 游戏使用者、Mod 作者，以及本仓库的 DX12 维护者。

本文按**当前源码的注册、解析与实际消费路径**逐项核对 DX12 运行时。DX11 的同名字段不能直接照搬；差异集中列在 [与 DX11 的差异](#10-与-dx11-的差异) 和 [失效条件与已知限制](#11-失效条件与已知限制)。

> 证据口径：下文所有"默认值 / 取值范围 / 失败路径"均来自源码静态阅读，每节末尾给出对应的源码位置。
> **本轮未启动游戏、未执行运行时回归**，因此文中不把任何示例描述为"已验证生效"；示例只说明语法与解析规则。

相关入口文档：[安装](../usage/dx12/index.md)、[INI 与命名空间](../usage/dx12/configuration.md)、[按键与变量](../usage/dx12/keys.md)、[迁移范围](../usage/dx12/compatibility.md)。

---

## 目录

1. [安装与加载](#1-安装与加载)
2. [配置文件定位与读取](#2-配置文件定位与读取)
3. [`[Include]` 语义](#3-include-语义)
4. [命名空间](#4-命名空间)
5. [INI 自定义入口：节的注册与消费](#5-ini-自定义入口节的注册与消费)
6. [重载语义](#6-重载语义)
7. [日志与诊断](#7-日志与诊断)
8. [用户设置与持久化](#8-用户设置与持久化)
9. [键绑定与预设](#9-键绑定与预设)
10. [与 DX11 的差异](#10-与-dx11-的差异)
11. [失效条件与已知限制](#11-失效条件与已知限制)
12. [源码依据索引](#12-源码依据索引)

---

## 1. 安装与加载

### 1.1 部署文件

| 文件 / 目录 | 作用 | 位置来源 |
| --- | --- | --- |
| `d3d12.dll` | 代理 DLL，注入并转发到系统 `d3d12.dll` | 由加载器放置 |
| `d3dx.ini` | 配置入口 | 进程当前工作目录优先，其次 DLL 所在目录 |
| `Mods/` | 递归加载目录（`include_recursive`） | 相对 `d3dx.ini` 所在目录 |
| `ShaderFixes/` | 内置着色器修复目录 | `<d3dx.ini 所在目录>\ShaderFixes`（由 `SetBasePaths` 推导） |
| `d3dx_user.ini` | 持久化文件 | 默认名；可用 `[Include] user_config` 改名，相对根 `d3dx.ini` 目录 |
| `d3d12_log.txt` | 诊断日志 | **仅诊断/TIMING 构建**；位于 DLL 模块所在目录 |

配置模板见 [`config/d3dx.ini`](../config/d3dx.ini)（该文件是 DX12 专用模板，包含 DX12 可用的 `[Hunting]` 快捷键）。

`[Preset*]` 仍待实现，见 §9.5。

模板中的 `Key = RBUTTON` 与 `Key = XB_LEFT_TRIGGER` 可共用一个 `hold` 设置。

### 1.2 进程门控（Process Gate）

`d3d12.dll` 可能被全局 CBT Hook 注入到启动器、Shell 等无关进程。`D3D12ProcessGateUtils::Evaluate()` 在 `DllMain(DLL_PROCESS_ATTACH)` 里先判定角色，**只有 `target` 角色才会初始化 D3D12 运行时**。

判定输入：

- 模块路径 = 本 DLL 的完整路径；进程路径 = 主可执行文件的完整路径。
- 门控读的配置是 **`<DLL 模块所在目录>\d3dx.ini`**（不是当前工作目录那份），且只解析 `[Loader]` 的 `target` 与 `loader` 两个键（字节级 lite 解析器 `src/common/ini_parser_lite.cpp`）。

判定规则：

| 条件 | 前置条件 | 结果 | 角色 |
| --- | --- | --- | --- |
| 进程完整路径以 `target` 结尾（忽略大小写） | `[Loader]` 中存在非空 `target` | 允许 | `target`：初始化 D3D12 钩子与运行时 |
| 进程名（不含路径）等于 `loader` | `[Loader]` 中存在非空 `target` **或** `loader` | 允许 | `bootstrap`：**不**初始化 D3D12 钩子 |
| 进程与模块同目录，且进程名为 `Run.exe` | `[Loader]` 中存在非空 `target` **或** `loader` | 允许 | `bootstrap` |
| 进程与模块同目录 | `[Loader]` 中**没有**可用的 `target` / `loader` | 允许 | `target` |
| 其它 | — | 拒绝（`reject`） | — |

第 2、3 行只在 `[Loader]` 段里存在非空 `target` 或 `loader` 时才适用（这是 `ReadLoaderConfig` 返回 true 的条件）。**没有**可用 target/loader 时走的是 fallback 分支：与模块同目录一律按 `target` 处理——**包含进程名为 `Run.exe` 的情况**，它会完整初始化 D3D12 钩子，而不是只做 bootstrap。

门控用的 lite 解析器会剥掉 `target` / `loader` 的首尾双引号（`TrimQuotes`），与主 INI 解析器不同（主解析器不去引号，见 §2.3）。因此 `target = "Game.exe"` 对门控有效。

`target` 的匹配是**路径后缀匹配**：`target = Game.exe` 可匹配 `C:\Games\Foo\Game.exe`；`target` 以 `\` 或 `:` 开头时按绝对路径比较。

判定结果通过 `OutputDebugStringW` 输出，形如：

```text
[Bunny-D3D12Gate] allow role=target: pid=1234 process=... module=... target=... loader=... config=... reason=...
```

### 1.3 `[Loader]` 段的两条读取路径

`[Loader]` 在 DX12 里有**两条互相独立**的读取路径，作用面差别很大：

| 路径 | 读取者 | 来源文件 | 读取的键 |
| --- | --- | --- | --- |
| 门控 | `D3D12ProcessGateUtils::ReadLoaderConfig` | `<模块目录>\d3dx.ini` | `target`、`loader` |
| 配置 | `MigotoConfig::ReadLoaderConfig` | 最终解析出的配置路径（CWD 优先） | `target`、`module`、`entry_point`、`launch`、`working_dir`、`working_directory`、`check_version`、`require_admin`、`wait_for_target`、`hook_proc`、`delay` |

配置路径读取的默认值：

| 键 | 默认值 |
| --- | --- |
| `entry_point` | `CBTProc` |
| `check_version` | `true` |
| `require_admin` | `false` |
| `wait_for_target` | `true` |
| `hook_proc` | `5` |
| `delay` | `0` |
| `target` / `module` / `launch` / `working_dir` / `working_directory` | 空字符串 |

**关键差异**：DX12 运行时不消费 `MigotoConfig::Loader()` 的任何字段（仓库内没有 `.Loader()` 的调用点）。`launch`、`require_admin`、`delay`、`module`、`entry_point` 等只是对**外部加载器**行为的描述，在 DX12 侧不产生任何行为。DX12 的 `CBTProc` 只是一个直通占位：

```cpp
extern "C" LRESULT CALLBACK CBTProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
```

即：**DX12 不依赖 CBT Hook 完成初始化**，与 DX11 中 `CBTProc` 承担初始化职责的做法不同。

### 1.4 代理与真实 `d3d12.dll`

- 真实库从 **`%SystemRoot%\System32\d3d12.dll`** 加载（`GetSystemDirectoryW` + `d3d12.dll`），不是从游戏目录，也不是从本代理所在目录。
- 代理转发 `D3D12CreateDevice`、`D3D12GetDebugInterface`、`D3D12GetInterface`、`D3D12SerializeRootSignature` 等导出到真实库。
- `RedirectMainD3D12Imports()` 会改写**主可执行文件** IAT 中指向系统 `d3d12.dll` 的三个创建入口（按名称或序号 `101` / `102` / `110`）。若该 IAT 槽位已被其它 Hook 占用，则**保留对方的 Hook** 并记录 `foreign_hook_preserved`，不覆盖；改写成功会记录 `installed`。

### 1.5 启动顺序

1. `DllMain(DLL_PROCESS_ATTACH)` → 门控判定。
2. `target` 角色 → `D3D12ProxyRuntime::Initialize()` → `DX12Runtime::Initialize()`：
   - 定位并 `Config.Load()` 读取 `d3dx.ini`。**读取失败不阻断进程启动**，只记录 `DX12Config status=missing_or_invalid`。
   - 读取成功 → `DX12ModScheduleInitialLoad()`：登记路径，把首次 Mod 加载**推迟**。
   - 状态提示：`DX12 mod load deferred until startup reload`。
3. 首次 Mod 加载在 Present 边界触发，条件为 `present > 2` 且 `elapsedMs >= 2000`（`kDeferredInitialModLoadDelayMs = 2000`）。不满足则下一帧重试；加载失败状态回到"待加载"，继续重试。

> 源码依据：`src/DirectX12/Proxy/D3D12Proxy.cpp`、`src/DirectX12/Proxy/D3D12ProcessGate.h`、`src/DirectX12/Resources/DX12RuntimeControl.h`、`src/DirectX12/Config/DX12ConfigLoader.h`、`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp`、`src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp`（`SetBasePaths`）、`src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h`（`kDeferredInitialModLoadDelayMs`）。

---

## 2. 配置文件定位与读取

### 2.1 `d3dx.ini` 查找顺序

`MigotoConfigLocator::FindDefaultConfigPath()`：

1. **进程当前工作目录**下的 `d3dx.ini`（`GetCurrentDirectoryW` + `d3dx.ini`）；
2. **DLL 所在目录**下的 `d3dx.ini`（`GetModuleFileNameW` + `d3dx.ini`）；
3. 两者都不存在 → 返回字面量 `d3dx.ini`，即相对当前工作目录。

**易踩的坑**：门控读的是**模块目录**那份 `d3dx.ini`（§1.2），配置读的是**当前工作目录优先**那份。两者不是同一文件时，会出现"按 A 文件判角色、按 B 文件取配置"的错配。排查配置无效时，先确认这两处是否存在不同的 `d3dx.ini`。

### 2.2 编码

| 场景 | 处理 |
| --- | --- |
| 根文件 | `IniDocument::LoadFromFile`：剥离 UTF-8 BOM；以 `CP_UTF8` + `MB_ERR_INVALID_CHARS` 解码，失败回退 `CP_ACP` |
| 被 include 的文件 | `MigotoIniLoaderPath::ReadFileText`：同样的 UTF-8 → ACP 回退 |
| 行首 | 第 1 行的 `\xfeff` 与 UTF-8 BOM 字节序列也会被去掉 |

GBK / ANSI 编码的 ini 可以读，但其中的非 ASCII 路径与变量名依赖系统 ACP，跨机器不可移植；建议统一用 UTF-8（不带 BOM 亦可）。

### 2.3 语法规则

| 规则 | 行为 |
| --- | --- |
| 注释 | trim 后以 `;` 或 `#` 开头的整行忽略 |
| 空行 | 忽略 |
| 节 | `[...]`，首尾空白被 trim；**节名比较大小写不敏感** |
| 键 | `key = value`；键名比较大小写不敏感；值两侧空白被 trim |
| 重复键 | **同一节内最后一个生效**（`GetString` 反向遍历取首个命中） |
| 无 `=` 的行 | 节内保留为 `hasEquals=false` 的条目；节外视为错误 |
| 节外条目 | 只有 `namespace = ...`（设置命名空间）与 `condition = ...`（忽略）被容忍；其它一律解析失败，错误文本 `Ini entry found before first section` |
| 布尔值 | `1` / `true` / `yes` / `on` → true；`0` / `false` / `no` / `off` → false；**其它值解析失败**（`GetBool` 返回 false），调用方保留原默认值，而不是当作 false |
| 整数值 | `wcstol(base = 0)`：`0x` 前缀为十六进制、`0` 前缀为八进制、其余十进制；**尾部有非空白字符则解析失败**（例如 `10px` 失败） |
| 引号 | **不做引号剥离**。值只 trim 两端空白，引号按字面保留（`name = "foo"` 读出的是 `"foo"`）。唯一例外是进程门控的 lite 解析器，它会剥掉 `[Loader] target` / `loader` 的首尾双引号，见 §1.2 |

### 2.4 路径长度限制

所有路径拼接都基于 `MAX_PATH` 的 API（`PathAppendW` / `PathRemoveFileSpecW` 等）：

- 根配置路径长度 ≥ `MAX_PATH` → 错误 `config path rejected: path too long`，**整次加载不进行**。
- 子路径（include / 资源）溢出 → 该条目被拒绝并记 warning，不影响其它文件。
- 资源文件名溢出 → `DX12ResourcePathRejected`，该资源加载失败（不回退到内联 `data`）。

> 源码依据：`src/DirectX12/Config/DX12ConfigLoader.h`（`FindDefaultConfigPath`、`ReadFileText`、`JoinPath`、`CopyPathBounded`）、`src/DirectX12/Config/DX12Config.h`（`IniDocument::LoadFromFile` / `Parse` / `AppendParse` / `GetString` / `GetBool` / `GetInt`）。

---

## 3. `[Include]` 语义

### 3.1 四个键

| 键 | 可重复 | 路径基准 | 说明 |
| --- | --- | --- | --- |
| `include` | 是 | **声明该键的文件所在目录** | 加载指定 `.ini` 文件 |
| `include_recursive` | 是 | 同上 | 递归加载目录下所有 `.ini` |
| `exclude_recursive` | 是 | — | 递归排除规则 |
| `user_config` | 取第一个非空值 | **根 `d3dx.ini` 所在目录** | 持久化文件，默认 `d3dx_user.ini` |

`include` / `include_recursive` / `exclude_recursive` 在**任何**以 `Include` 开头的节里都会被识别（大小写不敏感的前缀匹配），因此 `include_recursive` 写在子节里也有效。

### 3.2 递归加载顺序

对 `include_recursive` 指定的目录：

1. 先收集该目录下的 `.ini` 文件（后缀忽略大小写），按文件名忽略大小写排序后依次加载；
2. 再收集子目录，按目录名忽略大小写排序后逐个递归。

即**父目录的文件先于子目录的文件**被加载。跨文件的覆盖关系因此依赖这个顺序。

### 3.3 排除规则

- 名称以 `disabled` 开头（忽略大小写）的**文件或目录**会被自动排除，无需 `exclude_recursive`。
- `exclude_recursive` 规则整体忽略大小写，支持两种形态：
  - **精确同名**：如 `exclude_recursive = desktop.ini`；
  - **以 `*` 结尾的前缀匹配**：如 `exclude_recursive = DISABLED*`。
- **只有以 `*` 结尾才是通配前缀**。出现在中间的 `*`（如 `a*b`）不是通配：它既不进入通配分支也不进入前缀分支，只能匹配名字**恰好为 `a*b`** 的条目，不会排除 `abc` 或 `aFoo`。
- 规则恰好是 `*` 时，剥掉 `*` 后前缀为空，会匹配所有名称——等于排除该目录下的全部条目。

### 3.4 失败路径

| 情况 | 级别 | 日志文本 |
| --- | --- | --- |
| `include` 指向的文件不存在 | **error** | `unable to load ini: <path>` |
| `include` 指向的文件存在但解析失败 | **error** | `unable to parse ini: <path> (<原因>)` |
| `include_recursive` 目录不存在 | warning | `include_recursive path not found: <dir>` |
| 同一文件被 include 两次 | warning | `ini included multiple times: <path>` |
| 路径超长 | warning | `include path rejected: path too long: ...` / `include_recursive path rejected: path too long: ...` |
| `user_config` 已被某个 include 加载过 | warning | `user_config already loaded by an include: <path>` |
| `user_config` 文件不存在 | — | **静默**（首次运行的正常情况） |

注意 `include` 与 `include_recursive` 的失败级别不同：**文件缺失是 error，目录缺失只是 warning**。前者会让重载后的状态提示变成 "reloaded with N error(s)"。

### 3.5 `user_config` 的加载时机与优先级

- `user_config` 在**所有其它 ini 之后**加载 → 它的赋值覆盖其它文件声明的默认值。
- 路径解析：`[Include] user_config` 的值相对**根 `d3dx.ini` 所在目录**解析（不是声明它的文件所在目录）。已是绝对路径则原样使用（`C:\...`、`\\server\...`、`\...`、`/...`）。
- 文件不存在 → 静默跳过（这是首次运行的正常路径，不是错误）。
- 文件存在但读不了或解析不了 → 记 error。这样做是因为静默忽略会让运行时回退到 ini 默认值，而下一次保存就会覆盖用户的设置。
- 该路径同时是**保存目标**：`DX12UserConfigSave::RefreshTarget()` 在每次配置加载后刷新，所以改 `user_config` 的值在下一次重载后即生效，不需要重启游戏。

> 源码依据：`src/DirectX12/Config/DX12ConfigLoader.h`（`CollectIncludeEntries`、`LoadDirectoryRecursive`、`LoadFileRecursive`、`LoadUserConfigFile`、`MigotoUserConfigPath`）。

---

## 4. 命名空间

### 4.1 声明方式

`namespace = <名字>` 必须出现在该文件的**第一个节之前**：

```ini
namespace = Example

[Constants]
global $enabled = 1

[CommandListUpdate]
$enabled = 1
```

节外的 `namespace` 键被解析器特殊容忍；写在任何节**之后**则不再是命名空间声明，会被当成普通条目处理。

### 4.2 哪些节会被命名空间化

**前缀型节**（改名为 `前缀\<命名空间>\<原名>`）：

`TextureOverride`、`CommandList`、`ShaderOverride`、`CustomShader`、`ShaderRegex`、`BuiltInCommandList`、`BuiltInCustomShader`、`Pool`、`Resource`、`Key`、`Include`、`Preset`。

**精确匹配型节**（保持原名，多个文件合并进同一节）：

`Constants`、`Present`、`ClearRenderTargetView`、`ClearDepthStencilView`、`ClearUnorderedAccessViewUint`、`ClearUnorderedAccessViewFloat`、`Hunting`、`Logging`、`System`、`Device`、`Rendering`、`Loader`、`Profile`、`Stereo`、`ConvergenceMap`。

这一区分是刻意的：`[Constants]` 与 `[Present]` 必须是全局共享入口，否则 Mod 文件无法贡献变量声明与逐帧命令。

### 4.3 跨命名空间引用

```ini
; 文件 A（namespace = Example）
[CommandListUpdate]
$enabled = 1
```

其它文件调用：

```ini
run = CommandList\Example\Update   ; 显式全名
run = Update                        ; 隐式：解析为当前命名空间内的 CommandList
```

解析规则：

1. 引用以 `前缀\` 开头 → 原样使用（绝对引用）。
2. 引用以 `前缀` 开头（但不是 `前缀\`）→ 加上当前命名空间。
3. 其它 → 视为 `前缀 + 引用`，加上当前命名空间。

### 4.4 隐式命名空间

`include` / `include_recursive` 加载的文件，其隐式命名空间是**该文件相对根 `d3dx.ini` 目录的相对路径**（含文件名与 `.ini` 后缀）。例如根目录下的 `Mods\Sub\foo.ini` → 隐式命名空间 `Mods\Sub\foo.ini`，其 `[CommandListBar]` 变成：

```text
CommandList\Mods\Sub\foo.ini\Bar
```

根 `d3dx.ini` 自身的命名空间为空，其 `[CommandListFoo]` 就是 `CommandListFoo`。

**这意味着**：从根 `d3dx.ini` 里用 `run = CommandListFoo` 调用某个被 include 文件里的命令列表**不会命中**（它会被解析成 `CommandListFoo`，而目标叫 `CommandList\Mods\Sub\foo.ini\Bar`）。要跨文件调用，必须写完整名，或在目标文件里显式写 `namespace = X` 后用 `run = CommandList\X\Bar`。

`user_config` 文件的隐式命名空间是它的**完整路径**；由于它主要承载 `[Constants]`（精确匹配节），这不影响持久值生效。

### 4.5 变量与按键的命名空间

按键条件和变量赋值按各自 INI 条目的命名空间解析。`$swapkey0` 优先读取当前作用域及当前命名空间中的声明，然后回退到根变量。`$\LiuYin\swapkey0` 直接访问指定命名空间的全局变量，多个 Mod 可以各自声明同名选项。

持久变量以完整名称写入用户设置文件。读取旧版 DX12 的短名称时，优先恢复根变量。没有对应根变量时才恢复同名的可写持久变量。带 `locked` 的声明跳过保存和恢复。

---

## 5. INI 自定义入口：节的注册与消费

"节名被识别"不等于"该节的功能被实现"。下表按当前源码逐项列出：

### 5.1 实际被解析并执行的节

| 节 | 解析器 | 运行时用途 |
| --- | --- | --- |
| `[Constants]` | 命令列表解析器 | 变量声明与初始化、persist 声明（`DX12ConstantsVmUtils::Initialize`） |
| `[Present]` | 命令列表解析器 | 每帧命令（`CommandListHasRuntimeEffect(L"Present", ...)`） |
| `[CommandList*]` | 命令列表解析器 | 可复用命令列表 |
| `[BuiltInCommandList*]` | 命令列表解析器 | 同上 |
| `[CustomShader*]` | 命令列表解析器 + `DX12CustomShaderElementParser::BuildRegistry` | 自定义着色器 |
| `[BuiltInCustomShader*]` | 同上 | 同上 |
| `[Key*]` | `MigotoKeyBindingUtils::ParseSections` | 按键绑定（§9） |
| `[Resource*]` | `MigotoResourceParser::ParseSections` | 资源声明 |
| `[TextureOverride*]` | `MigotoTextureOverrideParser::ParseSections` | 资源 / 绘制匹配与替换 |
| `[ShaderOverride*]` | `MigotoShaderOverrideParser::ParseSections` | 着色器 Hash 匹配 |
| `[ShaderRegex*]` | `MigotoShaderRegexParser::ParseSections` | 着色器模式匹配 |

自定义着色器元素是从**同一份命令列表**重建出来的（`BuildRegistry`），因此注册表与命令体不会不一致。

### 5.2 只登记前缀、**没有**消费者的节

| 节 | 说明 |
| --- | --- |
| `[Preset*]` | DX12 **没有实现预设**。它只在前缀表里登记（因此会被命名空间化），没有任何解析器消费 |
| `[Pool*]` | DX12 没有 Pool 实现 |
| `[Hunting]`、`[System]` | 全局配置。Hunting 读取已接入的快捷键和重复速率，System 包含未知设置清理等选项 |
| `[Logging]`、`[Device]`、`[Rendering]`、`[Profile]`、`[Stereo]`、`[ConvergenceMap]` | 精确匹配节，保持全局但无消费者 |
| `[ClearRenderTargetView]`、`[ClearDepthStencilView]`、`[ClearUnorderedAccessViewUint]`、`[ClearUnorderedAccessViewFloat]` | 同上 |

注意 `[Constants]` 与 `[Present]` 被纳入命令列表解析器的判定集合（`IsExplicitCommandListSection`），而 `[Clear*]` 四个节**不在**其中——即使它们在命名空间前缀表里与 `[Constants]` 并列。

### 5.3 `[Loader]`

见 §1.3。DX12 运行时只从中真正使用的是门控读取的 `target` / `loader`（且读的是模块目录那份文件）。

### 5.4 `[Include]`

见 §3。

> 源码依据：`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp`（`DX12ModRuntimeLoad` 中的六次 `ParseSections` 调用）、`src/common/MigotoIniCommandListUtils.hpp`（`IsExplicitCommandListSection`）、`src/DirectX12/Config/DX12Config.h`（各 `ParseSections` 的节名前缀判定）。

---

## 6. 重载语义

### 6.1 触发方式

- `F10` → `DX12UiActions::PollKeyboard` 中的 `VK_F10` → `DX12UiActionBits::ModReload` → `DX12Input::RequestModReload()`。
- **全局修饰键门控**：Ctrl / Shift / Alt / Win 任一按下时，**所有**内置热键都不触发。
- **重入保护**：`DX12Input::ReloadInProgress` 用 `InterlockedCompareExchange` 保证同时只有一个重载。重载中再按 F10 只记 `status=skipped reason=already_running`，overlay 显示 "Reload already running"。
- **同步执行**：`DX12Input::CompleteReload()` 在调用线程（Present 边界）上直接调用 `DX12ModRuntimeReload()`，不是后台线程。重载期间该帧的 Present 会被拉长。

### 6.2 重载做了什么

1. 读取当前 `gConfigPath`；为空 → `status=skipped reason=empty_config_path`，返回失败。
2. **先保存持久设置**：`DX12UserConfigSave::Save("reload")`。这与 DX11 `ReloadConfig()` 先调 `SavePersistentSettings()` 的顺序一致——本次会话里改过的热键状态会先落盘，再被本次重载读回。
3. `DX12ModRuntimeLoad(configPath)`：
   - `SetBasePaths`：更新 `gConfigPath`、`gBaseDir`、`<base>\ShaderFixes`；
   - 重新执行完整的 include 解析（§3）与命名空间处理（§4）；
   - 重新解析 6 类节（§5.1）；
   - `DX12ConstantsVmUtils::Initialize`：清空全局变量表 → 重放 `[Constants]` → 重建 persist 注册表 → 清除脏标记；
   - 在 `gModLock` 写锁下整体交换各张运行时表；
   - `++gReloadGeneration`；
   - 清理：已加载资源、ShaderOverride PSO 匹配缓存、pre-skin 各级缓存、静态纹理 / IA 替换表快照、命令执行快照、pre-skin 的粘性匹配缓存。
4. `DX12UserConfigSave::ResetAutosaveTimer()`：自动保存计时器归零 → 下次变更立即保存。
5. 状态提示文本：

| 条件 | 提示 |
| --- | --- |
| 有 ini 错误 | `F10 DX12 mod config reloaded with N error(s)`（Error 级别） |
| 无错误但有 warning | `F10 DX12 mod config reloaded with N warning(s)`（Warning 级别） |
| 无错误无 warning | `F10 DX12 mod config reloaded: N ini, N TextureOverride, N Resource` |

### 6.3 重载**不**做什么

以下内容在进程生命周期内固定，**改配置无效，必须重启游戏**：

- 代理与钩子本身（DXGI / D3D12 wrapper、导入表重定向、dbghelp 离线保护）；
- D3D12 设备与交换链；
- 进程门控判定的角色（`target` / `bootstrap`），它在 `DllMain` 时就定死了；
- DLL 所在目录，进而是日志文件的位置；
- 已部署的 `d3d12.dll` 本身。

### 6.4 启动时的首次加载

首次加载不是同步的。`DX12ModScheduleInitialLoad()` 只登记路径与时间戳；`DX12ModMaybeRunDeferredInitialLoad()` 在 Present 边界等到 **`present > 2` 且 `elapsedMs >= 2000 ms`** 才真正执行一次 `DX12ModRuntimeReload()`（走同一条"保存 → 加载"路径）。加载失败时状态回退，下一帧重试。

因此**刚进游戏时配置还没生效是正常现象**：等 2 秒且过了前几帧之后才会加载。状态提示在加载完成前显示 `DX12 startup reload running`。

### 6.5 重载后的资源与着色器

- `[Resource*]` 的文件内容在加载阶段读取；编辑资源文件后需要 F10 才会重新读。
- 自定义着色器的字节码缓存按文件时间戳校验，编辑着色器文件后 F10 会重新编译。

> 源码依据：`src/DirectX12/Runtime/DX12Input.h`、`src/DirectX12/Resources/DX12RuntimeControl.h`、`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp`（`DX12ModRuntimeReload` / `DX12ModRuntimeLoad`）。

---

## 7. 日志与诊断

### 7.1 何时会有日志文件

**只有诊断 / TIMING 构建会写文件日志。** `DX12DiagLog::Open()` 在非 `DX12_TIMING_BUILD` 下直接返回成功，`sLog` 为空指针，于是所有日志写入路径都是空操作：Release 构建既不创建 `d3d12_log.txt`，也不创建写日志的线程。

### 7.2 日志位置与轮转

- 路径：`<DLL 模块所在目录>\d3d12_log.txt`。取自 `GetModuleFileNameW(DX12RuntimeState::GetModule())` 去掉文件名后拼接。**它跟着 DLL 走，不跟着 `d3dx.ini` 走。**
- 轮转：启动时若同目录已存在 `d3d12_log.txt`，先复制一份 `d3d12_log_<GetTickCount64()>.txt`，再以 `"w"` 模式新建当前日志。轮转是尽力而为的，任一路径构造失败就跳过复制，不触碰被截断的路径。

### 7.3 常用事件标签

| 标签 | 含义 |
| --- | --- |
| `DX12Bootstrap` | 代理 attach（模块路径、进程路径、判定原因） |
| `DX12ImportRedirect` | 主程序 IAT 重定向结果：`installed` / `foreign_hook_preserved` / `concurrent_hook_preserved` / `protect_failed` / `complete`（`complete` 带改写计数与作用范围） |
| `DX12Config` | 配置路径；`status=missing_or_invalid` 与错误信息 |
| `DX12ModRuntime` | 一次加载 / 重载的汇总：`iniFiles`、`errors`、`warnings`、各类节数量、`activeShaderOverrides` 等布尔量、`generation` |
| `DX12ModRuntimeIni` | 每个被加载的 ini 文件路径；每条 error / warning 原文 |
| `DX12ModRuntimeReload` | `begin` / `skipped`（`empty_config_path`、`already_running`）/ `begin_sync` / 完成耗时 |
| `DX12ModRuntimeDeferredLoad` | 启动延迟加载的 `scheduled` / `begin` / `done`（含 `present` 与 `elapsedMs`） |
| `DX12UserConfig` | 持久化保存的 `status=saved`（含 `reason`：`autosave` / `reload` / `exit`）与 `status=save_failed`（含失败阶段） |
| `DX12KeyDiag` | 仅 TIMING 构建：每个注册的按键绑定（节名、原始节名、按键、VK、类型、条件文本） |
| `DX12CustomShaderDiag` | 仅 TIMING 构建：每个注册的自定义着色器元素（各阶段文件、混合、剔除、拓扑等） |
| `DX12ResourcePathRejected` / `DX12ResourceLoad` / `DX12FallbackPath` | 资源路径超长、加载失败、根目录回退 |

排查配置问题时，`DX12ModRuntime` + `DX12ModRuntimeIni` 两条基本能定位到"哪个文件没加载上、为什么"。

崩溃相关的自诊断另见 [DX12-崩溃自诊断](./DX12-崩溃自诊断.md)。

> 源码依据：`src/DirectX12/Core/Diag/DX12Diag.h`（`DX12DiagLog::Open` / `Close`）、`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp`（各类 `DX12_LOG`）。

---

## 8. 用户设置与持久化

### 8.1 声明

```ini
[Constants]
global persist $some_persistent_variable = 1
```

- 可用前缀：`global persist $` 或 `persist $`（大小写不敏感）。
- `global ` 单独、`local ` **不**表示持久。
- 命名空间内的变量以完整名称写回文件，旧版短名称按 §4.5 恢复。

### 8.2 注册表与脏标记

- 每次配置加载后重建 persist 注册表（`DX12ConstantsVmUtils::Initialize` → `RegisterPersistedVariables`）。重复声明保留**第一个**。
- 只有被登记为 persist 的变量写入且**值发生变化**时，才把 persist 标记为待保存（`MarkPersistedWriteLocked`）。

### 8.3 写回时机

| 时机 | 触发点 | `reason` |
| --- | --- | --- |
| 自动保存 | Present 边界检查 | `autosave` |
| F10 重载前 | `DX12ModRuntimeReload()` | `reload` |
| 进程退出 | `DllMain(DLL_PROCESS_DETACH)` | `exit` |

自动保存节律与 DX11 对齐：`AutoSaveIntervalSeconds = 60`，计时器初始为 0，所以**加载后的第一次变更会立即写盘**，之后的变更最多落后一个间隔。

> **注意**：60 秒是 DX12 侧的**硬编码常量**（对齐 DX11 `[System] settings_auto_save_interval` 的默认值）。DX12 **不读** `[System]` 段的任何键，所以无法通过 ini 调整这个间隔。

### 8.4 文件格式

与 DX11 的 `SavePersistentSettings()` 兼容，可互相读取：

```ini
; AUTOMATICALLY GENERATED FILE - DO NOT EDIT
;
; 3DMigoto will overwrite this file whenever any persistent settings are
; altered by hot key or command list. Tag global variables with the "persist"
; keyword to save them in this file. Use the post keyword in the [Constants]
; command list if you need to do any intialisation after this file is loaded.
;
[Constants]
$some_persistent_variable = 1
```

数值以 `%.9g` 输出。

### 8.5 失败路径

| 情况 | 行为 |
| --- | --- |
| 目标目录或文件名为空 | 不写 |
| 当前没有任何 persist 声明 | 不写，并清除脏标记（避免每帧重试） |
| `fopen` 失败 | 上报 `status=save_failed stage=fopen`，**保持脏标记**，下一次自动保存 / 重载 / 退出再试 |
| `fputs` / `fwprintf_s` / `ferror` / `fflush` / `fclose` 任一失败 | 上报对应失败阶段，保持脏标记 |

**该文件是整体重写**（`_wfsopen(..., L"w")`）。手工编辑 `d3dx_user.ini` 会在下一次保存时被覆盖——只应通过 ini 里的 `persist` 声明来管理这些值。

> 源码依据：`src/DirectX12/Config/DX12ConstantsVmUtils.h`、`src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h`（`DX12UserConfigSave`）、`src/common/MigotoCommandVmUtils.hpp`（`RegisterPersistedVariables`、`SavePersistedVariables`、`MarkPersistedWriteLocked`）。

---

## 9. 键绑定与预设

### 9.1 `[Key*]` 段的字段

| 字段 | 说明 |
| --- | --- |
| `key` | 主组合键 |
| `back` | 反向组合键，**只对 `type=cycle` 生效** |
| `type` | `activate`（默认）/ `hold` / `toggle` / `cycle` |
| `smart` | 循环步进前按当前变量重新定位，默认开启 |
| `wrap` | 循环到达端点后可回绕，默认开启 |
| `transition` / `release_transition` | 激活、释放时的变量过渡毫秒数，默认为 0 |
| `transition_type` / `release_transition_type` | 两个方向分别选择 `linear` 或 `cosine`，默认为 `linear` |
| `condition` | 按键条件表达式。循环可逐档指定，空项表示无条件 |
| `run` | 逗号分隔的命令列表名，按 §4.3 的命名空间规则解析 |
| `$变量` | 逗号分隔的值列表；多个变量列表按循环位置配对 |

同一个 `[Key*]` 段内的多个 `key` 共用开关状态、循环位置和暂存值。每个组合键有独立的按下与松开事件。`cycle` 也可绑定多个 `back`，同一帧先按配置顺序处理反向键，再处理正向键。`hold` 的第二个按键会再次执行激活动作，仍保留首次按下前的值。任一已按下的组合键释放都会停用该 `hold`。

### 9.2 组合键语法

- 单行组合键用空格分隔，同时满足所有条件时触发。重复写 `key` 或 `back` 提供多个独立入口。无效的一行产生警告，同段其他有效绑定继续工作。
- 键盘和鼠标支持 Windows 虚拟键名称、十六进制键码、字母数字和 `esc` 别名。`no_` 表示松开指定键，`no_modifiers` 要求松开 Ctrl、Shift、Alt 和两个 Win 键。
- XInput 支持 A/B/X/Y、十字键、Start/Back/Guide、两个肩键、左右摇杆按下和两个模拟扳机。`XB_` 指任一已连接设备，`XB1_` 至 `XB4_` 指定端口。取反时，已断开的端口始终不满足条件。
- `XB2_RIGHT_TRIGGER > 128` 从 129 起生效，默认阈值为 30。沿用 DX11 的饱和规则，阈值为 255 时完全按下也会触发。Guide 的扩展读取器不可用时，其余按钮仍能使用。

示例：

```ini
[KeyNext]
key = NO_MODIFIERS F6
run = CommandListNext

[CommandListNext]
if $variant == 0
    $variant = 1
else
    $variant = 0
endif
```

### 9.3 类型行为

| `type` | 触发时机 | 变量处理 | `run` 的命令列表 |
| --- | --- | --- | --- |
| `activate` | 按下 | 从当前值过渡到第 0 档 | 先执行 pre，本帧变量更新后执行一次 post |
| `hold` | 按下、松开 | 按下时保存目标值，松开时过渡回保存值 | 按下执行 pre，松开执行 post，随后更新恢复值 |
| `toggle` | 每次按下切换 | 开启时保存，关闭时恢复 | 开启执行 pre，关闭执行 post |
| `cycle` | 按下步进 | 过渡到该档目标值 | 该档 pre 在变量更新前执行，post 在本帧更新后执行一次 |

多个长按或开关同时修改同一个变量时，最后一次释放会恢复最初保存的值。进行中的过渡被新按键打断时，新曲线从上一次帧更新的可见值开始。智能循环按进行中过渡的目标值定位。

- `smart` 和 `wrap` 默认开启。智能匹配优先检查当前档，数值改变后再从第一档寻找匹配。设 `smart = false` 时只按位置步进。设 `wrap = false` 后，到达端点时重复朝同一方向按键，既不赋值也不重复执行命令。
- 循环的变量、`condition` 和 `run` 按逗号分档。短列表沿用最后一项，连续两个逗号可留空一档，末尾的单个逗号不增加档位。先选择位置，再求该档条件。空条件允许执行，假条件跳过该档的赋值和命令。
- 只写命令而没有待比较变量时，每档都满足智能匹配。若要第一次按下前进键就执行第一条命令，请设 `smart = false`。

### 9.4 默认快捷键与改绑

| 键 | 作用 |
| --- | --- |
| 小键盘 `0` | 切换 hunting 模式 |
| 小键盘 `1`–`9`、`/`、`*`、`-` | 导航 / 标记 PS、VS、IB、VB |
| 小键盘 + | 清除 hunting 选择 |
| 小键盘 `1` / `2` / `3` + 小键盘 `.`（Decimal） | 导航 / 标记 CS |
| `F8` | 帧分析（需先开启 hunting 模式） |
| `F9` | 着色器导出 |
| `F10` | 保存并重载 Mod 配置 |
| Ctrl + Alt + F10 | 随包绑定。清空当前用户设置并重载 |
| `F11` | 性能 HUD 循环：Off → Compact → Summary → Top → Sections → Counters |
| `F12` | 冻结 / 解冻当前 HUD 报告 |

`[Hunting]` 中可重绑 `toggle_hunting`、`done_hunting`、`analyse_frame`、`monitor_performance`、`freeze_performance_monitor`、`reload_config` 和 `wipe_user_config`。导航与标记使用 `previous_`、`next_`、`mark_` 加目标名（`vertexshader`、`pixelshader`、`computeshader`、`vertexbuffer`、`indexbuffer`）。F9 保留为 DX12 着色器导出。

空值禁用对应绑定。只要有一个键名无效，整个组合即失效。`no_modifiers` 排除 Ctrl、Alt、Shift 和左右 Win 键。小键盘小数点的规则由各自绑定中的 `VK_DECIMAL` 或 `no_VK_DECIMAL` 决定。`repeat_rate` 只控制导航，随包为每秒 6 次，0 表示按住时不重复。

Mod 的 `[Key*]` 和 Hunting 各自判断组合键。两者使用相同按键时可同时触发，安排按键时请留意随包配置占用的小键盘和功能键。

### 9.5 `[Preset*]`

DX12 **没有实现预设**。`[Preset*]` 只在前缀表里登记（因此会被命名空间化），没有任何解析器消费它。DX11 的预设（`[PresetX]` 段 + `key` / `type` / 变量）不会在 DX12 生效；需要类似效果请用 `[Key*]` + `[CommandList*]` 组合实现。

> 源码依据：`src/common/MigotoKeyBindingUtils.hpp`、`src/common/vkeys.h`、`src/DirectX12/Config/DX12HuntingHotkeys.h`、`src/DirectX12/Resources/DX12RuntimeControl.h`（`DX12UiActions::PollKeyboard`）、`src/DirectX12/Runtime/DX12Input.h`。

---

## 10. 与 DX11 的差异

| 主题 | DX11 | DX12（当前源码） |
| --- | --- | --- |
| 代理入口 | `d3dx11.dll` / `d3d11.dll` | `d3d12.dll`；真实库来自 `%SystemRoot%\System32\d3d12.dll` |
| 初始化入口 | `CBTProc` 承担初始化 | `CBTProc` 只是直通占位；初始化在 `DllMain` + 进程门控后进行 |
| 配置路径 | 由加载器决定，`d3dx.ini` 为入口 | 当前工作目录优先，其次 DLL 目录；门控另读模块目录那份 |
| `[Include]` | `include` / `include_recursive` / `exclude_recursive` / `user_config` | 同名键均可用；`include` 缺文件是 error，目录缺失是 warning |
| 递归命名空间 | 目录级 | **文件相对路径级**（含 `.ini` 后缀），跨文件引用需写全名 |
| `[Preset*]` | 有预设系统 | **未实现**（只登记前缀） |
| `[Pool*]` | 有 Pool 实现 | **未实现**（只登记前缀） |
| `[Hunting]` / `[System]` | 有完整实现 | 部分。见 §9.4 与 DX12 配置模板 |
| `[Logging]` / `[Device]` / `[Rendering]` / `[Profile]` / `[Stereo]` | 有完整实现 | DX12 仅识别这些段名 |
| `[Logging] log_level` 等 | 生效 | 不生效 |
| `[Rendering] ini_params` 等 | 生效 | 不生效；DX12 的 `x/y/z/w` 常量走 `[Constants]` / 命令列表 |
| `[System] settings_auto_save_interval` | 生效（默认 60） | 不生效；60 秒是硬编码常量 |
| 布尔值解析 | 宽松 | 非法布尔值**解析失败**，保留原默认值 |
| 整数解析 | 宽松 | 尾部残留字符 → 解析失败；支持 `0x` / 八进制 |
| 键段多 `key` 行 | 多键共享状态 | 多个键盘、鼠标、手柄组合共用同一状态 |
| `back` 键 | 仅 `type=cycle` | 同 DX11 |
| 手柄键名 | `XB_*` | 支持 `XB_*`、端口、取反和扳机阈值，见 §9.2 |
| `smart` / `wrap` | 支持 | 支持，见 §9.3 |
| 激活与释放过渡 | 支持 | 标量支持线性和余弦曲线，见 §9.3 |
| 按键输入延时 | 支持 | 待实现 |
| 持久化 | `d3dx_user.ini` | 同名同格式，兼容互读 |

更完整的迁移清单见 [迁移范围](../usage/dx12/compatibility.md)。

---

## 11. 失效条件与已知限制

1. **门控与配置可能读不同的 `d3dx.ini`**。门控读 `<DLL 目录>\d3dx.ini` 的 `[Loader] target` / `loader`；配置读当前工作目录优先的 `d3dx.ini`。两处并存且内容不同时，行为难以推断。
2. **`[Loader]` 的多数字段在 DX12 无效**。`launch`、`require_admin`、`delay`、`module`、`entry_point` 只描述外部加载器；DX12 运行时不会执行它们。
3. **全局配置支持范围依后端而异**。DX12 能读取 §9.4 中的 `[Hunting]` 快捷键和 `repeat_rate`，其他 DX11 选项请对照 DX12 模板。
4. **DX11 的资源目录 / 缓存目录 / 着色器导出等开关不生效**。DX12 的 `ShaderFixes` 目录固定为 `<d3dx.ini 所在目录>\ShaderFixes`。
5. **`[Preset*]` 与 `[Pool*]` 未实现**。写了不会报错，但也不会有任何效果。
6. **跨文件的命令列表引用需要全名**（§4.4）。隐式命名空间是文件相对路径，不是目录名。
7. **跨 Mod 访问变量**时写出命名空间，见 §4.5。
8. **区分组合键和替代按键**。一行 `key = Z XB_A` 要求同时按下两键。拆成两行 `key`，任一设备均可触发。
9. **重载不能重建钩子与设备**（§6.3）。改 `[Loader]`、换 DLL 注入方式、换设备相关行为都需要重启游戏。
10. **首次加载有 2 秒 + 3 帧的延迟**（§6.4）。刚进游戏时配置未生效属正常。
11. **日志只在诊断 / TIMING 构建存在**，且位置跟随 DLL 目录（§7.2）。Release 构建不会有 `d3d12_log.txt`。
12. **持久化文件是整体重写**（§8.5）。手工编辑会在下一次保存时被覆盖。
13. **布尔 / 整数的严格解析**（§2.3）。`enabled = maybe` 不会当成 false，而是"保留默认值"——排查时容易被误认为"值生效了"。
14. **`MAX_PATH` 上限**（§2.4）。深层目录或超长文件名会被拒绝，报 `path too long`。
15. **内置热键在按住任意修饰键时全部失效**（§9.4）。`Ctrl+F10` 不会重载。
16. **按住小键盘 `.` 会抑制其它内置热键**（§9.4）。CS 导航 / 标记需要同时按住 `.`，此时 F8–F12 与其它小键盘键全部不响应。
17. **主 INI 解析器不去引号**（§2.3）。只有进程门控的 lite 解析器会剥掉 `[Loader] target` / `loader` 的首尾双引号。
18. **`exclude_recursive` 的 `*` 只在末尾有效**（§3.3）。中间带 `*` 的模式（如 `a*b`）只能匹配名字恰好为 `a*b` 的条目。

---

## 12. 源码依据索引

| 主题 | 文件 |
| --- | --- |
| 配置定位、`[Loader]`、include 遍历、user_config 路径 | `src/DirectX12/Config/DX12ConfigLoader.h` |
| INI 词法、节命名空间、值的类型解析、节前缀表 | `src/DirectX12/Config/DX12Config.h` |
| 节前缀表（命名空间化规则） | `src/DirectX12/Config/DX12Config.h`（`kMigotoCommandListSections` / `kMigotoRegularSections`） |
| 命令列表段判定、动作关键字 | `src/common/MigotoIniCommandListUtils.hpp` |
| 键绑定解析与执行 | `src/common/MigotoKeyBindingUtils.hpp` |
| 虚拟键名表 | `src/common/vkeys.h` |
| 变量存储、persist 注册与写回 | `src/common/MigotoCommandVmUtils.hpp` |
| `[Constants]` 重放与 persist 声明 | `src/DirectX12/Config/DX12ConstantsVmUtils.h` |
| 加载 / 重载主流程、状态提示、日志事件 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` |
| 持久化保存节奏、目标刷新 | `src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h`（`DX12UserConfigSave`） |
| 基础目录与 `ShaderFixes` 目录 | `src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp`（`SetBasePaths`） |
| 代理导出、导入重定向、`CBTProc`、`DllMain` | `src/DirectX12/Proxy/D3D12Proxy.cpp` |
| 进程门控 | `src/DirectX12/Proxy/D3D12ProcessGate.h` |
| 内置热键 | `src/DirectX12/Resources/DX12RuntimeControl.h`（`DX12UiActions`） |
| 重载入口与重入保护 | `src/DirectX12/Runtime/DX12Input.h` |
| 日志文件位置与轮转 | `src/DirectX12/Core/Diag/DX12Diag.h`（`DX12DiagLog::Open`） |
| 门控用的 ini lite 解析 | `src/common/ini_parser_lite.cpp` |

---

## 相关文档

- [安装](../usage/dx12/index.md)
- [INI 与命名空间](../usage/dx12/configuration.md)
- [按键与变量](../usage/dx12/keys.md)
- [管理 Mod](../usage/dx12/mods.md)
- [迁移范围](../usage/dx12/compatibility.md)
- [故障排查](../usage/dx12/troubleshooting.md)
- [INI 语言扩展总览](./ini-language-extensions/README.md)
- [INI 语言扩展：变量](./ini-language-extensions/variables/README.md)
- [INI 语言扩展：输入](./ini-language-extensions/input/README.md)
- [INI 语言扩展：参数](./ini-language-extensions/parameters/README.md)
- [INI 语言扩展：日志](./ini-language-extensions/logging.md)
- [DX12 崩溃自诊断](./DX12-崩溃自诊断.md)
