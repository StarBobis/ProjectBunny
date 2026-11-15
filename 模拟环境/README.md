# DX11 DLL 模拟环境

本目录测试编译出来的真实 x64 `d3d11.dll`。宿主通过绝对路径加载 DLL，调用其导出创建真正的 D3D11 WARP 软件设备；不重新实现生产包装器。所有构建、复制的 DLL、配置和原始结果均留在 `build/`，每个用例使用独立目录和子进程。

代理用例将宿主 EXE 也复制到对应目录，与 DLL、`d3dx.ini` 相邻，符合普通游戏布局。只设置工作目录不够：生产 `DllMain` 会校验 EXE 与 DLL 的实际位置；不满足目录关系且未指定 Loader 目标时会主动拒绝加载。首轮因此得到 1114，原始失败记录保留，不认定为生产缺陷。

## 构建和运行

需要 x64 PowerShell 7、Visual Studio C++ x64 工具与 Windows SDK、CMake/Ninja。脚本会发现 VS 工具路径。

在仓库根目录执行：

```powershell
# 构建测试宿主和当前生产 DX11 Release DLL；不部署，不启动游戏。
& '.\模拟环境\scripts\build.ps1' -BuildCandidate
# 验证运行器会识别失败、杀掉超时子进程。
& '.\模拟环境\scripts\self-test.ps1'
# 测试实际构建产物。
& '.\模拟环境\scripts\run-tests.ps1' -CandidateDll '.\模拟环境\build\candidate\src\DirectX11\d3d11.dll'
# 后续有修复前、修复后两个 DLL 时可同时运行。
# & '.\模拟环境\scripts\run-tests.ps1' -BaselineDll '绝对路径\旧版.dll' -CandidateDll '绝对路径\新版.dll'
```

测试结果位于 `build/runs/<唯一编号>/results.json` 和 `结果.md`。失败返回非零退出码，保留每个用例的 stdout/stderr、DLL 和 `d3dx.ini`。结果记录输入 DLL、实际加载路径（新宿主日志）、SHA256、配置哈希、系统原生 DLL、宿主哈希、HEAD、退出码和耗时。不要在测试过程中覆盖候选 DLL。既有冒烟宿主不输出加载路径；其参数和复制文件哈希会记录，加载路径自校验由新宿主覆盖。

`host-build-info.json` 绑定宿主源文件与可执行文件的哈希；源码或宿主变化后，运行器要求先重建。报告中的仓库 HEAD 是运行上下文，不能证明任意外部传入 DLL 的编译来源；外部基线与候选产物仍须各自保留构建记录。

## 已接入的测试面

| 入口 | 测试内容 | 证据边界 |
|---|---|---|
| 新宿主，System32 原生对照与候选 DLL 各跑一遍 | 能力查询；初始化/动态 buffer 的完整字节回读；非法零长度描述符；奇数尺寸纹理清屏与 RowPitch 回读；deferred command list 回放；隐藏窗口 swapchain、回读、Present、ResizeBuffers | WARP 基础集成行为。Present 成功不证明屏幕显示效果 |
| 复用 `src/DirectX11/tests/proxy_smoke.cpp`，原文件不修改 | wrapper、原生对照、hook=all、拒绝扩展接口四模式；导出/序号/字体资源；Context1 API；部分可选输出与引用生命周期 | 是真实 DLL 集成测试。既有引用计数断言若失败应结合原生对照诊断，不直接判生产缺陷 |
| 测试专用 `dx11_fault_provider.dll`，经 `proxy_d3d11` 接入 | 对合法设备/交换链创建参数，人工返回 `E_OUTOFMEMORY`，验证真实代理传播 HRESULT、输出为空，并确认两次调用确实到达该提供方 | 人工模拟的入口失败；没有耗尽内存，也没有模拟资源、shader 或 Map 失败；该提供方只支持这两个入口和测试计数 |
| 进程控制器自检 | 非零退出码、超时终止 | 验证测试装置，不计作 DLL 功能覆盖 |

原生对照失败时，对应候选用例记 `NOT_RUN`，先排查环境。状态包括 `PASS`、`FAIL`、`TIMEOUT`、`HARNESS_ERROR`、`NOT_RUN`；后续加入能力专属用例时应使用 `SKIP`，不得折算成通过。能力查询仅记录能力，不证明所有可选能力已测试。

## 与修复方案衔接

这是阶段 0 的基础装置，不能把一次冒烟通过写成 120 条问题全部修复。每项修复仍要新增或选用能触发对应问题的行为用例，验证正常、边界、失败与恢复；已知缺陷必须记录修复前可重现、修复后预期结果。源码正则、函数存在性、DLL 固定字节数不能作为功能验收。

源文件编译进 `dx11_upstream_regression` 的测试标注为“生产源码逻辑测试”，不能混称实际 DLL 测试。已通过 `-BuildBusiness` 接入整个既有业务回归、离线着色器工具转换、旧版版本测试及真实 CMake 版本生成器回归，再与实际 DLL 的完整集成用例一起运行。目前为 16 项，包含 F114 的实际 INI 解析与 IniParams GPU 字节回读。

```powershell
& '.\模拟环境\scripts\build.ps1' -BuildBusiness
& '.\模拟环境\scripts\run-full.ps1'                 # 完整业务基线
& '.\模拟环境\scripts\run-full.ps1' -Item 'F-001'  # 该项专项 + 完整业务 + DLL 集成
```

完整结果在 `build/f/<唯一编号>/results.json`。短目录避免人为放大旧 MAX_PATH 限制；每个业务进程仍独占 EXE/DLL/配置目录。专项必须显式报告对应 ID，未知/未接入专项不能通过。每条修复的证据摘要位于 `validation/`，进度见 `repair-progress.json`。

`record-item.ps1` 同时核对条目、专项回执、全部业务/集成状态、DLL 哈希、源码快照和完整构建清单哈希。仅重新构建但未重跑测试时，不能复用旧的通过报告。`-SpecialOnly` 仅用于复现和局部诊断，不能作为提交凭据。

本机 MSVC 将 Ninja 的无 BOM UTF-8 响应文件误读为 ANSI，含中文的库路径因而链接失败。本环境通过测试构建专用 CMake overlay/linker launcher，将这一份生成的响应文件改为 UTF-16 后调用原链接器；生产源文件、库、选项均不变，不依赖修改系统代码页。

锁清理专项通过 `DX11_AUDIT_ASAN_LOCKS=ON` 将真实 lock.cpp 包含进单独测试翻译单元，并只对该单元启用 AddressSanitizer，检测实际缓存节点释放后的读取。没有复制算法或安装全局锁钩子；原生删除计数后仍执行真实 Windows API。其他生产源码与实际 DLL 保持原 Release 构建。该选项默认关闭，专用环境要求 VS x64 ASan 组件，并随测试宿主复制匹配的运行库；与未插桩 STL 链接时关闭容器注解，堆 UAF 检测保留。

以下单独冻结，不能用本轮 WARP 结果销项：真实游戏/显卡驱动问题、加载器锁与卸载压力、并发钩子与所有权协议、资源/shader/Map 失败、64 UAV/CB 窗口能力、帧率和性能结论。`dxgi.dll` 与编译器代理 DLL 的黑盒集成尚未接入；修改这些模块时必须先补相应宿主与原生对照。

首版故障装置只验证设备创建导出失败，失败时不创建 COM 对象。后续资源级故障注入仍优先使用该链接机制，但需另行实现完整转发并验证 COM 身份、寿命与故障时点；不新增生产测试导出，不把非法参数失败等同于有效输入的 OOM，不与生产 vtable 钩子盲目叠加。

性能项与正确性项采用同一风险顺序：先建可比 Release 基线，再做局部、可证明保持语义的优化；无显著收益则不保留。WARP 与宿主耗时不用于推断真实游戏 FPS。
