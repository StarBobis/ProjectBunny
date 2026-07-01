# DX12 Capture Ledger Single Owner Notes

## 2026-07-01

- 根因确认：第二阶段 CPU 热点不只是异步队列本身，而是 capture 路径存在“同一 `BindingEvent` 被 worker 物化一次、tracker 保存一次、capture ledger 再复制一次、export 再整体复制一次”的多重重事件所有权。热点因此集中在 `BindingEvent` 大对象拷贝、IA 资源重复 retain/release、以及导出期大向量复制。
- 结构性修正：把 `DX12CaptureLedger` 定义为 frame-analysis / shader-dump capture 期的唯一重事件 owner。`DX12BindingTracker::StoreEventLocked()` / `StoreOutputEventLocked()` 先尝试把事件移动进 ledger，只有 ledger 未激活或 ledger 容量溢出时，才回退写入 tracker fallback 容器。
- 导出 seam 修正：新增统一 `DX12FrameEventSource`，由 `DX12BindingTracker::PopulateFrameEventSource()` 负责决定导出数据源。常规 capture 导出直接读取 ledger 视图，避免 `GetSnapshot()` 的整向量复制；若 ledger 溢出后有 spill 到 tracker fallback，则在一个地方做补位拼接，保证 Dump 完整性。
- 生命周期修正：capture 结束前必须先 `DrainPendingEvents()`，再 `DX12CaptureLedger::EndCapture()`。否则队列尾部尚未消费的事件会在 ledger 关闭后落入 tracker fallback，形成“同一 capture 帧拆成两个 owner”的边界风险。
- 正确性边界：保留 tracker fallback 容器不是旧架构残留，而是 ledger overflow 与非-capture 模式的兼容层。这样既不牺牲 Mod 常驻路径，也不因为 capture 期优化而丢失 Dump 内容。
- 失败经验：不能把“ledger 是 capture 主存储”简化成“export 只读 ledger”。只要 capture ledger 有容量上限，就必须设计 overflow seam；否则在长帧、高 draw/dispatch 或大量 output event 下，后半段事件会静默丢失。
- 下一阶段建议：继续把 `BuildEvent()` 从“热路径全量物化”推进到“事件事实快照 + 导出期按需解释”的深模块设计。当前这轮解决的是重复 owner 和复制风暴，下一轮才是进一步收缩单次事件物化成本。
