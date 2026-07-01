# DX12 Descriptor Heap Local Versioning Notes

## 2026-07-01

- 根因确认：当前常驻 CPU 热点的主要放大器不是资源收集队列本身，而是“全局描述符失效 + 查询侧跨线程同步 + dispatch 前全量 binding 解释”三者叠加。只要任意描述符写入都会 bump 全局 `descriptorQuerySerial`，`GetCurrentDescriptors()` / `GetCurrentComputeAll()` 的 TLS 缓存就会整体失效，导致 compute pre-skin probe 在高频 dispatch 下不断重查整套 root table。
- 结构性修正：把“描述符堆身份/边界元数据”和“描述符内容完成”拆成两个 seam。`DX12FindDescriptorHeapByGpuHandle()` / `DX12FindDescriptorHeapByCpuHandle()` 只查询已经在 `CreateDescriptorHeap` 时捕获的 heap 元数据，不再为了做 heap 边界映射去 `DrainForSnapshot()` 等待描述符内容管线。
- 局部版本策略：给 `DescriptorHeapRecord` 增加 `version`，仅在该 heap 槽位实际变化时递增。binding 查询缓存不再绑定全局 `CurrentDescriptorQuerySerial()`，而是绑定 “当前 command list 的 binding serial + 当前 CBV/SRV/UAV heap 指针 + 该 heap version”。这样 unrelated heap 写入不会再把当前 dispatch 查询缓存整片打穿。
- 正确性修正：根据 Microsoft `ID3D12GraphicsCommandList::SetDescriptorHeaps` 文档，切换 descriptor heap 后，之前设置的 descriptor tables 状态会变成 undefined。因此 `SetDescriptorHeaps()` 必须主动清空 `graphicsTables[]` / `computeTables[]`，并重置对应 TLS table cache；否则不仅有正确性风险，还会制造“旧表地址伪命中”的性能噪音。
- 第一阶段边界：这次只缩窄失效域和同步域，不改变 descriptor creation facts、copy facts、root signature facts、PSO-root facts 的捕获，也不移除真正需要 descriptor content completion 的 CBV 解析路径，所以不会以牺牲 Mod 生效或 Dump 完整性换性能。
- 失败经验：不要把“所有描述符变化都可能影响查询”直接等价成“全局 serial 一刀切失效”。这种建模最容易写出来，但会把局部变化放大成全局热点，最终拖慢 GPU 提交时机。
- 下一阶段建议：继续把 binding event materialization 从 always-on 路径里拆出来，改成“事实快照入队 + 需要导出时按 event 延迟解释”。这会比继续微调现有全量 `BuildEvent()` 更接近根治。
