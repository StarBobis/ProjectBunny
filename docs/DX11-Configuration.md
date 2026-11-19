# DX11 配置参考（d3dx.ini）

本文面向 ProjectBunny DX11（`d3d11.dll`）的使用者、Mod 作者与维护者，描述**当前源码实际实现**的配置行为：配置文件在哪里被查找、INI 如何读取与合并、路径与命名空间规则、`[Include]` 的递归/排除/条件、版本与日志、启动与运行时重载，以及兼容性开关。

> **证据边界**：本文结论来自对本工作树源码的阅读（基线提交 `71c11b2`），引用位置写作 `文件:行号`。本轮没有编译、没有启动游戏、没有执行配置回归测试，因此文中所有默认值、覆盖规则与失败路径都是**源码分支结论**，不是实测记录。示例 INI 是说明用的最小片段，未经游戏验证；请在自己的 Mod 目录里复制后再用。

术语：`d3dx.ini` 指主配置文件；「节」指 INI 中的 `[Section]`；「全局节」指不带命名空间的节（如 `[Present]`、`[Constants]`）；「命名空间节」指带命名空间的节（如 `[TextureOverride\Mods\a.ini\Body]`）。

## 1. 安装后的文件与配置入口

### 1.1 目录布局

以 **DLL 所在目录**（加载器/注入器部署目录）为基准，不一定是游戏根目录：

```text
d3d11.dll          ← 代理 DLL
d3dx.ini           ← 主配置（唯一被读取的配置文件）
d3dx_user.ini      ← 持久设置，运行后自动生成
d3d11_log.txt      ← 日志，log_level 非 disabled 时生成
Mods/              ← Mod 目录（由 include_recursive 指向）
ShaderFixes/       ← 着色器替换目录（由 override_directory 指向）
ShaderCache/       ← 着色器缓存目录（由 cache_directory 指向）
```

仓库中的 DX11 配置模板是 [`Dependencies/d3dx.ini`](../Dependencies/d3dx.ini)。该文件共 53814 字节，**除第 722 行注释里的一个字节外全部是 ASCII**：那一行是 `; scenarios where the game’s default buffer size is too small.`，撇号位置是一个单独的 `0x92`——按 Windows-1252 解码正好是正确的弯引号 `’`（U+2019），但它**不是合法的 UTF-8**（按 UTF-8 解码会变成 U+FFFD）。而 **DLL 的完整 INI 解析器只接受 UTF-8**（§1.6）：直接使用该模板时，第 722 行会被判为非法 UTF-8，坏字节替换为 U+FFFD，并产生 `Invalid UTF-8 in <path> at line 722; invalid bytes replaced with U+FFFD; re-save as UTF-8` 这条诊断（`src/DirectX11/IniHandler.cpp:871-873`）。该行是注释，不影响任何设置；而且这条诊断**只有在 `log_level ≥ warning`（会写进 `d3d11_log.txt`）或 `show_warnings = 1`（屏幕提示）时才可见**——按仓库模板的默认组合（`log_level = disabled`、`show_warnings = 0`）它是完全静默的（§5.2、§5.3）。**以该模板为基础做安装时，请先另存一份 UTF-8（可带 BOM）再修改**：既不会留下编码诊断，注释里的弯引号也能原样保留。用户手册入口见[安装](../usage/dx11/index.md)。

### 1.2 主配置文件的搜索规则

**只有一条搜索规则，没有多候选回退，也没有环境变量或命令行覆盖：**

```text
主配置 = <d3d11.dll 所在目录>\d3dx.ini
```

- 路径由 `GetModuleFileName(migoto_handle, ...)` 取得 DLL 自身路径后截断到目录部分再拼接 `d3dx.ini`（`src/DirectX11/IniHandler.cpp:34` 定义 `INI_FILENAME`，`src/DirectX11/IniHandler.cpp:5034-5046` 组装路径）。
- 路径过长或模块路径无法解析时，函数输出调试串并调用 `DoubleBeepExit()` 结束进程（`src/DirectX11/IniHandler.cpp:5036-5046`，`src/common/util.h:208`）。
- **游戏目录里的 `d3dx.ini` 不会被读取。** 若游戏目录存在同名文件且与 DLL 目录的文件不同，屏幕会提示一次 `Detected a conflicting d3dx.ini in the game directory that is not being used.`，并指出实际使用的配置路径（`src/DirectX11/IniHandler.cpp:4750-4769`）。该提示在日志文件打开之前发出（`src/DirectX11/IniHandler.cpp:5047` 早于 `:5053` 的 `InitializeLogging()`），因此**首次加载时只出现在屏幕上**，之后的每次重载才会同时写进日志；它不中断加载。

DLL 注入到目标进程时还有一道更早的检查：`DllMain` 用一个只依赖 Kernel32 API 与静态链接标准库的最小解析器，读取 `<DLL 目录>\d3dx.ini` 的 `[Loader]` 段来判断目标进程（`src/DirectX11/DLLMainHook.cpp:171-328`，解析器实现在 `src/common/ini_parser_lite.cpp`）。规则：

| 条件 | 结果 |
| --- | --- |
| DLL 与 exe 同目录，或 DLL 位于 exe 目录的子目录中 | 直接放行，不读 `d3dx.ini` |
| exe 是 `rundll32.exe` 且命令行含 `,Install3DMigotoDriverProfile ` | 放行（profile helper） |
| 其他情况 | 必须能在 `<DLL 目录>\d3dx.ini` 的 `[Loader]` 中匹配 `target` 或 `loader`，否则 `DllMain` 返回 `FALSE`，DLL 不进入该进程 |

因此使用外部加载器（XXMI Launcher 等）部署时，**DLL 旁边的 `d3dx.ini` 与 `[Loader] target` 是「能不能加载」的前置条件**，不是可选配置。

### 1.3 日志文件

- 路径固定为 `<DLL 目录>\d3d11_log.txt`（`src/DirectX11/IniHandler.cpp:5043`）。
- 文件在进程内**只打开一次**：首次加载时以 `"w"` 模式创建（截断既有内容），之后复用同一个 `FILE*`，重载时不会再次截断（`src/DirectX11/Logging.cpp:26-28`）。下一次启动进程会重新截断。
- 打不开文件时不会崩溃：等级被强制降为 `disabled`（`src/DirectX11/Logging.cpp:28`）。

### 1.4 持久设置文件（user_config）

- 默认 `<DLL 目录>\d3dx_user.ini`。
- 可在主配置的 `[Include]` 段用 `user_config = <路径>` 改写；相对路径以 **DLL 目录**为基准，形如 `C:\...` 的盘符路径或 `\\server\share\...` 的 UNC 路径按原样使用（`src/DirectX11/IniHandler.cpp:1802-1826`）。
- 该文件在所有其它 INI 之后解析，因此它的值会覆盖同名配置（`src/DirectX11/IniHandler.cpp:5126-5130`）。
- 文件内容由 DLL 自动生成，形如：

```ini
; AUTOMATICALLY GENERATED FILE - DO NOT EDIT
;
; 3DMigoto will overwrite this file whenever any persistent settings are
; altered by hot key or command list. Tag global variables with the "persist"
; keyword to save them in this file. ...
;
[Constants]
$my_named_variable = 1
```

写入采用「临时文件 + 原子替换」，并用独占创建避免覆盖既有文件（`src/DirectX11/IniHandler.cpp:5570-5645`）。手改该文件会在下次保存时被覆盖。如何声明持久变量见 §1.5。

### 1.5 声明持久变量（`persist`）

只有带 `global` 前缀的行才是变量声明；再叠加 `persist` 关键字表示「这个值要存进 `d3dx_user.ini`」：

```ini
[Constants]
global persist $my_named_variable = 1
```

- 关键字来自 `VariableFlags`：`global` / `persist` / `locked`（`src/DirectX11/CommandList.h:143-157`）。
- 声明在 `[Constants]` 段里解析：缺少 `global` 的行不算声明，而是运行期赋值（`src/DirectX11/IniHandler.cpp:2784-2791`）。
- 带 `persist` 的变量会被登记进持久变量表（`src/DirectX11/IniHandler.cpp:2715-2723`），其当前值在保存时写进 `d3dx_user.ini`（§1.4），并在下次启动时由该文件恢复（§6.6）。
- 被包含文件里的持久变量名带命名空间前缀，写进 `d3dx_user.ini` 时形如 `$\mods\example.ini\my_named_variable`（§3.3）。
- `persist` 可与 `locked` 同时使用：`locked` 表示声明后不允许运行期改写，这类变量不会被序列化（`src/DirectX11/IniHandler.cpp:5583-5590`）。
- 已消失或未知的持久项会被统计并提示，配合 `clear_unknown_settings` 决定是否清理（§6.6）。

### 1.6 配置文件编码

| 情况 | 行为 | 证据 |
| --- | --- | --- |
| UTF-8（可带 BOM） | 正常解析 | `src/DirectX11/IniHandler.cpp:773-828` |
| UTF-16（`FF FE` / `FE FF`） | **拒绝解析该文件**，警告 `INI file is UTF-16 encoded, not UTF-8; re-save as UTF-8` | `src/DirectX11/IniHandler.cpp:794-799,860-862` |
| 非法 UTF-8 字节 | 坏字节替换为 U+FFFD 后继续解析，并产生 `Invalid UTF-8 ...` 诊断（可见性见 §5.3） | `src/DirectX11/IniHandler.cpp:813-827,871-873` |
| 打不开 / 读取失败 | 警告 `Error opening/reading <path>`，该文件被整体跳过（该诊断不触发失败提示音，见 §5.3） | `src/DirectX11/IniHandler.cpp:856-866` |

## 2. INI 的读取与合并

### 2.1 自研解析器，一次读完

DX11 不再使用 `GetPrivateProfile*` 读配置（那套 API 每次读取都会重新解析整个文件，长 INI 会明显变慢，源码注释给出的例子是 Dreamfall Chapters 约 1 分 45 秒），改为每次加载配置时把文件读入内存再解析（`src/DirectX11/IniHandler.cpp:830-847` 的设计说明）。因此：

- 所有节/键在内存中建立索引，之后的取值是查表，不再触碰磁盘。
- 每次「加载配置」都会先 `ini_sections.clear()` 再重建整张表（`src/DirectX11/IniHandler.cpp:879-884`），即**全量重载**，不是增量打补丁。
- 例外：`[Logging] log_level` 与 `show_warnings` 由 Windows profile API 直接从主 `d3dx.ini` 读取，用于在解析器可用之前确定日志等级与警告显示（`src/DirectX11/Logging.cpp:18-30`，`src/DirectX11/IniHandler.cpp:1572-1587`）。**写在被包含文件里的 `log_level` / `show_warnings` 不起作用**，只有主 `d3dx.ini` 的值有效。

### 2.2 词法规则

| 规则 | 行为 | 证据 |
| --- | --- | --- |
| 注释 | 只有**行首（允许前导空白）**的 `;` 是注释；行中间的 `;` 不是注释 | `src/DirectX11/IniHandler.cpp:662-674` |
| 空行 | 忽略 | `src/DirectX11/IniHandler.cpp:655-660` |
| 节名 | 取到第一个 `]` 之前的部分，去掉首尾空白；没有 `]` 时取整行 | `src/DirectX11/IniHandler.cpp:435-450` |
| 键值 | 按**第一个** `=` 分割；`=` 两侧空白裁剪（整行的首尾空白在读行时已裁掉） | `src/DirectX11/IniHandler.cpp:586-601` |
| 无 `=` 的行 | 不进入键值表；命令列表类节与 `[Profile]` 里作为命令原样保存，其他情况警告 `Malformed line` | `src/DirectX11/IniHandler.cpp:138-141,620-629` |
| 空值 | `key =` 视为「有键但值为空」，警告 `No value found for` 并丢弃该行（`[Resource*]` 的 `match_format` / `match_dhash` 例外，会保留为空值以阻止别名回退） | `src/DirectX11/IniHandler.cpp:594-601` |

### 2.3 大小写

- **节名、键名不区分大小写**：内部用 `WStringInsensitiveHash` / `WStringInsensitiveEquality` 建表（`src/DirectX11/IniHandler.cpp:237-257`），`[TextureOverrideFoo]` 与 `[textureoverridefoo]` 视为同一节。
- **值与路径保留原样**，不做大小写归一（路径比较在需要时另外做，见 §3.4）。
- 变量名在声明时被转成小写（`src/DirectX11/IniHandler.cpp:2776-2778`）。

### 2.4 重复节与重复键

| 情况 | 行为 | 证据 |
| --- | --- | --- |
| 同一文件内重复节（命名空间节） | 警告 `Duplicate section found`，**只解析第一个**，重复节内的行被忽略并逐行警告 `Entry outside of section` | `src/DirectX11/IniHandler.cpp:465-484,580-583` |
| 同一节内重复键 | 警告 `Duplicate key found`，**先出现的值生效** | `src/DirectX11/IniHandler.cpp:611-618` |
| 白名单 | `[Key*]` 的 `key` / `back`、`[ShaderRegex*]` 的 `exclude_hash`、整个 `[Include]` 段的键不报重复 | `src/DirectX11/IniHandler.cpp:143-158` |
| 命令列表类节 | 不报「重复键」，重复检查交给命令列表解析器 | `src/DirectX11/IniHandler.cpp:504-506` |

「命令列表类节」= `[TextureOverride*]`、`[CommandList*]`、`[Constants]`、`[Present]`、`[ShaderOverride*]`、`[CustomShader*]`、`[ShaderRegex*]`、`[BuiltInCommandList*]`、`[BuiltInCustomShader*]`、`[ClearRenderTargetView]`、`[ClearDepthStencilView]`、`[ClearUnorderedAccessViewUint]`、`[ClearUnorderedAccessViewFloat]`（`src/DirectX11/IniHandler.cpp:96-110`）。

### 2.5 全局节与命名空间节的合并优先级

这是本项目最容易踩坑的一条规则：

- **命名空间节**（`[TextureOverride*]`、`[CommandList*]`、`[Resource*]`、`[Pool*]`、`[Key*]`、`[Preset*]`、`[Include*]`、`[ShaderRegex*]`、`[CustomShader*]` 等带前缀的节）在被包含文件里会被改名为 `前缀\命名空间\原名`，因此与主文件同名节**互不冲突**，各自独立。
- **全局节**（`[Constants]`、`[Present]`、`[System]`、`[Device]`、`[Rendering]`、`[Logging]`、`[Hunting]`、`[Input]`、`[Loader]`、`[Profile]`、`[Stereo]`、`[ConvergenceMap]`、`[ClearRenderTargetView]`、`[ClearDepthStencilView]`、`[ClearUnorderedAccessViewUint]`、`[ClearUnorderedAccessViewFloat]`）**不参与命名空间**，主文件与所有被包含文件共用同一个节。判定规则：节名只有落在「带前缀的白名单」（`[TextureOverride*]`、`[CommandList*]`、`[ShaderOverride*]`、`[CustomShader*]`、`[ShaderRegex*]`、`[BuiltInCommandList*]`、`[BuiltInCustomShader*]`、`[Pool*]`、`[Resource*]`、`[Key*]`、`[Preset*]`、`[Include*]`）里才会被命名空间化，其余节全部是全局节（`src/DirectX11/IniHandler.cpp:96-133,211-219`）。
  - 标量键：**后加载的文件覆盖先加载的**（`section_entry->kv_map[key] = val`，`src/DirectX11/IniHandler.cpp:603-606`）。
  - 命令行：所有文件的行按加载顺序**追加**到同一张列表里，因此 `[Present]`、`[Constants]` 等全局节里的命令会累积执行；同名 `global` 重复声明会触发 `Redeclaration of $x` 警告。
- 加载顺序：**主 `d3dx.ini` → `[Include]` 段里的各行（按文件内出现顺序，`include` 与 `include_recursive` 交错处理）→ 持久设置文件**。持久设置最后加载，因此在它之后才读取的设置上最终生效。
- 但「一次加载」内部还有先后：`[Include]` 与持久文件是在 `[System]` 段的**中途**被解析的（`src/DirectX11/IniHandler.cpp:5126-5130`）。因此：
  - `[Logging]` 的全部键，以及 `[System] dll_initialization_delay` / `skip_early_includes_load` / `config_initialization_delay`，**只反映主 `d3dx.ini`**（它们在包含文件被解析之前读取，`src/DirectX11/IniHandler.cpp:5067-5124`）；
  - 其余 `[System]`（从 `proxy_d3d11` 起）、`[Input]`、`[Device]`、`[Rendering]`、`[Hunting]` 与所有命令列表节，反映的是**主文件 + 已解析的包含文件 + 持久设置**的合并结果。
- 默认配置下第一次加载根本不解析包含文件（§6.2），所以首帧之前的所有设置都只来自主文件。

### 2.6 未知节与未知键

- 不在「命令列表类节」也不在「普通节」白名单里的节 → 警告 `Unknown section type`（`src/DirectX11/IniHandler.cpp:117-133,507-509`）。普通节白名单：`[Pool*]`、`[Resource*]`、`[Key*]`、`[Include*]`、`[Preset*]`、`[Hunting]`、`[Logging]`、`[System]`、`[Input]`、`[Device]`、`[Rendering]`、`[Loader]`、`[Profile]`、`[Stereo]`、`[ConvergenceMap]`。
- `[Profile]`、`[Stereo]`、`[ConvergenceMap]` 只是**被接受**（不再报警），不代表本分支实现了对应功能。
- `[Include]` 段里出现 `include` / `include_recursive` / `exclude_recursive` / `user_config` 之外的键 → 警告 `Unrecognised entry`（`src/DirectX11/IniHandler.cpp:1888-1909`）。

### 2.7 取值解析与失败回退

所有 `GetIni*` 取值遵循同一套语义（`src/DirectX11/IniHandler.cpp:1411-1451`）：

| 输入 | 结果 |
| --- | --- |
| 键不存在 | 返回调用方给的默认值，不报警 |
| 键存在但值为空 | 返回默认值；`warn` 为真时报警 `Unable to parse ... from empty string` |
| 值无法解析 | 返回默认值；按调用方是否传 `warn` 决定是否报警 |
| 值合法 | 返回解析结果，并把 `found` 置真 |

类型解析细节：

- 布尔：接受 `1/0`、`true/false`、`yes/no`、`on/off`（大小写不敏感）；其它字符串按表达式求值（`src/DirectX11/IniHandler.cpp:1541-1558`）。
- 整数：十进制字面量，超范围饱和到 `INT_MIN/INT_MAX`（`src/DirectX11/IniHandler.cpp:1497-1522`）。
- 浮点：`wcstof` 解析，保留溢出/下溢/负零（`src/DirectX11/IniHandler.cpp:1460-1470`）。
- 枚举：用名称表匹配（如 `shader_hash = 3dmigoto`），非法值回落默认值（`src/DirectX11/IniHandler.cpp:1686-1751`）。
- 表达式：无法解析或无法静态求值时按上面的「失败回退」处理（`src/DirectX11/IniHandler.cpp:1343-1403`）。

### 2.8 内置节

`[BuiltInCustomShaderDisableScissorClipping]`、`[BuiltInCustomShaderEnableScissorClipping]`、`[BuiltInCommandListUnbindAllRenderTargets]` 由代码内建注入，用户无需声明即可 `run =`（`src/DirectX11/IniHandler.cpp:886-914`）。

## 3. 路径与命名空间

### 3.1 命名空间的来源

| 来源 | 命名空间取值 |
| --- | --- |
| 主 `d3dx.ini` | 空（即默认命名空间，表达式解析时显示为 `d3dx.ini`，见 `src/DirectX11/globals.h:447`） |
| `include = Mods\a.ini` | `Mods\a.ini` |
| `include_recursive = Mods` 下的 `Mods\sub\b.ini` | `Mods\sub\b.ini` |
| 文件头部 `namespace = Example` | `Example`（覆盖上面的推导值） |

命名空间就是**该 INI 相对 DLL 目录的路径**（不含 DLL 目录本身）。

### 3.2 显式 `namespace`

在文件**第一个节之前**写 `namespace = <名字>` 可以改写该文件的命名空间（`src/DirectX11/IniHandler.cpp:540-570`）：

```ini
namespace = Example

[Constants]
global $enabled = 1

[CommandListUpdate]
$enabled = 1
```

此时 `[CommandListUpdate]` 实际节名为 `CommandList\Example\Update`，变量名为 `$\Example\enabled`。其它文件引用时写完整名字：

```ini
run = CommandList\Example\Update
```

注意：`namespace` 只在**第一个节之前**有效；写在节内**不会**有「未知键」告警——解析器只对重复键、无 `=` 的行、未知节以及命令列表里的未知条目告警（`src/DirectX11/IniHandler.cpp:572-632,1908,2698`）。写在普通节里会被当成普通键值静默忽略；写在命令列表节里整行不被识别，报 `Unrecognised entry`（`src/DirectX11/IniHandler.cpp:2698`）；写在 `[Include]` 段里同样报 `Unrecognised entry`（`src/DirectX11/IniHandler.cpp:1908`）。只有当该行的**值**被当作表达式解析、且其中出现未知标识符时，才会由表达式解析器报 `Unrecognised identifier`（`src/DirectX11/CommandList.cpp:5621`）。`namespace` 只改名字，**不改变文件所在目录**，因此资源相对路径仍按文件实际位置查找。

### 3.3 命名空间化规则

- 节名：`<前缀>\<命名空间>\<原名>`，前缀取自带前缀的节白名单（`src/DirectX11/IniHandler.cpp:319-328`）。只有带前缀的节会被改名，全局节永远保持原名。
- 变量名：`$\<命名空间>\<变量名>`（`src/DirectX11/IniHandler.cpp:340-351`）。
- 主文件里的全局变量没有命名空间前缀，直接是 `$name`。

### 3.4 相对路径基准

| 配置项 | 相对基准 | 说明 |
| --- | --- | --- |
| `include`（主文件里） | DLL 目录 | `src/DirectX11/IniHandler.cpp:1878-1892` |
| `include`（被包含文件里） | **包含它的那个文件所在目录** | 同上，命名空间路径被截断到父目录后拼接 |
| `include_recursive` | DLL 目录（主文件里）/ 包含它的那个文件所在目录（被包含文件里） | 与 `include` 共用同一段相对路径拼接（`src/DirectX11/IniHandler.cpp:1878,1900-1902`，搜索路径 `src/DirectX11/IniHandler.cpp:1001`） |
| `exclude_recursive` | 无路径概念，只匹配**文件名** | `src/DirectX11/IniHandler.cpp:1016` |
| `user_config` | DLL 目录（绝对路径除外） | `src/DirectX11/IniHandler.cpp:1811-1815` |
| `override_directory` / `cache_directory` | DLL 目录 | `src/DirectX11/IniHandler.cpp:5215-5242`，并自动创建目录 |
| `[Resource*] filename` | 先相对**所属 INI 所在目录**，找不到再相对 **DLL 目录** | `src/DirectX11/IniHandler.cpp:2361-2399` |

路径分隔符：多数配置项直接使用写入的字符串（`include` 不做 `/`→`\` 替换），Windows 能解析正斜杠，但混用会让日志与命名空间看起来不一致；只有 `user_config` 显式把 `/` 换成 `\`（`src/DirectX11/IniHandler.cpp:1811`）。建议统一写反斜杠。

### 3.5 资源文件查找

`[Resource*] filename = <file>` 的查找顺序（`src/DirectX11/IniHandler.cpp:2361-2399`）：

1. 若该节来自被包含文件：`<DLL 目录>\<该 INI 所在目录>\<file>`；
2. 上面找不到时：`<DLL 目录>\<file>`；
3. 路径过长 → 警告 `Resource filename rejected: path too long`，该资源被丢弃。

因此把 Mod 目录整体搬走时，配套资源要一起搬；只搬 INI 会让资源回退到 DLL 目录查找并可能「找不到」。

## 4. `[Include]`：递归、排除与条件

`[Include]` 是唯一被特殊处理的节（`src/DirectX11/IniHandler.cpp:1828-1916`）。它支持的键只有四个：

```ini
[Include]
include = Common\settings.ini      ; 载入指定 INI，可重复
include_recursive = Mods           ; 递归载入目录中的 INI，可重复
exclude_recursive = DISABLED*      ; 排除递归匹配到的名字，可重复
user_config = d3dx_user.ini        ; 持久设置文件路径
```

### 4.1 include

- 值相对基准见 §3.4；可以用正斜杠，但建议反斜杠。
- 被包含文件被解析时带命名空间（§3.1），其内的自定义节与变量自动隔离。
- **同一相对路径只会被包含一次**：重复包含会警告 `File included multiple times` 并跳过（`src/DirectX11/IniHandler.cpp:1880-1886`）。
- 包含文件里可以再写 `[Include*]` 继续包含别人；外层循环会一直处理到没有新的包含节出现为止（`src/DirectX11/IniHandler.cpp:1852-1912`）。

### 4.2 include_recursive

遍历规则（`src/DirectX11/IniHandler.cpp:993-1045`）：

1. 搜索 `<基准>\<目录>\*` 并逐项处理；基准与 `include` 相同：主 `d3dx.ini` 里是 DLL 目录，被包含文件里是该文件所在目录（`src/DirectX11/IniHandler.cpp:1878,1900-1902,1001`）；
2. 名字命中 `exclude_recursive` 的项被整体跳过（文件与目录一视同仁）；
3. 目录（除 `.` 与 `..`）先入队，文件则只收 `.ini`（大小写不敏感）；
4. **先处理本目录的全部 `.ini`，再递归子目录**；同级内按名字**不区分大小写排序**，保证多次运行顺序一致；
5. 每个 `.ini` 的命名空间是 `<相对目录>\<文件名>`，例如 `Mods\a.ini`。

注意：

- 递归目录**不存在**时只写一条 `Recursive include path "..." not found` 的 info 日志，不报警、不中断（`src/DirectX11/IniHandler.cpp:1010-1013`）。
- 递归目录里**不递归**读取非 `.ini` 文件；被引用的外部文件由 `[Resource*]` / `[CustomShader*]` 各自解析。
- **不要既 `include` 某个文件、又用 `include_recursive` 覆盖它所在的目录**：两条路径得到的命名空间相同，文件会被解析两次，第二次触发 `Duplicate section found`，全局节里的 `global` 声明还会触发 `Redeclaration of $x`。
- `include_recursive` 的目录里**不会**自动排除 `ShaderFixes`、`ShaderCache` 等目录，除非显式写 `exclude_recursive`。

### 4.3 exclude_recursive

- **只在主 `d3dx.ini` 的 `[Include]` 段里生效**；被包含文件里写的 `exclude_recursive` 不参与（源码中留有 TODO：`src/DirectX11/IniHandler.cpp:1848-1850`）。
- 模式是 glob，用 PCRE2 的 glob 转换编译，**大小写不敏感**（`src/DirectX11/IniHandler.cpp:916-938`）。
- 匹配对象是**条目名本身**（不含目录），所以 `DISABLED*` 能同时挡掉 `DISABLEDfoo.ini` 和名为 `DISABLEDfoo` 的目录（`src/DirectX11/IniHandler.cpp:1016-1019`）。
- 常用写法：

```ini
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
exclude_recursive = *.disabled.ini
```

### 4.4 条件与命名空间前置声明

文件**第一个节之前**可以写：

```ini
condition = <常量表达式>   ; 为假则整个文件被跳过
namespace = <名字>         ; 改写本文件的命名空间
```

- `condition` 必须能被**静态求值**，而且**只能是常量表达式**：
  - 静态求值时不展开变量（`static_evaluate` 的 `evaluate_variables` 默认为 `false`，`src/DirectX11/CommandList.h:1435`）；
  - 变量名在解析时若解析不出来，表达式直接解析失败（`src/DirectX11/CommandList.cpp:6463-6474`）；
  - 而全局变量要到本文件解析**之后**的 `ParseConstantsSection()` 才注册（`src/DirectX11/IniHandler.cpp:2759-2818`），因此引用任何 `global` 变量都会失败。
  - 结果：引用变量的 `condition` 会报警并**跳过整个文件**（`Unable to parse include condition` 或 `Include condition could not be statically evaluated`，`src/DirectX11/IniHandler.cpp:515-538`）。
- 条件为假时只写一条 info 日志 `condition = false, skipping "..."`，不报警。
- 这两行只允许出现在**第一个节之前**；写在节内不会有「未知键」告警：普通节里被静默忽略，命令列表节里整行不被识别、报 `Unrecognised entry`（`src/DirectX11/IniHandler.cpp:2698`），`[Include]` 段里也报 `Unrecognised entry`（`src/DirectX11/IniHandler.cpp:1908`）；若该行的值被当作表达式解析且含未知标识符，则由表达式解析器报 `Unrecognised identifier`（`src/DirectX11/CommandList.cpp:5621`）。
- `namespace` 只改名字，**不改变文件所在目录**（见 §3.2）。

可用的写法（常量条件 + 显式命名空间）：

```ini
condition = 1 == 1
namespace = Example

[CommandListUpdate]
$enabled = 1
```

如果需要「按运行时变量决定是否载入」，当前实现做不到；请改用命令列表里的条件命令（`if ...`）在运行期判断。

## 5. 版本与日志

### 5.1 版本号

- DLL 内部版本在构建时生成：`MAJOR.MINOR.REVISION.BUILD`。`MAJOR`/`MINOR`/`REVISION`（以及可选的第 4 段 `BUILD`）取自**最近的 `v*` tag 的名字**（`git describe --tags --match "v[0-9]*" --abbrev=0`），随后再把该 tag 到 HEAD 的提交数累加到 `BUILD`；tag 名没有第 4 段时 `BUILD` 从 `0` 起算（`cmake/GenerateDX11Version.cmake:38,44-53,76-80`）。没有可用 tag 时回退到脚本内置的 `1.3.16`（`cmake/GenerateDX11Version.cmake:5-7`）——该字面量与 `CMakeLists.txt:2-6` 的 `project(... VERSION 1.3.16)` 数值相同，但来源不同。
- 生成头 `dx11-version.generated.h` 覆盖 `src/DirectX11/DX11Version.h:6-14` 的兜底常量；`src/DirectX11/DX11Version.cpp` 导出 `MigotoFileVersionString()`。
- 运行期版本串打在日志首行：`D3D11 DLL starting init - v <version> - <time>`（`src/DirectX11/IniHandler.cpp:5057`），同时日志里记录 `Game path` 与 `3DMigoto path`，便于确认到底加载了哪一份（`src/DirectX11/IniHandler.cpp:5059-5063`）。
- DLL 文件属性里的产品名是 `ProjectBunny DX11 (XXMI/3Dmigoto)`（`src/DirectX11/DX11Version.h:20`）。

### 5.2 日志等级

```ini
[Logging]
log_level = info
```

| `log_level` | 效果 |
| --- | --- |
| `disabled` | 不写日志文件 |
| `warning` | 写入日志文件；overlay 提示仅在 `show_warnings = 1` 时显示 |
| `info` | 追加普通信息（含 API 使用记录） |
| `debug` | 追加详细诊断输出 |

- 合法值优先于旧的 `calls` / `debug` 组合；`log_level` 缺失、为空或非法时才回退：旧 `calls = 1` 开 info，再加 `debug = 1` 升到 debug；单独 `debug = 1` 不开日志（`src/DirectX11/Logging.cpp:6-16`）。
- **文件日志与屏幕提示是两个独立通道**，`show_warnings` 只闸门**非 INFO 级**的 overlay 消息（判据是 `level == LOG_INFO || G->gShowWarnings`，`src/DirectX11/Overlay.cpp:924-960`）：
  - `LOG_INFO` 级提示（例如 `> Reloaded config in 0.123s`、`> Wiped user config file ...`）**始终显示**在屏幕上，与 `show_warnings` 无关；
  - `LOG_WARNING` 级提示（例如 INI 解析告警）在 `show_warnings = 0` 时**不显示**，但只要日志等级 ≥ warning 仍会写进日志文件；
  - `log_level = disabled` 时没有日志文件，INFO 级屏幕提示照常出现。
- 按仓库模板部署（`log_level = disabled`、`show_warnings = 0`，`Dependencies/d3dx.ini:1072,1091`）就是这种组合：不会生成 `d3d11_log.txt`，但重载耗时之类的 INFO 提示仍会出现在屏幕上。
- `log_level` 与 `show_warnings` 只在**主 `d3dx.ini`** 中有效（见 §2.1）。

### 5.3 警告与提示音

解析过程中的问题分三条路径处理：

1. **写日志文件**：等级 ≥ warning 时写入（`src/common/log.h:26-33`）；
2. **屏幕提示**：`LOG_WARNING` 级只在 `show_warnings = 1` 时显示，`LOG_INFO` 级始终显示（`src/DirectX11/Overlay.cpp:924-960`）；
3. **失败提示音**：只有 `IniWarning` / `IniWarningW` 类诊断会置位提示标志，并在**整次加载结束后统一响一次**（`src/DirectX11/IniHandler.cpp:294-317,5472`）。

| 诊断 | 写日志 | 屏幕提示 | 失败提示音 |
| --- | --- | --- | --- |
| 重复节 / 重复键 / 未知节 / 未知条目（`IniWarningW`） | 是 | `show_warnings = 1` 时 | 是 |
| UTF-16 编码、非法 UTF-8（`IniWarningW`） | 是 | `show_warnings = 1` 时 | 是 |
| INI 文件打不开 / 读取失败（`LogOverlay`，`src/DirectX11/IniHandler.cpp:856-866`） | 是 | `show_warnings = 1` 时 | **否** |

也就是说「文件打不开」只写日志与屏幕提示，不会响失败提示音；只有解析器级别的 `IniWarning` 诊断会响。

### 5.4 诊断开关

| 键 | 默认 | 作用 |
| --- | --- | --- |
| `unbuffered` | `0` | 日志不缓冲，便于实时查看 |
| `force_cpu_affinity` | `0` | 绑定到单个 CPU，用于调试多线程问题 |
| `debug_locks` | `0` | 打开死锁检测 |
| `crash` | `0` | 非 0 时安装崩溃处理器（写 minidump） |
| `waitfordebugger` | `0` | >0 时等待调试器附加，>1 时再触发一次 `__debugbreak` |
| `dump_all_profiles` | `0` | 把全部 NVIDIA 驱动 profile 写进日志（否则只记录 base/global/当前 profile） |

这些开关只影响诊断行为，不改变渲染结果；复现问题后应恢复默认。

## 6. 启动流程与运行时重载

### 6.1 启动顺序

```text
DllMain
 ├─ verify_intended_target()      读 <DLL 目录>\d3dx.ini 的 [Loader] 判定目标进程
 ├─ 安装 d3d11 / LoadLibraryExW / DXGI 钩子
 └─ InitializeDLL()               src/DirectX11/D3D11Wrapper.cpp:100-120
     ├─ LoadConfigFile()          src/DirectX11/IniHandler.cpp:5025
     │   ├─ 组装 <DLL 目录>\d3dx.ini 与 d3d11_log.txt
     │   ├─ warn_of_conflicting_d3dx()   游戏目录同名配置提示
     │   ├─ InitializeLogging() / InitializeEarlyWarningSettings()
     │   ├─ ParseIniFile()               只解析主 d3dx.ini
     │   ├─ InsertBuiltInIniSections()
     │   ├─ 读取 [Logging] 与 [System] 的前几个键（此时只有主文件的内容）
     │   ├─ ParseIncludedIniFiles() + ParsePersistentSettings()（条件见 §6.2）
     │   ├─ 读取其余 [System]、[Input]、[Device]、[Rendering]（此时已含包含文件）
     │   └─ 解析 [Hunting]、各类命令列表节、[Present] 等
     └─ 校验 [Loader] target（不匹配则禁用核心功能）
```

### 6.2 延迟加载与延迟初始化

| 键 | 默认 | 含义 |
| --- | --- | --- |
| `[System] dll_initialization_delay` | `0` | 毫秒；在 `LoadConfigFile()` 之后、正式初始化之前 sleep（`src/DirectX11/D3D11Wrapper.cpp:113-116`） |
| `[System] skip_early_includes_load` | `1`（true） | 初始化阶段**跳过** `[Include]` 的加载 |
| `[System] config_initialization_delay` | `0` | 秒；首次 Present 时强制重载的等待时间；负数表示禁用该重载 |

判定逻辑（`src/DirectX11/IniHandler.cpp:5116-5130`）：

```text
if (config_initialization_delay < 0) gConfigInitialized = true
if (gConfigInitialized || !skip_early_includes_load) {
    ParseIncludedIniFiles()
    ParsePersistentSettings()
}
```

因此**默认配置下（`skip_early_includes_load = 1`），DLL 初始化阶段只读主 `d3dx.ini`**，Mod 与被包含文件要到首次 Present 时的强制重载才生效。这正是模板注释描述的「把 Mod 加载与 DLL 初始化解耦」。

想要「启动即加载全部 INI」，把 `skip_early_includes_load = 0`；此时 `config_initialization_delay` 应保持非负（模板注释建议配合 `config_initialization_delay = 0`），否则会出现两次不同的加载时序。

### 6.3 首次 Present 的强制重载

`src/DirectX11/HackerDXGI.cpp:249-267`：每次 Present 先派发输入，再处理待重载标志；若尚未完成初始化且 `G->gTime > config_initialization_delay`（`gTime` 单位为秒，见 `src/DirectX11/FrameTiming.cpp:41`），则置 `gConfigInitialized = true` 并调用 `ReloadConfig()`。

源码注释说明这是为了修掉「启动时 `LoadConfigFile()` 在部分边界情况下没有把资源加载完整」的补丁式做法（`src/DirectX11/HackerDXGI.cpp:252-254`）。所以 Mod 生效的时点是**第一帧之后**，不是 DLL 加载瞬间。

### 6.4 运行时重载

| 绑定 | 位置 | 行为 |
| --- | --- | --- |
| `reload_config` | `[Hunting]` | 重载配置（默认模板为 `no_modifiers VK_F10`） |
| `wipe_user_config` | `[Hunting]` | 删除持久设置文件后再重载（默认模板为 `ctrl alt no_shift VK_F10`） |

- 这两个绑定**即使 `hunting = 0` 也会注册**（`src/DirectX11/Hunting.cpp:2075-2082`），所以不开启 hunting 也能用 F10 重载。
- 快捷键本身在节内以字符串给出，格式为「修饰键 + 键名」，如 `ctrl alt no_shift VK_F10`、`no_modifiers VK_F9`、`XB_LEFT_TRIGGER`；无法解析时报警 `UNABLE TO PARSE KEY BINDING`（`src/DirectX11/input.cpp:443-494`）。
- 重载不是立刻发生：按键回调只置标志 `gReloadConfigPending`，由下一次 Present 统一处理，避免在输入回调里改键位（`src/DirectX11/IniHandler.cpp:4704-4715`，`src/DirectX11/HackerDXGI.cpp:246-250`）。
- `ReloadConfig()` 会先保存（或按需清空）持久设置，再清空键位绑定与命令列表性能计数，最后重新 `LoadConfigFile()` 并重跑 `[Constants]`（`src/DirectX11/IniHandler.cpp:5697-5778`）。持久保存失败时**放弃本次重载**，保留旧状态以便重试（`src/DirectX11/IniHandler.cpp:5710-5717`）。
- 重载耗时会被打印成 `> Reloaded config in 0.123s`（`src/DirectX11/IniHandler.cpp:5774-5777`）。

### 6.5 哪些改动需要重启游戏

重载只重建「配置表 + 键位 + 命令列表 + 着色器补丁」，以下属于进程级状态，只能重启生效：

- `DllMain` 里安装的钩子（d3d11 / `LoadLibraryExW` / DXGI 工厂）；
- `[System] dll_initialization_delay`（只在初始化时使用一次）；
- 已经在游戏里创建好的 D3D 设备、交换链与后处理状态；
- DXGI 层已固定的分辨率/全屏模式（`[Device]` 的多数项在下次交换链重建时才生效）；
- 已编译并缓存的着色器（改了 `shader_hash` / `texture_hash` 后既有 Hash 会失配，见 §7.3）。

模板注释另外建议 `rasterizer_disable_scissor`、`hide_cursor` 改动后重启（见 [`Dependencies/d3dx.ini`](../Dependencies/d3dx.ini) 对应注释）；源码层面这些项是在每次加载时重新读取的（`src/DirectX11/IniHandler.cpp:5246,5464-5465`），是否需要重启取决于具体游戏。

### 6.6 持久设置的保存时机

| 时机 | 说明 |
| --- | --- |
| 自动保存 | 每 `settings_auto_save_interval` 秒检查一次（间隔值的读取与夹取在 `src/DirectX11/IniHandler.cpp:5152-5155`，实际周期检查在 `src/DirectX11/HackerDXGI.cpp:255-261`；负数表示禁用）；只有存在未保存改动时才真正写盘 |
| 显式重载 | `ReloadConfig()` 开始时保存（`src/DirectX11/IniHandler.cpp:5714`） |
| `wipe_user_config` | 删除文件后重载（`src/DirectX11/IniHandler.cpp:5656-5675`） |

自动保存的周期检查位于 `if (G->gConfigInitialized)` 分支内，因此要等 §6.3 描述的首次 Present 强制重载把配置初始化标记置为真之后才开始；在此之前不会写盘。

未识别的持久变量会被单独统计并提示；`clear_unknown_settings` 控制是否在下次重载时清掉「名称与数值都没变」的未知项（默认 `true`；模板里显式设为 `0` 以保留）。相关提示会给出重载与清空的按键名（`src/DirectX11/IniHandler.cpp:5522-5567`）。持久变量如何声明见 §1.5。

## 7. 兼容项

以下开关用于适配不同游戏，**一次只改一个**并重启验证。

### 7.1 [System]

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `proxy_d3d11` | 空 | 链式加载的代理 DLL：先试 `<DLL 目录>\<值>`，失败再按原样路径加载；**启用后 `load_library_redirect` 被强制为 `0`**（否则代理 DLL 会回调自己形成死循环）（`src/DirectX11/D3D11Wrapper.cpp:407-454`） |
| `load_library_redirect` | `2` | 按源码注释：`0`=不重定向；`1`=只把 nvapi.dll 重定向到游戏目录；`2`=同时重定向 d3d11.dll 与 nvapi.dll（`src/DirectX11/D3D11Wrapper.cpp:1198-1216`） |
| `hook` | 未设置 | `recommended` / `deferred_contexts` / `immediate_context`(=`context`) / `device` / `all`；`except_*`、`skip_*` 已废弃，只保留兼容并提示删除（`src/DirectX11/DLLMainHook.h:16-48`） |
| `allow_check_interface` | `false` | **当前分支只解析该键、没有任何消费点**（`src/DirectX11/IniHandler.cpp:5145`，`src/DirectX11/globals.h:462`），设置它不会改变行为 |
| `allow_create_device` | `0` | `0`=请求的 feature level 不是 `D3D_FEATURE_LEVEL_11_0` 时返回 `E_INVALIDARG`；`1`=原样放行；`2`=强制改成 11.0（`src/DirectX11/D3D11Wrapper.cpp:693-722`） |
| `allow_platform_update` | `false` | 允许平台更新接口：`IDXGIFactory2` 与 `ID3D11Device1` / `ID3D11Context1`；关闭时这些接口返回 `E_NOINTERFACE`（`src/DirectX11/HookedDXGI.cpp:1003-1008`，`src/DirectX11/HackerDevice.cpp:1593-1598`，`src/DirectX11/HackerContext.cpp:1247`） |
| `check_foreground_window` | `false` | 仅在前台窗口时处理输入 |
| `additional_foreground_window` | 空 | 额外允许的前台窗口标题 |
| `screen_width` / `screen_height` | `1920` / `1080` | 探测失败时用作 `window_width`/`window_height` 回退；超出 `640..15360` / `480..8640` 会被强制改回默认值（`src/DirectX11/IniHandler.cpp:5160-5168`） |
| `settings_auto_save_interval` | `60` | 持久值自动保存间隔（秒） |
| `clear_unknown_settings` | `true` | 是否清理未知持久项 |
| `force_detect_color_space` | `false` | 为兼容性补丁额外检测 PNG 的 sRGB 元数据 |
| `dll_initialization_delay` | `0` | 初始化延迟（毫秒） |
| `skip_early_includes_load` | `true` | 初始化阶段跳过 `[Include]` |
| `config_initialization_delay` | `0` | 首次强制重载延迟（秒） |

`allow_create_device` 与 `allow_platform_update` 的推荐尝试顺序（模板注释）：先 `allow_check_interface`（当前分支无效），不行再 `allow_create_device = 2`，最后 `allow_create_device = 1`。

### 7.2 [Device]

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `width` / `height` | `-1` | 强制分辨率；`-1` 表示不改写 |
| `refresh_rate` | `-1` | 强制刷新率 |
| `filter_refresh_rate` | 空 | 逗号分隔的刷新率白名单，最多 10 个（`src/DirectX11/IniHandler.cpp:5189-5195`） |
| `full_screen` | `-1` | `1`=强制独占全屏；`2`=全屏并额外禁用 `SetWindowPos`；`-1`=不干预（`src/DirectX11/HookedDXGI.cpp:270-284`） |
| `upscaling` / `upscale_mode` | `0` / `0` | 上采样开关与模式；`upscale_mode = 1` 会创建第二个交换链 |
| `get_resolution_from` | 未设置（INVALID） | `swap_chain` 或 `depth_stencil`，决定纹理 Hash 的分辨率来源 |
| `hide_cursor` | `false` | 隐藏硬件光标 |
| `cursor_upscaling_bypass` | `true` | 上采样时绕过光标处理 |
| `allow_windowcommands` | `false` | 重新启用窗口消息处理（截图、Alt+Tab） |
| `toggle_full_screen` / `force_full_screen_on_key` | 未设置 | 全屏切换/强制全屏快捷键 |

### 7.3 [Rendering] 中影响兼容的项

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `shader_hash` | `3dmigoto`（FNV） | `embedded` / `bytecode`；**改动后既有 Hash 不再匹配**，需重新生成补丁（`src/DirectX11/globals.h:81-92`） |
| `texture_hash` | `0` | 新版纹理 Hash；开启后不要沿用旧 Hash |
| `override_directory` | 空 | 未设置时 `override_directory` 或 `cache_directory` 为空，着色器文件替换路径整体被跳过，`ShaderFixes` 不会生效（`src/DirectX11/HackerDevice.cpp:1155-1156`） |
| `cache_directory` | 空 | 未设置时着色器缓存不可用 |
| `ini_params` | `120` | IniParams 寄存器；不小于 `D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT` 时报警并置 `-1`（`src/DirectX11/IniHandler.cpp:5285-5290`） |
| `mod_runtime_optimizations` | `1` | 运行期优化总开关。设为 `0` 可退回原路径 |
| `expression_short_circuit` | `1` | 逻辑短路和标量条件优化开关。设为 `0` 可恢复原求值路径 |
| `cache_resource_data` | 未设置 | `none` / `all` / `vertex_buffer index_buffer constant_buffer` 组合 |
| `track_region_hashes` | `false` | 开启后同时启用三类缓冲数据缓存 |
| `allow_buffer_resize` | `true` | 允许按 `override_vertex_count` 扩容缓冲 |
| `recursive_include` | `false` | HLSL `#include` 的相对搜索行为 |
| `storage_directory` | — | **DX11 的 `IniHandler` 不读取该键**；它只被 `d3dcompiler_47` 包装层使用（`src/D3DCompiler/d3dcWrapper.cpp:72`），写在 DX11 配置里不会有效果 |

`fix_ZRepair_*`、`fix_InvTransform`、`fix_BackProjectionTransform*`、`fix_ObjectPosition*`、`fix_MatrixOperand*` 这一组旧的立体 / 反编译修正键，其用途、格式与适用条件统一见 [DX11 立体渲染与 HLSL 反编译修正](DX11-Legacy-Stereo.md)。

### 7.4 [Loader]

DX11 DLL 自身只读取两个键：

| 键 | 作用 |
| --- | --- |
| `target` | 目标进程名；既用于 `DllMain` 阶段的目标判定，也用于运行期校验（支持路径后缀匹配） |
| `check_target_even_without_loader` | 默认 `false`（不校验）；置 `1` 后按 `[Loader] target` 校验当前 exe 路径，`target` 缺失时视为通过（`src/DirectX11/D3D11Wrapper.cpp:65-95`） |

`module`、`launch`、`require_admin`、`delay` 是**外部加载器**的参数，DX11 DLL 不读取（`src/DirectX11/D3D11Wrapper.cpp:65-95`，`src/DirectX11/DLLMainHook.cpp:171-328`）。使用 XXMI Launcher 时由启动器自己管理游戏配置。

## 8. 默认值速查

下表是「键不存在时」的取值，全部来自源码调用点。

### [Logging]

| 键 | 默认 | 备注 |
| --- | --- | --- |
| `log_level` | 回退到 `calls`/`debug`，两者都为 0 时等同 `disabled` | 只认主 `d3dx.ini` |
| `calls` / `debug` | `0` / `0` | 旧式等级开关 |
| `unbuffered` | `false` | |
| `force_cpu_affinity` | `false` | |
| `debug_locks` | `false` | |
| `crash` | `0` | |
| `waitfordebugger` | `0` | |
| `dump_all_profiles` | `false` | |
| `show_warnings` | `true` | 只认主 `d3dx.ini` |

### [System]

| 键 | 默认 |
| --- | --- |
| `dll_initialization_delay` | `0` |
| `skip_early_includes_load` | `true` |
| `config_initialization_delay` | `0` |
| `proxy_d3d11` | 空 |
| `load_library_redirect` | `2` |
| `hook` | 未设置（不启用额外钩子） |
| `allow_check_interface` | `false` |
| `allow_create_device` | `0` |
| `allow_platform_update` | `false` |
| `check_foreground_window` | `false` |
| `additional_foreground_window` | 空 |
| `settings_auto_save_interval` | `60`（秒） |
| `clear_unknown_settings` | `true` |
| `screen_width` / `screen_height` | `1920` / `1080` |
| `force_detect_color_space` | `false` |

### [Input]

| 键 | 默认 | 备注 |
| --- | --- | --- |
| `input` | `true` | 只在进程首次加载时读取；运行期切换可跨重载保留 |
| `input_disable_mode` | `mods` | 另一取值 `all` |
| `toggle_input` | 未设置 | 该绑定在输入被禁用时依然可用 |

### [Device]

| 键 | 默认 |
| --- | --- |
| `width` / `height` / `refresh_rate` | `-1` |
| `filter_refresh_rate` | 空 |
| `full_screen` | `-1` |
| `upscaling` / `upscale_mode` | `0` / `0` |
| `get_resolution_from` | 未设置（INVALID） |
| `hide_cursor` | `false` |
| `cursor_upscaling_bypass` | `true` |
| `allow_windowcommands` | `false` |

### [Rendering]

| 键 | 默认 |
| --- | --- |
| `shader_hash` | `3dmigoto`（FNV） |
| `texture_hash` | `0` |
| `dhash_mip_size` | `256` |
| `dhash_allow_rehash` | `false` |
| `override_directory` / `cache_directory` | 空 |
| `cache_shaders` | `false` |
| `mod_runtime_optimizations` | `1` |
| `expression_short_circuit` | `1` |
| `rasterizer_disable_scissor` | `false` |
| `track_texture_updates` | `0` |
| `cache_resource_data` | 未设置（`none` 语义） |
| `track_region_hashes` | `false` |
| `track_implicit_index_buffers` | `false` |
| `draw_has_override_slots` / `auto_check_texture_slots` | 未设置 |
| `allow_buffer_resize` | `true` |
| `assemble_signature_comments` | `false` |
| `disassemble_undecipherable_custom_data` | `false` |
| `patch_assembly_cb_offsets` | `false` |
| `recursive_include` | `false` |
| `export_fixed` / `export_shaders` / `export_binary` / `dump_usage` | `false` |
| `export_hlsl` | `0` |
| `ini_params` | `120` |
| `fix_sv_position` / `recompile_all_vs` | `false` |
| `fix_ZRepair_*` / `fix_InvTransform` / `fix_BackProjectionTransform*` / `fix_ObjectPosition*` / `fix_MatrixOperand*` | 未设置（分组说明见 [DX11 立体渲染与 HLSL 反编译修正](DX11-Legacy-Stereo.md)） |

### [Hunting]

| 键 | 默认 | 备注 |
| --- | --- | --- |
| `hunting` | `0` | `0`=禁用（实际降级为「软禁用」），`1`=启用，`2`=软禁用 |
| `overlay_buffer_hash_lifetime` | `-1` | 开启 `track_region_hashes` 时会被强制为 `0` |
| `reload_config` / `wipe_user_config` | 未设置 | 即使 `hunting = 0` 也会注册 |
| `monitor_performance` / `freeze_performance_monitor` | 未设置 | |
| `monitor_performance_interval` | `1.0`（秒） | |
| `toggle_hunting` | 未设置 | |
| `repeat_rate` | `8` | 自动重复速率 |
| `marking_mode` | 未设置（INVALID） | `skip` / `original` / `pink` / `mono` |
| `marking_actions` | 未设置 | |
| `verbose_overlay` | `false` | |
| `dump_mips` | `true` | |

`hunting = 0`（默认）时，`ParseHuntingSection()` 在读完 `hunting` 之后会把状态降级为「软禁用」并直接返回（`src/DirectX11/Hunting.cpp:2093-2098`）。因此上表中的 `repeat_rate`、`marking_mode`、`marking_actions`、`toggle_hunting`、`next_marking_mode` **只在 `hunting` 为 `1` 或 `2` 时才会被读取**；`reload_config` 与 `wipe_user_config` 注册在提前返回之前，所以始终有效（§6.4）。

## 9. 常见失败路径

| 现象 | 源码行为 | 定位 |
| --- | --- | --- |
| 配置全都不生效 | 默认 `skip_early_includes_load = 1`，Mod 要到首次 Present 后的强制重载才加载 | §6.2、§6.3 |
| 屏幕提示「游戏目录存在未被使用的 d3dx.ini」 | DLL 只读自己旁边的 `d3dx.ini` | §1.2 |
| DLL 根本不加载 | 注入部署时 `[Loader] target` 不匹配，`DllMain` 返回 `FALSE` | §1.2 |
| Mod 的同名节互相覆盖 | 全局节不隔离，自定义节才隔离 | §2.5 |
| 变量名对不上 | 被包含文件的变量带命名空间前缀 `$\<命名空间>\<变量>` | §3.3 |
| 资源文件找不到 | 先找「相对所属 INI」，再找「相对 DLL 目录」 | §3.5 |
| 递归目录里的文件没被载入 | 只收 `.ini`；被 `exclude_recursive` 命中则跳过；目录不存在只写 info 日志 | §4.2 |
| 同一文件被载入两次 | `include` 与 `include_recursive` 覆盖同一文件 → `Duplicate section found` | §4.2 |
| `exclude_recursive` 没效果 | 只认主 `d3dx.ini` 的 `[Include]`，且只匹配文件名 | §4.3 |
| 整个 Mod 被跳过 | 文件头 `condition` 不是常量表达式（引用变量即失败） | §4.4 |
| 配置文件读不出内容 | UTF-16 编码被拒绝；非法 UTF-8 字节被替换 | §1.6 |
| 改了 `log_level` 没反应 | 只认主 `d3dx.ini`，且必须在重载/重启后生效 | §2.1、§5.2 |
| F10 没反应 | 快捷键在 `[Hunting]` 的 `reload_config`；输入还受前台窗口限制 | §6.4 |
| 改完配置画面没变 | 属于进程级状态，需要重启 | §6.5 |

## 10. 相关文档

- 用户手册：[安装](../usage/dx11/index.md)、[INI 与命名空间](../usage/dx11/configuration.md)、[全局设置](../usage/dx11/settings.md)、[管理 Mod](../usage/dx11/mods.md)、[排错](../usage/dx11/troubleshooting.md)
- INI 语言扩展：[总览](ini-language-extensions/README.md)、[日志](ini-language-extensions/logging.md)、[输入控制](ini-language-extensions/input/README.md)、[表达式](ini-language-extensions/expressions/README.md)
- 工程文档：[发布流程](Release-Package.md)、[DX11 上游同步记录](DX11-Upstream-Sync.md)、[DX11 立体渲染与 HLSL 反编译修正](DX11-Legacy-Stereo.md)（旧的 `fix_*` 立体 / 反编译修正键分组参考）
- 配置模板：[`Dependencies/d3dx.ini`](../Dependencies/d3dx.ini)（第 722 行注释里含一个非 UTF-8 字节，使用前请先另存为 UTF-8，详见 §1.1；注释比本文更细，但默认值以本文表格与源码为准）

> 模板与源码不一致时**以源码为准**；发现不一致请按仓库的审查流程记录，不要直接改模板掩盖差异。
