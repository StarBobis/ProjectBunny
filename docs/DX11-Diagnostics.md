# DX11 诊断手册（Hunting / 帧分析 / 日志 / 排错 / 性能）

面向使用 DX11 版 `d3d11.dll` 的 Mod 作者、DLL 使用者与仓库维护者。文中所有选项名、命令名、文件名与日志字段都按当前源码的**注册 → 解析 → 消费**链路给出，并在括号里标注源码位置，方便复查。

> **证据说明**
>
> - 本文的结论来自对当前工作树源码的静态阅读（下列文件与行号即证据），**本轮没有启动游戏、没有编译、没有联网**。
> - 凡是涉及“实际运行后会看到什么”的描述，只引用仓库里已经公开的文档（`usage/dx11/*.md`）与源码中写死的字符串，未在本轮实测的步骤一律标注为“未在本轮验证”。
> - 章节 §2、§3、§5 的选项表是对已有 `usage/dx11/hunting.md`、`usage/dx11/frame-analysis.md` 的复用与扩展，链接为仓库内相对路径。

相关文档：

- 使用者文档：[`usage/dx11/hunting.md`](../usage/dx11/hunting.md)、[`usage/dx11/frame-analysis.md`](../usage/dx11/frame-analysis.md)、[`usage/dx11/troubleshooting.md`](../usage/dx11/troubleshooting.md)、[`usage/dx11/configuration.md`](../usage/dx11/configuration.md)、[`usage/dx11/keys.md`](../usage/dx11/keys.md)
- 维护者文档：[`docs/DX11-Upstream-Sync.md`](DX11-Upstream-Sync.md)、[`docs/DX11-Log-Level-Review.md`](DX11-Log-Level-Review.md)

---

## 0. 症状索引

| 症状 | 先看 |
| --- | --- |
| 想找到某个效果用的着色器 / 顶点缓冲 / 索引缓冲 | [§2 Hunting](#2-hunting定位着色器与缓冲) |
| 想导出某一帧的资源、缓冲、纹理 | [§3 帧分析](#3-帧分析frame-analysis) |
| 只想抓某个 draw 的某几个缓冲 | [§3.6 定点 dump](#36-定点-dumppre--post) |
| 想知道 DLL 读到了哪些配置、警告了什么 | [§4 日志](#4-日志) 、 [§5 故障](#5-配置--编译--匹配--加载故障) |
| Mod 不生效、Hash 对不上、资源加载失败 | [§5 故障](#5-配置--编译--匹配--加载故障) |
| 想复现问题并验证改动 | [§6 调试构建](#6-调试构建与回归) |
| 想知道 DLL 自己占了多少 CPU / GPU | [§7 性能观察](#7-性能观察) |

---

## 1. 环境与前提

### 1.1 配置文件与日志文件的位置

DX11 DLL 以**自身所在目录**为基准找配置：

| 文件 | 位置 | 证据 |
| --- | --- | --- |
| `d3dx.ini` | `d3d11.dll` 同目录 | `src/DirectX11/IniHandler.cpp:5034-5043`（`GetModuleFileName(migoto_handle, ...)` 后拼接 `INI_FILENAME`） |
| `d3d11_log.txt` | `d3d11.dll` 同目录 | `src/DirectX11/IniHandler.cpp:5041-5043`（同目录拼接 `L"d3d11_log.txt"`） |

`config/d3dx.ini` 是**仓库自带的 DX12 配置**，文件头已声明 `[Hunting]`、`[Device]`、`[Rendering]`、`[Logging]` 属于 DX11 运行时专有段而被移除（`config/d3dx.ini:4-8`）。因此 DX11 的 hunting / 日志 / 渲染选项必须写在**游戏目录里那份 `d3dx.ini`**（或被它 `[Include]` 的文件）上，仓库内这份模板不提供它们。

### 1.2 启动顺序

`LoadConfigFile()`（`src/DirectX11/IniHandler.cpp:5025`）的顺序是固定的，可用来判断某个设置“到底有没有被读到”：

1. `setlocale(LC_CTYPE, "en_US.UTF-8")`；
2. 拼出 `d3dx.ini` / `d3d11_log.txt` 路径；路径过长直接 `DoubleBeepExit()`（`IniHandler.cpp:5036-5046`）；
3. `warn_of_conflicting_d3dx(iniFile)`：游戏目录里存在**同名但不是当前使用的** `d3dx.ini` 时给出警告（`IniHandler.cpp:4767`：`Detected a conflicting d3dx.ini in the game directory that is not being used.`）；
4. `InitializeLogging(iniFile, logFilename)`：先按 `[Logging]` 决定日志级别并打开日志（见 §4.1）；
5. `ParseIniFile(iniFile)` → `InsertBuiltInIniSections()` → 解析各节；
6. 之后才处理 `[Logging]` 的其余开关、`[System]`、`[Device]`、`[Rendering]` 等全局项。

> 结论：**`[Logging] log_level` 在 INI 其它内容之前生效**，因此即使解析中途报错，也能在 `d3d11_log.txt` 里看到启动横幅（`D3D11 DLL starting init - v <版本> - <时间>`、`Game path:`、`3DMigoto path:`）。

---

## 2. Hunting（定位着色器与缓冲）

### 2.1 开关与三种状态

```ini
[Hunting]
hunting = 2
```

| 值 | 源码常量 | 行为 |
| --- | --- | --- |
| `0` | `HUNTING_MODE_DISABLED` | **被强制改写**。`ParseHuntingSection()` 读取后立刻置为 `HUNTING_MODE_SOFT_DISABLED` 并 `return`（`src/DirectX11/Hunting.cpp:2093-2098`） |
| `1` | `HUNTING_MODE_ENABLED` | 直接启用；`toggle_hunting` 在 ENABLED ↔ SOFT_DISABLED 之间切换（`Hunting.cpp:2047-2054`） |
| `2` | `HUNTING_MODE_SOFT_DISABLED` | 待命：命令已注册，但 `AnalyseFrame()`、`HuntNext()` 等入口检查 `G->hunting != HUNTING_MODE_ENABLED` 后直接返回（`Hunting.cpp:1340-1341`、`Hunting.cpp:1457-1458`） |

**关键约束（源码可复查）**：`hunting = 0` 时 `ParseHuntingSection()` 提前 `return`，因此除 `reload_config` / `wipe_user_config` 外的**所有 hunting 按键绑定都不会被注册**（`Hunting.cpp:2081-2082` 的注册在 `return` 之前，其余在之后）。源码注释写明这是为了规避 `hunting = 0` 下 ShaderRegex 重载与着色器编译的异常：

```
// There are glitches related to ShaderRegex processing for reloaded inis and shader compilation with `hunting = 0`
// So we better make sure that hunting mode won't actually be ever fully disabled
```

实际影响：**`hunting = 0` 时不能靠按键打开定位，也不能靠按键触发帧分析**；要开只能改 INI 后按 F10 重载。帧分析本身也要求按键瞬间 `hunting` 处于 ENABLED（`AnalyseFrame()` 的 `if (G->hunting != HUNTING_MODE_ENABLED) return;`）。

### 2.2 注册的 hunting 命令

`ParseHuntingSection()`（`src/DirectX11/Hunting.cpp:2056-2187`）用 `RegisterIniKeyBinding(L"Hunting", L"<命令名>", ...)` 注册以下命令。**命令名是稳定接口**，按键由 INI 绑定决定；本仓库 `config/d3dx.ini` 不含 `[Hunting]` 段，所以 DX11 侧的绑定需由游戏适配包或用户 INI 提供（键位表见 [`usage/dx11/hunting.md`](../usage/dx11/hunting.md)）。

| 命令名 | 作用 | 证据 |
| --- | --- | --- |
| `reload_config` | 重载配置与 Mod 资源 | `Hunting.cpp:2081` |
| `wipe_user_config` | 重载配置并清除持久设置 | `Hunting.cpp:2082` |
| `toggle_hunting` | ENABLED ↔ SOFT_DISABLED | `Hunting.cpp:2101` |
| `monitor_performance` | 切换性能监视模式 | `Hunting.cpp:2087` |
| `freeze_performance_monitor` | 冻结性能显示并把当前报告写入日志 | `Hunting.cpp:2088`、`Hunting.cpp:1408-1422` |
| `next_marking_mode` | 切换标记方式 | `Hunting.cpp:2111` |
| `marking_actions` | 标记动作集合（字符串选项） | `Hunting.cpp:2113-2117` |
| `next_pixelshader` / `previous_pixelshader` / `mark_pixelshader` | PS 遍历与标记 | `Hunting.cpp:2119-2121` |
| `next_vertexbuffer` / `previous_vertexbuffer` / `mark_vertexbuffer` | VB 遍历与标记 | `Hunting.cpp:2123-2125` |
| `next_vertexbuffer_slot` / `previous_vertexbuffer_slot` | VB 槽位遍历 | `Hunting.cpp:2127-2128` |
| `next_indexbuffer` / `previous_indexbuffer` / `mark_indexbuffer` | IB 遍历与标记 | `Hunting.cpp:2130-2132` |
| `next_vertexshader` / `previous_vertexshader` / `mark_vertexshader` | VS 遍历与标记 | `Hunting.cpp:2134-2136` |
| `next_computeshader` / `previous_computeshader` / `mark_computeshader` | CS 遍历与标记 | `Hunting.cpp:2138-2140` |
| `next_geometryshader` / `previous_geometryshader` / `mark_geometryshader` | GS 遍历与标记 | `Hunting.cpp:2142-2144` |
| `next_domainshader` / `previous_domainshader` / `mark_domainshader` | DS 遍历与标记 | `Hunting.cpp:2146-2148` |
| `next_hullshader` / `previous_hullshader` / `mark_hullshader` | HS 遍历与标记 | `Hunting.cpp:2150-2152` |
| `next_rendertarget` / `previous_rendertarget` / `mark_rendertarget` | 渲染目标遍历与标记 | `Hunting.cpp:2154-2156` |
| `done_hunting` | 结束定位 | `Hunting.cpp:2158` |
| `reload_fixes` | 从 `ShaderFixes` 重载着色器补丁 | `Hunting.cpp:2160` |
| `show_original` | 按住时临时显示原始效果（`DisableFix`/`EnableFix`） | `Hunting.cpp:2162` |
| `analyse_frame` | 触发 / 结束帧分析 | `Hunting.cpp:2164` |
| `kill_deferred` | 关闭/恢复延迟上下文的命令执行 | `Hunting.cpp:2172` |
| `tune1_up` … `tune4_down` | 参数微调（4 组） | `Hunting.cpp:2177-2183` |

> 上表按源码原样列出，三组几何/域/外壳着色器命令分别对应 GS/DS/HS（`Hunting.cpp:2142-2152`）。

### 2.3 其它 `[Hunting]` 选项与默认值

| 选项 | 默认值 | 含义 | 证据 |
| --- | --- | --- | --- |
| `hunting` | `0` | 定位模式，见 §2.1 | `Hunting.cpp:2065` |
| `overlay_buffer_hash_lifetime` | `-1` | VB/IB 的 Hash 在覆盖层缓存里保留多少帧后被清理；`>= 0` 时 `PurgeStaleVisitedBufferHashes()` 每帧在 `HackerSwapChain::Present()` 开头清理一次过期项 | `Hunting.cpp:2067-2073`、`Hunting.cpp:2224-2248` |
| `repeat_rate` | `8` | 遍历类命令的按住重复速率 | `Hunting.cpp:2103` |
| `marking_mode` | `MarkingMode::INVALID`（未选中任何标记方式） | 标记方式；配置重载时若该键未变则**不重置**当前标记方式 | `Hunting.cpp:2105-2110` |
| `marking_actions` | `MarkingAction::DEFAULT` | 标记时执行的动作集合，按枚举字符串解析 | `Hunting.cpp:2113-2117` |
| `analyse_options` | `FrameAnalysisOptions::INVALID`（不写即无任何导出） | 帧分析的默认导出选项，见 §3.3 | `Hunting.cpp:2165-2169` |
| `tune_enable` | `false` | 打开微调 | `Hunting.cpp:2174` |
| `tune_step` | `1.0` | 微调步长 | `Hunting.cpp:2175` |
| `verbose_overlay` | `false` | 覆盖层详细输出 | `Hunting.cpp:2185` |
| `dump_mips` | `true` | 导出纹理时包含 mip | `Hunting.cpp:2186` |
| `monitor_performance_interval` | `1.0`（秒） | 性能监视刷新间隔，内部乘 1e6 存成微秒 | `Hunting.cpp:2089` |

补充规则：`overlay_buffer_hash_lifetime` 缺省为 `-1`（不清理）；**若同时开启了 `[Rendering] track_region_hashes` 且该项仍为负，会被改成 `0`**（`Hunting.cpp:2072-2073`），即每帧清空缓存。

### 2.4 定位面板的日志线索

遍历时日志会打印当前集合大小与选中位置（`Hunting.cpp:1479-1494`）：

```
> traversing to next vertex buffer #3. Number of vertex buffers in frame: 12
> starting at vertex buffer #0. Number of vertex buffers in frame: 12
```

这些行只在 `d3d11_log.txt` 的 `info` 级别下可见（见 §4.1），是“到底有没有捕获到目标”的直接证据。

### 2.5 `reload_fixes` 的文件约定

`reload_fixes` 只扫描**严格命名**的文件（`Hunting.cpp:1261-1263`）：

```
<ShaderFixes>\<16位十六进制>-<用途>.txt     例如 00aa7fa12bbf66b3-ps_replace.txt
```

源码用 `????????????????-??*.txt` 匹配，因此命名不合规的文件**不会被重载**。重载结果只有两条提示：

- 成功：`> successfully reloaded shaders from ShaderFixes`（`Hunting.cpp:1280`）
- 失败：`> FAILED to reload shaders from ShaderFixes` + `BeepFailure()`（`Hunting.cpp:1284-1285`）

两条都是**屏幕提示**（`LogOverlay`，声明见 `src/DirectX11/Overlay.h:124-125`），但同时会按 **WARNING 级别**写入 `d3d11_log.txt`——`LogOverlay` / `LogOverlayW` 内部先调用 `vLogWarning` / `vLogWarningW`（`src/DirectX11/Overlay.cpp:936`、`:970`），因此 `log_level >= warning` 时可以在日志里搜到这两行。失败那条是 `LOG_WARNING` 级，屏幕显示还要求 `show_warnings = 1`（`Overlay.cpp:926`、`:960`）。

另外，未在本次重载中被访问到的着色器会被 `RevertMissingShaders()` 还原为原始着色器（`Hunting.cpp:1273-1276`）——这是“改了补丁画面反而恢复原状”的常见原因。

---

## 3. 帧分析（Frame Analysis）

### 3.1 触发与前置条件

`AnalyseFrame()`（`src/DirectX11/Hunting.cpp:1319-1373`）按顺序检查：

1. **必须能取到 `IID_FrameAnalysisContext`**。取不到时屏幕上给出
   `Frame Analysis Context is missing: Restart the game with hunting enabled`（`Hunting.cpp:1326-1329`），并直接返回——这条提示意味着**本次会话的上下文不是被分析的上下文**，按提示重启游戏并开着 hunting 启动即可。
2. **不能重复触发**：分析已在进行时再次按下，会在分析日志里写 `----- Frame analysis aborted -----` 并结束本次会话（`Hunting.cpp:1332-1338`）。
3. **`hunting` 必须处于 ENABLED**，否则静默返回（`Hunting.cpp:1340-1341`）。

### 3.2 输出目录与日志文件

| 项目 | 值 | 证据 |
| --- | --- | --- |
| 目录名 | `FrameAnalysis-%Y-%m-%d-%H%M%S`（精度 1 秒） | `Hunting.cpp:1343-1345` |
| 目录位置 | `d3d11.dll` 同目录 | `Hunting.cpp:1347-1350` |
| 创建失败 | `Error creating frame analysis directory: <GetLastError()>`，本次分析不启动 | `Hunting.cpp:1357-1360` |
| 即时上下文的日志 | `<目录>\log.txt` | `src/DirectX11/FrameAnalysis.cpp:120-128` |
| 延迟上下文的日志 | `<目录>\log-0x<p context>.txt` | `FrameAnalysis.cpp:126`、`FrameAnalysis.cpp:183` |
| 延迟上下文的资源 | 放在 `<目录>\ctx-0x<p context>\` 子目录 | `FrameAnalysis.cpp:2039-2043`、`FrameAnalysis.cpp:2148-2152` |
| 编码 | UTF-8（`WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)`）；写失败给出 `Frame analysis UTF-8 log write failed` | `FrameAnalysis.cpp:56-67`、`FrameAnalysis.cpp:144-146`、`FrameAnalysis.cpp:201-202` |

日志正文格式（`FrameAnalysis.cpp:135`、`FrameAnalysis.cpp:140-142`）：

```
analyse_options: 00000000
000001 VSSetConstantBuffers(StartSlot:0, NumBuffers:1, ppConstantBuffers:0x...)
...
```

- 首行固定是 `analyse_options: %08x`，即**本次会话实际生效的选项位掩码**，可直接与 §3.3 的选项表对照；
- 每行前缀是 6 位 draw 计数器（`%06u `），与资源文件名里的 draw 编号一致；
- 开启 `hold` 时前缀额外带帧号 `N.`（`FrameAnalysis.cpp:140-141`）；
- 延迟上下文的分析日志分开写，`log-*.txt` 与 `log.txt` 互不覆盖。

### 3.3 `analyse_options` 全部选项

来源：`src/DirectX11/globals.h:192-243`（`FrameAnalysisOptionNames`），解析入口 `parse_enum_option_string<wchar_t *, FrameAnalysisOptions>`（`Hunting.cpp:2165-2169`）。**默认值全部为不写即不生效**；`analyse_options` 缺省时是 `FrameAnalysisOptions::INVALID`（`Hunting.cpp:2169`）。

**选项名拼错会被静默忽略**：名字表里查不到的 token 会被丢弃，并在屏幕上给出 `WARNING: Unknown option: <名字>`（`src/common/util.h:344-357`），因此写错一个名字不会报错，只会让对应选项失效（见 §5.1）。

**绑定选择（决定抓什么）**

| 选项 | 含义 |
| --- | --- |
| `dump_rt` | 渲染目标与 UAV |
| `dump_depth` | 深度模板 |
| `dump_tex` | 着色器输入资源（SRV） |
| `dump_cb` | 常量缓冲 |
| `dump_vb` | 顶点缓冲 |
| `dump_ib` | 索引缓冲 |

**格式选择（决定怎么存）**

| 选项 | 含义 |
| --- | --- |
| `jps` / `jpg` / `jpeg` | 2D 纹理转成预览图片 |
| `dds` | 2D 纹理存成 DDS（完整数据，体积可能很大） |
| `jps_dds` / `jpg_dds` / `jpeg_dds` | 可预览时用 JPS，否则回退 DDS（`FMT_2D_AUTO`） |
| `buf` | 缓冲二进制输出 |
| `txt` | 缓冲文本输出（按 `float4` 展示） |
| `desc` | 资源描述 |

**行为控制**

| 选项 | 含义 | 证据 / 注意事项 |
| --- | --- | --- |
| `clear_rt` | 首次使用目标时清空渲染目标 / UAV | `FrameAnalysis.cpp:2837`、`FrameAnalysis.cpp:2869`；**会改变画面** |
| `persist` | 该选项保留到后续帧 | `globals.h:178`；由 `FrameAnalysisTrigger()` 处理（`FrameAnalysis.cpp:2890`） |
| `mono` | 单目输出 | 立体位（`STEREO_MASK = 0x00030000`，`globals.h:180`）在 `FrameAnalysisOptionNames` 里**没有可解析的名字**，INI 无法请求立体输出；非延迟上下文会强制 MONO（`FrameAnalysis.cpp:2977-2978`、`:3034-3035`） |
| `filename_reg` | 文件名先按寄存器排序 | `globals.h:220` |
| `filename_handle` | 文件名追加 `@<指针>` | `globals.h:221` |
| `log` | **已废弃**，解析后不再产生任何效果（现在始终写日志） | `globals.h:222` |
| `hold` | 按住键连续抓帧 | `globals.h:223`；会话在松开时结束 |
| `dump_on_unmap` | 捕获 Map/Unmap 更新 | `globals.h:224` |
| `dump_on_update` | 捕获 UpdateSubresource 更新 | `globals.h:225` |
| `deferred_ctx_immediate` | 用即时上下文抓延迟上下文资源 | `globals.h:226` |
| `deferred_ctx_accurate` | 延后抓取以保留更新后的数据（会额外保存一份资源） | `globals.h:227` |
| `share_dupes` | 去重文件跨抓帧目录共享 | `globals.h:228`；目录变为 `<dll 目录>\FrameAnalysisDeduped`（`FrameAnalysis.cpp:2015-2027`） |
| `symlink` | 去重文件用符号链接代替硬链接 | `globals.h:229` |

**历史组合项（仍可解析）**：`dump_rt_jps`、`dump_rt_dds`、`dump_depth_jps`（源码注释标明 *Doesn't work yet*）、`dump_depth_dds`、`dump_tex_jps`、`dump_tex_dds`、`dump_cb_txt`、`dump_vb_txt`、`dump_ib_txt`（`globals.h:231-240`）。

**未指定格式时的兜底**：`set_default_dump_formats()` 按调用场景分叉（`FrameAnalysis.cpp:2923-2943`）：

- 2D 侧：没有显式纹理格式时补 `FMT_2D_AUTO`（可预览时 JPS、否则 DDS），所有路径一致（`:2926-2927`）。
- 缓冲侧：draw 路径（`draw == true`）只有在命中 `DUMP_XB_MASK`（即 `dump_cb` / `dump_vb` / `dump_ib`）时才补 `FMT_BUF_TXT`，**不补** `FMT_BUF_BIN`（`:2930-2934`）；命令列表 `dump`、`dump_on_update`、`dump_on_unmap` 等非 draw 路径才同时补 `FMT_BUF_TXT` 与 `FMT_BUF_BIN`（`:2935-2941`）。

因此只写 `analyse_options = dump_tex` 会得到可用的纹理输出；只写 `dump_cb` 时 draw 路径**只产出文本**，要二进制文件必须显式写 `buf`。

最小可用组合（先小后大）：

```ini
[Hunting]
hunting = 1
analyse_options = dump_cb dump_vb dump_ib buf txt
```

### 3.4 输出文件命名

`FrameAnalysisFilename()`（`FrameAnalysis.cpp:2029-2137`）拼出的文件名由以下片段组成：

| 片段 | 来源 | 证据 |
| --- | --- | --- |
| `<目录>\` | `G->ANALYSIS_PATH` | `FrameAnalysis.cpp:2038` |
| `ctx-0x<p>\` | 延迟上下文专属子目录 | `FrameAnalysis.cpp:2039-2043` |
| `<帧号>.` | 仅 `hold` 模式 | `FrameAnalysis.cpp:2048-2049` |
| `%06i-` | draw 计数器 | `FrameAnalysis.cpp:2050` |
| `<类型前缀><寄存器><索引>` | 资源种类与槽位（如 `cb0`、`vb1`） | `FrameAnalysis.cpp:2053-2058` |
| `-` + `%06i` | 仅 `filename_reg` | `FrameAnalysis.cpp:2060-2067` |
| `=%08x` | 资源 Hash（当前值） | `FrameAnalysis.cpp:2106` |
| `(%08x)` | 原始 Hash，仅当与当前值不同 | `FrameAnalysis.cpp:2108-2109` |
| `=!…!` | Hash 污染标记 | `FrameAnalysis.cpp:2092-2103` |
| `@%p` | 仅 `filename_handle` | `FrameAnalysis.cpp:2111-2112` |
| `-vs=` / `-hs=` / `-ds=` / `-gs=` / `-ps=` / `-cs=` | 当前绑定的着色器 Hash | `FrameAnalysis.cpp:2114-2127` |

污染标记的读法（`FrameAnalysis.cpp:2092-2103`）：`=!M!` 表示存在 **M**ap 污染，`U` 为 **U**pdate 污染，`C` 为 **C**opy 污染，`S` 为 **S**（region）污染；多个字母会同时出现。看到 `=!MU!` 说明这个 Hash 不可直接用于 `[TextureOverride] hash=`，需要先处理污染。

文本缓冲的内容格式由 `DumpBufferTxt()` 决定（`FrameAnalysis.cpp:824-862`）：按 `float4` 逐分量打印，标签为 `buf[i].c`（通用缓冲）或 `cb0[i].c` / `vb0[i].c` / `ib0[i].c`（带类型与索引）。**它不按 SRV/UAV 的真实元素格式解码**，所以不能把 `buf0[3].y` 直接当成结构体字段读。文件名后缀规则（`-buf0.txt`、`-buf1.txt` 等槽位后缀）见 [`usage/dx11/frame-analysis.md`](../usage/dx11/frame-analysis.md)。

### 3.5 `Dump timing` 汇总

每个上下文独立统计，**在分析日志关闭时追加**（`FrameAnalysis.cpp:87-89`、`FrameAnalysis.cpp:101`、`FrameAnalysis.cpp:158`）。格式由 `src/DirectX11/FrameAnalysisTiming.h:34-41` 固定：

```
%06u 3DMigoto Dump timing <stage>: calls=<n> total_ms=<x.xxx> max_ms=<x.xxx>
```

`<stage>` 取值只有 8 个（`FrameAnalysisTiming.h:34-35`）：

| stage | 含义 |
| --- | --- |
| `capture-inclusive` | Draw/Dispatch 之后的抓取函数累计耗时 |
| `buffer-create` | 创建 staging 资源的耗时 |
| `map-wait` | 导出器直接调用的 Map 等待 |
| `hash` | 去重/命名用的 Hash 计算 |
| `text-save` | 文本缓冲写盘 |
| `binary-save` | 二进制缓冲写盘 |
| `texture-save-inclusive` | 纹理保存调用总耗时 |
| `link` | 去重文件硬链接/符号链接 |

**读数口径（务必遵守）**：

- `capture-inclusive` 与 `texture-save-inclusive` 是**包含式**统计，内部阶段已经计入，**不能与子阶段相加**（`FrameAnalysisTiming.h:17-18` 的注释）；其余阶段是各自独立的调用点。
- 这些数字**不是整次 F8 操作的墙钟耗时**，只是导出器内部各阶段的自计时。
- `map-wait` 只统计导出器自己发起的 Map；纹理保存库内部的 Map 包含在 `texture-save-inclusive` 里。
- 每写一次汇总后计数清零（`FrameAnalysisTiming.h:42`），所以同一次分析里可能看到多条同 stage 记录。
- 计时使用 `QueryPerformanceCounter`，按每个上下文独立汇总。

### 3.6 定点 dump（`pre` / `post`）

在匹配的 `[ShaderOverride]` / `[TextureOverride]` / `[CommandList]` 里可以只导出关心的资源：

```ini
[TextureOverrideExample]
hash = 1a2b3c4d
pre dump = buf txt vs-cb0
post dump = dds o0
```

- `pre` 在原始操作前执行，`post` 在操作后执行。
- `dump` 的值前半段是**帧分析选项**（与 §3.3 同一张表），后半段是**目标**；`dump` 由 `ParseFrameAnalysisDump()` 解析（`src/DirectX11/CommandList.cpp:1012-1063`）。
- 目标名会被写进文件名，`< > : " / \ | ? *` 会被替换成 `_`（`CommandList.cpp:1044-1054`），即形如 `[Section]-target`。
- 目标解析走通用的 `ParseTarget()`（`CommandList.cpp:1037`）；**`POOL` 目标只接受 `POOL_RANGE` 模式**，否则解析失败（`CommandList.cpp:1040-1042`）。
- **目标是选项名之后第一个不再被识别为选项的整段文本**：`parse_enum_option_string` 把第一个未识别的 token 留在 `unrecognised` 里返回（`src/common/util.h:313-316`、`:348-349`），`ParseFrameAnalysisDump()` 再把它交给 `ParseTarget()`（`CommandList.cpp:1031-1037`）；之后再出现的未知 token 会被丢弃并给出 `WARNING: Unknown option: ...`（`util.h:352`）。
- 本节示例中的两个目标都是**有效**写法：`vs-cb0` 匹配常量缓冲槽位格式 `%lcs-cb%u%n`（`CommandList.cpp:9086`，且 `is_shader_resource('v')` 为真，见 `:8952-8959`），`o0` 匹配渲染目标槽位格式 `o%u%n`（`CommandList.cpp:9068`，槽位需小于 `D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT`）。同一张表里的其它关键字还有 `ib`、`od`（深度模板）、`vb<n>`、`so<n>`、`vs-t<n>`、`ps-u<n>`、`iniparams` 等（`CommandList.cpp:9065-9095`）。

改变某次 draw 的分析选项：

```ini
[ShaderOverrideExample]
hash = 1a2b3c4d
analyse_options = dump_tex dds
```

`analyse_options` 由 `FrameAnalysisChangeOptionsCommand` 解析（`src/DirectX11/CommandList.cpp:2554-2582`），执行时调用 `FrameAnalysisTrigger()`（`FrameAnalysis.cpp:2890`）；带 `persist` 时保留到后续帧，不带则只生效一次。

> 注意：命令列表里的 `dump` / `analyse_options` 属于**帧分析会话内部**的动作；如果当前没有进行中的帧分析，它们不会有输出。`FrameAnalysisChangeOptionsCommand::noop()` 在 `G->hunting == HUNTING_MODE_DISABLED` 或帧分析未注册时把命令视为空操作（`CommandList.cpp:2579-2582`）。

### 3.7 帧分析的失败路径

| 现象 | 源码给出的原因 | 处理 |
| --- | --- | --- |
| `Frame Analysis Context is missing: Restart the game with hunting enabled` | 取不到 `IID_FrameAnalysisContext` | 按提示重启并开着 hunting 启动（`Hunting.cpp:1326-1329`） |
| 目录没生成 | `Error creating frame analysis directory: <err>` | 目录已存在（同名时间戳）或无写权限（`Hunting.cpp:1357-1360`） |
| `Skipped dumping Buffer (No buffer formats enabled)` | 选了绑定但没选缓冲格式 | 补 `buf` / `txt`（`FrameAnalysis.cpp:1976`） |
| `Skipped dumping Texture2D (No Texture2D formats enabled)` | 同上，纹理侧 | 补 `dds` / `jps`（`FrameAnalysis.cpp:1986`） |
| `Skipped 3DMigoto resource in slot <slot>` / `Skipped undeclared resource in slot <slot>` | 该槽位绑定的是 DLL 自己的资源或未声明资源 | 换槽位或补声明（`FrameAnalysis.cpp:2486`、`FrameAnalysis.cpp:2490`） |
| `WARNING: dump_on_unmap used on deferred context, but no deferred_ctx options enabled` | 在延迟上下文上用了 `dump_on_*` 但没开延迟上下文选项 | 补 `deferred_ctx_immediate` 或 `deferred_ctx_accurate`（`FrameAnalysis.cpp:3028`） |
| `Max hard links exceeded, rotating deduped file` | 去重文件的硬链接数到达上限 | 自动轮转，无需干预（`FrameAnalysis.cpp:2337`） |
| 结束时 `Frame analysis saved to <目录>` | 会话结束 | 路径就是产物目录（`Hunting.cpp:1316`） |

`hold` 模式结束时还会给出帧数：`Frame analysis hold mode ended after <n> complete frames`（`Hunting.cpp:1387-1388`），`<n>` 用来判断“是不是只抓到半帧”。

---

## 4. 日志

### 4.1 日志开关与级别

```ini
[Logging]
log_level = info
```

`InitializeLogging()`（`src/DirectX11/Logging.cpp:18-30`）在**解析 INI 之前**直接用 `GetPrivateProfileStringW` 读取：

| 情形 | 生效级别 |
| --- | --- |
| `log_level = disabled` | `LogVerbosity::DISABLED` |
| `log_level = warning` | `LogVerbosity::WARNING` |
| `log_level = info` | `LogVerbosity::INFO` |
| `log_level = debug` | `LogVerbosity::DEBUG` |
| 未写 / 写空 / 写不认识的值 | 回退到旧式组合：`calls != 0` → `INFO`；`calls != 0 && debug != 0` → `DEBUG`；否则 `DISABLED`（`Logging.cpp:6-16`） |

级别语义（`src/common/log.h:19-48`）：`LogWarning*` 需要 ≥ WARNING，`LogInfo*` 需要 ≥ INFO，`LogDebug*` 需要 DEBUG；`gLogDebug` 只在 DEBUG 级为真（`Logging.cpp:29`）。

**日志文件打不开就没日志**：`LogFile = _wfsopen(log_path, L"w", _SH_DENYNO)`，失败时 `gLogVerbosity` 被强制为 `DISABLED`（`Logging.cpp:26-28`）。目录不可写或文件被独占锁定时表现就是“完全没有日志”，此时应先确认 `d3d11.dll` 同目录可写。

日志开头会回显生效配置，包括 `----------- d3dx.ini settings -----------` 之后的各节与 `[Logging] log_level=<n>`（`IniHandler.cpp:5065-5068`）——**这是确认“某个设置到底生效没有”的第一手证据**，**前提是 `log_level = info` 或 `debug`**：这几行都是 `LogInfo`（`IniHandler.cpp:5065`、`:5067-5068`），而同一函数里的启动横幅 `D3D11 DLL starting init`、`Game path:`、`3DMigoto path:` 是 `LogWarning`（`:5057`、`:5062-5063`），所以 `log_level = warning` 时只能看到横幅，看不到设置回显。

### 4.2 其它 `[Logging]` 选项

| 选项 | 默认 | 作用 | 证据 |
| --- | --- | --- | --- |
| `calls` | `0` | 旧式开关，等价于 `log_level = info` | `Logging.cpp:24` |
| `debug` | `0` | 旧式开关，与 `calls` 同开时等价 `log_level = debug` | `Logging.cpp:25` |
| `unbuffered` | `false` | `setvbuf(_IONBF)`，无需 `fflush` 即可实时看到日志 | `IniHandler.cpp:5076-5080` |
| `force_cpu_affinity` | `false` | `SetProcessAffinityMask(process, 1)`，把进程绑到 0 号 CPU | `IniHandler.cpp:5083-5088` |
| `waitfordebugger` | `0` | `>0` 时循环等待调试器附加；`>1` 时附加后 `__debugbreak()` | `IniHandler.cpp:5091-5100` |
| `crash` | `false` | 安装崩溃处理器（`install_crash_handler`） | `IniHandler.cpp:5102-5104` |
| `dump_all_profiles` | `false` | 导出全部 profile | `IniHandler.cpp:5106` |
| `debug_locks` | `false` | 打开锁依赖检查 | `IniHandler.cpp:5108-5109` |
| `show_warnings` | `true` | 是否在配置加载后发出失败提示音 | `IniHandler.cpp:5111` |

`show_warnings` 特殊在它是**提前读取**的：`InitializeEarlyWarningSettings()` 直接从文件读，因为此时 INI 还没进解析表（`IniHandler.cpp:1570-1587`）。

### 4.3 屏幕通知与提示音

INI 解析告警走 `IniWarning` / `IniWarningW` 宏：既 `LogOverlay(LOG_WARNING, ...)` 打到屏幕，又置位 `ini_warned`（`IniHandler.cpp:298-305`）；同一条文本还会按 WARNING 级别写入 `d3d11_log.txt`（`Overlay.cpp:936`、`:970`）。配置（重）载结束时 `emit_ini_warning_tone()` 在 `G->gShowWarnings` 为真时发一次失败提示音（`IniHandler.cpp:310-317`）。

也就是说：**屏幕上的黄字 + 一声蜂鸣 = 有配置告警**。两者都要求 `show_warnings = 1`（默认）：`show_warnings = 0` 会同时关掉蜂鸣（`IniHandler.cpp:310-317`）**和** `LOG_WARNING` 级的屏幕提示（`Overlay.cpp:926`、`:938`、`:960`、`:972`），但告警文本仍会按 WARNING 级别写进 `d3d11_log.txt`（`Overlay.cpp:936`、`:970`），只要 `log_level >= warning`。`LOG_INFO` 级的提示（如 `> successfully reloaded shaders from ShaderFixes`）始终显示，不受 `show_warnings` 影响（`Overlay.cpp:926` 的 `level == LOG_INFO` 判定）。

### 4.4 采集一份可用的日志

```powershell
# 1) 关闭游戏后取日志尾部（TIMING 版日志很大，不要整文件读）
Get-Content .\d3d11_log.txt -Tail 200

# 2) 只想看配置告警
Select-String -Path .\d3d11_log.txt -Pattern 'Unknown section|Duplicate|not found|rejected|Cannot|Failed'
```

排查建议：

1. 先把 `log_level` 设为 `debug` 复现一次，再降到 `info` 长期使用——`debug` 会把每次 API 包装调用写进日志，体积增长很快。
2. 需要“边跑边看”时加 `unbuffered = 1`，日志立即落盘（`IniHandler.cpp:5076`）。
3. 提交问题时的清单见 [`usage/dx11/troubleshooting.md`](../usage/dx11/troubleshooting.md)（游戏与版本、DLL 版本、加载方式、复现步骤、相关 INI、`d3d11_log.txt`）。

---

## 5. 配置 / 编译 / 匹配 / 加载故障

### 5.1 INI 解析类

以下告警文本出自 `src/DirectX11/IniHandler.cpp`（`IniWarning`/`IniWarningW`）：

| 告警（原文） | 位置 | 含义与处理 |
| --- | --- | --- |
| `Unknown section type` + 换行 + ` - [%ls] @ [%ls]` | `:508` | 节名不在白名单内。该消息源码含 `\n`，屏幕/日志上是两行。合法节见 `:96-142`：命令列表类 `TextureOverride`/`CommandList`/`Constants`/`Present`/`ShaderOverride`/`CustomShader`/`ShaderRegex`/`BuiltInCommandList`/`BuiltInCustomShader`/`ClearRenderTargetView`/`ClearDepthStencilView`/`ClearUnorderedAccessViewUint`/`ClearUnorderedAccessViewFloat`；普通节 `Pool`/`Resource`/`Key`/`Include`/`Preset`/`Hunting`/`Logging`/`System`/`Input`/`Device`/`Rendering`/`Loader`/`Profile`/`Stereo`/`ConvergenceMap` |
| `Duplicate section found` | `:481` | 同名节重复 |
| `Empty section header` | `:446` | `[` 后面没有名字 |
| `Entry outside of section` | `:568`、`:581` | 节外出现键值行 |
| `No value found for: "%ls"` | `:595` | 键后面没有 `=` 或值为空 |
| `Duplicate key found` / `Duplicate non-command list key found` | `:617`、`:2662` | 同节内重复键 |
| `Malformed line` | `:626` | 行格式无法解析 |
| `Unrecognised entry` / `Unrecognised %ls=%ls` | `:1908`、`:2698`、`:3984`、`:4181` | 键不被该节接受 |
| `WARNING: Unknown option: %s` | `src/common/util.h:352` | 选项字符串里有名字表不认识的项（例如 `analyse_options` 把 `dump_tex` 写成 `dump_texture`），该项被丢弃 |
| `Scope unbalanced` | `:2707` | `if/else/endif` 不配对 |
| `File included multiple times` | `:1883` | 同一文件被重复 include |
| `INI file is UTF-16 encoded, not UTF-8; re-save as UTF-8` | `:861` | **存成 UTF-8** |
| `Invalid UTF-8 in %ls at line %Iu; invalid bytes replaced with U+FFFD` | `:872` | 非法字节被替换为 U+FFFD |
| `Hash parse error` / `Hex string parse error` | `:1601`、`:1625` | Hash 必须是纯十六进制 |
| `Unknown Enum` / `Unrecognised Enum` / `Enum value out of range` | `:1704`、`:1676`、`:1794` | 枚举值拼错或越界 |
| `"%ls=%ls" too long` | `:1117` | 值超出长度上限 |
| `Detected a conflicting d3dx.ini in the game directory that is not being used.` | `:4767` | 游戏目录里另有一份同名 INI 未被使用 |

> **没有 `=` 的行**：`[Profile]` 与 `[ShaderRegex]` 两个节允许出现不含 `=` 的行（例如多行的 profile 值），这类行不会触发 `No value found for` / `Malformed line`（`IniHandler.cpp:138-141`、`:511-512`）。

### 5.2 匹配类

| 告警（原文） | 位置 | 含义 |
| --- | --- | --- |
| `Section missing Hash=` | `:2941` | `[ShaderOverride]` 没有 Hash |
| `Possible Mod Conflict: Duplicate ShaderOverride hash=%16llx` | `:2866` | 两个 ShaderOverride 用同一个 Hash |
| `Section missing Hash= or valid match options` | `:3883` | `[TextureOverride]` 既没有 Hash 也没有匹配条件 |
| `Cannot use hash= and match options together!` | `:3888` | 同一节里同时写了 `hash=` 与匹配选项 |
| `Possible Mod Conflict: Duplicate TextureOverride hash=%08lx` | `:3803` | 两个 TextureOverride 用同一个 Hash |
| `Section can never match any resources` | `:3767` | 匹配条件自相矛盾，永远匹配不上 |
| `RegEx section missing shader_model` + 换行 + ` - [%ls]` | `:3047` | `[ShaderRegex]` 缺少 `shader_model`（源码里 `shader_model` 后面**没有** `=`） |
| `Invalid ShaderRegex exclude_hash=` | `:3054` | `exclude_hash` 值非法 |
| `Missing corresponding pattern section` / `Missing section` | `:3150`、`:3185` | ShaderRegex 组的模式节缺失 |

排查顺序建议（每步都能在 `d3d11_log.txt` 里验证）：

1. **先确认节被解析**：日志里出现该节名且没有 `Unknown section type`。
2. **再确认 Hash 合法**：没有 `Hash parse error`；Hash 是 16 位十六进制（Shader）/ 8 位（Texture）。
3. **再确认不冲突**：没有 `Possible Mod Conflict`。
4. **最后确认匹配条件不是死条件**：没有 `Section can never match any resources`。
5. 用 §2 的 hunting 面板拿到**实际** Hash，替换 INI 里手抄的 Hash——这是最快的定位手段。

### 5.3 着色器编译 / 替换类

| 现象 | 证据 | 处理 |
| --- | --- | --- |
| 补丁不生效 | `reload_fixes` 只扫描 `<16位十六进制>-<用途>.txt`（`Hunting.cpp:1263`） | 改成严格命名 |
| 重载整体失败 | `> FAILED to reload shaders from ShaderFixes` + 蜂鸣（`Hunting.cpp:1284-1285`） | 该消息在 `log_level >= warning` 时也会进入 `d3d11_log.txt`（`Overlay.cpp:936`、`:970`）；它对应文件名 Hash 非法、编译产物为空或创建着色器对象失败（`Hunting.cpp:788-792`、`:856-857`、`:901-902`） |
| HLSL 编译报错 | 错误文本走**屏幕提示**：`LogOverlay(LOG_NOTICE, "%s\n", errMsg)`（`Hunting.cpp:667`，源码注释写着 “If there are errors they go to the overlay”）；**只有编译警告**才写日志文件（`:669-675`） | 先看屏幕上的编译错误；日志里对应 `compile result for replacement HLSL shader: %x`（`:656`）与 BEGIN/END 段（`:663`、`:676`），需要 `log_level >= info` |
| 汇编失败 | `LogOverlay(LOG_NOTICE, "Error assembling %S: %s\n", ...)`（`Hunting.cpp:710-711`） | 按屏幕提示修正汇编语法 |
| 改完画面反而变原样 | `RevertMissingShaders()` 会把本次未被访问的着色器还原（`Hunting.cpp:1273-1276`） | 确认补丁文件命名与目标 Hash 对应 |
| 反编译不成功 | 标记着色器时尝试导出 HLSL/汇编，**不保证成功**（见 [`usage/dx11/hunting.md`](../usage/dx11/hunting.md)） | 退而使用导出的汇编 |
| 需要打开着色器处理开关 | `[Rendering]` 下有 `export_shaders`、`export_hlsl`（整数值）、`export_binary`、`export_fixed`、`assemble_signature_comments`、`disassemble_undecipherable_custom_data`、`patch_assembly_cb_offsets`、`fix_sv_position`、`recompile_all_vs`、`dump_usage`（`IniHandler.cpp:5273-5294`） | 按需开启，代价是启动变慢；`export_fixed` 与 `dump_usage` 见下 |

**`dump_usage` 与 `export_fixed`**：

- `dump_usage`（`IniHandler.cpp:5283`，默认 `false`）打开后，**帧分析会话结束**时会调用 `DumpUsage(G->ANALYSIS_PATH)`，把资源使用报告写进本次分析目录（`Hunting.cpp:1308-1317`）。它对应 §7.2 里的 `dump_usage overhead` 这一项性能开销。
- `export_fixed`（`IniHandler.cpp:5279`，默认 `false`）控制是否导出修复后的着色器。
- 其余着色器处理开关（`export_shaders`、`export_hlsl`、`export_binary`、`assemble_signature_comments`、`disassemble_undecipherable_custom_data`、`patch_assembly_cb_offsets`、`fix_sv_position`、`recompile_all_vs`）默认都是 `false` / `0`（`IniHandler.cpp:5279-5294`），只在需要时开启。

### 5.4 资源加载类

| 告警（原文） | 位置 | 处理 |
| --- | --- | --- |
| `Resource filename rejected: module path missing or too long` / `path too long` | `:2373`、`:2379`、`:2389`、`:2394` | 缩短路径或改为绝对路径 |
| `Unknown format "%ls"` | `:2406` | `[Resource]` 的 `format` 拼错 |
| `Unknown %ls "%ls"` | `:2420` | 匹配类键值非法 |
| `Invalid match_confidence; using 0.75` | `:2430` | 置信度越界，已回退 |
| `Initial data currently only supported on buffers` | `:2213` | `initial_data` 只能用于 buffer |
| `Initial data and filename cannot be used together` | `:2219` | 二选一 |
| `Unsupported format for specifying initial data as text` | `:2190` | 初始数据格式不支持 |
| `Pool variables persistence is not supported for "%ls" index type` | `:2516` | 该索引类型不支持持久化 |
| `Specified spatial radius "%d" is below minimum "1"` | `:2497` | 半径至少为 1 |
| `Resource pool rejected: template filename path too long` | `:2506` | 缩短模板路径 |

资源文件的查找规则（相对所属 INI 优先）见 [`usage/dx11/configuration.md`](../usage/dx11/configuration.md)；把 INI 移目录时必须一起移动配套资源，或改写 `filename`。

### 5.5 一条可复查的排错流程

1. `Get-Content .\d3d11_log.txt -Tail 200` 取日志尾部（[`usage/dx11/troubleshooting.md`](../usage/dx11/troubleshooting.md)）。
2. 搜索 `Unknown section type|Duplicate|Malformed|Unrecognised|Cannot|Failed|rejected`，把告警按 §5.1/§5.2/§5.4 归类。
3. 修 INI → **按 F10 重载** → 回到第 1 步，直到没有新告警。
4. 仍然不生效：开 `hunting = 1`，用 §2 拿到真实 Hash/资源，再写 `[TextureOverride]` / `[ShaderOverride]`。
5. 需要看 DLL 内部到底做了什么：把 `log_level` 提到 `debug` 再复现。

> 只有 DLL 加载、设备创建与部分 `[System]` 设置需要重启游戏；INI 与 Mod 资源用 F10 重载即可（[`usage/dx11/configuration.md`](../usage/dx11/configuration.md)）。

---

## 6. 调试构建与回归

### 6.1 只构建 DX11 Release

```powershell
powershell -File scripts\build_release_dx11_x64.ps1
```

该脚本（`scripts/build_release_dx11_x64.ps1`）用 `vswhere` 找 MSVC、导入 `vcvars64.bat`，然后：

```
cmake -S <root> -B build\dx11-release-x64 -G Ninja "-DCMAKE_MAKE_PROGRAM=<ninja 路径>" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDX12_RELEASE_TIMING=OFF
cmake --build build\dx11-release-x64 --target DirectX11 --parallel 6
```

产物：`build\dx11-release-x64\src\DirectX11\d3d11.dll`。它**不部署、不启动游戏**。

上面是脚本调用的摘录：脚本自己用 `Get-Command` 解析 cmake 与 ninja 的路径（`scripts/build_release_dx11_x64.ps1:41-42`），并显式传入 `-DCMAKE_MAKE_PROGRAM=<ninja>`（`:43-46`），所以 ninja 不必在 PATH 上；实际以脚本为准。

### 6.2 Debug 构建

```powershell
powershell -File scripts\build-cmake.ps1 -Configuration Debug -Platform x64
```

- 构建目录：`build\cmake-x64-debug`；产物复制到 `x64\Debug\`（`scripts/build-cmake.ps1:71`、`:207-223`）。
- **注意**：不带 `-ReleasePackage` 时脚本构建的 target 是 `DirectX12`（`scripts/build-cmake.ps1:172`）。要同时构建 DX11 请显式加开关：

```powershell
powershell -File scripts\build-cmake.ps1 -Configuration Debug -Platform x64 -ReleasePackage
```

- Debug 配置会定义 `_DEBUG_LAYER=1` 并链接 pcre2 调试库（`src/DirectX11/CMakeLists.txt:115-120`、`:147`）；`DX12_RELEASE_TIMING` 只影响 DX12，对 DX11 无效。
- DX11 保持 C++17（根 `CMakeLists.txt:10-13`、`src/DirectX11/CMakeLists.txt:111`）。

### 6.3 回归测试

`-DBUILD_TESTING=ON` 时会额外生成 `dx11_upstream_regression` 等目标并注册 ctest 用例（`src/DirectX11/CMakeLists.txt:157-457`）：

| ctest 名称 | 说明 |
| --- | --- |
| `dx11_upstream_regression` | 主回归集（超时 60 秒，`src/DirectX11/CMakeLists.txt:443`） |
| `dx11_overlay_text_regression` | 覆盖层文字回归 |
| `dx11_texture_match_regression` / `dx11_texture_match_hooked_regression` | 纹理匹配回归 |
| `dx11_proxy_smoke` | 代理 DLL 冒烟（超时 30 秒） |
| `dx11_legacy_version_regression` | 旧版版本号读取回归 |
| `dx11_version_generator_regression` | 版本头生成脚本回归（需要 Python 与 Git） |

已公开的运行方式（**本轮未执行**）：

```powershell
ctest --test-dir build\dx11-dump-validation --output-on-failure
```

该命令出自 [`usage/dx11/troubleshooting.md`](../usage/dx11/troubleshooting.md)，用于 Overlay 专项（覆盖 Unicode、并发缓存、GDI 句柄释放、COM 状态不变、WARP 合成与交换链缩放）。文档同时写明：**它不替代目标游戏全屏、HDR、内核驱动与其他 Hook 的兼容性验收**。

### 6.4 上游同步检查

```powershell
python scripts\check-dx11-upstream.py
```

报告写在 `build\upstream\dx11-check.json`；基线与逐提交状态在 `docs\dx11-upstream-sync.json`，规则见仓库根 [`AGENTS.md`](../AGENTS.md) 与 [`docs/DX11-Upstream-Sync.md`](DX11-Upstream-Sync.md)。**本轮由经理执行上游检查，没有新提交。**

---

## 7. 性能观察

### 7.1 性能监视器

```ini
[Hunting]
monitor_performance_interval = 1.0
```

- `monitor_performance` 每次触发把 `Profiling::mode` 往前推一档（`Hunting.cpp:1393-1406`），模式枚举见 `src/DirectX11/profiling.h:9-17`：

  | 模式 | 覆盖层内容 |
  | --- | --- |
  | `NONE` | 不显示 |
  | `SUMMARY` | CPU 汇总 + Map 查找 + GPU 影响统计 |
  | `TOP_COMMAND_LISTS` | 命令列表 Top 表 |
  | `TOP_COMMANDS` | 单条命令 Top 表 |
  | `CTO_WARNING` | 提示隐式 post `checktextureoverride` 未能被优化掉的命令 |

  `CTO_WARNING` 在没有隐式 post 检查时会自动跳过（`Hunting.cpp:1398-1399`）。

- `freeze_performance_monitor` 冻结当前画面，并把已生成的报告写进日志（`Hunting.cpp:1408-1422`）——**这是把性能报告离线分析的主要途径**，但有三个前提：
  1. 必须先按 `monitor_performance` 选到非 `NONE` 模式：`NONE` 是初始模式，`FreezePerf()` 在 `NONE` 下直接返回，既不冻结也不写日志（`Hunting.cpp:1413-1414`）。
  2. 日志级别必须是 `info` 或 `debug`：落盘走的是 `LogInfoW`（`Hunting.cpp:1421`），需要级别 ≥ INFO（`src/common/log.h:34-37`）。
  3. 写进日志的是**冻结瞬间**的 `Profiling::text` 快照（`Hunting.cpp:1418`），而采样间隔未到时该文本不会刷新（`src/DirectX11/profiling.cpp:514-515`），因此快照可能落后一个采样窗口。
- 刷新间隔由 `monitor_performance_interval` 控制（默认 1.0 秒，`Hunting.cpp:2089`）；`Profiling::update_txt()` 在间隔未到且已有内容时跳过更新（`src/DirectX11/profiling.cpp:514-515`）。

### 7.2 SUMMARY 模式能读到什么

`update_txt_summary()`（`src/DirectX11/profiling.cpp:203-403`）输出三组：

**CPU Performance Summary**：`Present overhead`、`Overlay overhead`、`Draw call overhead`、`Command lists total`、`Map/Unmap overhead`、`Pipeline cache invalidation`、`Blocking readback Map`、`IniParams upload`、`track_texture_updates`、`track_region_hashes`、`dump_usage overhead`、`ShaderRegex overhead`、`Mouse cursor overhead`。

**Map Lookups (CPU)**：`Shader hash`、`Live reloaded shaders`、`Original shaders`、`ShaderOverride`、`Texture hash / info`、`TextureOverride`、`Resource pools`，每项都带命中/总数。

**GPU Performance Impacting Stats (costs are guidelines only)**：`IniParams GPU resource updates`、`Full resource copies`（标注 High cost）、`By-Reference resource copies`（Low cost）、`Inter-device resource copies`（Extremely high cost）、`MSAA resources resolved`（High cost）、`Region buffer copies`、`Resources cleared`、`Resources [re]created`（High cost）、`Resource pool swaps`（Low cost）、`max_copies_per_frame exceeded`、`Injected draw/dispatch calls`、`Skipped draw calls`、`max_executions_per_frame exceeded`。

另有 `TO candidates rebuild`、`TO candidates lookup`、`Texture update hits`（`profiling.cpp:389-402`）。

命令列表 Top 表按“含子列表 / 不含子列表”两列给出 CPU 每帧耗时与 fps 代价，并标出 `pre` / `post` 与所属节名（`profiling.cpp:405-442`）；命令 Top 表按命令行的 `pre` / `post` 耗时排序（`profiling.cpp:444-491`）。

### 7.3 读数口径（不要误读）

- **`~fps` 是代价估计，不是实测帧率**。源码注释写明：“fps estimate based on the assumption that if we took 100% CPU time it would cost all 60fps”（`profiling.cpp:424-425`、`:465-466`）。所有 `~xxfps` 应读作“若把全部 CPU 时间都算进去会掉多少帧”的量级估计。
- 所有耗时都是 **CPU 时间**（`QueryPerformanceCounter`），不含 GPU 完成时间；源码里 GPU 统计的注释明确写着 “costs are guidelines only”。
- `present_overhead` 已扣除 `overlay_overhead`，即 HUD 自身的开销单独列出而不是重复计入（`profiling.cpp:224-231`）。
- 采样窗口按 `monitor_performance_interval` 切分，窗口切换时会清零计数（`profiling.cpp:544-588`），因此不同窗口之间不能直接相加。
- `Profiling::clear()` 在每次切换模式时调用（`Hunting.cpp:1405`），**切模式会丢弃上一窗口的累计值**。
- 帧分析里的 `Dump timing` 是导出器内部耗时，与这里的 DLL 开销统计是**两套独立口径**，不要相加（见 §3.5）。

### 7.4 帧分析自身的开销

帧分析会显著增加开销并占用磁盘：

- `hold` 连续抓帧 + `dds` 全量纹理会快速吃满磁盘（[`usage/dx11/frame-analysis.md`](../usage/dx11/frame-analysis.md)）。
- `clear_rt` 会在首次使用目标时清空渲染目标/UAV，**会改变被观测画面本身**（`FrameAnalysis.cpp:2837`、`:2869`）。
- 延迟上下文默认不全局抓取；需要精确定位资源时才用 `deferred_ctx_accurate`，代价是额外保存一份资源（`globals.h:227`）。

---

## 8. 附录

### 8.1 标识符速查

| 标识符 | 含义 | 位置 |
| --- | --- | --- |
| `ParseHuntingSection()` | 解析 `[Hunting]` 全部选项与键绑定 | `src/DirectX11/Hunting.cpp:2056` |
| `FrameAnalysisOptionNames` | 帧分析选项名表 | `src/DirectX11/globals.h:192` |
| `FrameAnalysisOptions` | 帧分析选项位掩码 | `src/DirectX11/globals.h:139` |
| `FrameAnalysisContext` | 帧分析上下文（`IID_FrameAnalysisContext`） | `src/DirectX11/FrameAnalysis.h` |
| `FrameAnalysisFilename()` | 资源文件名生成 | `src/DirectX11/FrameAnalysis.cpp:2029` |
| `FrameAnalysisTiming` / `DumpTimer` / `DumpStage` | 导出阶段计时 | `src/DirectX11/FrameAnalysisTiming.h` |
| `ParseFrameAnalysisDump()` | 解析 `dump` 命令 | `src/DirectX11/CommandList.cpp:1012` |
| `FrameAnalysisChangeOptionsCommand` | 解析 `analyse_options` 命令 | `src/DirectX11/CommandList.cpp:2554` |
| `InitializeLogging()` / `ParseLogVerbosity()` | 日志开关与级别 | `src/DirectX11/Logging.cpp:18`、`:6` |
| `LogVerbosity` | `DISABLED`/`WARNING`/`INFO`/`DEBUG` | `src/common/log.h:21` |
| `IniWarning` / `IniWarningW` / `emit_ini_warning_tone()` | INI 告警与提示音 | `src/DirectX11/IniHandler.cpp:298`、`:310` |
| `Profiling::mode` | 性能监视模式 | `src/DirectX11/profiling.h:9` |
| `Profiling::update_txt_summary()` | 性能汇总文本 | `src/DirectX11/profiling.cpp:203` |

### 8.2 本文未覆盖 / 待确认

- 本文所有结论均来自源码静态阅读；**没有在本轮启动游戏验证 hunting 面板、帧分析产物、性能数值或任何示例 INI 的运行结果**。
- 帧分析产物的**具体文件名示例**由源码的格式化规则推导（`%06i-`、`=%08x`、`-vs=%016I64x` 等），未与真实抓帧目录逐一比对。
- `[Hunting]` 各命令的**默认按键**不在本仓库内（`config/d3dx.ini` 明确是 DX12 配置），键位以游戏适配包与 [`usage/dx11/hunting.md`](../usage/dx11/hunting.md) 为准。
- `usage/dx11/troubleshooting.md` 提到的 `build/dx11-dump-validation` 构建目录本轮不存在，相关 ctest 未运行。
- DX12 侧的对应能力不在本文范围，见 [`docs/DX12-崩溃自诊断.md`](DX12-崩溃自诊断.md)。
