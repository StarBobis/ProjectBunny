# DX11 日志等级独立接线审查

**集成后更新：** 下文保留最初只读快照。随后已修复早期 show_warnings（main/profile共用同源bool/表达式helper），显式空level恢复上游legacy回退，补同锁只读notice快照。真实FILE输出包含窄/UTF16LE片段，测试按真实标记读取而未改变生产编码。最终DX11 CTest1/1（1.31s）及真实DX12兼容构建均通过；profile helper已直接执行其解析/队列路径。主入口show=true语法错误提示音、UI实际渲染和真正DLL卸载仍未实测。


本审查读取 `440ac3a99d20cbcbaf81e08ea4790bea7571e08a`、`a24d95f23bfc92a7f9c08956f563e2471152f1c7` 完整补丁、`DX11-Upstream-After-PNG-Audit.md`、本地日志实现及现有回归源码。只读生产与测试；本次未构建、未运行测试，以下结论为源码接线证据，不是运行 PASS。主代理正在进行的构建结果不计入本文。

## 确定问题：早期 INI 警告使用上一次显示设置

审查时 `IniHandler.cpp::LoadConfigFile` 在 `InitializeLogging` 后调用 `ParseIniFile`（约4543行），而 `G->gShowWarnings = GetIniBool(...show_warnings...)` 在其后（约4584行）。`Globals` 将该字段初始化为 true。解析器在重复 section、重复 key、缺值和 malformed 行上立即调用 `IniWarningW`；后者立即进入 `LogOverlayW`，按当时的 `G->gShowWarnings` 决定是否将消息加入 notices 队列。

因此首次配置含 `show_warnings=0` 时，早期语法警告仍可加入屏幕队列；重载 true→false 和 false→true 时，早期警告使用旧显示策略。后面更新字段不能撤销已排队消息。末尾 beep 读取的是新字段，因此这个问题具体影响早期 overlay 排队，不应笼统称为所有警告或 beep 均失效。

`LoadProfileManagerConfig` 同样在 `ParseIniFile` 前没有更新该字段，而且其后也没有读取 `show_warnings`，只能沿用已有/默认显示值。两入口已经正确地在解析前更新文件日志等级，文件 warning 门控不受此遗漏影响。

建议在开始解析前由当前源文件读取显示设置（默认 true），同时保留正常解析阶段的完整配置处理；确保不重新依赖旧 `ini_sections`。应补实际早期语法警告在首次加载及双向重载时的屏幕队列验证。现有 `log_level_regression.cpp` 构造函数直接预置 `G->gShowWarnings=false`，配置中的非法 `color_space` 又在解析阶段之后才处理，因而该测试源码不能覆盖这个时序缺口。本问题已直接通知主代理，本文不自行修改生产。

## 已核对的生产接线

| 范围 | 实际源码证据与结论 |
| --- | --- |
| FILE 与等级 | `src/common/log.h` 的 DX11 warning/info/debug 及所有窄/宽 va_list 变体均同时检查 `LogFile` 和相应等级。空指针时不会进入 stdio；不能因此推导非空悬空 FILE 也安全。 |
| 首加载与重载 | `Logging.cpp::InitializeLogging` 直接用传入 INI 路径的 Windows profile API，在 `ParseIniFile` 前确定当前等级；没有调用只查旧 `ini_sections` 的 `GetIniEnumClass`，规避上游首次 map 空、重载读旧 map 的顺序缺陷。 |
| 缺失、空与非法 | 缺失 key 用单字符 sentinel 区分，只有缺失回退 legacy。显式空、非法、disabled 均关闭；warning/info/debug 大小写不敏感。显式空禁用是本地明确的解析契约，上游440用“非空”判断是否指定，二者不可混称逐字实现相同。 |
| legacy 优先 | 新 key 存在就覆盖 calls/debug；缺失时 calls 为0默认关闭，calls=1 开 info，且只有 calls 非0且 debug 非0时开 debug。`gLogDebug` 在每次初始化同步为最终等级是否 DEBUG。 |
| 文件复用与失败 | 仅在启用且尚无 FILE 时 `_wfsopen`；已打开流可以降等级或禁用而不关闭。打开失败强制等级 DISABLED、debug false。再次初始化启用时可以重新尝试打开。复用现有 FILE 不切换路径，与现有设计一致；不是本轮新增轮转功能。 |
| overlay 与 va_list | 两个 `LogOverlay` 版本将 FILE 输出和显示条件分开，文件统一走 warning 等级；隐藏屏幕警告仍可写文件。每次 `va_start` 后用 `va_copy` 单独消费 file 参数，`va_end(file_args)`，再用原参数格式化屏幕消息；隐藏分支也结束原列表。没有照搬上游重复消费同一 va_list。 |
| INI warning 与 beep | `IniWarning/W/Beep` 无条件设置 `ini_warned`；文件是否输出交给日志宏，beep 仅在末尾且显示开关开启时执行。早期显示设置时序问题见上节。 |
| profile | `LoadProfileManagerConfig` 将 profile 专用 `d3d11_profile_log.txt` 路径传给同一初始化器，再解析 Profile 配置；首次无活动 FILE 时会使用该路径。当前没有 profile 实际运行证据，不宣称该外部 helper 已验证。 |
| DLL detach | `D3D11Wrapper.cpp::DestroyDLL` 只保留注释，没有日志、保存或 fclose；与a24移除loader-lock下stdio/保存的意图一致。显式/重载/帧安全点保存仍有生产调用；源码审查不等于实际 FreeLibrary/进程退出已测试。 |
| CMake 与 DX12 | DX11 CMake接入 `Logging.cpp/.h` 并设置 `MIGOTO_DX=11`；DX12设置12。共享 `log.h` 的新枚举、gLogVerbosity声明与等级宏均位于 `#if MIGOTO_DX == 11`，else保留DX12原有 FILE/gLogDebug宏，不要求DX12定义新全局。此次只核对编译边界，未执行DX12构建。 |

## 现有回归源码与证据边界

已读取 `src/DirectX11/tests/log_level_regression.cpp`：它包含真实 LoadConfigFile/ReloadConfig、tmpfile 实际字节、等级正负标记、hidden overlay、窄/宽 va_list、空 FILE、打开失败以及直接调用 DestroyDLL hook 的断言。只读源码不能报告这些断言已执行通过。直接调用 hook 也不等于真实操作系统 loader lock 下卸载验证。

主代理另有旧 enum 日志 capture 显式 debug 和正标记的修改；本审查没有重复改这些测试，也没有用“没有输出”单独证明捕获有效。待主代理收集构建与 CTest 结果后，可将具体命令和结果写入同步账本。实际 profile helper、游戏屏幕警告显示、进程退出/主动卸载，以及 DX12目标构建需分别保留相应证据边界。
