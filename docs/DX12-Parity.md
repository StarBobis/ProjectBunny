# DX12 功能对齐进度

对照起点为本仓库 DX11 的 `0ce23af`。以 Mod 实际能执行的操作核对兼容性，配置解析、运行时执行、重载和失败回退都属于同一项工作。独立于图形 API 的规则放在 `src/common`，资源绑定、命令提交和同步由各后端实现。

| 功能范围 | 当前已接入 | 待对齐 |
| --- | --- | --- |
| ShaderRegex | 无 Pattern 的模型匹配，逐组排除着色器 Hash（`66848dd`） | Pattern、Replace、声明插入、临时寄存器、filter_index |
| 表达式、变量和命名空间 | INI 文件头 `condition`（`8c4b49e`），表达式中的 `DXGI_FORMAT_*` 和 `D3D11_BIND_*` 常量（`67a230c`），早期配置读取顺序（`ffa7773`），float32 字面量、算术、数值比较与条件判断，位模式比较（`8ea57d6`），21 个共享一元数学函数（`d82316d`），6 个共享位运算与移位运算符（`3ac9c70`），有界随机数、按 DX11 规则对已支持的操作数执行逻辑短路（`ed422c8`），Key 与普通数值配置按 DX11 核对操作数作用域，CPU 端 IniParams 分量别名、标量按命名空间解析、只读声明、标量持久化、保留未加载 Mod 的数值设置、原子保存、按 `clear_unknown_settings` 跨重载确认并清理未知项、通过可配置热键清空当前用户设置 | 资源池运行时持久化、Preset 条件的解析和运行、其他二元运算含多个随机调用时的求值顺序 |
| 命令列表和触发时机 | 基本 run、阶段链接 | 回调和代理、abort、每帧限制、资源命令的 pre/post、清理事件 |
| 运行时参数和输入 | 窗口、光标、键盘和 XInput 共用 Key 开关与循环、按变量同步或按位置步进、可选环绕、按键和释放延时、按前台进程或完整窗口标题过滤输入、按加载来源或全局范围禁用快捷键、逐档条件和命令、标量线性和余弦过渡、按下和释放的命令列表时序、标量持久化、Hunting 模式及定位热键注册（`2fe507f`）、帧分析实时状态（`bdfa277`）、帧号、逐帧时间、DX11 平滑帧率、直接绘制的类型和六项参数、计算蒙皮匹配时的直接派发类型和线程组数 | 普通计算 ShaderOverride 命令列表、间接调用上下文、draw_has_override、绘制与派发计数、IniParams 绑定、Preset、IniParams 过渡、暂停 |
| 资源与匹配 | 基本文件资源、别名、固定槽捕获、Hash 和绘制条件 | 通用复制、创建选项、资源池、区域、动态槽、范围、资源和着色器 getter、描述匹配、dHash Filter、贴图快照、store |
| 图形与计算 | IB/VB 和 SRV 替换、图形自定义元素、部分计算蒙皮、二进制着色器替换、显式实例绘制（`73f4e70`）、从当前 VB0/IB 自动推导绘制数量 | 独立自定义 CS、各阶段替换、图形状态合并、输入布局、流输出、同一命令列表逐绘制切换 IA 绑定、间接命令、Present 独立元素的索引绘制、文本补丁 |
| 分析与设备配置 | Hunting 和性能 HUD 可配置快捷键、导航重复速率、F8/F9、DX12 日志 | 定点 dump、按 draw 配置分析、MCP、其他 Loader/Device/Rendering/Logging 设置 |

每轮用生产入口做功能回归，共享逻辑同时跑 DX11 回归。驱动特有能力按具体效果评估。WARP 或离线配置回归覆盖的阶段与实机游戏复测分别记录。

## 已执行的回归

| 范围 | 验证入口 |
| --- | --- |
| 按键条件 | 两端使用同一组 21 条表达式核对命令列表外的操作数，通过配置注册及 F16–F21 的按键事件检查普通与循环条件、错误回退、重复键、长度边界和 IniParams 别名。DX12 另用 WARP 交换链核对首次 Present 按键读取分辨率。 |
| 图形常量 | 两端的生产求值器遍历 DX11 格式名称表和 10 个绑定标志，核对格式编号、组合表达式和无效名称。DX12 另通过文件头条件、数值配置、Constants 赋值和逻辑短路检查常量的使用。 |
| 文件头条件 | 两端使用同一组 21 条表达式核对文件取舍和随机数计数。DX12 通过完整加载及重载核对嵌套包含、命名空间、变量和按键注册，并验证无效条件不会确认删除未知设置。文件头读取加载前的 Hunting 状态，只有值 0 能用于静态求值。 |
| Hunting 模式 | 两端通过配置加载、真实热键注册和运行时表达式验证 0、1、2、其他数值及非法输入。DX12 同时检查 Constants 快照、软关闭的跟踪开销开关、失败重载和重试。旧 DX12 配置省略 `hunting` 时保留定位快捷键和当前开关状态。 |
| 帧分析表达式 | DX11 使用 WARP 命令列表和已优化表达式核对 `frame_analysis` 的活动期。DX12 在真实帧分析会话中核对开始、结束、会话内重载和 Constants 快照，同时检查读取此标志不会启动窗口和光标查询。 |
| 早期配置读取 | DX11 的配置代际回归与 DX12 的独立启动回归使用相同的含变量配置和固定随机序列，检查早期开关与 `[Constants]` 的读取顺序。DX12 经快捷键分发和实际用户设置文件核对前台过滤、输入启用、Hunting 重复间隔、重复键与包含覆盖，以及未知条目确认清理。 |
| 逻辑短路 | DX11 与 DX12 的生产求值器共用 19 组子表达式和 4 组随机顺序用例。DX11 通过 WARP 资源查询计数验证短路，DX12 通过浮点转换异常标志观测变量读取，并覆盖开关、调试、帧分析、重载失败与重试。WARP 交换链验证真实 Present 和测试 Present 的按键条件及采样次数。 |
| 输入禁用范围 | DX11 输入注册表和 DX12 逐帧按键处理加载同一组多文件 INI，验证显式与递归包含的顺序、嵌套包含、命名空间、开关、循环、长按和重载。DX12 另通过 WARP 交换链验证手柄开关、禁用期间松手后的恢复，以及跨重载的旧输入采样。 |
| 前台窗口过滤 | 两端通过实际配置和 Win32 隐藏窗口验证同进程、完整标题、大小写、无前台窗口、重载和无效配置。测试提供前台句柄和进程身份。DX12 在普通及轻量帧检查后台期间的按键、手柄采样、Hunting 动作和已开始的标量过渡。 |
| 按键事件延时 | DX11 注册表分发与 DX12 逐帧按键处理读取同一份 INI，使用相同时间点检查短按取消、按下和释放临界值、释放期间重按、条件变化，以及循环前进、后退和备用键的独立计时。DX12 另在 WARP 交换链上验证长按期间重载、测试帧及手柄断开后的延时恢复。 |
| ShaderRegex 排除规则 | DX11 的 `dx11_upstream_regression --shader-regex-exclusions`，DX12 的 `dx12_runtime_audit`。后者读入 INI、调用运行时组匹配，并重新加载配置 |
| 数字字面量与运算符 | DX11 的 `dx11_upstream_regression --audit-item P-02`，DX12 的 `dx12_runtime_audit`。后者执行 `[Constants]` 并覆盖结合顺序、负零、最大长度和非法输入 |
| 算术、数值比较和条件 | 抽取前后的 DX11 分别运行 36 条表达式，检查运行时、静态求值和优化后的结果。DX12 执行同一组表达式，并通过 `[Constants]` 检查局部变量、条件和着色器常量。用例覆盖逐步舍入、小除数、正负零、无穷和 NaN。 |
| 位运算与移位 | 抽取前的 DX11 求值器、抽取后的 DX11 P-02 及 DX12 运行时执行相同的 24 组表达式，包括负数右移、移位数边界、非有限值和连续运算的 float32 舍入 |
| 一元数学函数 | 两端执行 `src/common/tests/IniUnaryMathCases.h` 中同一组用例。DX11 的 `dx11_upstream_regression` 同时检查静态求值和优化，DX12 的 `dx12_runtime_audit` 同时检查变量、嵌套调用和非法调用 |
| 有界随机数 | DX11 主回归覆盖极小边界、并发采样和换种。DX12 的 `dx12_runtime_audit` 检查 Constants、嵌套顺序、非法表达式、非有限边界、按帧换种、WARP 交换链的 Present 入口和采样次数 |
| 帧时间与帧号 | DX11 主回归覆盖真实时间表达式与 FPSCounter。DX12 回归验证平滑、零间隔、长帧、倒退时间戳、跨帧表达式快照和配置重载，并在 WARP 交换链上调用 Present、Present1 与 DXGI_PRESENT_TEST |
| 显式实例绘制 | DX11 的 `dx11_upstream_regression` 覆盖共用的参数分割入口。DX12 的 `dx12_runtime_audit` 从 INI 经静态计划和动态执行到原生绘制回调核对参数，包括负基顶点、零实例、越界和非法表达式 |
| 直接绘制参数 | DX12 的 `dx12_runtime_audit` 使用 WARP 图形 PSO、真实 INI 和绘制包装器，检查两类绘制、相同参数的连续调用、零计数、float32 舍入、负基顶点、重载和 post。DX11 主回归覆盖共享绘制参数在原后端的读取及匹配。 |
| 标量按键过渡 | DX11 F-193 和 DX12 WARP Present 使用相同的激活、长按和开关配置，逐帧核对执行次数、局部变量、嵌套 pre/post、覆盖前后的值及交错释放。DX12 另用固定时钟核对线性、余弦曲线及改向，并在实际 Present1 上验证测试帧、断开释放和配置重载。 |
| 按键循环 | DX11 的 F-193 和 DX12 的 WARP Present 使用同一组 9 份 INI，逐次核对 37 次前进或后退后的变量和命令执行结果，覆盖重复值、外部改值、空项、短列表、单档、边界、非法配置与重载。DX11 另验证原有定时过渡及 pre/post 回调。 |
| XInput 与多键状态 | DX11 和 DX12 的按钮解析与判定使用同一组手柄状态样本，覆盖所有数字按钮、端口、取反和扳机边界。DX12 运行时另验证交替触发、组合键、断开恢复、重连、重载撤销以及普通帧与轻量帧的按键处理。在 WARP 交换链上调用 Present 和 Present1，确认按键命令执行、断开后长按值恢复，DXGI_PRESENT_TEST 保留输入状态。实体手柄与游戏实测待进行。 |
| Hunting 快捷键 | DX12 运行时回归检查快速点按、配置速率、重载时长按、无效与禁用绑定、多个动作共用按键和单次采样。按键经实际动作消费入口选择 WARP 计算着色器，叠字显示所选 Hash，清除选择后计数保持。 |
| 用户设置重置 | DX12 运行时回归通过配置热键读取临时设置文件，覆盖长按、改绑、文件占用、重载中写入、配置缺失、切换设置路径和保护主配置。DX11 主回归覆盖键盘与手柄组合解析、无效组合和原有设置清空行为。 |
| 用户设置保留 | DX11 主回归验证原保存和清理行为。DX12 通过 F10 和退出保存入口验证禁用和重新启用 Mod、切换文件、读锁、替换失败和重试。写入、刷新或关闭失败时，回归逐字节核对旧文件。两端使用同一套未知项比较规则；DX12 另覆盖 ±0、NaN、只含未知项的文件、包含项加载失败、资源池声明、重新启用和 F10 提示。 |
| 命名空间与只读声明 | DX11 主回归覆盖原变量解析与非法声明。DX12 通过多个实际 INI、命令列表、Key 和 UTF-8 用户配置覆盖同名隔离、局部遮蔽、延后赋值、旧格式恢复和重载更换标志。 |
| 计算蒙皮派发参数 | DX12 的 `dx12_runtime_audit` 用 WARP 执行原始派发和替换派发，跨描述符页复用暂存槽，通过 UAV 回读核对着色器实际读取的缓冲值。另覆盖匹配失败、资源缺失、重复派发、零组数、重载，以及原生调用前后的变量值。 |
| 自动绘制数量 | DX11 的 `dx11_upstream_regression --audit-item F-155` 核对 WARP 上的偏移、步长和空绑定。DX12 的 `dx12_runtime_audit` 经命令列表包装器记录真实缓冲视图，在绘制回调处核对原绑定、替换绑定及失效后的数量和实例范围 |

以上脚本语义已通过本地回归，游戏内 Mod 的组合行为仍需目标环境复测。

## 2026-11-28 交接 · 第 29 轮

本次工作停在按键条件对齐。代码基线是本仓库 DX11 的 `0ce23af`，前一轮检查点是 `2fe507f`。本轮代码、回归、使用文档与本节同次提交。可用 `git log -1 --diff-filter=A -- src/common/IniExpressionOperands.h` 找到该检查点。上游检查在本轮读到 `SpectrumQT/XXMI-Libs-Package` 的 `9c6b40d5a12743439182f215450001a84744bd39`，新增提交数为 0。上游逐项验证状态单独保存在 `docs/dx11-upstream-sync.json`。

### 已合入的工作从哪里找

| 范围 | 提交与作用 |
| --- | --- |
| ShaderRegex 与绘制 | `66848dd` 实现按组排除 Hash，`73f4e70` 支持显式实例绘制参数，`071a4c1` 从有效 IA 视图推导自动绘制数量，`753f863` 提供原始直接绘制参数，`69295c4` 提供计算蒙皮原始派发参数 |
| 表达式 | `8ea57d6` 二进制字面量、位比较和幂，`d82316d` 一元函数，`3ac9c70` 位运算，`5785b34` 随机数，`c5abf88` float32 算术与比较，`ed422c8` 逻辑短路，`67a230c` 图形常量 |
| 配置与用户设置 | `e2063c7` 命名空间和只读标量，`bd0ac38` 原子保存并保留未加载 Mod 的设置，`9a38c95` 清理未知项，`238656c` 组合键与清空用户设置，`ffa7773` 早期配置求值顺序，`8c4b49e` 文件头条件 |
| 输入和过渡 | `7287576` Hunting/HUD 热键，`9080d1f` XInput，`5298eaf` 逐档循环，`ea55c81` 标量过渡与按下/释放阶段，`f2354b6` 输入延时，`31efd47` 前台过滤，`b66790c` 按来源控制输入 |
| 帧与诊断 | `d3a557c` 帧号、时间与 FPS，`bdfa277` 帧分析活动状态，`2fe507f` Hunting 模式和重载。已有 DX12 配置省略 `[Hunting] hunting` 时，保留此前的快捷键和当前开关状态 |

### 本轮实现与边界

- DX11 与 DX12 共用 `src/common/IniExpressionOperands.h` 的关键字与命令列表外操作数规则，共用 `src/common/IniParameterName.h` 的分量名及索引解析。DX11 保留原有的设备对象、参数分配、求值器和 C++17 构建。DX12 中 `x`、`x0`、`x00` 引用同一 CPU 参数，GPU 绑定仍待对齐。
- DX12 的普通数值配置和按键条件现在会在求值前检查作用域。`random(2) + frame_time` 作为数值配置无效且不消耗随机样本，命令列表仍可读取 `frame_time`。按键可读取已声明的全局变量、分辨率等合法操作数，两侧逻辑表达式均求值。有效且绑定了按键的分辨率条件会在首个实际 Present 采样，未绑定及无效条件不会开启采样。
- 普通 Key 取第一条非空 `condition`，读取上限为 259 个 UTF-16 代码单元。解析失败会告警并让该按键按无条件执行。循环按键读取完整的逗号分档条件，错误回退仅影响对应档。用法见 [按键与变量](../usage/dx12/keys.md)。
- 共用样本在 `src/common/tests/IniKeyConditionCases.h`。DX11 的 `ini_expression_regression.cpp` 核对表达式，`key_cycle_audit.inl` 的 F-193 验证实际配置注册和按键回调。DX12 的 `runtime_audit.cpp` 中，`TestKeyConditions` 核对回调与错误回退，`TestKeyCyclesOnPresent` 在 WARP 交换链验证首次读分辨率。DX11 用例须先通过设备创建 IniParams 资源，再执行 Constants，本轮已修正测试夹具的这一初始化顺序。

### 复跑与接手

| 本轮最终验证 | 结果 | 本机记录 |
| --- | --- | --- |
| DX11 Release 完整 CTest | 15/15 通过，含上游主回归及其 WARP 用例。修正夹具后也单独运行了 F-193，通过 | `build/key-scope-d11-verified.log` |
| DX12 Release 的 CTest | 3/3 通过，覆盖着色器模型、含 WARP 的运行时审计、独立进程早期配置 | `build/key-scope-d12-verified.log` |
| VitePress | 构建通过 | `build/key-scope-docs.log` |
| x64 Release TIMING 包 | 构建通过。逐个核对 SHA-256，包内的 `d3d11.dll`、`d3d12.dll`、`d3dcompiler_47.dll` 均与对应构建产物一致。游戏内验证待进行 | `build/key-scope-package-final.log` |

在仓库根目录的 x64 MSVC 开发环境中可复跑下列命令。此处的两个测试目录均为 Ninja Release，DX12 测试目录启用了 `BUILD_TESTING` 和 `DX12_AUDIT_TESTS`。最后一条生成 x64 TIMING 包，输出至仓库的 `x64/Release`。

```powershell
python scripts/check-dx11-upstream.py
cmake -S . -B build/dx11-dump-validation -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/dx11-dump-validation --target DirectX11 dx11_upstream_regression
cmake --build build/dx11-dump-validation --target dx11_legacy_version_regression dx11_toolkit_audio dx11_proxy_smoke dx11_crash_diagnostics_host dx11_loader_test_host
ctest --test-dir build/dx11-dump-validation --output-on-failure
cmake -S . -B build/dx12-goal-audit -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDX12_AUDIT_TESTS=ON
cmake --build build/dx12-goal-audit --target DirectX12 dx12_runtime_audit dx12_shader_model_audit
ctest --test-dir build/dx12-goal-audit -R '^dx12_' --output-on-failure
npm run docs:build --prefix usage
.\scripts\build-cmake.ps1 -Configuration Release -Platform x64 -ReleaseTiming -ReleasePackage
```

下次可从命令列表的 `handling = abort` 接手。DX11 在 `src/DirectX11/CommandList.cpp` 中把它登记到 pre 和 post，并由 `AbortCommand::run` 更新当前执行状态。DX12 的 `src/DirectX12/Config/DX12Config.h` 已有 `handling = skip` 分支。对齐前先用相同 INI 在 DX11 确定嵌套 `run`、显式 pre/post、下一独立回调各自的终止范围，再核对 DX12 的普通命令列表、按键回调及静态执行计划。该项此次尚未实现。

更大的待办按本文开头的七个功能范围追踪。其中 ShaderRegex 的文本改写、通用资源复制和资源池、GPU IniParams、独立自定义计算着色器、逐绘制帧分析仍需分别实现并验证。游戏实测待进行，可用含合法条件、错误条件及逐档条件的 Mod，在首次显示和 F10 重载后分别核对触发结果、日志告警及画面。
