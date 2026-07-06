# ADR：运行时 Descriptor 录制不可跳过

日期：2026-07-07
状态：已接受

## 背景

尝试在普通 Mod 运行（无 Capture/Dump/Hunting）时跳过 `RecordRuntimeDescriptorCopyRange`
以节省约 60ms/帧。结果导致 Mod 完全失效。

## 发现

**`RecordRuntimeDescriptor` 和 `RecordRuntimeDescriptorCopyRange` 两者都是 Mod 正确运行的
必要条件，缺一不可，都不能跳过。**

### 原因

1. `RecordRuntimeDescriptor`（个体创建录制）写入 `gRuntimeDescriptorByCpuHandle`，
   存储每次 CreateShaderResourceView 等操作的 descriptor 数据。
   这是 CopyDescriptors 源解析时 `FindLatestDescriptorLocked` 的第一优先级查找目标。

2. `RecordRuntimeDescriptorCopyRange`（拷贝范围录制）写入 `gRuntimeDescriptorByCpuHandle`，
   存储拷贝后的目标 descriptor 数据。
   Draw 时 `FindDescriptorSummaryByCpuHandleLight` 优先查这张表。

3. Worker 线程和 `EnsureDescriptorHandlesVisible` 不能完全替代 runtime 表：
   - `FindLatestDescriptorLocked` 查找顺序：runtime 表 → heap 槽位 → 常规表
   - 没有 runtime 录制时，CopyDescriptors 的源解析在第一层就失败
   - draw 时查询可能找不到 Worker 还没处理完的 descriptor

## 决策

运行时 descriptor 录制（`RecordRuntimeDescriptor` + `RecordRuntimeDescriptorCopyRange`）
**必须无条件执行**，覆盖所有 descriptor 操作。它不是可选的性能优化目标，而是 Mod 正确性的基础。

Lock-Free Ring Buffer 重构（将 `mapsMutex` 与队列操作解耦）不触碰运行时录制路径，是安全的性能优化。

## 性能启示

约 86ms 的运行时录制开销不是冗余工作，而是维护游戏线程即时可见 descriptor 缓存的必要代价。
Worker 线程提供持久化和历史记录，runtime 表提供即时性——两者各司其职，不可互相替代。
