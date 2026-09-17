# DX12 代理 · DLSS 帧生成卡死闪退 —— 交接文档

> **后续纠正：以下旧交接内容保留作历史记录，不再作为当前结论。**
> 已在三个 2026-09-17 崩溃目录的 `Player.log` 中发现明确的
> `CreateSwapChainForHwnd failed (80070005); swapchain creation aborted.`。
> `swapchain_create_leave/return` 只表示调用返回，不代表 HRESULT 成功。
> 因此旧文中“交换链创建成功”“不是创建失败”“只能继续查等待句柄”的结论均不成立。
> 同样，`STATUS_BREAKPOINT` 本身不能证明是卡死检测；崩溃处理阶段的等待栈不能倒推为最初故障。
> 当前定位：`DX12CustomShaderSurfaceCache` 持有旧原生交换链的 QI 引用，
> wrapper 销毁时没有失效缓存，且旧 `InvalidateChain` 也没有释放 QI 引用。
> 旧 flip chain 因此继续占用 HWND，阻止 FG 创建替代交换链。
> 修复、验证方法和边界见 [修复报告](DX12-DLSSFG-修复报告.md)。

> 编写时间：2026-09-17 18:2x
> 交接原因：接手人未能定位剩余缺陷，按用户要求移交。
> 适用仓库：`D:\Dev\ProjectBunny`（分支 `main`，HEAD = `4b519a9`，另有未提交改动）
> 现场：`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12`（TIMING 版构建目标）

---

## 0. 一句话结论

**已确证并修复两个真实缺陷（网络符号抓取卡死链、符号服务器桩未安装）。剩余缺陷的现场指向"引擎在重建交换链之后、进入下一个 Present 之前卡死"，此刻**我们的 DLL 不在任何线程栈上**；触发条件是开启 DLSS 帧生成。下一步必须用崩溃转储（`%TEMP%\miHoYo\绝区零\Crashes\*\crash.dmp`）定位，日志已经给不出更多信息。**

---

## 1. 环境与操作手册

### 1.1 构建

```powershell
cd D:\Dev\ProjectBunny
.\build_release_timing_x64.ps1
```

构建脚本会：
1. `Remove build\` 与 `x64\`（**会清空 `build/_scratch/`，我放在那里的临时探针已多次被删**）
2. 把上一份 `d3d12_log.txt` 轮转为 `d3d12_log.before_<yyyyMMdd-HHmmss>.txt`
3. 部署 `d3d12.dll` / `Run.exe` / `dxcompiler.dll` / `dxil.dll` 到 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\`
4. 不启动游戏（`LaunchGame=false`）

验证部署是否与构建一致：比对 `x64\Release\d3d12.dll` 与游戏目录的 SHA256。

### 1.2 日志

- 主日志：`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`（TIMING 版，极密，**不要整文件读**）
- 轮转日志：同目录 `d3d12_log.before_*.txt`（一次会话对应一份，是本项目最有价值的资产）
- 引擎日志：`%USERPROFILE%\AppData\LocalLow\miHoYo\绝区零\Player.log`（**含崩溃处理器抓的符号化栈尾**）
- 崩溃报告：`%TEMP%\miHoYo\绝区零\Crashes\Crash_<yyyy-MM-dd>_<hhmmssfff>\`（含 `crash.dmp` + `Player.log`）
- 系统事件：`Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddHours(-1)}`
  - `Application Hang 1002 / AppHangXProcB1` = 挂起后被关
  - `Application Error 1000 / APPCRASH` = 硬崩溃（本次就是这种）

### 1.3 日志里的关键字段

| 字段 | 含义 |
|---|---|
| `func=DX12FreezeWatchdog,event=stall … stage=<X>` | 帧边界面包屑，**最后一个到达的标记点** |
| `stage` 取值 | `queue_execute_leave` / `present_primary_check` / `present_enter` / `present_original_call` / `present_leave` / `swapchain_create_*` / `swapchain_wrap_*` |
| `func=DX12ThreadStack,threadId=…` | 看门狗在卡死时对所有线程做的原生栈快照（每 1.5s 一次，每个 stall 只抓一次） |
| `func=HookInstall,status=…,result=0x…` | 内联钩子安装结果，**失败码必须看** |
| `func=DX12DbgHelpHooks` | dbghelp/symsrv 加固层的运行报告 |

---

## 2. 缺陷全景（按确认顺序）

### 2.1 【已修复·已实证】符号服务器网络抓取卡死

**现象**：开 DLSS 帧生成后游戏永久挂起（`AppHangXProcB1`），设备未被移除、无 TDR。

**证据**（`d3d12_log.before_20260917-175135.txt`，渲染线程 2092 的栈）：

```
ntdll!NtWaitForSingleObject
  <- wininet.dll  (+0xcf4bf / +0x86279 / +0x10e478)
  <- symsrv.dll   (C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symsrv.dll)  ×9
  <- dbghelp.dll  (同一 Debuggers\x64 目录) ×8
  <- UnityPlayer.dll (+0x63244c …)
```

**根因**：引擎在开启帧生成时做一次原生栈符号化，dbghelp 带着
`SRV*C:\websymbols*http://msdl.microsoft.com/download/symbols` 的搜索路径去 symsrv 取 PDB，
symsrv 走 wininet 做**无超时**网络请求，渲染线程被钉死在用户态。

**修复**（`src/DirectX12/Presentation/DX12DbgHelpHooks.cpp` 等）：
1. 搜索路径消毒：`SymInitializeW` / `SymInitialize` / `SymSetSearchPathW` / `SymSetSearchPath` 四个入口剥掉 `srv*<cache>*<store>`、`symsrv*`、含 `://` 的 token；`_NT_SYMBOL_PATH`、`_NT_ALT_SYMBOL_PATH` 环境变量一并清理。
2. symsrv 的 16 个取数出口（`SymbolServer*` / `SymbolServerPing*` / `SymbolServerStore*`）全部打桩返回 0 + `ERROR_FILE_NOT_FOUND`。
3. **模块全表扫描 `Rescan()`**：LoadLibrary 钩子不够用——引擎那份 symsrv 是被 Debuggers 目录里同目录的 dbghelp 通过加载器拉起来的，**不经过被 hook 的 kernel32 出口**。`Rescan()` 用 toolhelp 快照枚举全进程模块，按模块基址记账，对所有 `dbghelp*` / `symsrv*` 副本逐一处理。安装时调用一次，看门狗判定卡死时再调用一次。
4. 四个消毒回调改为"锁内拷进栈缓冲、放锁后再转发"，消除持锁调外部模块。

**实证**（`d3d12_log.txt`，2026-09-17 18:19 会话）：

```
event=dbghelp_load_detected,module=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbghelp.dll
event=sanitize,api=SymInitialize,
  original =.;…\ZenlessZoneZero Game;…;SRV*C:\websymbols*http://msdl.microsoft.com/download/symbols;
  sanitized=.;…\ZenlessZoneZero Game;…;C:\WINDOWS;C:\WINDOWS\system32
event=symsrv_load_detected,module=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symsrv.dll
event=symsrv_hooks_installed,module=00007FFC07D40000,resolvable=16,exports=16,blocked=16
```

并且此后所有卡死现场的快照里**再无 wininet/symsrv/dbghelp 帧**。**这一条可以认为结了。**

---

### 2.2 【已修复·已实证】Nektra 钩子因 `lplpCallOriginal=nullptr` 全军覆没

**现象**：`symsrv_hooks_installed,exports=16,blocked=0`，16 个桩一个都没装上。

**根因**：`CNktHookLib::Hook(__out SIZE_T*, __out LPVOID *lplpCallOriginal, …)` 的第二个参数是
**必须的 out 参数**，传 `nullptr` 直接返回 `0x57`（`ERROR_INVALID_PARAMETER`）。

**实证**（独立探针，链接 `3rdparty\Nektra\NktHookLib64.lib`）：

```
Hook(original=nullptr) -> 0x57
Hook(original=&var)    -> 0x0  original=00007FFBE1FE002D
```

**修复**：为每个 symsrv 模块准备 16 个 trampoline 槽位（进程级生命周期，钩子永不摘除），并把日志改成
`resolvable=N,exports=16,blocked=N` 双计数，下次一眼可辨。

> ⚠️ **给接手人的教训**：本项目所有 `DX12HookRegistry::HookFunction` 调用点都必须传有效的
> `void**`；传 `nullptr` 静默失败且只体现在计数上。

---

### 2.3 【未解决】交换链重建后卡死 → 闪退 —— 本次交接的核心

#### 现场（`d3d12_log.before_20260917-182223.txt`，18:18:25 构建，18:19:36 崩）

```
t=58695703  DX12PrimarySwapChain,event=release,swapchain=00000180941CFF80,identity=00000180941CFF80
t=58695718  DX12CommandQueueState,event=accept_direct,queue=000001843BACEF90,type=0
t=58695718  DX12SwapChainCreate,api=CreateSwapChainForHwnd,hwnd=…250DE6,
              width=1280,height=720,format=28,buffers=6,sampleCount=1,
              flags=0x842,swapEffect=3
t=58695765  kind=command_queue,stage=create,…,type=2          （计算队列）
t=58695765  kind=graphics_command_list,stage=create
t=58695765  dbghelp_load_detected (Debuggers\x64)  → hooks_installed → sanitize
t=58696421  symsrv_load_detected → symsrv_hooks_installed … blocked=16
t=58696734  sanitize (第二次 SymInitialize)
t=58696984  DX12FreezeWatchdog,event=stall present=4449 stalledMs=1500 stage=swapchain_create_leave
t=58697093  DX12ThreadStackSummary,captured=168,seen=184,skipped=0,modules=166
            ← 日志到此为止，进程随后死亡
t≈58698xxx  kind=swapchain,stage=create,wrapper=…            （新交换链包装器创建成功）
```

系统事件：`18:19:37 Application Error 1000`，`出错模块 unknown`，
`异常代码 0x00000000`，`P8: PCH_84`；WER `APPCRASH / StackHash_0000`。

`flags=0x842` = `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (0x40)`
`| DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (0x800)` —— **这就是帧生成要的交换链。**

#### 已排除的假设（都有证据）

1. **不是符号网络抓取**：全量扫描 168 条线程栈，无 wininet/symsrv/dbghelp 帧。
2. **不是我们自己的锁**：看门狗锁探针全部 `free`（`mod/preskin/upload/vmGlobals/overlay/descriptor/wrapper`）。
3. **不是交换链创建失败**：`CreateSwapChainForHwnd` 返回了（面包屑推进到 `_leave`），且包装器随后创建成功。
4. **不是设备移除**：`deviceRemovedReason=0x0`。
5. **不是 GPU 死锁**：见下方转储分析，等待发生在**用户态**。

#### ★ 转储分析结论（2026-09-17 18:19 崩溃，已用 `dev_tests/crash_site_probe` 逐帧解析）

**"闪退"的真相**：不是崩溃，是**引擎自己的卡死检测主动自杀**。

```
=== EXCEPTION ===
  threadId        = 30320            ← 就是渲染线程（卡死的那条）
  exceptionCode   = 0x80000003       ← STATUS_BREAKPOINT
  exceptionAddr   = 00007ffb9b5c601b
  faulting module = UnityPlayer.dll  (+0x12c601b)
  param[0]        = 0x0
```

`0x80000003` = **断点异常**，由 UnityPlayer 内部主动 `DebugBreak()` 触发（用
`UnityPlayer.dll+0x12c601b` 附近的跳转表 dispatch）。也就是说：卡死数秒后引擎判定自己挂了，
主动断点 → 崩溃处理器写转储 → 进程退出。**系统事件日志里那句
`Application Error 1000 / 出错模块 unknown / 异常代码 0x00000000` 是 WER 对它的归一化记录，
`PCH_84` 是 Unity 处理器内部签名，不要被它误导成"空指针调用"。**

**渲染线程 30320 的真实栈（模块归属的栈字，自栈顶向下）**：

| 栈偏移 | 地址 | 符号 |
|---|---|---|
| +0x0000 | `KERNELBASE+0x22773` | `WaitForMultipleObjectsEx+0x123` |
| +0x0390 | `KERNELBASE+0x22641` | `WaitForMultipleObjects+0x11` |
| +0x03d0 | `d3d12.dll+0xada1f` | **`DX12WaitHookCallbacks::HookedWaitForMultipleObjects+0x10f`** |
| +0x0400 | `UnityPlayer+0x62a659` | （无符号，引擎 D3D12 后端） |
| +0x0560 | `UnityPlayer+0x12c6d2a` | （无符号） |
| +0x05d0 | `KERNELBASE+0x10ef83` | `UnhandledExceptionFilter+0x1f3` |
| +0x0820 | `nvwgf2umx+0x1d4a43` | 驱动 |
| +0x0840 | `nvwgf2umx+0x3284e9` | 驱动 |
| +0x0ac8 | `nvwgf2umx+0x338ce0` | 驱动 |
| +0x0dd0 | `UnityPlayer+0x58aa9b` | 引擎主循环 |
| +0x0de0 | `UnityPlayer+0xab4ca8` | `UnityMain+0x4590d8` |

（符号由 `dev_tests/sym_probe` 解析：本仓库 PDB + `srv*C:\SymbolCache*https://msdl.microsoft.com/download/symbols`。
UnityPlayer 的 PDB 不在公共符号服务器上，所以只有导出符号 `UnityMain` 能解析。）

**读法**：

1. 渲染线程（引擎主线程）**主动调用 `WaitForMultipleObjects(..., INFINITE)`**，调用点在
   `UnityPlayer+0x62a659`（引擎自己的 D3D12/帧循环代码，**不是**我们的包装器）。
2. 这个 wait 走进了我们的 `HookedWaitForMultipleObjects`——那是 TIMING 版为归因卡死装的**纯 passthrough 钩子**
   （只测量 + 可选记录，**不改变行为**）。栈上出现它**不等于**它是原因；它只是把等待暴露出来。
3. 等待**永不满足**：`present` 停在 4449，此后没有任何 `Present`。
4. 旧交换链在 t=58695703 被 release，新交换链以
   `flags=0x842`（`FRAME_LATENCY_WAITABLE_OBJECT | ALLOW_TEARING`）创建成功
   —— **这正是帧生成要的交换链**。

#### ★ 第二次复现（2026-09-17 18:49 崩溃）—— 旧假设被证伪，新增决定性探针

同一天的第二次崩溃（`Crash_2026-09-17_104901608`，日志 `d3d12_log.before_20260917-185134.txt`）
给出了**同一个停点**，并推翻了上一版文档里的假设：

| 项 | 18:19 | 18:49 |
|---|---|---|
| 异常 | `0x80000003` `STATUS_BREAKPOINT` @ `UnityPlayer+0x12c601b` | **完全相同** |
| 异常线程 | 30320（渲染线程） | 31808（渲染线程） |
| 我们的 DLL 栈帧 | `HookedWaitForMultipleObjects+0x10f` | `HookedWaitForMultipleObjectsEx+0x179` + `HookedWaitForMultipleObjects+0x15f` |
| 停点面包屑 | `swapchain_create_leave` | `swapchain_create_return` |
| 引擎等待调用点 | `UnityPlayer+0x62a659` | **相同** |
| 驱动返回地址 | `nvwgf2umx+0x1d4a43 / 0x3284e9 / 0x338ce0` | `nvwgf2umx+0x1d0033 / 0x3284e9 / 0x338d57` |
| 交换链 | `flags=0x842` 重建成功 | 同 |

两次的调用形态（渲染线程的栈自顶向下）：

```
ntdll!NtWaitForSingleObject
  KERNELBASE!WaitForMultipleObjectsEx+0x123
  KERNELBASE!WaitForMultipleObjects+0x11
  d3d12.dll!DX12WaitHookCallbacks::HookedWaitForMultipleObjects(...)   ← 我们的 passthrough 钩子
  UnityPlayer+0x62a659                                                  ← 引擎自己的等待代码
  UnityPlayer+0x1af9004
  ... UnhandledExceptionFilter / UnityMain/主循环
  nvwgf2umx 驱动返回地址（进入驱动的历史帧，不是当前等待）
```

**关键否定结果**：全部 8 份日志（含最新一份）里

- `func=DX12FrameLatency` 出现 **0 次**
- `matchesFrameLatency=true` 出现 **0 次**

也就是说：**游戏从头到尾没有调用过 `IDXGISwapChain2::GetFrameLatencyWaitableObject`**。
上一版文档里"引擎在等帧延迟可等待对象 / 我们返回了旧句柄"的假设（分支 (a)/(b)）**不成立**，
`DX12FrameLatency` 那条日志也已被证伪，不要再沿着它查。

#### 现在的位置（下一步就查这个）

事实收敛为一句话：**渲染线程卡在自己的 `WaitForMultipleObjects` 上，等一个永不触发的句柄；
调用点是引擎固定的一处（`UnityPlayer+0x62a659`），两次崩溃完全一致。**

转储里**拿不到那个句柄**：异常是另一个线程用 `DebugBreak` 注入的，`CONTEXT` 的寄存器
已被崩溃处理器覆盖（实测 `Rcx/Rdx/R8/R9` 全是 0/1/2 这类崩溃处理状态，不是等待参数）。
栈字扫描只能恢复模块顺序，恢复不了句柄值。

**因此新增了"长等待见证"探针**（`src/DirectX12/Presentation/DX12WaitHooks.cpp`）：
`WaitForSingleObject` / `WaitForMultipleObjects` 钩子在调用**之前**把句柄登记进一个槽位表
（只对 `INFINITE` 或 `>= 1200ms` 的等待登记），一个 250ms 周期的看门狗线程发现某个等待
已持续超过 1.2 秒，就写下：

```
func=DX12LongWait,event=blocked,api=WaitForMultipleObjects(any),threadId=<T>,
                        elapsedMs=<N>,waitMs=<timeout>,count=<n>
func=DX12LongWait,event=handle,api=…,slot=0,handle=<H0>
func=DX12LongWait,event=handle,api=…,slot=1,handle=<H1>
```

**因为它在等待真正返回之前就落盘，所以句柄一定拿得到。** 拿到 `handle=H` 之后：

- 在该句柄上找日志里其它事件：`DX12WaitableObject … handle=<H>`（fence event / 帧延迟对象）
  → 说明等的是哪个 fence / 哪个事件，进而判断是 GPU 没完成还是事件没被 signal；
- 若 `H` 在日志里从未出现 → 是引擎自建的事件/fence，
  再看同一时刻 `deviceRemovedReason`、`uploadPending`、GPU 提交数，判断是"GPU 卡住"还是"CPU 侧漏 signal"。

这是目前唯一能把"等谁"钉死的手段，**下一次复现就能一步到底**。

---

## 3. 下一步该怎么做（按优先级）

### 第 1 步：再复现一次，读 `DX12LongWait`（唯一还没被证伪的判据）

开帧生成复现 → 日志里出现卡死时，看

```
func=DX12LongWait,event=blocked,…
func=DX12LongWait,event=handle,…
```

用拿到的 `handle` 反查：

```powershell
Select-String -Path D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt -Pattern "handle=<H>"
```

- 命中 `func=DX12WaitableObject,kind=fence_event` → 等的是游戏 fence，看 `fenceValue` 与
  `completedBefore/After`：**GPU 没完成** = 提交侧问题（代理可能注入了坏命令），
  **CPU 没 signal** = 逻辑漏信号；
- 命中 `func=DX12FrameLatency` → 帧延迟对象（当前证据显示不会发生，但保留判据）；
- 完全没命中 → 引擎自建事件，结合同一时刻的 `uploadPending` / `deviceRemovedReason` /
  `executedPresent` 判断是 GPU 卡住还是 CPU 漏 signal。

### 第 2 步：直接用现成工具分析已有转储（本仓库已提供，不需要装 WinDbg）

本机 `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\` **只有 DLL 没有 cdb/windbg**，
所以我把转储分析做成了仓库内的工具（见 `dev_tests/README.md`）：

```powershell
cd D:\Dev\ProjectBunny\dev_tests
.\crash_site_probe.exe "$env:TEMP\miHoYo\绝区零\Crashes\Crash_2026-09-17_101936912\crash.dmp" 8
.\sym_probe.exe "D:\Dev\ProjectBunny\x64\Release\d3d12.dll" `
    "D:\Dev\ProjectBunny\build\cmake-x64-release-timing\src\DirectX12\d3d12.pdb" 0xada1f
.\sym_probe.exe "C:\Windows\System32\KERNELBASE.dll" `
    "srv*C:\SymbolCache*https://msdl.microsoft.com/download/symbols" 0x22773
```

`crash_site_probe` 会给出：异常码、异常线程、每线程 `Rip/Rsp`、以及**按模块归属的栈字**
（跳过不认识的字，因此对没有符号的第三方 DLL 也有效）。这是本次能定位到
`HookedWaitForMultipleObjects` 的关键工具，务必保留。

### 第 3 步：跑一次对照组（成本最低）

用 **RELEASE 版**（非 TIMING）构建再试一次开帧生成：

```powershell
.\build_release_x64.ps1
```

TIMING 版在热路径上有大量插入（QPC、每帧计数、看门狗、`DX12MarkStage`、**wait 钩子**）。
上面的栈证明渲染线程确实走进了 wait 钩子——虽然逻辑是 passthrough，但**它会改变时序**，
而且 engine 的卡死检测阈值是固定的。若 RELEASE 版不卡，方向完全不同。

### 第 4 步：二分定位（若能取到历史构建）

用户口径是"0.1.4 能、0.1.5 起不能"，但仓库 tag 只到 `v0.1.4`（= 提交 `3bb5a057`，F315），
0.1.5～0.1.9 **不在 git 里**。发布版可在发布仓库按 tag 取：
- `git log --oneline v0.1.4..HEAD` 共 95 个提交
- 优先怀疑的提交（都动过提交路径/交换链/描述符）：
  `ebc1347`（面板 Mod 支持）、`74ca120`（日志重构）、`999222b`（per-draw 60KB 零填、pre-skin 复用）、
  `7bc7f2d`（Profiling 面板）、`fcdb9e8`（修 Hunting overlay 启动卡死）、`4b519a9`（d3dx_user.ini 持久化）

### 第 5 步：DLSS-G 侧的旁证

开帧生成时游戏必然：
- 以 `FRAME_LATENCY_WAITABLE_OBJECT + ALLOW_TEARING` 重建交换链（**已证实，flags=0x842**）
- 通过 `IDXGISwapChain2::GetFrameLatencyWaitableObject` 拿句柄并等待
- 每个显示帧调用**两次** `Present`

**代理最可能造成差异的地方**就是这里：句柄由我们转发，而等待方是引擎自己的线程。
第 1 步的 `DX12FrameLatency` 日志就是为它准备的。

---

## 4. 本次未提交的改动清单

| 文件 | 内容 | 状态 |
|---|---|---|
| `src/DirectX12/Presentation/DX12DbgHelpHooks.cpp` | 新增文件；搜索路径消毒 + symsrv 16 出口打桩 + 模块全表扫描 + trampoline 槽位 | 新增，未提交 |
| `src/DirectX12/CMakeLists.txt` | 加入 `DX12DbgHelpHooks.cpp` | 改，未提交 |
| `src/DirectX12/Resources/DX12RuntimeControl.h` | 声明 `DX12DbgHelpHooks`（`Install/Shutdown/InstallDbgHelpHooks/InstallSymSrvHooks/Rescan`）；worker 线程安装 | 改，未提交 |
| `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp` | 看门狗：卡死时 `Rescan()`；线程栈快照 `CaptureThreadStacks()` | 改，未提交 |
| `src/DirectX12/Core/Diag/DX12Diag.h` | `DX12DiagLog::WriterThreadId()`（栈快照跳过日志写线程） | 改，未提交 |
| `src/DirectX12/Presentation/DX12FactoryWrappers.h` | `CreateSwapChainForHwnd` 面包屑 + `DX12SwapChainCreate` 描述符日志；`GetFrameLatencyWaitableObject` 新增 `DX12FrameLatency` 句柄日志 | 改，未提交 |
| `src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h` | `Present`/`Present1` 增加 `present_primary_check` 面包屑 | 改，未提交 |
| `src/DirectX12/Presentation/DX12WrapperApi.cpp` | `WrapDXGISwapChainResult` 增加 `swapchain_wrap_enter/leave` 面包屑 | 改，未提交 |
| `src/DirectX12/Presentation/DX12WaitHooks.cpp` | **新增"长等待见证"**：`DX12LongWait` 在等待未返回时落盘句柄（这是当前唯一能钉死"等谁"的手段）；原有 passthrough 测量逻辑未变 | 改，未提交 |
| `dev_tests/crash_site_probe.cpp` + `.exe` | 迷你转储分析器（异常码/异常线程/按模块归属的栈字） | 新增，未提交 |
| `dev_tests/sym_probe.cpp` + `.exe` | 模块+偏移 → 符号（支持符号服务器） | 新增，未提交 |
| `docs/DX12-DLSSFG-交接文档.md` | 本文档 | 新增，未提交 |

已部署：`d3d12.dll`（TIMING，2026-09-17 18:51 构建；`x64\Release` 与游戏目录 SHA256 一致，
含 `DX12LongWait` 见证探针）。

### 可复用的调试探针

`dev_tests/` 下有两个**常驻**工具（不会像 `build/_scratch` 那样被构建清空）：

| 工具 | 用途 |
|---|---|
| `crash_site_probe.exe <dump> [maxThreads]` | 解析迷你转储：异常码/异常线程/各线程 `Rip/Rsp`/按模块归属的栈字 |
| `sym_probe.exe <module> [symbolPath] <offHex>…` | 模块 + 偏移 → 符号（本地 PDB 或 `srv*…` 符号服务器） |

`build/_scratch/` 下曾有 4 个一次性探针（**已被构建清空，需要时按下表重建**）：

| 探针 | 用途 |
|---|---|
| `SymsrvProbe.cpp` | 复现 symsrv 加载、枚举 16 个出口、打桩并验证调用即时返回 |
| `NektraProbe.cpp` | 验证 `CNktHookLib::Hook` 对 `original=nullptr` 返回 `0x57`（钉死 2.2 的关键） |
| `ResolveSymbol.cpp` | 早期版本，已被 `dev_tests/sym_probe` 取代 |
| `Extract-Stacks.ps1` | 把日志里的线程栈快照整理成可读文本（`-Log -StartLine -Out`） |

**符号解析的坑**：`SymLoadModuleEx` 要用 PE 头里的 **ImageBase**（本 DLL 是 `0x180000000`），
不是 0；`fInvadeProcess` 传 TRUE 时模块已被枚举，不要重复 Load。
`CNktHookLib::Hook` 的 `lplpCallOriginal` 必须非空。

---

## 5. 项目约定（务必遵守）

- C++20；代码要优雅且高性能；**禁止在函数内写 struct/class**
- 构建验证用 `build_release_timing_x64.ps1`；构建很快，等构建不要超过 60 秒，多轮等
- 不要把临时构建产物放在仓库根目录，放 `build/` 下
- 日志排查：**不要整文件读**，用采样或只读尾部
- 内核钩子相关：`DX12HookRegistry::HookFunction` 会挂起所有线程再打补丁，**不要在持有
  loader lock 或其它模块锁的上下文里调用**；`GetProcAddress` 会取 loader lock，所以
  看门狗的栈快照必须发生在所有线程恢复之后（现有实现已保证）

---

## 6. 接手人最需要知道的三件事

1. **两个已修复的缺陷是有实证的，不要回退**：`symsrv blocked=16` 与 `wininet` 链消失是硬证据。
2. **"闪退"不是崩溃，是引擎自己 DebugBreak**（`0x80000003` @ `UnityPlayer+0x12c601b`，两次崩溃完全一致）。
3. **渲染线程的停点已经钉死**：引擎固定一处代码（`UnityPlayer+0x62a659`）调用
   `WaitForMultipleObjects` 等一个**永不触发的句柄**。差的只是**那个句柄是谁**——
   `DX12LongWait` 见证探针已上线，**下一次复现必然拿到句柄值**，然后按 §3 第 1 步反查即可收口。

### 已经排除的方向（不要重复走）

- 符号服务器网络抓取（`wininet` 链）—— 已修复并实证；
- 帧延迟可等待对象句柄错位 —— **已证伪**（8 份日志里 `DX12FrameLatency` 与
  `matchesFrameLatency=true` 均为 0 次，游戏从未调用 `GetFrameLatencyWaitableObject`）；
- 我们自己的锁死锁 —— 看门狗锁探针全部 `free`；
- 交换链创建失败 / 设备移除 / GPU TDR —— `deviceRemovedReason=0x0`，创建成功。
