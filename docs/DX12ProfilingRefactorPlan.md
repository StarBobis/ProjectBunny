# DX12 Profiling 与日志重构计划

## 背景

DX12 部分目前同时存在多套 Profiling/计时体系，互相交织且性能差：

- `Core/HotPath/DX12Profiling.h` + `Core/CommandFlow/DX12ProfilingImpl.h`：计数器 + overlay 文本体系（几十个 `Counter` 成员、F11 切换、`overlayText`）。
- `Hooks/DX12Profiler.h`：`DX12Profiler::Scope`，把上面三套揉在一起的 RAII。
- `Core/ModState/DX12OuterTimingProbe.h`：API wall bucket / present slice / queue slice / 等待堆栈 / 线程时间线（约 585 个调用点）。
- `Hooks/DX12HotPathTiming.h`：hot-path stage 计时。
- `Hooks/DX12CreateTimingTelemetry.h`、`Hooks/DX12DescriptorShadowTelemetry.h`、`Core/CommandFlow/DX12ResidentMaintenanceTelemetry.h`：三套独立 telemetry。
- `Core/DX12LogManager.h` 内部还有一套 log-cost 自我计时。

## 目标

只保留一个工具类，放在 `src/DirectX12/Core/HotPath/DX12Profiling.h`，用法：

```cpp
DX12Profiling::Start("SomeFunction");
// 业务代码
DX12Profiling::End("SomeFunction");

// 或 RAII：
DX12Profiling::Scope scope("SomeFunction");
```

约束：

- 只在 TIMING 版（`DX12_RELEASE_TIMING`）有开销；Release/Debug 全部编译为空操作。
- 只写轻量日志：每个名字一行（calls / totalUs / avgUs / maxUs），积累一批后一次性 flush（每 60 帧合并成一条日志入队，由既有异步写线程批量写盘）。
- 热路径无锁：每线程一张 128 槽开放寻址表，按字符串字面量指针哈希，Start/End 一对约两次 QPC + 一次 TLS 查表。

## 阶段划分

每阶段结束都必须保证 `build_release_timing_x64.ps1` 和 `build_release_x64.ps1` 双绿。

- [x] 阶段 0：计划文档 + 新引擎本体
  - 在 `DX12Profiling.h` 中以增量方式加入新引擎：`Start/End/Scope/Count/AddTimeUs/FlushBatch`。
  - 旧体系原样保留，双构建验证。
- [x] 阶段 1：替换 `DX12ProfilingImpl.h`
  - `DX12Profiling.h` 删除全部旧机器（Mode/Counter/State/几十个计数器成员、`overlayText`、Toggle、ResetFrame），只保留新引擎。
  - `DX12ProfilingImpl.h` 精简为纯宏：`DX12_PROFILE_FUNCTION(counter)` → `DX12Profiler::Scope(Counter, #counter)`；`DX12_PROFILE_FAST_FORWARD()` → `Count("FastForward")`。
  - `DX12Profiler::Scope` 的 Counter 分支改为持有 `const char *name`，Pause/Resume/Finish 走 `DX12Profiling::Start/End(name)`。
  - `Record*` 系列在类内 inline 转发到 `Count/AddTimeUs`；`ShouldCollectCounters()` 固定 false；`IsActive()` TIMING 下 true。
  - `BeginFrame/EndFrame` → `FlushBatch()`；删除 `DX12RuntimeControl.h` 的 `Init()` 调用。
  - 双构建验证绿。
- [x] 阶段 2：替换 `DX12Profiler.h` 与 `DX12HotPathTiming.h`
  - `DX12HotPathTiming.h` 只保留 `DX12HotPathStage` 枚举 + `Name()` 表（保住 300+ 调用点的编译期检查）；`DX12HotPathTimingUtils`（TLS 累加器、LogAndResetAll、LogFrameCpuDiagnostics）整体删除。
  - `DX12Profiler::Scope` 的 Stage/ApiStage 分支改用 `DX12Profiling::Start/End(stage 名)`；Api 分支暂时保留 OuterTimingProbe 调用（阶段 3 处理）。
  - `DX12_HOTPATH_TIME_*` 宏不再受 `DX12_HOTPATH_TIME_ENABLED` 门控，直接转发（DX12Profiling 自带 TIMING 门）；`DX12_HOTPATH_LOG_AND_RESET()` → `DX12Profiler::FlushFrame()`。
  - `FlushFrame()` = `FlushBatch()` + 三套 telemetry 的 LogAndReset（阶段 4 迁移），Release 下整体门控为空。
  - 双构建验证绿。
- [ ] 阶段 3：替换 `DX12OuterTimingProbe.h`
  - 585 个调用点不动；类内实现换成薄壳：`ApiWallScope` → Scope（bucket 名），`LogPresentSlice/LogQueueExecuteSlice/...` → `AddTimeUs("present.xxx" ...)`。
  - 等待堆栈、线程时间线、fence 事件登记等重诊断直接删除（Release 本就不编译，TIMING 也不需要）。
- [ ] 阶段 4：替换三套 telemetry 头
  - `DX12CreateTimingTelemetry.h` / `DX12DescriptorShadowTelemetry.h` / `DX12ResidentMaintenanceTelemetry.h` 的 `Record*` 全部转发到 `Count/AddTimeUs`，删除各自的原子累加器与 LogAndReset。
- [ ] 阶段 5：日志收尾
  - 删除 `DX12LogManager` 的 log-cost 自我计时（`LogCost*`、`TakeLogCostSnapshot`、`DX12LogCostSnapshot`）。
  - 清理残留宏与死代码，双构建 + 跑游戏看 `d3d12_log.txt` 中 `func=DX12Profiling` 输出验证。

## 新引擎设计（阶段 0 已实现）

- `Bucket { name, calls, totalUs, maxUs }`，每线程 128 槽开放寻址表，键为字符串字面量指针（哈希 = 指针混合 + 线性探测），无分配、无锁。
- `Start` 压入 TLS 作用域栈（深度 32），`End` 弹出并累计；`Scope` 自带 RAII 不依赖栈。
- 表满 / 栈溢出时丢弃该次计时（只影响诊断，不影响业务）。
- `FlushBatch()` 每帧 Present 调用一次，内部每 60 帧合并所有线程表，按 totalUs 降序格式化成一条多行日志，经 `DX12LogManager::EnqueueLogLine` 一次入队（异步线程本来就按批 fflush）。
- 全程用 `#if defined(DX12_RELEASE_TIMING)` 编译期开关，Release 下所有接口为 `__forceinline` 空函数 / 空类。
