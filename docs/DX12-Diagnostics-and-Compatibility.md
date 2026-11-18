# DX12 诊断与兼容性指南

本文面向 DX12 版 `d3d12.dll`（3DMigoto / ProjectBunny）的使用者、Mod 作者与仓库维护者，汇总 **DX12 Hunting、FrameAnalysis、着色器导出与日志、TIMING 版诊断与崩溃自诊断、兼容链（DXGI / DLSS 帧生成 / 外部加载器边界）** 的实际用法、适用条件、默认值与失败路径。

阅读约定：

- 本文所有行为描述均对照**当前仓库源码**（注册、解析与实际消费路径）写成，涉及的源码位置逐条给出，见 [§10 源码证据](#10-源码证据)。
- 仓库里已有的专题报告用相对链接引用，并标注**适用条件**与**历史结论**；历史报告描述的是当时的现场，不等于当前结论。
- 本文**没有**执行任何游戏内测试。凡属"需要在目标游戏里复测"的条目一律标为 *待验证*，不写成已验证结论。
- 使用者视角的操作说明见 [usage/dx12/](../usage/dx12/index.md)；本文是诊断链路的深入版，两者有出入时以本文的源码依据为准。

---

## 1. 功能速查

| 能力 | 触发方式 | 产物 | 适用构建 | 前置条件 |
| --- | --- | --- | --- | --- |
| 定位（Hunting） | 小键盘 `0` | 游戏内绿色文字 + 剪贴板 Hash | 全部构建 | 游戏在前台；修饰键全部松开 |
| 抓帧（FrameAnalysis） | `F8` | `FrameAnalysis-<日期-时间>\` 目录 | 全部构建 | **必须先开启 Hunting** |
| 着色器导出 | `F9` | `ShaderFixes\` 目录 | 全部构建 | 无（Hunting 可关） |
| 重载 Mod 配置 | `F10` | 无（覆盖层提示） | 全部构建 | 无 |
| 性能面板（HUD） | `F11` 切换 / `F12` 冻结 | 游戏内叠加文本 | 全部构建 | 无 |
| 诊断日志 | 自动 | `d3d12_log.txt` | **仅 TIMING 版** | 无 |
| 卡死看门狗 / 长等待见证 | 自动 | 日志 | **仅 TIMING 版** | 无 |
| 崩溃自诊断 | 崩溃时自动 | 日志尾部 + `.dmp` | **仅 TIMING 版** | 无 |

> TIMING 版 = 用 `scripts/build_release_timing_x64.ps1` 构建的 Release（定义 `DX12_RELEASE_TIMING`）。普通 Release 版**不产生日志文件**，唯一的可见诊断是 HUD 叠加层。

---

## 2. DX12 Hunting（定位）

### 2.1 开关与前提

- 小键盘 `0` 切换 Hunting。开关与游戏内绿色文字是**同一个状态**：不存在"Hunting 开着但文字被隐藏"的中间态（`DX12HuntToggle` 同时翻转 `gHuntingActive` 与运行模式 `DX12RuntimeMode::Hunting` / `Mod`）。
- 打开 Hunting 会**清空**此前累积的所有候选集合（VS/PS/CS/IB/VB 的 visited 列表与已选中的项），并提升跟踪代次（`DX12CommandListRuntime::BumpTrackingGeneration`）、预热描述符历史（`DX12PrimeDescriptorHistoryForHunting`）。
- 关闭 Hunting 会清掉热路径开关，但**不会**清理描述符/资源跟踪队列里已经记录的条目。

DX11 的 `[Hunting]` 配置段在 DX12 **不存在**：`config/d3dx.ini` 开头已注明 `[Hunting]`、`[Device]`、`[Rendering]`、`[Logging]` 属于仅 DX11 运行时支持的段落并被移除。

### 2.2 按键

所有快捷键都要求 **Ctrl / Alt / Shift / Win 全部松开**（`modifierDown` 判定），并且游戏在前台。导航键以 166 ms 为间隔自动重复（约 6 次/秒）。

| 目标 | 上一个 | 下一个 | 复制 Hash |
| --- | --- | --- | --- |
| 像素着色器 PS | 小键盘 `1` | 小键盘 `2` | 小键盘 `3` |
| 顶点着色器 VS | 小键盘 `4` | 小键盘 `5` | 小键盘 `6` |
| 索引缓冲区 IB | 小键盘 `7` | 小键盘 `8` | 小键盘 `9` |
| 顶点缓冲区 VB | 小键盘 `/` | 小键盘 `*` | 小键盘 `-` |
| 计算着色器 CS | 小键盘 `.` + `1` | 小键盘 `.` + `2` | 小键盘 `.` + `3` |

- 小键盘 `+`（`VK_ADD`）清除**当前选择**（只取消 armed 标记，候选列表保留）。
- **所有**快捷键都受小键盘 `.` 的双向判定约束（统一生效条件是 `!modifierDown && (decimalDown == binding.requiresDecimal)`）：
  - CS 的三个键**必须按住小键盘 `.`**（`requiresDecimal = true`）；
  - 其余全部绑定——PS/VS/IB/VB、小键盘 `+` 以及 `F8` / `F9` / `F10` / `F11` / `F12`——都要求小键盘 `.` **没有**按下（`requiresDecimal = false`）。按住小键盘 `.` 时，这些功能键同样被抑制。
- `F8` / `F9` / `F10` / `F11` / `F12` 不受 Hunting 开关限制（但仍受上面的小键盘 `.` 约束）。

### 2.3 采到的 Hash 是什么

| 类别 | Hash 位宽 | 来源 |
| --- | --- | --- |
| VS / PS / CS | 64 位 | 着色器字节码的 FNV-1a 64（`Fnv1a64`） |
| IB / VB | 32 位 | 绑定**视图**的内容 Hash（`DX12HashIaBufferContentView`，输入为 GPU VA、大小、stride、格式、槽位） |

IB/VB 的身份是**视图级**而不是"每次 draw 的片段"：同一个网格用不同的 `baseVertex` / 索引区间绘制时会折叠成同一条候选，因此场景里通常只出现少量缓冲。复制出来的 IB/VB Hash 与 Mod 里 `ib=` / `vb=` 匹配使用的那套值一致，可直接粘贴。

候选集合在 Hunting 打开期间**不会老化淘汰**（对齐 DX11 的 `overlay_buffer_hash_lifetime=-1`）。这是刻意的：已 armed 的选择会让它自己的绘制被跳过，若此时淘汰条目就会出现"跳过→消失→再出现"的抖动。

### 2.4 选择的效果与屏幕文本

一旦某个类别被 armed（选择了候选），匹配的绘制/调度会被**跳过**：

- armed VS 或 PS：跳过 PSO 里带该 VS / PS 的绘制。
- armed IB：跳过绑定了同 Hash 索引缓冲的索引绘制。
- armed VB：跳过绑定了同 Hash 顶点缓冲的绘制。
- armed CS：跳过 PSO 里带该 CS 的 `Dispatch`。

屏幕叠加文本（`DX12HuntComposeOverlayText`）两行：

```
VS:2/17  PS:1/23  CS:0/4  IB:3/9  VB:0/12
VS 3a1f0c2b9d4e5f60   PS 7c3d5e1f0a2b4c6d   IB 1a2b3c4d
```

第一行是各类的"当前位置/总数"（位置为 `0` 表示该类有候选但未 armed）；第二行只列出 armed 项及其 Hash。

复制 Hash 走剪贴板（`CF_UNICODETEXT`）：VS/PS/CS 为 16 位十六进制，IB/VB 为 8 位十六进制。没有 armed 选择时提示 `DX12 hunting: no selected hash to copy`；剪贴板写入失败提示 `Copy <目标> hash failed`。**复制 Hash 不会导出着色器文件**，需要文件请用 `F9`。

### 2.5 日志字段

Hunting 相关的日志行（TIMING 版）：

| `func` | 关键字段 | 含义 |
| --- | --- | --- |
| `DX12HuntToggle` | `stage=state\|hotpath_update\|prime_descriptor_history\|bump_generation`、`enabled` | 开关 Hunting 的各个阶段 |
| `DX12HuntSelect` | `target=VS\|PS\|CS\|IB\|VB`、`selected`、`count`、`hash` | 上下切换候选 |
| `DX12HuntIaStats` | `frame`、`ib`、`vb` | 每 120 个呈现帧统计一次候选数量 |
| `DX12ShaderHunt` | `stage=unregister_command_list`、`commandList` | 命令列表注销 |

### 2.6 失败路径与限制

- 候选是在命令列表**发布**（publish）时才并入全局列表，因此刚出现的对象可能要等一个命令列表结束才会出现在计数里。
- 小键盘按键在 Num Lock 关闭时会被系统当作导航键，务必确认 Num Lock 状态。
- 复制 Hash 依赖剪贴板可用；某些以更高完整性级别运行的进程里剪贴板可能被拒绝，此时只有失败提示，没有降级路径。

---

## 3. FrameAnalysis（`F8` 抓帧）

### 3.1 触发与前提

`F8` 的入口是 `DX12UiActions::RunFrameAnalysisRequest`：

1. **先检查运行模式**：不是 Hunting 模式时直接失败，写
   `func=FrameAnalysisBeginFailed,reason=requires_hunting_mode`，叠加层提示 `Enable Hunting before Frame Dump`，**不会**创建目录。
2. 创建目录 `<d3d12.dll 所在目录>\FrameAnalysis-%Y-%m-%d-%H%M%S`；创建失败写
   `func=FrameAnalysisBeginFailed,reason=create_directory_failed`，叠加层提示 `Frame analysis failed: cannot create directory`。
3. 成功则写 `func=FrameAnalysisArmed`，叠加层提示 `Frame analysis armed`，并请求捕获。

捕获窗口是**恰好 1 个呈现帧**（`CaptureFramesPerDump = 1`）。捕获期间引擎侧命令列表会被挂起以避免漏记。

### 3.2 目录结构

```
<d3d12.dll 所在目录>/
└─ FrameAnalysis-2026-09-19-181234/
   ├─ log.jsonl          # 帧清单（JSON Lines）
   └─ deduped/           # 去重后的实际资源文件
      ├─ 1a2b3c4d_5e6f7a8b-DXGI_FORMAT_R8G8B8A8_UNORM.dds
      ├─ 0a1b2c3d.buf
      └─ 0a1b2c3d.txt     # 缓冲区的文本视图（十六进制）
```

- 纹理导出为 **DDS**，文件名形如 `<huntHash>_<fileHash>-<DXGI_FORMAT 名>.dds`；`huntHash` 是可用于 Mod 匹配的 Hash，`fileHash` 是本次读回内容的校验值。
- 缓冲区导出为 `.buf`（原始字节）并附带同名 `.txt` 文本视图。
- 去重键在 Hunting 模式下按"一个 `ID3D12Resource` 只读回一次"归并（对齐 RenderDoc 粒度）；非 Hunting 模式按描述符视图归并。

### 3.3 `log.jsonl`

每行一条 JSON，首行是表头：

```json
{"index":0,"func":"Header","schema":"dx12-fa-log/3","present_count_at_start":12345}
```

主要记录类型：

| `func` | `type` | 内容 |
| --- | --- | --- |
| `Header` | — | schema 版本与抓取起始的 present 计数 |
| `DrawIndexedInstanced` / `DrawInstanced` / `Dispatch` | `call.draw` / `call.dispatch` | `call_index`、`draw_id` / `dispatch_id`、`cmdlist`、`pipeline_state`、`pso`、`vs` / `ps` / `cs`（16 位十六进制，缺失为 `-`）、`topology`、顶点/索引数、`ib_gpu` / `ib_bytes` / `ib_fmt`、dispatch 的 `groups_x/y/z` |
| `BindIA` | `DX12FrameAnalysisManifest.h:416` | IA 绑定：`role`（`IB` / `VB`）、`slot`、`resource`、`dim`、`gpu`、`offset`、`bytes`、`stride`、`fmt`、`fmt_name`、`start_index` / `index_count` / `base_vertex`、`state` / `state_known`、`skin_source`、`producer_*`（计算生产者信息）、`file`、`hash` / `hunt_hash` / `file_hash` |
| `BindResource` | `DX12FrameAnalysisManifest.h:574` | 描述符/资源绑定：`bind`、`root`、`range`、`reg`、`space`、`desc`、`kind`、`resource`、`dim`、`width` / `height` / `fmt` / `fmt_name`、`view_dimension`、`gpu` / `offset` / `bytes`、`state` / `state_known`、`file`、`status`、`note`、`hash` / `hunt_hash` / `file_hash`、`hash_source` |
| `FileDump` | `DX12FrameAnalysisManifest.h:338` | 一次成功的资源导出：`status`、`kind`（`texture` / `buffer`）、`file`、`text`、`hash`、`bytes`、`note` |

`hash_source` 区分 `content`（TEXTURE2D 读回内容哈希）与 `descriptor_fallback`（拿不到内容哈希时用描述符视图哈希兜底）——两者数值含义不同，不要混用。

### 3.4 资源导出与跳过记录

导出前会先算候选任务，再逐个复制/映射。

> **这批 `func=` 记录的落盘文件**：下面 `func=ResourceDump`、`func=ResourceDumpComputeBindingsCollected`、`func=ResourceCopyResult`、`func=ResourceMapWriteSkipped`、`func=IaBindingPrepareSkipped`、`func=IaBindingAliasSkipped`、`func=IaBindingAliasMissingCanonicalDump`、`func=FrameAnalysisComplete` 都由 `DX12FrameAnalysis::LogJsonFunc` 写出，落到**抓帧目录下的 `log.jsonl`**（`DX12FrameAnalysisState::OpenLogLocked` 打开的那个文件），**不在 `d3d12_log.txt` 里**——请用 `Select-String` 查 `log.jsonl`。只有同名的 `func=DX12ResourceDump` 一行（`"bindings":…,"ia":…,"outputs":…,"frameReadbackBudget":…`）走 `DX12_LOG`，落在 `d3d12_log.txt`。两套记录各自独立：`log.jsonl` 每条都 `fflush`，不会像 `d3d12_log.txt` 的队列那样丢行（后者溢出时才出现 `func=DX12DiagLog,count=N`）。

相关日志：

- `func=ResourceDump,mode=Hunting|Mod,bindings=,ia=,outputs=,frameReadbackBudget=`：`mode` 反映当时的运行模式。
- `func=ResourceDumpComputeBindingsCollected`：计算管线绑定的收集与排队统计。
- `func=ResourceCopyResult,gpuCopy=,readbackOk=,copied=,copyFailed=,directMap=,tasks=,duplicates=`：复制阶段结果。
- `func=FrameAnalysisComplete,...`：收尾汇总，含 `resourceFilesWritten` / `resourceFilesSkipped` / `resourceFilesFailed`。

**失败与跳过路径**（这些是"某个资源没导出"的正常原因，不要当成抓帧整体失败）：

| `func` | `reason` / `note` | 含义 |
| --- | --- | --- |
| `ResourceMapWriteSkipped` | `missing_direct_resource` / `missing_readback` | 源资源或回读堆已失效 |
| `ResourceMapWriteSkipped` | `map_failed` | `Map` 失败，`hr` 字段给出 HRESULT |
| `ResourceMapWriteSkipped` | `exception=0x...` | 映射/写盘过程中触发结构化异常 |
| `IaBindingPrepareSkipped` | `ia_dedupe_key_build_failed` | 索引/顶点缓冲无法构造去重键 |
| `IaBindingPrepareSkipped` | `unresolved_gpu_va` | GPU VA 无法解析到资源；附带 `nearest_begin` / `nearest_end` / `nearest_resource` 供人工定位 |
| `IaBindingPrepareSkipped` | `unsupported_or_no_resource_pointer` | 绑定存在但没有可导出的资源指针 |
| `IaBindingAliasSkipped` | `note=copy_batch_aborted` | 复制批次被中止，别名条目不再写 |
| `IaBindingAliasMissingCanonicalDump` | `note=canonical_dedupe_path_missing` | 去重主条目缺失 |

另外，`DX12ResourceDumpCore::FrameReadbackBudgetBytes()` 当前实现恒返回 `0`，即**没有按字节数截断**；日志里的 `frameReadbackBudget` 字段因此恒为 0，不能当成配额读。

### 3.5 已知限制

- 只抓 1 帧；帧内的 draw/dispatch 数量可能很大，抓帧本身会明显卡顿并占用磁盘。
- 同一资源可有多个视图（不同偏移/大小），**Hash 相同不等于视图、偏移与用途相同**。
- 缺少某个资源文件时，先查上面的跳过/失败记录，不要直接认定"该资源内容为空"。
- 资源状态（`state`）在无法从创建或 barrier 追踪到观测值时是由推断函数（`InferDescriptorSourceState` / `InferIaBufferSourceState`）补的，此时 `state_known=false`。

---

## 4. 着色器导出（`F9`）

### 4.1 流程与产物

`F9` 不要求 Hunting 开启。流程是"上膛 → 抓下一帧 → 后台线程写文件"：

1. `DX12RequestShaderDump()`：若已有导出在进行（armed / capturing / running），写 `F9 shader dump ignored: already armed/running`，叠加层提示 `F9 shader dump still running`。
2. 上膛后叠加层提示 `F9 armed: waiting for next frame`；下一帧开始抓取时提示 `F9 capturing next frame shaders`。
3. 抓到的帧内着色器在独立线程里落盘；线程内异常会被捕获，叠加层提示 `F9 shader dump failed`，**异常码 `exception=0x...` 只出现在 TIMING 版日志里**（日志行 `F9 shader dump failed after exception=0x...`），普通 Release 没有日志文件，异常码完全不可见。

输出目录固定为 `<d3d12.dll 所在目录>\ShaderFixes`（无法创建时提示 `F9 shader dump failed: cannot create ShaderFixes`，日志为 `Failed to resolve DX12 ShaderFixes directory`）。

| 文件 | 内容 |
| --- | --- |
| `<16位 hash>-<stage>.bin` | 着色器字节码原始文件，stage 为 `vs` / `ps` / `cs` |
| `<16位 hash>-<stage>.asm.txt` | 反汇编（需要 `d3dcompiler_47.dll`；加载失败时日志为 `Failed to load d3dcompiler_47.dll for shader disassembly`） |
| `ShaderUsage.txt` | 着色器 → PSO 的使用关系 |
| `ShaderAnalysis.txt` | 着色器分析汇总 |
| `PsoResourceSummaryDX12.txt` | PSO 资源摘要 |

**已存在的同名文件不会被覆盖**，会被计入 `skippedShaders` / `skippedAsm`，状态栏以 `F9 <模式> complete: <n> bin / <n> asm, PSO summary ok|failed` 汇报。

### 4.2 失败路径

| 情况 | 叠加层提示 |
| --- | --- |
| 导出已在进行 | `F9 shader dump still running` |
| 该帧没抓到任何着色器/PSO | `F9 not ready: no frame shaders captured` |
| 内存不足 | `F9 shader dump failed: out of memory` |
| 无法创建线程 | `F9 shader dump failed: cannot start thread` |
| 目录不可用 | `F9 shader dump failed: cannot create ShaderFixes` |
| 导出线程内异常 | `F9 shader dump failed`（异常码只写在 TIMING 版日志的 `F9 shader dump failed after exception=0x...` 里） |

### 4.3 边界

- 导出的 `.asm.txt` 是反汇编，**不等于可直接编辑的 HLSL**；它用于分析，不是替换素材。
- `F9` 抓的是**帧内**用到的着色器，不是缓存里全部着色器。
- DX11 的 `ShaderFixes` 语义（HLSL 覆盖、运行时热重载）在 DX12 侧没有等价实现——DX12 的 `ShaderFixes` 目录只是导出目标。

---

## 5. 诊断日志

### 5.1 位置与轮转

- **只有 TIMING 版会写日志**：`DX12DiagLog::Open()` 在 `DX12Runtime::Initialize` 里被 `#if DX12_TIMING_BUILD` 包住。普通 Release 版既不建文件也不建写线程（除非你打开 HUD）。
- 文件位置 = **已加载的 `d3d12.dll` 所在目录** + `d3d12_log.txt`（`GetModuleFileNameW(DX12RuntimeState::GetModule())` 取目录）。例如把 DLL 放在 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\`，日志就在该目录。
- **运行期轮转**：启动时若已存在 `d3d12_log.txt`，先复制成 `d3d12_log_<GetTickCount64()>.txt` 再覆盖（best-effort，两个名字任一构造失败就跳过复制，不动截断路径）。
- **构建期轮转**：构建脚本会把上一份日志轮转为同目录的 `d3d12_log.before_<时间戳>.txt`（见 `AGENTS.md` 与 [交接文档](DX12-DLSSFG-交接文档.md)）。
- 日志很大，**不要整文件读**，用采样或只读尾部。

### 5.2 行格式与写入模型

每行是 `time=...,func=...,key=value,...` 形式；原本写成 JSON 片段的字段会被规范化成 `key=value`，Windows 路径里的反斜杠会被保留。

写入是异步的：每个线程把行追加进自己的 TLS 缓冲（热路径无锁、无分配），缓冲超过 8 KB 或显式 Flush 时整块发布给队列；队列上限 8192 行，**溢出被丢弃的行数**会以 `func=DX12DiagLog,count=<N>` 单独记一行——出现这一行就说明日志有缺口，不要把缺口当成"没有发生"。

写线程在事件上等待，超时 100 ms 轮询；帧汇总的合并、排序与格式化全部在写线程上完成，不发生在渲染线程的 Present 里。

### 5.3 常用检索

```powershell
# 只看尾部
Get-Content .\d3d12_log.txt -Tail 200

# 找失败/告警
Select-String -Path .\d3d12_log.txt -Pattern 'failed|unsupported|warning|error' | Select-Object -Last 50

# 崩溃自诊断（仅 TIMING 版）
Select-String -Path .\d3d12_log.txt -Pattern 'func=DX12Crash'

# 交换链创建结果（DLSS 帧生成重建时重点看）
Select-String -Path .\d3d12_log.txt -Pattern 'func=DX12SwapChainCreate'
```

常用 `func` 前缀速查：

| 前缀 | 含义 |
| --- | --- |
| `DX12Config` | 配置文件路径与加载结果 |
| `DX12ModRuntimeReload` | `F10` 重载：`status=begin_sync\|done_sync\|skipped` |
| `DX12Hunt*` / `DX12ShaderHunt` | Hunting 状态与选择 |
| `DX12SwapChainCreate` / `DX12SwapChainBoundary` | 交换链创建与包装边界 |
| `DX12BootstrapWarning` | 代理边界告警（例如设备未被拦截） |
| `DX12FrameLatency` | 帧延迟可等待对象 |
| `DX12LongWait` | 长等待见证 |
| `DX12FreezeWatchdog` / `DX12ThreadStack*` | 卡死看门狗与线程栈快照 |
| `DX12Crash*` | 崩溃自诊断 |
| `DX12ExecuteGapDiag` | 提交间隔（墙钟 + 线程 CPU） |

> **两套日志不要混查**：上表的前缀全部属于 `d3d12_log.txt`（仅 TIMING 版存在）。抓帧产生的 `func=ResourceDump` / `BindResource` / `BindIA` / `FileDump` / `FrameAnalysisComplete` 等记录属于**抓帧目录下的 `log.jsonl`**，用
> `Select-String -Path .\FrameAnalysis-*\log.jsonl -Pattern 'BindResource'` 查。
> 去 `d3d12_log.txt` 里搜这些 `func=` 不会有结果（普通 Release 更是连日志文件都没有）。

### 5.4 日志分析脚本

仓库自带 `scripts/analyze-dx12-log.ps1`，输出帧时间线、PreSkin 缓存命中率、API 墙钟 TOP15、Create 的 original/record 拆分、热路径单次成本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analyze-dx12-log.ps1 -Log "D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt"
```

它依赖的日志字段名是 `DX12FrameTimeline`（`present` / `frameWallUs` / `outsidePresentUs` / `presentWallUs` / `presentOriginalUs` / `queueEstimatedTotalUs`）、`category=preskin_bind_cache`（`calls` / `hits`）、`DX12ApiWallTiming`（`bucket` / `calls` / `totalUs` / `maxUs`）、`DX12CreateTiming`（`api` / `stage` / `calls` / `totalUs`）、`DX12HotPathTiming`（`stage` / `calls` / `totalUs`）。

脚本的 `-Log` 默认值是 `D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`，与常见的 `D:\MMTCacheFolder\...` 部署路径不同，**务必显式传参**。脚本注释提到的 `docs/log-analysis-guide.md` 在当前 `docs/` 下不存在。

脚本内部用 **`rg`（ripgrep）** 检索日志行，**依赖 PATH 上存在 ripgrep**；脚本开头只做 `Test-Path $Log` 检查，没有 `rg` 可用性检测。因此在没装 `rg` 的机器上脚本**不会报错**，而是安静地取不到数据（输出空表）——外观上与"日志里确实没有这些记录"完全相同。排查前先确认 `rg --version` 可用。

---

## 6. TIMING 版诊断

### 6.1 构建与代价

```powershell
.\scripts\build_release_timing_x64.ps1
```

它调用 `scripts/build-cmake.ps1 -ReleaseTiming`，打开 `DX12_RELEASE_TIMING` 选项（`src/DirectX12/CMakeLists.txt`），从而定义 `DX12_RELEASE_TIMING` / `DX12_TIMING_BUILD`。

TIMING 版在热路径上插入了 QPC 计时、每帧计数、看门狗、wait 钩子等。**不要用 TIMING 版测帧率**，它只用来读分段耗时日志；游戏内体验请用普通 Release 版。

TIMING 专属（普通 Release 里根本没有这些符号/线程）：

- 崩溃自诊断：`Core/Diag/DX12CrashReport.cpp` 只在 `DX12_RELEASE_TIMING=ON` 时编进 target；`DX12CrashBreadcrumb()` 在非 TIMING 版是空内联，编译器会把调用整个折掉。
- wait 钩子（`DX12WaitHooks`）、卡死看门狗（`DX12FreezeWatchdog`）、长等待见证线程。
- 日志文件与写线程。

### 6.2 性能面板（F11 / F12）

- `F11` 循环切换模式。当前源码的页面是 **Off → Overview → Buckets → Counters** 三页（`DX12ProfilingHud::Mode` 枚举：`Off / Overview / Buckets / Counters`），页码写在标题行里，截图即可自证是哪一页。
- `F12` 冻结/恢复显示：探针继续采样、日志继续写，只有屏幕上的数字停住，方便读数。
- 聚合窗口按**帧数**而不是墙钟计（`kWindowFrames = 60`，下限 30 帧，20 fps 兜底约 3 秒）。按帧计数是刻意的：按毫秒计会让 `max` 随帧率漂移。
- 面板是 **Release 也有的功能**：非 TIMING 版只在 HUD 打开时采样，TIMING 版无条件采样。
- HUD 首行会写明当前构建的时间戳（`DX12BuildStamp`），用来判断"截图是不是带修复的那个构建"。

> 面板档位以 `DX12ProfilingHud.h` 的枚举为准。六档的旧描述仍见于 `src/DirectX12/Resources/DX12RuntimeControl.h:257-259` 的按键注释。

### 6.3 卡死看门狗与长等待见证

**看门狗**（`DX12FreezeWatchdog`，TIMING 专属）每 250 ms 轮询一次 present 计数；当 present 长时间不前进时：

1. 先调 `DX12DbgHelpHooks::Rescan()`——晚于安装时刻加载的 dbghelp/symsrv 副本是唯一能靠"扫模块"化解的卡死源（正在走网络抓 PDB 的符号服务器请求对所有锁探针都是隐形的）。
2. 写 `func=DX12FreezeWatchdog,event=stall`，字段：`present`、`executedPresent`、`stalledMs`、`stage`、`skipAll`、`hookFlags`、`uploadPending`、`uploadPendingFence`、`uploadCompleted`、`gameQueue`、`deviceRemovedReason`、`overlayRecords`。卡死持续时会**持续重复上报**，锁在两次上报之间被释放会表现为状态跳变。
3. 写 `func=DX12FreezeWatchdog,event=locks`，逐个探测 `mod` / `preskin` / `upload` / `vmGlobals` / `overlay` / `descriptor` / `wrapper` 七把锁，值为 `free` 或 `HELD`。
4. 停顿超过阈值后抓一次全线程栈：先写 `func=DX12ThreadStackSummary,captured=,seen=,skipped=,modules=`，再逐帧写 `func=DX12ThreadStack,threadId=,frame=,address=,module=,moduleBase=,moduleOffset=`。**只有模块与偏移，没有符号**；写线程自身会被跳过（挂起的写线程无法排空队列）。

`stage` 字段来自 `DX12MarkStage` 的帧边界标记，常见取值：`present_primary_check`、`present_enter`、`present_original_call`、`present_leave`、`swapchain_create_enter` / `swapchain_create_leave` / `swapchain_wrap_call` / `swapchain_factory_release` / `swapchain_create_return`、`prepare_present_enter` / `prepare_present_leave`、`overlay_*`、`run_named_list`。它告诉你**卡在帧边界的哪一步**。

**长等待见证**（`DX12LongWait`，TIMING 专属）补的是看门狗给不出的那一块：看门狗能证明卡住了，但拿不到"在等谁"——线程栈快照时寄存器已被崩溃处理覆盖。看门狗线程（250 ms 周期）在等待**尚未返回**时就把句柄落盘：

```
func=DX12LongWait,event=blocked,api=WaitForMultipleObjects,threadId=<T>,elapsedMs=<N>,waitMs=<M>,count=<n>
func=DX12LongWait,event=handle,api=WaitForMultipleObjects,slot=0,handle=<H0>
```

拿到句柄后反查同目录日志里的其它事件（`DX12FrameLatency`、`DX12WaitableObject` 等）即可判断是 GPU 没完成还是 CPU 没 signal。等待钩子对**小于 50 ms 且句柄未被跟踪**的等待直接跳过测量（`CanSkipMeasurement`），短轮询不付记账开销。

### 6.4 崩溃自诊断

TIMING 版在 `DX12Runtime::Initialize` 里、`DX12DiagLog::Open()` 之后、**任何 D3D12 对象创建之前**安装未处理异常过滤器（`DX12CrashReportInstall`），并把"让写线程先停机"的回调交给报告器（`DX12CrashReportSetLogQuiesce(&DX12DiagLog::QuiesceForCrash)`）。

两个组成部分：

1. **面包屑环**：512 条定长记录（作用域名指针、线程 ID、tick），挂在每个被打点的 `Api.*` / `Create.*` / `Stage.*` 包装器入口上，热路径无锁无分配。它记录的是"我们确实进过这个 API"，这是原始栈给不出的信息（驱动会覆写返回地址，栈里会混进历史返回地址）。
2. **崩溃报告**：异常记录、寄存器、按本地 PDB 符号化的栈、全部线程的 RIP、面包屑尾巴，以及一份 minidump。

报告字段：

| `func` | 关键字段 |
| --- | --- |
| `DX12Crash` | `event=detected`、`event=report_done`、`mode=log\|separate_file`、`file` |
| `DX12CrashRecord` | `code`、`flags`、`address`、`where`、`access`、`accessAddress` |
| `DX12CrashContext` | 崩溃线程的全部寄存器 |
| `DX12CrashStack` | `threadId`、`frame`、`address`、`where`、`symbol`、`line` |
| `DX12CrashThread` | 其它线程当时的 `rip` / `where` / `faulting` |
| `DX12CrashBreadcrumb` | `seq`、`ageMs`、`threadId`、`crashThread`、`scope`；以及 `event=summary` 的 `written` / `kept` / `capacity` |
| `DX12CrashDump` | `status=ok\|create_failed\|write_failed`、`bytes`、`path` |
| `DX12CrashCode` | `event=requested`、`address`、`bytes`、`fnv1a`、`where`（崩溃地址处的代码字节） |

**落盘位置的选择**：报告默认追加到 `d3d12_log.txt` 尾部（`mode=log`）；如果写线程在 500 ms 内停不下来（卡在堆锁里），报告**不再与该文件共享**，改写自己的 `d3d12_crash_<日期-时间>.txt`（`mode=separate_file`）。理由是写线程持有自己的 `FILE*` 与文件位置，它下一次缓冲刷新会从旧位置覆盖掉刚追加的报告——宁可多一个文件，也不要一份被覆盖的证据。

**文件名**：报告正文与 minidump **同目录、同时间戳**——`d3d12_crash_<YYYYMMDD-HHMMSS>.txt` 与 `d3d12_crash_<YYYYMMDD-HHMMSS>.dmp`（源码 `DX12CrashReport.cpp` 里两处拼接共用同一个 `stamp`，格式 `%04u%02u%02u-%02u%02u%02u`）。该目录就是日志所在目录。

**边界**：

- 报告是**进程内**写的：`MiniDumpWriteDump` 会短暂挂起其它线程；崩溃线程若卡在堆锁里，报告可能不完整，因此报告正文先落盘、转储最后写。
- 只有**本模块**的栈帧能解析出函数名与源码行；`D3D12Core.dll` / 驱动只有"模块+偏移"（符号服务器路径被 [§6.5](#65-dbghelp--symsrv-加固) 的加固层挡住）。
- 每个进程只报一次（第二个崩溃线程不重复写，但仍走链式调用，引擎自带的崩溃上报不受影响）。
- 面包屑环允许读到一条撕裂记录（诊断用途，不是有依赖的数据结构）。
- 崩溃发生在 `DX12Runtime::Initialize` 之前（连 `D3D12CreateDevice` 都没走到）时没有报告。
- 报告刻意不依赖运行时状态，因此**不含 present 计数**；需要帧信息时看面包屑的 `ageMs` 与日志里最后的 `Present.*` 行。

完整设计与回归验证见 [DX12-崩溃自诊断.md](DX12-崩溃自诊断.md)。

### 6.5 dbghelp / symsrv 加固

`DX12DbgHelpHooks` **在所有构建里都安装**（`DX12Runtime::WorkerThread` 里无条件调用 `DX12DbgHelpHooks::Install()`，并循环 200 次 × 50 ms 以覆盖"游戏晚加载 dbghelp"的情况）。它做两件事：

1. **搜索路径消毒**：`SymInitialize` / `SymInitializeW` / `SymSetSearchPath` / `SymSetSearchPathW` 的搜索路径里的 `srv*`、`symsrv*`、含 `://` 的 token 被剥掉，符号服务器相关环境变量一并清理。
2. **取数出口打桩**：symsrv 的 16 个取数导出（`SymbolServer*` / `SymbolServerPing*` / `SymbolServerStore*`）全部桩成"立即失败"。

原因是实测过的：某些引擎在开帧生成等时刻会做一次原生栈符号化，dbghelp 带着符号服务器路径去取 PDB，symsrv 走 wininet 做**无超时**网络请求，把渲染线程钉死在用户态。

> 这两个缺陷的历史定位过程、证据与"不要重复走的方向"见 [DX12-DLSSFG-交接文档.md](DX12-DLSSFG-交接文档.md)（该文档顶部有明确的"旧结论已被推翻"说明）。

---

## 7. 兼容链

### 7.1 外部加载器边界（进程门）

`d3d12.dll` 是**代理 DLL**：它按名字被外部加载器注入目标进程，再转发到系统 `d3d12.dll`。第一道关卡是进程门 `D3D12ProcessGateUtils::Evaluate`，它读 `<d3d12.dll 所在目录>\d3dx.ini` 的 `[Loader]` 段：

**判定顺序**（`D3D12ProcessGateUtils::Evaluate`）：

1. **先读 `d3dx.ini` 的 `[Loader]` 段**。读不到该段，或该段里 `target` 与 `loader` 都没写 → 走第 3 条；读到 → 走第 2 条。
2. **存在 `[Loader]` 段时**：
   - 进程路径匹配 `target` → 允许，**运行完整运行时**（`role = target`）。
   - 否则，进程名匹配 `loader`，**或**（DLL 与进程同目录 **且** 进程名为 `Run.exe`）→ 允许，**仅引导**（`role = bootstrap`）。
   - 否则 → **拒绝**（`role = reject`，reason = `rejected by configured loader target`）。
3. **不存在 `[Loader]` 段时**：DLL 与进程同目录 → 允许，**运行完整运行时**（`role = target`，reason = `same directory proxy fallback`）；不同目录 → **拒绝**（reason = `no configured target and not same directory`）。

> "同目录 + `Run.exe`"这条**只在存在 `[Loader]` 段时**才可能被判成 `bootstrap`；没有 `[Loader]` 段时，同目录一律走第 3 条，得到的是 `target`（完整运行时）。

判定结果（**允许与拒绝都会写**）会进 `OutputDebugStringW`：

```
[Bunny-D3D12Gate] deny role=reject: pid=1234 process=… module=… target=… loader=… config=… reason=rejected by configured loader target
```

路径发现失败（模块路径过长、进程路径过长、缺少分隔符等）一律按**拒绝**处理——宁可拒绝，也不要拿截断路径去比目录。

**进程门实际只读两个字段**：`target` 与 `loader`（`D3D12ProcessGateUtils::ReadLoaderConfig` 在 `[Loader]` 段里只取这两项）。`config/d3dx.ini` 的 `[Loader]` 段里还有 `module`、`launch`、`require_admin`、`delay`，但它们**只给外部加载器（XXMI Launcher）使用，本仓库不解析**——例如 `module` 告诉加载器去哪里找代理 DLL（必须与加载器同目录，并以同名加载进目标进程）。

### 7.2 D3D12 代理

- 真实实现从 `%SystemRoot%\System32\d3d12.dll` 加载（`D3D12ProxyRuntime::LoadRealD3D12`），并缓存 `D3D12CreateDevice`、`D3D12GetDebugInterface`、根签名序列化等导出。
- `D3D12CreateDevice` 成功之后才包装设备；失败时原样返回 HRESULT。
- 代理导出函数本身也是打点对象（日志里 `D3D12CreateDevice,event=enter|leave,…,wrapped=true|false`），可以直接判断"设备到底有没有被拦截"。

### 7.3 DXGI 钩子

`DXGIHooks::Install()` 对 `dxgi.dll` 的三个导出打补丁：`CreateDXGIFactory`、`CreateDXGIFactory1`、`CreateDXGIFactory2`。若 `dxgi.dll` 尚未加载，本次安装会被推迟（由 worker 线程重试），日志为 `dxgi.dll is not loaded yet; DXGI wrapper bootstrap deferred`。

工厂被包装后，`CreateSwapChainForHwnd` / `CreateSwapChain` / `CreateSwapChainForCoreWindow` 才会走我们的包装路径。

### 7.4 交换链边界（Mod 生效的前提）

`WrapCreatedSwapChain` 的判定只有一条：**传入的 device 必须是我们包装过的**（`DX12Wrapper::Unwrap(device) != device`）。

| 情况 | `DX12SwapChainBoundary` 的 `mode` | 后果 |
| --- | --- | --- |
| device 是包装过的 | `wrapped` | `modInterception=active`，交换链被包装，Mod 生效 |
| device 是包装过的但包装失败 | `wrap_failed` | Mod 不生效 |
| device 是**原生**的 | `native_passthrough` | 交换链保持原生；同时写 `DX12BootstrapWarning,reason=unwrapped_swapchain_queue,action=load_proxy_before_device_creation,note=DLL_loaded_does_not_mean_D3D12_device_intercepted` |

> **"DLL 已被加载"不等于"D3D12 设备已被拦截"**。若代理注入晚于 `D3D12CreateDevice`，整个 Mod 链路就是旁路状态——此时画面正常、INI 正常、没有报错，但替换一条都不生效。排查"Mod 完全没反应"时先在日志里搜 `DX12SwapChainBoundary` 与 `DX12BootstrapWarning`。

### 7.5 DLSS 帧生成

帧生成会**重建交换链**，请求里带一组 DXGI 标志位。现场日志里的 `flags=0x842` 按 Windows SDK 的 `DXGI_SWAP_CHAIN_FLAG_*` 定义（`dxgi.h`：`ALLOW_MODE_SWITCH = 0x2`、`FRAME_LATENCY_WAITABLE_OBJECT = 0x40`、`ALLOW_TEARING = 0x800`）分解为 `0x800 | 0x40 | 0x2`，即 `ALLOW_TEARING | FRAME_LATENCY_WAITABLE_OBJECT | ALLOW_MODE_SWITCH`。

> 这些标志位**由 DLSS / 游戏侧请求，本仓库不设置它们**：`src/DirectX12/Presentation/DX12FactoryWrappers.h:329-334` 只把调用方传入的 `pDesc->Flags` 原样记进日志（`"flags":"0x%x"`）。
>
> `docs/DX12-DLSSFG-交接文档.md:153` 把 `0x842` 写成 `FRAME_LATENCY_WAITABLE_OBJECT (0x40) | ALLOW_TEARING (0x800)`——两者相加是 `0x840`，不含 `0x2`，**该历史文档此处数值有误**；本文按 SDK 头文件更正。该历史文档不在本轮可写范围内，未做修改。

相关探针：

- `func=DX12SwapChainCreate,api=CreateSwapChainForHwnd,hwnd=,width=,height=,format=,buffers=,sampleCount=,flags=,swapEffect=`：每次创建一行，**在调用前**就写出参数（调用卡住时不会有事后日志）。
- `func=DX12SwapChainCreate,event=result,api=CreateSwapChainForHwnd,hr=0x…,swapchain=…`：**返回后的真实 HRESULT**。注意旧的 `swapchain_create_return` 标记只表示"调用返回了"，不代表成功——判断成功只看 `event=result` 的 `hr`。
- `func=DX12FrameLatency,event=get_waitable,swapchain=,handle=`：`GetFrameLatencyWaitableObject` 的句柄与交换链身份配对。

历史结论（**均为历史现场，不是当前结论**）：

- [DX12-DLSSFG-交接文档.md](DX12-DLSSFG-交接文档.md) 顶部已写明旧结论被推翻："交换链创建成功"、"只能继续查等待句柄"都不成立；`STATUS_BREAKPOINT` 也不能单独证明引擎触发了卡死检测。该文档保留作历史记录。
- 当前定位与修复见 [DX12-DLSSFG-修复报告.md](DX12-DLSSFG-修复报告.md)：绘制缓存持有旧原生交换链的 QI 引用、wrapper 销毁时未失效缓存，导致旧 flip-model 交换链继续占用 HWND，帧生成创建替代交换链失败（游戏侧 `CreateSwapChainForHwnd failed (80070005)`）。
- 修复报告中的验证是 WARP / DXGI 层面的回归测试与构建部署核对，**游戏内 DLSS 帧生成复测仍需用户完成**（报告里给出了复测步骤）。本文不把该复测写成已完成。

### 7.6 与 DX11 配置的兼容边界

DX12 不按 DX11 语义执行的段落与语法（`[Hunting]`、`[Device]`、`[Rendering]`、`[Logging]`、`[System]` 的非 dx12 键、Pool/Region 扩展语法、`@/#/->` 表达式接口、位运算与数学函数扩展、`store`/`dump`/`clear`/`analyse_options` 等）见 [usage/dx12/compatibility.md](../usage/dx12/compatibility.md) 与 [DX11 配置迁移范围](../usage/dx12/compatibility.md)。抓取用 `F8`/`F9`，没有 DX11 的 `analyse_options` 等价物。

---

## 8. 已知限制

**功能边界**

- `F8` 抓帧**必须先开 Hunting**，否则只写 `FrameAnalysisBeginFailed,reason=requires_hunting_mode` 并提示 `Enable Hunting before Frame Dump`。
- `F8` 只抓 1 帧；`F9` 只抓帧内用到的着色器。
- 复制 Hash 不导出文件；`F9` 导出的反汇编不是可编辑的 HLSL。
- 帧分析**不提供** DX11 的 `dump` / `clear` / `store` / `analyse_options` 等价命令。
- Hunting 的 IB/VB Hash 是视图级，跨视图（不同偏移/大小）不通用。

**构建边界**

- 普通 Release **不写日志**、**没有**崩溃自诊断、**没有** wait 钩子与卡死看门狗。
- TIMING 版会改变热路径时序，**不能**用来测帧率；游戏内卡死阈值敏感的对比实验要用普通 Release 做对照组。
- 非 TIMING 版的诊断数据只有 HUD 叠加层（3 页 + 冻结）。

**兼容边界**

- 代理注入晚于 `D3D12CreateDevice` 时整条 Mod 链路旁路（`native_passthrough`），且**不会报错**。
- 进程门拒绝时（`role=reject`）不会加载运行时，只能从 `OutputDebugString` 里看到原因。
- DX12 的 `ShaderFixes` 目录只是导出目标，没有 DX11 的 HLSL 覆盖/热重载语义。
- `d3d12.dll` 只代理系统 `d3d12.dll`；游戏若自带或使用第三方 D3D12 运行时，代理边界之外的行为不在本仓库控制范围内。

**分析边界**

- 崩溃报告只有本模块的帧能解析符号；其它模块只有"模块+偏移"。
- 线程栈快照只有模块与偏移，没有符号。
- `frameReadbackBudget` 当前恒为 0，没有按字节截断。
- 日志队列溢出会丢行（以 `func=DX12DiagLog,count=N` 提示），丢行之后不能断言"某事件没发生"。
- 交换链重建、资源回读都会带来停顿；不要把抓取期间的停顿当成正常帧率。

---

## 9. 已有报告索引

以下都是仓库内既有文档，本文按需引用；每份的**适用条件**与**历史结论属性**见各文档开头。

| 文档 | 内容 | 适用条件 |
| --- | --- | --- |
| [DX12-崩溃自诊断.md](DX12-崩溃自诊断.md) | TIMING 版崩溃自诊断的设计、报告格式、日志写线程交接、回归验证、已知边界 | 仅 TIMING 版；验证用的是仓库内崩溃子进程脚本 |
| [DX12-DLSSFG-交接文档.md](DX12-DLSSFG-交接文档.md) | 帧生成卡死、闪退的排查记录及探针清单 | 历史记录，当前结论见修复报告 |
| [DX12-DLSSFG-修复报告.md](DX12-DLSSFG-修复报告.md) | 交换链重建失败（`80070005`）的定位与修复、WARP 回归用例、用户复测步骤 | 修复与 WARP 回归已验证；**游戏内帧生成复测待用户完成** |
| [DX12-Mod性能优化报告.md](DX12-Mod性能优化报告.md) | Mod 路径的性能优化 | 性能结论只在其自身条件下成立 |
| [DX12-RemainingHotspots-20260918.md](DX12-RemainingHotspots-20260918.md) | 剩余热点 | 快照式结论，随代码变化失效 |
| [DX12-描述符复制范围缓存优化报告.md](DX12-描述符复制范围缓存优化报告.md) | 描述符复制范围缓存优化 | 同上 |
| [DX12-DrawIndexedInstanced优化报告.md](DX12-DrawIndexedInstanced优化报告.md) | `DrawIndexedInstanced` 路径优化 | 同上 |
| [DX12-NonCopyDescriptor优化报告.md](DX12-NonCopyDescriptor优化报告.md) | 非拷贝描述符优化 | 同上 |
| [wolong-model-load-crash-20260919.md](wolong-model-load-crash-20260919.md) | 模型加载崩溃调查 | 单次现场 |
| [wolong-startup-crash-20260919.md](wolong-startup-crash-20260919.md) | 启动崩溃调查 | 单次现场 |
| [ini-language-extensions/logging.md](ini-language-extensions/logging.md) | INI 语言扩展的日志相关约定 | 与 DX11 语义对齐的扩展 |
| [usage/dx12/](../usage/dx12/index.md) | 使用者视角的 DX12 文档 | 速查；与本文冲突时以本文的源码依据为准 |

---

## 10. 源码证据

本文结论对应的源码位置：

| 主题 | 位置 |
| --- | --- |
| 快捷键注册与消费 | `src/DirectX12/Resources/DX12RuntimeControl.h`（`DX12UiActionBits`、`DX12UiActions::PollKeyboard` / `Consume` / `RunFrameAnalysisRequest`） |
| 运行时初始化、日志打开、崩溃过滤器安装 | `src/DirectX12/Resources/DX12RuntimeControl.h`（`DX12Runtime::Initialize`） |
| 进程门（外部加载器边界） | `src/DirectX12/Proxy/D3D12ProcessGate.h` |
| D3D12 代理与真实库加载 | `src/DirectX12/Proxy/D3D12Proxy.cpp`、`src/DirectX12/Proxy/D3D12ProxyRuntime.h` |
| DXGI 工厂钩子 | `src/DirectX12/Presentation/DXGIHooks.cpp` |
| 交换链创建与包装边界 | `src/DirectX12/Presentation/DX12FactoryWrappers.h`（`CreateSwapChainForHwnd`、`WrapCreatedSwapChain`、`GetFrameLatencyWaitableObject`） |
| Hunting 状态机、跳过逻辑、复制、叠加文本 | `src/DirectX12/ShaderTools/DX12ShaderHunt.cpp` |
| 帧分析状态机与 `log.jsonl` | `src/DirectX12/Core/CommandState/DX12FrameAnalysisState.h`、`src/DirectX12/Tracking/DX12FrameAnalysisTracking.h` |
| 帧分析清单字段 | `src/DirectX12/Runtime/DX12FrameAnalysisManifest.h` |
| 资源导出、去重命名、跳过原因 | `src/DirectX12/Resources/Dump/DX12ResourceDump.h`、`…/DX12ResourceDumpCore.h`、`…/DX12ResourceDumpFileWriter.h`、`…/DX12ResourceDumpNaming.h`、`…/DX12ResourceDumpDedupedNaming.h`、`…/DX12ResourceDumpIndexWriter.h` |
| 着色器导出（`F9`）与产物文件名 | `src/DirectX12/ShaderTools/DX12ShaderDump.cpp` |
| 日志写入、队列、轮转、崩溃静默 | `src/DirectX12/Core/Diag/DX12Diag.h` |
| 崩溃自诊断 | `src/DirectX12/Core/Diag/DX12CrashReport.h`、`src/DirectX12/Core/Diag/DX12CrashReport.cpp` |
| TIMING 编译开关 | `src/DirectX12/CMakeLists.txt`（`option(DX12_RELEASE_TIMING …)`、`Core/Diag/DX12CrashReport.cpp`） |
| 性能面板模式与窗口 | `src/DirectX12/Core/Diag/DX12ProfilingHud.h` |
| 卡死看门狗与线程栈快照 | `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp` |
| 长等待见证 | `src/DirectX12/Presentation/DX12WaitHooks.cpp` |
| dbghelp / symsrv 加固 | `src/DirectX12/Presentation/DX12DbgHelpHooks.cpp`（声明见 `src/DirectX12/Resources/DX12RuntimeControl.h`） |
| 日志分析脚本依赖的字段名 | `scripts/analyze-dx12-log.ps1` |
| `[Loader]` 段示例与 DX11 段移除说明 | `config/d3dx.ini` |

---

## 11. 待确认事项

以下条目留给维护者处理；其中标出位置的已在本轮对照源码或 Windows SDK 头文件核实。

- `scripts/analyze-dx12-log.ps1` 默认查找 `D:\SSMTCacheFolder\...`，使用不同部署目录时请传入 `-Log`。运行前确认 `rg` 可用。
- [DX12 崩溃自诊断](DX12-崩溃自诊断.md) 引用的 `dev_tests/crash_report_test.ps1`、`dev_tests/crash_site_probe.exe`、`dev_tests/sym_probe.exe` 在当前仓库中缺失，原验证命令无法按文中路径复跑。
- 当前 `frameReadbackBudget` 为 0，未设置按字节截断的资源读回预算。
- 游戏内 DLSS 帧生成复测仍按 [DLSSFG 修复报告](DX12-DLSSFG-修复报告.md) 的步骤执行。
