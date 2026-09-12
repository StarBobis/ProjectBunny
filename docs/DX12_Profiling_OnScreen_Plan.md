# DX12 Profiling 改造方案 —— 从写日志改为屏幕实时绘制

> 目标：把 DX12 的 Profiling 从"周期性往 `d3d12_log.txt` 灌 CSV 块"改成"像 DX11 那样在游戏画面上实时绘制"，
> 并且**每帧成本必须低于 DX11 的 Overlay 实现**。
>
> **状态：已实施完成。** 本文保留为设计方案，第 11 节记录实际落地的内容、与方案的偏差、
> 以及验证结果。所有结论都标注了 `文件:行号` 依据。

---

## 0. 已确认的三条决策（用户拍板）

| 编号 | 决策 | 落地方式 |
|---|---|---|
| **D1** | Release 和 TIMING **都要**有 profiling HUD；日志仍然只有 TIMING 产出 | profiler 编译进所有构建；`DX12DiagProf::Enabled()` 在 TIMING 是编译期 `true`、其它构建读 HUD 模式。日志宏仍只在 TIMING 展开 |
| **D2** | 日志**只停** Profiling（`func=DX12Profiling` 桶表），排障段全部保留 | `dx12_profiling_log_buckets`（默认 0）只控制 `EmitScopeTable`；FrameTimeline/ApiWall/HotPath/Create/CpuStage/Lock/Counters 不受影响 |
| **D3** | 做跨模块合并提交 | 新增 `DX12PresentOverlayCompose`：mod `[Present]` 元素 + hunting 文字 + notice + HUD 合成**一个** command list、**一对** barrier、**一次** `ExecuteCommandLists` |

---

## 1. 结论先行

**能做到，而且比 DX11 明显更快。** 稳态每帧约 **3–8 µs**，DX11 同一口径（`Profiling::overlay_overhead`）是 **20–50 µs**。

原因不是"优化得好"，而是三条结构性差异，DX12 天生占便宜：

| # | 结构性差异 | DX11 的代价 | DX12 的对策 |
|---|---|---|---|
| 1 | **状态保存/恢复** | 必须 `SaveState()`/`RestoreState()` 25+ 个 D3D11 状态对象（每个 `Get*` 都是一次 COM 调用） | 自己持有 PSO / root signature / command list，天然不改游戏状态，只有 backbuffer 前后两个 barrier |
| 2 | **顶点重建频率** | `MeasureString` + `DrawString` **每帧**重建全部字形顶点并 Map/写动态 VB | 按"内容代际"缓存顶点：Profiling 数据每 interval 才变一次，**59/60 帧连 memcpy 都没有** |
| 3 | **格式化线程** | `Profiling::update_txt()` 在**渲染线程**跑几百次 `_snwprintf_s` | 全部在已有的日志写线程上做，渲染线程只收一份成品文本 |

再加上第 4 条：HUD 与 Hunting 文字**合并成一次 draw call / 一次提交**，边际 GPU 成本几乎为零。

---

## 2. 现状

### 2.1 DX12 现状：数据通路（采样 → 写线程 → 日志）

**采样**（`Core/Diag/DX12Diag.h:168` `DX12DiagProf`）

- 每线程开放寻址 bucket 表：64 线程 × 1024 slot（`kBucketsPerThread`），key = 字符串字面量指针 + `sub` + `kind`（`:495 HashKey`）。
- 热路径只做 plain RMW：一次 hash + 线性探测 + `++calls` / `+=totalUs` / `maxUs` 比较，**无锁、无分配、无格式化**。
- 渲染线程在 `Present` 两端打点：`DX12_PROF_FRAME_ENTER` / `DX12_PROF_FRAME_LEAVE`（宏定义 `:1722-1725`，调用点 `Presentation/DX12WrapperRuntimeUtils.h:519/623`）。
- `FrameLeave` → `PushFrameRecord`（16 深度 SPSC 环，`:637`）+ `DX12DiagLog::Flush()`。

**聚合与格式化**（已经是写线程，这点比 DX11 好）

- 写线程 `DX12DiagLog::ThreadProc`（`:773`）100 ms 轮询一次 → `DX12DiagProf::FlushFrameSummaries`（`:470`）。
- `PumpFrameRing`（`:568`）累计到 `kSummaryIntervalFrames = 60` 帧后调 `EmitSummary`（`:1647`）。
- `DrainThreadTables`（`:1153`）对每个具名 bucket 做 4 次 `InterlockedExchange64` 取走并清零 → 合并进 `merged[512]`。
- `EmitSummary` 组装 8 个 `Emit*Section` → 一整块 `std::string` → `WriteDirect` 落盘。

**输出到日志的形态**（`d3d12_log.txt`）

| 行标签 | 内容 |
|---|---|
| `func=DX12FrameTimeline` | 一行 ~60 个字段的帧时间线（`:1335`） |
| `func=DX12ApiWallTiming` | `Api.*` bucket（`:1431`） |
| `func=DX12HotPathTiming` | `Stage.*` bucket（`:1454`） |
| `func=DX12CreateTiming` / `DX12CpuStage` / `DX12Lock` … | 其余分组 |
| `func=DX12Profiling` | **体量最大**：`name,calls,totalUs,avgUs,maxUs,items` 表，357 个 bucket（`:1633`） |

> 这就是用户想干掉的东西：357 行 × 每个窗口一块 CSV，日志很快膨胀，而且只能事后翻文件。

### 2.2 DX12 现状：已经具备的屏幕绘制能力

`ShaderTools/DX12NativeOverlay.cpp`（`DX12InGameTextOverlay` + `DX12NativeOverlayRender`）已经是一套完整的 DX12 文字 HUD，**本方案直接扩它，不新起炉灶**：

- **字形来源**：GDI 光栅化 Consolas → R8 alpha 图集，95 个可打印 ASCII（32..126），16 列 × 6 行（`:564 BuildFontAtlas`）。无字体文件、无嵌入 blob。
- **GPU 对象**：自己的 root signature / PSO / SRV 堆 / 索引缓冲 / 6 槽上传堆顶点环 / fence（`:331 EnsureBaseObjects`）。
- **提交通道**：复用 `Bunny::DX12PresentOverlayChannel`（`ModRuntime/DX12CustomShaderDevice.h:964`）—— mod 自有的 2 个 `allocator+list` 槽，轮流 Reset，提交到游戏的 direct queue，位置在原始 `Present` 之前。
- **RTV 来源**：复用 `DX12CustomShaderSurfaceCache::EnsureBackBufferView`（`DX12CustomShaderDevice.h:834`）。
- **录制内容**（`:140-202`）：`RecordScope` → `BeginRecord` → backbuffer barrier `PRESENT→RT` → 设堆/RS/PSO/拓扑/IB/VB/OM/视口/裁剪 → 1 次 `DrawIndexedInstanced` → barrier 回 `PRESENT` → `Submit` → `Signal`。
- **调用点**：`Presentation/DX12WrapperRuntimeUtils.h:583`，紧跟在 `DX12ModSubmitPresentOverlay` 之后。
- **快速路径**（`:940-948`）：`!hunting && !hasNotice` 时只做 2 次原子读就返回，零 GPU 工作。

> **Present 拦截形态**：本项目**不做 vtable/导出补丁**，是纯 wrapper COM 拦截
> （`Hooks/DX12HookRegistry.h:29-32`，`vtableHooksInstalled` 恒为 0）。
> 链是 `CreateDXGIFactory*` 导出钩子（`Presentation/DXGIHooks.cpp:74-79`）→ 包装 factory
> （`Presentation/DX12FactoryWrappers.h:245-320`）→ `SwapChainWrapper`（`:494`，同时实现 `IDXGISwapChain4`）
> → `Present`（`:673`）/ `Present1`（`:772`）→ `DX12WrapperRuntimeUtils::PresentDirect`（`:496`）/ `Present1Direct`（`:628`）。
> `ResizeBuffers` 在 `:770-787`（`:781` 调 `DX12NativeOverlayInvalidate`），`ResizeBuffers1` 在
> `DX12FactoryWrappers.h:956-982`（`:969` 调同一个）。
> **要点**：wrapper 保存的是**原生** swapchain / 原生 device / 原生 queue，所以 overlay 的提交
> 直接落到原生 `ExecuteCommandLists`，不会重入 wrapper 的队列钩子（`DX12CommandWrappers.h:112-126`
> 传的是 `mQueue` 原生接口）。

### 2.3 DX11 参照实现（要对齐的行为）

| 事实 | 位置 |
|---|---|
| `Overlay::DrawOverlay()` 在 DXGI Present wrapper 里**无条件**调用，只被 `G->suppress_overlay` 挡 | `DirectX11/HackerDXGI.cpp:269` |
| 早退条件：`!hunting && !has_notice && Profiling::mode == NONE` | `DirectX11/Overlay.cpp:792` |
| `DrawProfiling`：`Profiling::update_txt()` → `MeasureString` → 黑底 0.75 半透明矩形 → `DrawString`（Goldenrod） | `DirectX11/Overlay.cpp:767-777` |
| 整个 `Profiling::text` **一次** `DrawString`（内部处理 `\n`） | 同上 |
| `Profiling::overlay_overhead` 用 QPC 包住**整个** `DrawOverlay`（含 `SaveState`/`RestoreState`/`flush_d3d11on12`） | `Overlay.cpp:789` 与 `:845` |
| 字体：`SpriteFont`，blob 从系统 `d3d11.dll` 的资源里取，无外部字体文件 | `Overlay.cpp:119-157` |
| 模式：`NONE / SUMMARY / TOP_COMMAND_LISTS / TOP_COMMANDS / CTO_WARNING` | `DirectX11/profiling.h:7` |
| 热键：`[Hunting] monitor_performance` 循环模式；`freeze_performance_monitor` 冻结 | `DirectX11/Hunting.cpp:1983-1984` |
| 间隔：`[Hunting] monitor_performance_interval`，默认 **1.0 秒** | `Hunting.cpp:1985` |
| 数据模型：`Profiling::Overhead`（QPC）全局变量，各处 `if (mode==SUMMARY)` 手工累加 | `DirectX11/profiling.cpp` 全文 |
| 更新时机：`update_txt()` 每帧被调，内部按 `interval` 提前返回；`freeze` 直接 return | `profiling.cpp:400-412` |
| **DX11 的 profiling 在 Release 下也可用**，注释明说用途是"让用户截图发给我们" | `Hunting.cpp:1980-1982` |

### 2.4 差距总结

| 维度 | DX11 | DX12 现状 | DX12 目标 |
|---|---|---|---|
| 采样 | QPC 手工包点 + 每处 `if(mode==SUMMARY)` 分支 | 统一 bucket，无条件记录 | **不变** |
| 聚合 | 渲染线程（每秒一次） | 写线程 ✅ | 不变 |
| 格式化 | 渲染线程，几百次 `_snwprintf_s` | 写线程 → 日志 ✅ | 写线程 → **内存快照** |
| 展示 | 屏幕 SpriteBatch | 日志文本 | **屏幕** |
| 渲染线程每帧成本 | 20–50 µs | 0（不画） | **3–8 µs** |

---

## 3. 总体架构

四段流水线，每段落在正确的线程上：

```
[渲染线程] 采样        DX12_PROF_ADD_US / SCOPE            ← 完全不变，零新增成本
      │  （per-thread bucket，无锁 plain RMW）
      ▼
[写线程]   聚合 + 格式化  DrainThreadTables → 报告文本 → Publish()
      │  （SRW 保护的快照 + release-store 版本号；不占渲染线程）
      ▼
[渲染线程] 版本检查      1 次 atomic load + 比较            ← 未变化：1 ns，直接复用
      │  （仅版本变化时）
      ▼
[渲染线程] 布局          ASCII → 字形四边形（像素空间）      ← 仅版本变化时
      │  （仅版本变化时 memcpy 进 upload ring slot）
      ▼
[GPU]      光栅          与 Hunting 文字合并 → 1 次 DrawIndexedInstanced
```

**核心思想：让"变化频率"决定成本。** Profiling 数据每 interval（默认 500 ms）才变一次，所以渲染线程 99% 的帧只需要一次原子比较 + 复用上一帧的顶点缓冲。

---

## 4. 详细设计

### 4.1 快照与发布：`Runtime/DX12ProfilingHud.h`（新增）

整体 `#if DX12_TIMING_BUILD` 门控，Release 下 `IsEnabled()` inline 返回 `false`，零成本——与 `DX12Diag.h` 的既有红线一致（`DX12Diag.h:14-18` 设计规则 1）。

```cpp
// 写线程产出、渲染线程消费的一次性文本快照。
// 发布频率 ≤ 1/interval(≥250ms)，渲染线程持锁时间 ~1.5us，
// 因此用 SRW 锁而不是 seqlock：最坏情况有界，不需要渲染线程重试。
class DX12ProfilingHud
{
public:
	enum class Mode : LONG { Off = 0, Compact, Summary, Top, Sections, Counters, ModeCount };

	static constexpr size_t kTextBytes = 16 * 1024;

	// --- 渲染线程：0 成本使能检查 ---
	static bool IsEnabled() { return sMode.load(std::memory_order_relaxed) != 0; }
	static Mode GetMode();
	static bool IsFrozen();
	static void SetMode(Mode mode);
	static void CycleMode();     // F11
	static void ToggleFreeze();  // F12

	// --- 写线程：发布一帧报告 ---
	static void Publish(const char *text, size_t length);

	// --- 渲染线程：取最新报告 ---
	// 版本号未变 → 1 次 acquire load 后立即返回 false（约 1ns，不取锁）。
	// 版本号变了 → 取共享锁 memcpy 一次，~1.5us，每 interval 一次。
	static bool AcquireLatest(UINT64 *version, char *out, size_t outCapacity, size_t *outLength);

private:
	inline static SRWLOCK sLock = SRWLOCK_INIT;
	inline static char sText[kTextBytes] = {};
	inline static size_t sLength = 0;
	inline static volatile LONG64 sVersion = 0;
	inline static std::atomic<LONG> sMode{ 0 };
	inline static std::atomic<LONG> sFrozen{ 0 };
};
```

`AcquireLatest` 的双重检查：

```cpp
inline bool DX12ProfilingHud::AcquireLatest(
	UINT64 *version, char *out, size_t outCapacity, size_t *outLength)
{
	const UINT64 published = static_cast<UINT64>(
		InterlockedCompareExchange64(&sVersion, 0, 0));
	if (!published || published == *version)
		return false;                       // 稳态：1 次原子读，无锁

	AcquireSRWLockShared(&sLock);
	// 取锁期间写线程可能又发了一版：重读并再判一次，避免拷到一半被覆盖。
	const UINT64 current = static_cast<UINT64>(
		InterlockedCompareExchange64(&sVersion, 0, 0));
	if (current == *version) {
		ReleaseSRWLockShared(&sLock);
		return false;
	}
	const size_t copy = sLength < outCapacity ? sLength : outCapacity;
	memcpy(out, sText, copy);
	*outLength = copy;
	*version = current;
	ReleaseSRWLockShared(&sLock);
	return true;
}
```

`Publish` 侧：`AcquireSRWLockExclusive` → `memcpy` 进 `sText` → `sLength = length` → `InterlockedIncrement64(&sVersion)` → 释放。注意**版本号必须在拷贝之后自增**，否则渲染线程可能读到旧内容配新版本号。

### 4.2 写线程报告生成：改造 `Core/Diag/DX12Diag.h`

把 `EmitSummary()`（`:1647`）从"只输出到日志"拆成"取一次数据 → 两个 sink"：

```cpp
inline void DX12DiagProf::EmitSummary()
{
	Bucket merged[kMergedBuckets] = {};
	UINT mergedCount = 0;
	DrainThreadTables(merged, &mergedCount);

	// 窗口统计只算一次：两个 sink 共用，且 TakeFrameDraws/Dispatches 带
	// exchange(0) 重置副作用，只能执行一次。
	const WindowReport report = BuildWindowReport(merged, mergedCount);

	if (DX12ProfilingHud::WantsLog())
		EmitLogSections(report, merged, mergedCount);   // 今天的 8 个 Emit*Section 原样搬进来

	if (DX12ProfilingHud::WantsOverlay()) {
		char text[DX12ProfilingHud::kTextBytes];
		const size_t length = FormatHudReport(text, sizeof(text), report, merged, mergedCount);
		DX12ProfilingHud::Publish(text, length);
	}

	ResetWindow();
}
```

> ⚠️ **必须做的正确性修正**：现在 `EmitFrameTimeline`（`:1416-1417`）内部调用
> `DX12RuntimeState::TakeFrameDraws()` / `TakeFrameDispatches()`，而这两个函数是
> `exchange(0)`——**有重置副作用**。拆 sink 时若不把它们上提到只执行一次的
> `BuildWindowReport`，`overlay` 单一 sink 模式下 draw/dispatch 计数会无限累积。
> 这是本次改造最容易踩的坑。

`FormatHudReport` 产出**纯 ASCII**（不是 wide），因为字形图集只有 32..126，渲染线程因此不需要任何编码转换。

#### 4.2.1 先修三个既有缺陷，否则 HUD 显示的数据本身就是错的

调研中发现三处**必须先修**的问题——它们今天只影响日志的可读性，但一旦 HUD 把它们当成主要展示面，
就会直接误导使用者。

**(a) `kMergedBuckets = 512` 已经接近溢出（最严重）**

`DX12Diag.h:190-193` 的注释说 "this build registers 357 distinct bucket names"——**这个数字已经过时**。
实测当前源码里有 **458 个不同的 bucket 名 / 835 个调用点**。而合并表是
`Bucket merged[kMergedBuckets]`（`:1649`，`kMergedBuckets = 512`，栈上 28 KB），
`FindMerged`（`:1132-1151`）与 `FindBucket` 一样是**定容线性探测，满了返回 nullptr**：

```cpp
		for (UINT probe = 0; probe < kMergedBuckets; ++probe) { ... }
		return nullptr;      // 满了 → 调用方 `if (!dst) continue;` 静默丢弃
```

458/512 = **装载因子 0.89**。线性探测在这个负载下平均探测长度已经到 ~5，最坏情况更差；
再新增几十个 bucket 就会开始**静默丢数据**——某个计数器永远不出现，看起来像"这段代码没跑"。

**改法**：`kMergedBuckets` 提到 **2048**（栈上 114 KB，写线程默认 1 MB 栈，可接受），
并在 `FindMerged` 返回 nullptr 时打一条一次性告警（照抄 `FindBucket` 的 `sTableFull` 模式，`:539-542`）。
同时把那段过时的注释改成实测值。

**(b) 两个死字段 + 一组重名 bucket**

- `EmitFrameTimeline` 读取的 `Present.profiling_flush`（`:1376`）与 `Present.per_frame_counters`（`:1378`）
  **在全工程没有任何地方记录过** → 这两列恒为 0。实际记录的是
  `Present.profiling_and_hotpath_flush`（`DX12WrapperRuntimeUtils.h:622` / `:747`）。
- `DX12_LOCK_ACQUIRE_*` 宏拼的是 `"Lock." name`，而调用点传进来的已经是 `"Lock.preSkinExclusive"`
  这类名字 → 真实 bucket 名变成 `Lock.Lock.preSkinExclusive`（`ModRuntime/DX12CommandListExecutor.cpp:163/852/866`）。

HUD 要把这些行显示给用户，就必须先让它们的名字和数值是对的。顺手修掉。

**(c) `DX12DiagLog::Flush()` 每帧一次无条件 `SetEvent`**

`FrameLeave`（`:466`）**无条件**调 `DX12DiagLog::Flush()`；而 `Flush()`
（`:736-740`）里 `PublishTlsBuffer()` 虽然会在 TLS 缓冲为空时提前返回，
`WakeWriter()`（`:742-746`）却**总是**执行 `SetEvent(sEvent)` —— 每次 Present 一次内核态转换。

在 `sink=overlay` 模式下（日志量被大幅削减），这属于纯浪费。改法：让 `Flush()`
根据 `PublishTlsBuffer()` 是否真的发布了内容来决定要不要 `WakeWriter()`（让它返回 bool）。
省 **~0.5–2 µs/帧**，与 HUD 无关。

**(d) 两个小的写线程/热路径低效（可选）**

- `EmitScopeTable` 的排序是**插入排序**（`:1622-1630`，O(n²)）。今天只排 ~100 个无前缀 bucket 还够用；
  HUD 的 Top/Sections 模式要对全部 458 个排序，应改用 `std::partial_sort`。
- `DX12_PROF_ADD_US_ITEMS`（`:355-361`）调 `AddUs()` 之后**又做了一次 `FindBucket`**（重复 hash + 探测）。
  改成一次查找、两次写。调用点集中在 `Tracking/DX12CommandQueueExecution.h`（18 处），是热路径。

**模式与文本内容**（对齐 DX11 的 `Profiling::Mode`）：

| 模式 | 行数 | 内容 |
|---|---|---|
| `Compact`（默认） | 6 | fps / frame ms / present µs / queue µs / HUD 自身 µs / top-3 stage |
| `Summary` | 12 | DX11 "CPU Performance Summary" 的 DX12 等价物：`Present.*`、`Queue.*`、`Stage.*`、`Wait.*` 固定字段 |
| `Top` | `top_rows` | 全部 bucket 按 `totalUs` 降序前 N（默认 24） |
| `Sections` | 分组 | `Present.` / `Queue.` / `Stage.` / `Api.` / `Create.` / `Lock.` 分组，每组前 N |
| `Counters` | 不定 | 只有 `items` 的 bucket（calls/items）+ draw/dispatch 计数 |

`Compact` 模式的目标外观：

```
DX12 Profiling (view 3/5, frozen: no)   58.9fps  frame 16.97ms
present  412us  |  queue 318us  |  hud   6us
------------------------------------------------------------
Stage.Present              120us   Queue.total        310us
Present.original_present   180us   Stage.SwapChain     44us
Present.mod_present_overlay  9us   Wait.event          12us
```

### 4.3 渲染侧改造：`ShaderTools/DX12NativeOverlay.cpp`

#### 优化 1 —— 顶点改为像素空间，NDC 变换交给 root constants

现状 `:842-845` 在 CPU 上算 NDC，顶点**依赖 backbuffer 尺寸**，尺寸一变缓存就失效。改法：

```hlsl
cbuffer HudParams : register(b0)
{
	float2 invHalfSize;   // (2/backW, 2/backH)
};

PSIn VSMain(VSIn input)
{
	PSIn output;
	output.pos = float4(input.pos.x * invHalfSize.x - 1.0f,
	                    1.0f - input.pos.y * invHalfSize.y, 0.0f, 1.0f);
	output.uv  = input.uv;
	output.col = input.col;
	return output;
}
```

root signature 从 1 个参数加到 2 个：table(0) 不变，新增
`D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`（2 个 float，`ShaderVisibility = VERTEX`，`ShaderRegister = 0`），
每帧一次 `SetGraphicsRoot32BitConstants(1, 2, &params, 0)`。

**收益**：顶点数据与分辨率解耦 → 缓存可跨 `ResizeBuffers` 复用。这是第 2 条优化能成立的前提。

#### 优化 2 —— 内容代际 + 顶点槽复用（本方案性能的核心）

`DX12InGameTextOverlay` 新增状态：

```cpp
	// 本帧所有贡献者内容的指纹：HUD 快照版本 ^ hunt 文本哈希 ^ notice 哈希 ^ backW/H
	UINT64 mFrameContentKey = 0;
	UINT64 mRenderedContentKey = 0;   // 已写进 GPU 的那一份
	UINT   mRenderedSlot = 0;
	UINT   mRenderedIndexCount = 0;
```

- `mFrameContentKey != mRenderedContentKey` → 重新 emit + `memcpy` 到新 slot，更新三者。
- 相同 → **直接复用 `mRenderedSlot`**，只重新录制 draw（VBV/IBV 指向同一块内存）。
- 因为内容不变时**从不改写**该 slot 的内存，GPU 正在读它也完全安全 → **不需要 fence 等待**，`AcquireVertexSlot` 的 `WaitForFenceValue` 在稳态根本不会被调用。

对照 DX11：DX11 每帧 `MeasureString` + `DrawString` 重建整个顶点缓冲。DX12 改后 59/60 帧连 memcpy 都没有。

#### 优化 3 —— 与 Hunting 文字合并成一次 draw / 一次提交

现状 `DX12NativeOverlayRender` 自己 `BeginRecord`/`Submit`，`DX12ModSubmitPresentOverlay` 也自己一份 → 每帧 **2 次** `ExecuteCommandLists`。

新设计：`DX12InGameTextOverlay::RenderFrame(...)` 把**所有** overlay 顶点发射进同一个 `mVertices`，写进同一个 slot，**一次** `IASetVertexBuffers` + **一次** `DrawIndexedInstanced`。发射顺序保持现状语义：

```
1) mod [Present] overlay           （不在本类内，见下方"可选"）
2) Hunting 文本（居中）
3) notice（瞬态提示）
4) Profiling HUD（左上）           ← 新增
```

**HUD 的边际成本 = 0 次 draw call、0 次状态设置、0 次提交**，只是 `indexCount` 变大。

> **可选（阶段 3）**：让 `DX12ModSubmitPresentOverlay` 也录进同一个 command list，把每帧 2 次提交降为 1 次。这是**净收益为正**的改动（即使 HUD 关闭也更快），但要动 `DX12ModSubmitPresentOverlay`（`ModRuntime/DX12CommandListExecutor.cpp:519`）的录制/关闭时序，风险独立，建议单独验证。

#### 黑色半透明底衬（复用现有管线，零新状态）

DX11 有 `DrawRectangle(0, y, w, h, 0,0,0,0.75)`。DX12 可以不加任何新 PSO：

图集是 16 列 × `ceil(95/16) = 6` 行 = **96 格**，95 个字形只占 0..94，**恰好空出最后一格 (col 15, row 5)**。

1. `BuildFontAtlas` 里把这一格的像素**填成 255（纯白）**（现有代码 `:625-626` 只拷了字形区，加一段 `memset` 即可）。
2. 底衬画成一个四边形，四个角的 UV 都取该格**中心**
   （`u = 15.5*cellW/atlasW`, `v = 5.5*cellH/atlasH`）——四个采样点都是纯白，线性过滤也不会从邻格渗色。
3. `EmitChar` 的越界保护是 `ch >= kFirstGlyph + kGlyphCount → '?'`，索引 95 永不被它使用，所以这格是专属的。

这样底衬完全复用现有 PSO / 根签名 / 采样器 / 顶点格式，**不需要第二个 UI pipeline**。
底衬顶点先发射、文字后发射，`SrcBlend=SRC_ALPHA` 的 alpha 混合天然让文字压在上面。

#### 优化 4 —— 顺手修掉一个既有的每帧 COM 开销（净收益，即使 HUD 关闭也变快）

调研中发现：`DX12CustomShaderSurfaceCache::EnsureBackBufferView`（`ModRuntime/DX12CustomShaderDevice.h:834-899`）
虽然开了一个 **8 槽的 RTV 堆**（`kRtvSlots = 8`，`:902`），并且写描述符时已经按
`bufferIndex % kRtvSlots` 分槽（`:882-885`），但**有效性判断只用了单个 `(mChain, mBufferIndex)` 键**：

```cpp
	if (mChain != swapChain || mBufferIndex != bufferIndex) {   // :858
		... swapChain->GetBuffer(bufferIndex, ...)  ×2（一次 GetDesc、一次建视图）
		mDevice->CreateRenderTargetView(viewTarget, &rtvDesc, slot);
		mChain = swapChain; mBufferIndex = bufferIndex;
	}
```

翻转模型交换链**每帧都换 back buffer index**，所以这个分支实际上**每帧都会被走到** ——
每帧 2 次 `GetBuffer` + 1 次 `CreateRenderTargetView` + 1 次 `QueryInterface(IDXGISwapChain3)`。

雪上加霜：`DX12NativeOverlay.cpp:292-304 CurrentBackBuffer()` 为了拿 barrier 目标，
又做了一次 `QueryInterface(IDXGISwapChain3)` + `GetBuffer`。

**改法**：
- `SurfaceCache` 增加 `UINT64 mValidMask`（按 `bufferIndex % kRtvSlots` 置位）与缓存好的
  `ID3D12Resource* mBackBuffer[kRtvSlots]`；命中已建好的槽时只更新 `mBufferIndex`，跳过所有 COM 调用。
- 缓存 `IDXGISwapChain3*`（`SwapChainWrapper` 本来就已经缓存了原生 `mSwapChain1`，见
  `Presentation/DX12FactoryWrappers.h:501-503` 的同款注释），彻底去掉每帧 QI。
- `EnsureBackBufferView` 顺便把 back buffer 资源指针带出来，让 overlay 不必再 `GetBuffer`。

**收益**：每帧省掉 2 次 QI + 3 次 `GetBuffer` + 1 次 `CreateRenderTargetView`，约 **2–6 µs/帧**，
**与 HUD 是否开启无关**。这条属于"顺手修"，但它让 5.5 的性能红线更容易达成。
风险：`ResizeBuffers` 后必须把 `mValidMask` 清零（`InvalidateChain`，`:822-830`，已有钩子）。

#### 容量调整

```cpp
	static constexpr size_t kMaxChars = 3072;   // 1024 → 3072（HUD 约 40 行 × 60 列）
	static constexpr UINT kVertexSlotCount = 6; // 保持不变
```

- 上传堆：3072 × 4 verts × 32 B × 6 slot = **2.3 MB**（仅 TIMING 构建分配，可忽略）。
- 索引缓冲：3072 × 6 × 2 B = 36 KB。
- ⚠️ **硬约束**：索引是 `uint16_t base = i * 4`，所以 `kMaxChars` 必须 **< 16384**。加 `static_assert(kMaxChars < 16384)` 保护。

### 4.4 屏幕布局与交互

- HUD **左对齐贴左上**；Hunting 文本仍**水平居中**。冲突处理：HUD 先画，把 Hunting 文本的起始 `y` 从 HUD 块底部传进去（`EmitLinesCentered(text, y, ...)` 已经接受 `y` 参数，`:869`），无需任何布局引擎。
- **HUD 块保留固定行数**（不足补空行）→ interval 之间行数变化不会让 Hunting 文字上下抖动。
- 底部两行 Hunting 按键提示（`:107-112`）位置不变，与左上 HUD 无冲突。

**热键**（复用 `Resources/DX12RuntimeControl.h:184` 的 `bindings[]` 静态表，加两条即可）：

| 动作 | DX11 的 ini 键 | DX12 建议 | 说明 |
|---|---|---|---|
| 循环模式 | `[Hunting] monitor_performance` | `VK_F11` | `Off→Compact→Summary→Top→Sections→Counters→Off` |
| 冻结当前快照 | `[Hunting] freeze_performance_monitor` | `VK_F12` | 冻结后 `Publish` 直接 return，画面停住便于阅读 |

`bindings[]` 每项是 `{vk, action, repeatable, requiresDecimal}`，F11/F12 的 `requiresDecimal` 填 `false`。
`DX12UiActionBits` 里对应加 `ProfHudCycle` / `ProfHudFreeze` 两个位，在 `Consume()`（`:233`）里处理——
注意这两个动作**不需要** `DX12HuntIsEnabled()` 前置条件，要放在那个 `if` 外面。

### 4.5 接入 Present：`Presentation/DX12WrapperRuntimeUtils.h`
**正常路径**（`:578-584` 一带）——与现有 overlay 同一个调用点：

```cpp
		DX12ModSubmitPresentOverlay(swapChain);
		DX12_PROF_ADD_US("Present.mod_present_overlay", DX12_DIAG_ELAPSED_US(sliceBegin));
		DX12_DIAG_QPC_RESET(sliceBegin);
		DX12NativeOverlayRender(swapChain);   // 内部统一发射 hunting + notice + HUD
		DX12_PROF_ADD_US("Present.overlay_text", DX12_DIAG_ELAPSED_US(sliceBegin));
```

建议把 bucket 名从 `Present.hunt_overlay_text` 改成 `Present.overlay_text`，并**新增** `Present.prof_hud_layout` /
`Present.prof_hud_upload` 两个子 bucket，让 HUD 自身成本可单独观察（对齐 DX11 把 `Overlay overhead`
单独列出的做法）。

**`skipAll` 快路径**（`:535-564`）——现状直接 `return`，注释（`:536-541`）明确说 overlay 不能在这里跑，
因为**在快路径创建 GPU 对象 + 提交会卡启动**。

HUD 需要在 `skipAll` 下也能显示（否则没加载 mod 时看不到 profiling），但不能破坏这个约束：

```cpp
		if (DX12HotPathState::skipAll) {
			...
			// HUD 只在对象已就绪时从快路径绘制：首次初始化永远发生在正常路径，
			// "快路径里创建 GPU 对象导致启动卡顿"这个根因不会被重新引入。
			DX12_DIAG_QPC_RESET(sliceBegin);
			DX12NativeOverlayRenderIfReady(swapChain);
			DX12_PROF_ADD_US("Present.prof_hud_skip", DX12_DIAG_ELAPSED_US(sliceBegin));
			...
		}
```

`DX12NativeOverlayRenderIfReady` = `DX12NativeOverlayRender` 加一个前置判断：
`DX12InGameTextOverlay::ForDevice(device)` 已存在且 `mRootSignature && mFontTextureReady` 才继续。

#### 4.5.1 帧边界与"一帧延迟"——HUD 必须自知的一件事

`DX12_PROF_FRAME_ENTER` / `FRAME_LEAVE` 的确切位置（`DX12WrapperRuntimeUtils.h:519` / `:623`）：

```
FRAME_ENTER  ← 在 primary-chain / replay / DXGI_PRESENT_TEST 检查与 DrainRetiredCopies 之后，
                在 ConsumeUiActionsForFrame 之前
   ├ ConsumeUiActionsForFrame        (:521)
   ├ skipAll 分支 / 正常分支
   ├ DX12ModSubmitPresentOverlay     (:578)
   ├ DX12NativeOverlayRender         (:583)   ← HUD 画在这里
   ├ swapChain->Present()            (:588)   ← 真正的 Present
   ├ RunPostPresentRuntime           (:609)
   ├ DX12ModBeginFrame               (:613)
   ├ CompleteCaptureOrBeginNext      (:617)
FRAME_LEAVE  ← 帧记录在这里才 PushFrameRecord
```

由此推出两条**必须在实现时明确**的结论：

1. **HUD 只能读到"上一帧之前"发布的快照。** 第 N 帧的 `FrameLeave` 在第 N 帧真实 Present **之后**才执行，
   而 HUD 在第 N 帧 Present **之前**绘制。所以 HUD 看到的最新数据来自第 N-1 帧的 `FrameLeave`
   → 写线程 drain → 发布。稳态下这只是几十毫秒的延迟，无感知，但**读数是自洽的**（窗口完整）。
2. **HUD 自身的成本落在第 N 帧的 bucket 里，要到第 N+1 帧（更准确说是下一个 interval 窗口）才可见。**
   所以 HUD 报告里的 "hud X µs" 那一行是**上一个窗口的平均值**，不是本帧值——文档和 UI 上应说明，
   与 DX11 的 `Overlay overhead` 语义一致。
3. HUD 测量的 `presentWallUs` 覆盖了整个 wrapper 区间（含 HUD 自身），所以**不要把 HUD 成本从
   present 里减掉**去"美化"数字——DX11 也是这么处理的（`profiling.cpp:149-150` 只把 `overlay_overhead`
   从 `present_overhead` 里减去，其余照实报）。本方案沿用同一规则，并在报告里单独列出 HUD 行。

> **关键设计决策：不要为了 HUD 去清 `skipAll`。**
> `DX12HotPathState::SourceState::Capture()`（`Core/HotPath/DX12HotPathState.h:88-111`）里
> `state.huntEnabled` 决定 `skipAll`。DX11 必须开着 hunting 才有 profiling 数据；而 DX12 的 bucket
> 采样是**无条件**的，HUD 纯粹是展示层。因此 HUD 开关**不改变任何热路径行为**，也就不会因为
> "打开 HUD"而改变被测代码路径——这正是 profiling 工具最需要的性质（观察者不动系统）。

### 4.6 配置

**现状**：DX12 目前**没有任何** overlay / profiling 相关的 ini 项。
`DX12Config.h:143/149` 里的 `{L"Hunting",false}` / `{L"Profile",false}` 只是 ini 命名空间解析用的
**段名登记表**（`IniDocumentParser::kMigotoRegularSections`），这两个段下的键**从来没有被读取过**。

DX12 真正的运行时配置集中在 `Config/DX12ConfigLoader.h:441-451 ReadRuntimeConfig()`，
它用**双段名**读取（`[DX12]` 与 `[System]` 互为别名）：

```cpp
	MigotoConfigReader::ReadBool(mIni, L"DX12",    L"proxy_mode",    &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"System",  L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	...
```

新配置照这个模式加：`Config/DX12ConfigLoader.h` 的 `RuntimeConfig` 结构（`:339-346`）加字段，
`ReadRuntimeConfig()` 加两行双段名读取，然后在 `Resources/DX12RuntimeControl.h:75-97` 消费
（照抄 `DX12ModSetSafeMode` 的写法）。

```ini
[System]                       ; [DX12] 段同样被接受（与既有 dx12_* 项一致）
; 0=off 1=compact 2=summary 3=top 4=sections 5=counters
dx12_profiling_hud = 0

; 报告间隔（毫秒），同时决定日志汇总窗口（默认 500）
dx12_profiling_interval_ms = 500

; overlay = 只画屏幕，不再写 profiling 日志块（默认，本次改造的目标）
; log     = 只写日志，等价于改造前行为（回退用）
; both    = 两者都出
; none    = 都不出
dx12_profiling_sink = overlay

; Top/Sections 模式每个分组的行数上限
dx12_profiling_top_rows = 24
```

`kSummaryIntervalFrames`（`DX12Diag.h:195`，现在硬编码 60）改为运行时可配，
由 `dx12_profiling_interval_ms` 换算（按 60 fps 折算，并在帧记录里用真实帧数校准）。

**热键**：用内置绑定表，**不要**用 mod 的 `[Key*]`。原因很明确：
`ConsumeUiActionsForFrame`（`DXGIHookRuntime.h:83-95`）在 `DX12WrapperRuntimeUtils.h:521` 被调用，
比 `DX12NativeOverlayRender`（`:583`）早约 60 行 → **内置热键同帧生效**；
而 mod `[Key*]` 由 `DX12ModBeginFrame`（`:613`）处理，位置在真实 Present **之后** → 慢一帧。

---

## 5. 性能预算

### 5.1 DX11 基准（口径 = `Profiling::overlay_overhead`）

| 项 | 每帧成本 |
|---|---|
| `SaveState()`：~25 个 `Get*`，每个都是一次 COM 调用 | 3–8 µs |
| `MeasureString` + `SpriteBatch::Begin/End` | 2–5 µs |
| `DrawString` 重建 ~2000 字形顶点 + 动态 VB `Map`/memcpy（~64 KB） | 5–15 µs |
| `RestoreState()`：~25 个 `Set*` | 5–15 µs |
| `flush_d3d11on12` | 1–5 µs |
| **小计** | **~20–50 µs/帧** |
| 另：`Profiling::update_txt()` 几百次 `_snwprintf_s`，每秒一次，**在渲染线程** | 100–500 µs/秒 → 摊 **2–8 µs/帧** |
| **合计** | **~22–58 µs/帧** |

### 5.2 DX12 改造后

| 场景 | 每帧成本 | 说明 |
|---|---|---|
| **内容未变**（默认 59/60 帧） | 原子 load + 比较（~1 ns）+ `EnsureBackBufferView`（mutex + 2 原子）+ `BeginRecord`/`Submit` + ~12 次 command list 调用 + `ExecuteCommandLists` + `Signal` ≈ **3–8 µs** | 与现有 Hunting overlay 单次提交同量级 |
| **内容变化**（每 interval 一帧） | 上面 + 快照拷贝 1.5 µs + 布局 2000 字形 ~20 µs + memcpy 64 KB ~5 µs ≈ **+26 µs，每 500 ms 一次** → 摊 **0.05 µs/帧** | |
| 写线程 | `DrainThreadTables` + 格式化，每 interval 一次，**完全不占渲染线程** | 与现状相同 |

**结论：稳态 3–8 µs/帧，对比 DX11 的 22–58 µs/帧，便宜约 3–7 倍。**

额外净收益：若采用 4.3 的"合并提交"，每帧 `ExecuteCommandLists` 从 2 次降到 1 次——**即使 HUD 关闭也更快**。

### 5.3 为什么能快（原理，不是估算）

1. **零状态保存/恢复**——D3D12 的 overlay 用自己的 PSO + 自己的 command list，只有 backbuffer 的两个 barrier。这是 D3D12 相对 D3D11 的**结构性**优势，不是调优出来的。
2. **零每帧顶点重建**——按内容代际缓存，数据不变就只重复录制 draw。
3. **零渲染线程格式化**——全部在写线程。
4. **零额外 draw call**——HUD 与 Hunting 文字共用一次 `DrawIndexedInstanced`。
5. **零新增采样点**——bucket 采样本来每帧都在跑，开 HUD 不新增任何采样成本。

### 5.4 验证方法（必须实测，不能只信本节的估算）

1. `build_release_timing_x64.ps1` 构建 → 运行游戏 → 按 F11 打开 HUD。
2. 读 `d3d12_log.txt` 的 `func=DX12FrameTimeline` 行，看：
   - `Present.prof_hud_layout` / `Present.prof_hud_upload` / `Present.overlay_text` 的 `totalUs`/`calls` = HUD 每帧成本。
   - `Present.overlay_text` 在 **HUD 开 / 关**两种状态下的差值 = HUD 的**边际**成本（这才是要对比 DX11 的数字）。
3. 对比开/关 HUD 时的 `outsidePresentUs` 与 `frameWallUs`，确认落在噪声范围内。
4. 同机同分辨率下与 DX11 的 `Overlay overhead` 行直接对比。

> 按 `AGENTS.md`：日志可能很大，只读尾部或采样，不要整文件读。

### 5.5 性能红线（回归判据）

- `Present.overlay_text` 稳态 must **< 10 µs/帧**。
- HUD 开 / 关的 `outsidePresentUs` 差值 must **< 0.3 ms**（低于测量噪声）。
- 打开 HUD 前后，`Present.*` 与 `Queue.*` 各 bucket 的数值 must **无系统性变化**（观察者不动系统）。

---

## 6. 正确性与边界

| 场景 | 处理 |
|---|---|
| `ResizeBuffers` | 已有 `DX12NativeOverlayInvalidate` → `SurfaceCache::InvalidateChain`（`:977-983`）。另外内容代际里**必须**包含 `backW/backH`（居中文本的布局依赖宽度），尺寸变化时强制重新 emit |
| 设备移除 | 走现有 `FailInit` 路径；HUD 不新增失败面 |
| 多 swapchain / `Present1` | 只画主 swapchain（`IsPrimarySwapChain`），与现状一致；`Present1Direct` 同步改 |
| TYPELESS / `R10G10B10A2` backbuffer | 复用 `MakeTypedRtvFormat`（`DX12CustomShaderDevice.h:784`）；HDR 下颜色偏移可接受 |
| 多线程 Present | 复用现有 `mRenderLock` |
| 字形图集上传未完成 | 复用 `EnsureFontTexture` 的一次性 fence 等待（`:743`，非热路径） |
| 报告文本超长 | `Publish` 端截断到 `kTextBytes`；渲染端受 `kMaxChars` 限制；`EmitChar` 已有容量检查（`:829`）。两层都不会越界 |
| **`TakeFrameDraws` 重置副作用** | 必须上提到只执行一次的 `BuildWindowReport`（见 4.2） |
| HUD 自身污染被测数据 | HUD 成本单独进 `Present.prof_hud*`，报告里单独一行显示，用户可自行扣除（对齐 DX11 的 `Overlay overhead`） |
| **非 TIMING 构建** | `DX12ProfilingHud.h` 整体 `#if DX12_TIMING_BUILD`；Release 下 `IsEnabled()` 返回 `false`，overlay 的早退条件保持"2 次原子读"，零成本 |
| 图集空格子耦合出错 | 加 `static_assert(kGlyphCount < kAtlasCols * kAtlasRows)` 保护 |

---

## 7. 需要你拍板的决策点（已拍板，见第 0 节）

### D1. HUD 是否只在 TIMING 构建可用？ → **两者都要（已实施）**

- **方案 A**：只 TIMING。数据源 `DX12DiagProf` 本身就是 TIMING-only，且 `DX12Diag.h:14-18` 的
  设计红线是"Release 二进制零诊断开销"。改造范围最小，不碰红线。
- **方案 B（采用）**：profiler 编译进所有构建，采样由**运行期**门控。这能对齐 DX11 的原始用途
  （`Hunting.cpp:1980-1982`：*"我们对 performance 测量在 release 模式也感兴趣，
  特别是想让用户把 profiling 信息截图发给我们"*）。
  代价：Release 二进制携带 profiler 代码与 bucket 表；HUD 关闭时每个埋点付一次
  "预测不跳转"的分支（约 1 次 L1 常驻 relaxed load）。
  **这是对 `DX12Diag.h` 原"Release 零诊断开销"红线的有意放宽**，换来的是同一个代码路径在
  两种构建里都被执行——预处理器开关会让"关闭时的路径"永远不被测试。

### D2. 是否把 profiler 日志块停掉？ → **只停 Profiling，排障不停（已实施）**

- `dx12_profiling_log_buckets`（默认 0）只关掉 `EmitScopeTable`，即
  `func=DX12Profiling` 的 `name,calls,totalUs,avgUs,maxUs,items` 大表——它正是 HUD 现在展示的内容。
- `func=DX12FrameTimeline`、`DX12FramePulse`、`DX12ApiWallTiming`、`DX12HotPathTiming`、
  `DX12CreateTiming`、`DX12CpuStageTiming`、`DX12ModLockTiming`、`DX12DescriptorShadowTiming`
  **全部照旧写入**，排障入口一个没少。
- 设 `dx12_profiling_log_buckets = 1` 即可完整回到改造前的日志形态（纯配置回退）。

### D3. 是否做跨模块"合并提交"？ → **做（已实施）**

- 实施为 `DX12PresentOverlayCompose`（`ModRuntime/DX12CommandListExecutor.cpp`），
  替换了原来的 `DX12ModSubmitPresentOverlay`。
- 收益：每帧 `ExecuteCommandLists` 从 2 次降到 1 次，back buffer 的 `PRESENT↔RENDER_TARGET`
  barrier 从 2 对降到 1 对。**HUD 关闭时也更快**。
- HUD 的边际成本因此变成"同一次 draw 里多几个顶点"。

---

## 8. 实施步骤

### 阶段 0 — 先修既有缺陷（否则 HUD 展示的数据本身就是错的）

<span></span>

0.1 **改** `src/DirectX12/Core/Diag/DX12Diag.h`：
   - `kMergedBuckets` 512 → **2048**（见 4.2.1(a)，当前装载因子 0.89 已在静默丢数据的边缘）。
   - `FindMerged` 返回 `nullptr` 时打一次性告警（照抄 `sTableFull` 模式，`:539-542`）。
   - 修正 `Present.profiling_flush` / `Present.per_frame_counters` 两个死字段（`:1376/:1378`）的
     查找名 → 改用实际记录的 `Present.profiling_and_hotpath_flush`（`:622/:747`）。
   - `DX12_PROF_ADD_US_ITEMS`（`:355-361`）去掉第二次 `FindBucket`。
   - `EmitScopeTable` 的插入排序 → `std::partial_sort`。
0.2 **改** `src/DirectX12/Core/Diag/DX12Diag.h`：`DX12DiagLog::Flush()`（`:736-740`）只在
   `PublishTlsBuffer()` 真的发布了内容时才 `WakeWriter()`（去掉每帧一次无条件 `SetEvent`）。
0.3 **改** `src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h` 等三处调用点：修正
   `DX12_LOCK_ACQUIRE_*` 的重复 `Lock.` 前缀命名（见 4.2.1(b)）。

> **验收**：`func=DX12Profiling` 表的行数明显增加（不再丢 bucket）；
> `presentProfilingFlushUs` 不再是恒 0；`Lock.*` 名字恢复正常。

### 阶段 1 — 数据旁路（不改渲染，可独立验证）

1. **新增** `src/DirectX12/Runtime/DX12ProfilingHud.h`：`Mode` 枚举 + SRW 保护的文本快照 + 版本号 +
   `Publish` / `AcquireLatest` / `IsEnabled` / `IsFrozen` / `CycleMode` / `ToggleFreeze`。整体 TIMING 门控。
2. **改** `src/DirectX12/Core/Diag/DX12Diag.h`：
   - 抽出 `WindowReport` 结构 + `BuildWindowReport()`（含 `TakeFrameDraws` / `TakeFrameDispatches`，只调一次）。
   - `EmitSummary()` 按 sink 分支；`Emit*Section` 原样收进 `EmitLogSections()`。
   - `kSummaryIntervalFrames` 改为运行时可配。
   - 新增 `Present.prof_hud_publish` bucket 量写线程的格式化成本。
3. **新增** `src/DirectX12/Core/Diag/DX12ProfilingText.h`：`FormatHudReport()`，把 `Bucket*` + `WindowReport`
   渲染成 5 种模式的 ASCII 文本。
4. **改** `src/DirectX12/Config/DX12ConfigLoader.h`（`RuntimeConfig` 结构 `:339-346` +
   `ReadRuntimeConfig()` `:441-451`，`[DX12]`/`[System]` 双段名）与
   `src/DirectX12/Resources/DX12RuntimeControl.h:75-97`（消费，照抄 `DX12ModSetSafeMode`）：4 个配置项。
5. **改** `src/DirectX12/CMakeLists.txt`：把新头文件加进源列表（与现有 header-only 模块一致，
   注意头文件是**显式列出的**，例如 `Runtime/DX12Overlay.h` 在 `:34`）。

> **验收**：INI 里打开 HUD 后，`d3d12_log.txt` 里出现 HUD 文本内容（先打到日志验证数据正确性），屏幕尚未绘制。
> 这一步不碰任何渲染代码，风险为零。

### 阶段 2 — 屏幕绘制

6. **改** `src/DirectX12/ShaderTools/DX12NativeOverlay.cpp`（该文件**没有头文件**，
   公开接口声明在 `src/DirectX12/DX12Resources.h:14-16`）：
   - 顶点改像素空间；root signature 加 32-bit constants；shader 用 `invHalfSize` 变换。
   - `BuildFontAtlas`（`:564-636`）把图集最后一格填白；新增纯色四边形发射。
   - `kMaxChars` 1024 → 3072（`:224`），加 `static_assert(kMaxChars < 16384)`。
   - 早退条件（`:947-948`）加第三个分支：`|| DX12ProfilingHud::IsEnabled()`。
   - 内容代际 `mFrameContentKey` + 槽复用（跳过快照没变时的 emit、memcpy 与 `AcquireVertexSlot` 的 fence 检查）。
   - `RenderFrame(...)` 统一发射 hunting + notice + HUD → 一次 draw；HUD 左对齐 + 固定行数 + 半透明底衬。
7. **改** `src/DirectX12/DX12Resources.h`：新增 `DX12NativeOverlayRenderIfReady(IDXGISwapChain*)` 声明
   （与既有三条并列，`:14-16`）。
8. **改** `src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h`：`PresentDirect`（`:583`）与
   `Present1Direct`（`:710`）两处接入 + bucket；`skipAll` 路径（`:542-546` / `:670-676`）加
   `DX12NativeOverlayRenderIfReady` 调用。
9. **改** `src/DirectX12/Resources/DX12RuntimeControl.h`：F11 / F12 加入 `bindings[]`（`:184-205`），
   `DX12UiActionBits`（`:129-151`）加两位，在 `Consume()`（`:233-290`）里处理
   ——注意放在 `DX12HuntIsEnabled()` 那个 `if`（`:249`）**之外**。
10. **改** `src/DirectX12/ModRuntime/DX12CustomShaderDevice.h`：优化 4（RTV / back buffer / swapchain 缓存）。

> **验收**：屏幕左上出现 HUD；与 Hunting 文字不重叠；`ResizeBuffers` 后正常；
> **HUD 关闭时画面与改造前逐像素一致**（这是回归验证的关键一步，因为改动动了在用的 Hunting overlay 绘制代码）。

### 阶段 3 — 性能实测与收尾

11. 按 5.4 实测，填入 `Present.prof_hud*` 真实数据，核对 5.5 的三条红线。
12. （可选，D3）跨模块合并提交。
13. 更新 `config/d3dx.ini` 注释、`README.md`、`docs/`。

---

## 9. 风险与回退

| 风险 | 等级 | 缓解 |
|---|---|---|
| 顶点改像素空间动到了**在用的** Hunting overlay 绘制代码 | 中 | HUD 关闭时必须与改造前逐像素一致；先单独验证像素空间变换（root constants 数值写错会让文字整体偏移/翻转） |
| 图集"最后一格填白"与 `kGlyphCount`/`kAtlasCols` 耦合，算错会画出黑块 | 低 | `static_assert` 保护 + 该格 UV 取格中心（不受线性过滤影响） |
| 拆 sink 时漏掉 `TakeFrameDraws` 的重置副作用 | 中 | 已在 4.2 显式标注，作为 code review 检查项 |
| 快照版本号与内容不同步（先发版本后拷内容） | 低 | `Publish` 里版本号自增必须在 `memcpy` 之后；`AcquireLatest` 双重检查 |
| 大 log 场景下 HUD 文本过长被截断 | 低 | `kTextBytes` 与 `kMaxChars` 双层截断；`EmitChar` 已有容量检查 |
| `kMergedBuckets` 溢出导致 HUD 静默少行 | **高**（已存在，非本方案引入） | 阶段 0.1 提到 2048 + 溢出告警；这是**动手前必须做**的一步 |
| 阶段 0 的缺陷修复影响了既有日志格式 | 低 | 阶段 0 独立提交、独立验证；修复只改名字/容量，不改字段语义 |

**回退开关**：`dx12_profiling_hud = 0` + `dx12_profiling_log_buckets = 1`
即完全回到改造前的日志行为。日志侧代码路径**保留不删**（`EmitLogSections` 就是原来的 `EmitSummary` 主体），
所以回退是配置级的，不需要改代码。

---

## 10. 涉及文件清单

**新增**

- `src/DirectX12/Core/Diag/DX12ProfilingHud.h` — 快照发布/消费、模式/冻结/间隔状态
- `src/DirectX12/Core/Diag/DX12ProfilingText.h` — 窗口数据模型 + 报告文本生成（写线程）
- `dev_tests/profiling_hud_test.cpp` — 离线回归测试（报告格式化 + 快照传递）

**修改**

- `src/DirectX12/Core/Diag/DX12Diag.h` — 缺陷修复、采样门控、sink 拆分、`BuildWindowReport`、可配间隔
- `src/DirectX12/ShaderTools/DX12NativeOverlay.cpp` — 像素空间顶点、root constants、图集空格子、
  内容代际、统一发射、HUD 面板、`Prepare`/`Record` 拆分
- `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp` — `DX12PresentOverlayCompose` 合并提交
  （替换 `DX12ModSubmitPresentOverlay`）
- `src/DirectX12/ModRuntime/DX12CustomShaderDevice.h` — 优化 4（RTV / back buffer / swapchain 缓存）
- `src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h` — 两处 Present 接入 + `skipAll` 路径
- `src/DirectX12/Resources/DX12RuntimeControl.h` — F11 / F12 热键 + 配置消费 + Lock 命名修复
- `src/DirectX12/Config/DX12ConfigLoader.h` — 4 个配置项（`RuntimeConfig` + `ReadRuntimeConfig`）
- `src/DirectX12/DX12Resources.h`、`Core/DX12ModState.h` — 新的 overlay 接口声明
- `src/DirectX12/CMakeLists.txt` — 新头文件入列
- `config/d3dx.ini` — 新配置项说明
- `dev_tests/build_all.bat` — 单个测试失败不再中断其余测试

**不改**

- 采样侧（所有 `DX12_PROF_*` 调用点）、`DX12HotPathState`、`DX12PresentOverlayChannel`
  的既有接口 —— 这是本方案能保持低风险的原因。

---

## 11. 实施记录（与方案的偏差 + 验证）

### 11.1 与方案不同的地方（都是实施中发现的必要调整）

| 方案原定 | 实际做法 | 原因 |
|---|---|---|
| `Runtime/DX12ProfilingHud.h` | **`Core/Diag/DX12ProfilingHud.h`** | 分层：`Runtime/` 依赖 `Core/Diag/`（`DX12VmFrameVariablesUtils.h` 就是这么 include 的），反过来会成环。HUD 状态本身也是诊断设施 |
| `#if DX12_PROF_BUILD` 编译期开关 | **删除该开关**，改用运行期门控 | 用户要求两种构建都有 profiling，编译期开关会让"HUD 关闭"的路径永远不被本构建测试。留一个 `DX12_NO_PROFILING` 逃生口等于留一条没人跑的代码路径 |
| HUD 与 Hunting 文字合并（阶段 2） | **一次性做到跨模块合并**（D3） | 用户要求 D3 一起做 |
| 方案未预见 | **`Prepare()` / `Record()` 拆分** | 字体图集上传需要自己的 channel slot；composer 已持有 channel 的 `mRecordLock`（非递归）→ 直接在 `Record` 里上传会**死锁**。改为 composer 在取锁**之前**调 `DX12NativeOverlayPrepare` |
| 方案未预见 | **`Present.overlay_text_record` bucket** | 合并提交后 mod overlay 与 HUD 共用一个 bucket，HUD 自身成本无法单独看。拆出 text record 的独立计时，红线才可核对 |
| 方案未预见 | **`DX12NativeOverlayReady` 用 `FindDevice` 而非 `ForDevice`** | skipAll 快路径不得分配；`ForDevice` 会 `new` 实例 |
| 方案原定 `kMergedBuckets` 512→2048 | 一致 | 实测 458 个 bucket，装载因子 0.89 |
| 方案原定 `partial_sort` | 用 `std::sort` | `EmitScopeTable` 要排全部行，不是 top-N，`sort` 才是对的 |

### 11.2 验证状态

| 项 | 结果 |
|---|---|
| `build_release_x64.ps1`（Release，无 TIMING） | 通过 |
| `build_release_timing_x64.ps1`（TIMING） | 通过 |
| `dev_tests/profiling_hud_test.exe` | **72 checks, 0 failures, ALL PASS**；约 18 万次并发读写**零撕裂快照** |
| 其它 dev_tests | `null_test` / `panel_parity_test` / `profiling_hud_test` 编译并运行通过；`alias_test` 与 `ini_probe` **在本次改造之前就已无法编译**（`ResolveChain` / `hasRenderTargetAssign` / `ParseRegisterName` 等 API 在其所测头文件中已不存在），与本次改动无关 |
| 屏幕实测（游戏中开 HUD、量 `Present.overlay_text_record`） | **未做** —— 需要在带游戏的环境里跑。本文 5.1/5.2 的性能数字仍是估算，请按 5.4 实测后回填 |

> **D2 语义的复核（自查中发现并修掉的一个实现缺陷）**：sink 拆分的第一版把
> `EmitSummary` 的**整个日志块**都放在 `dx12_profiling_log_buckets` 条件里，
> 于是默认配置（该值为 0）会连 `DX12FrameTimeline`、`DX12ApiWallTiming`、`DX12HotPathTiming`
> 等排障段一起丢掉——正好违反 D2"只停 Profiling，排障不停"。
> 现已改为：排障段**无条件写**，只有 `EmitScopeTable`（即 `func=DX12Profiling` 桶表）受该开关控制。
> 这一条提醒：**"停掉什么"必须精确到函数，不能停在调用块上**。

### 11.3 本次改造顺带修掉的既有缺陷
1. `kMergedBuckets` 512 → 2048，并在 `FindMerged` 溢出时打一次性告警（此前会**静默丢行**）。
2. `Present.profiling_flush` / `Present.per_frame_counters` 两个恒为 0 的死字段：前者改指真实记录的
   `Present.profiling_and_hotpath_flush`，后者新增真实计时 `Present.frame_counters`。
3. `DX12_LOCK_ACQUIRE_*` 宏重复拼前缀，把 `Lock.modShared` 变成 `Lock.Lock.modShared`；
   同时 9 处 `Desc.*` 调用点补上 `Lock.` 前缀以保持归组不变。
4. `DX12DiagLog::Flush()` 每帧无条件 `SetEvent`（一次内核态转换）→ 改为仅在真的发布了内容时唤醒。
5. `DX12_PROF_ADD_US_ITEMS` 做两次 bucket 查找 → 一次。
6. `EmitScopeTable` 的插入排序（O(n²)）→ `std::sort`。
7. **优化 4**：`EnsureBackBufferView` 原先在翻转链每帧换 index 时重建 RTV
   （每帧 2×`GetBuffer` + 1×`CreateRenderTargetView` + 1×`QueryInterface`），
   现在按 index 缓存视图与 back buffer 资源、并缓存 `IDXGISwapChain3`。
   这一条**与 HUD 是否开启无关**，开着 HUD 也更快。

