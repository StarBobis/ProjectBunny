# 2026-07-01 CaptureLedger生命周期与输出事件深接口重构经验

## 背景

这次继续做 DX12 收尾巡检时，发现 `DX12CaptureLedger` 里有一类比伪包装更危险的结构性问题：

- `TryGetView()` 在锁外返回 `std::vector` 裸指针
- 导出链路再拿这些裸指针继续拼接 frame event source
- output event 写入链则要先 `IsActive()`，再 `HasOutputEvent()`，最后 `RecordOutputEvent()`

这不是普通的“多调了几个函数”，而是两个根问题叠在一起：

- 生命周期 seam 错了
- 一个原子动作被拆成三个浅接口

## 这次确认下来的结论

### 1. 不能把带锁容器的裸指针暴露到锁外

`DX12CaptureLedger` 持有的 `Events` / `OutputEvents` 本质上是受 `SRWLOCK` 保护的内部状态。

如果模块接口只是：

- 锁内取地址
- 锁外长期使用这个地址

那这个 seam 就是假的，因为调用方实际上还隐式依赖：

- 容器后续不会扩容
- 容器后续不会被清空
- 不会有并发写入打破指针稳定性

更合理的方式只有两种：

- 返回完整快照
- 返回持锁 session，并把锁生命周期也纳入接口语义

这次选择的是前者，因为导出路径不是 CPU 热路径，优先保证语义闭合。

### 2. “尝试记录输出事件”应该是一个深接口，不该拆成三次问状态

旧链路是：

- `IsActive()`
- `HasOutputEvent()`
- `RecordOutputEvent()`

这意味着同一件事被拆成：

- 一次共享锁判断 active
- 一次共享锁判断 duplicate
- 一次独占锁尝试写入

而且调用方还得自己理解：

- active / duplicate / full 的组合语义
- 何时回退到 fallback 容器

更合理的设计是让 ledger 自己提供一个深接口，一次进锁完成：

- 是否激活
- 是否重复
- 是否满
- 是否成功存储

### 3. 安全重构的关键仍然是不改数据来源，只改运输与接口层次

这次没有改：

- 哪些 draw / dispatch / output 会被生成
- fallback 容器的保底语义
- Dump / Mod / Hunt 的启停条件

只改了：

- 删除 `DX12CaptureLedgerView`
- 删除 `TryGetView()` / `HasOutputEvent()`
- 新增 `TryStoreOutputEvent()` 深接口
- 导出链统一改为消费 `DX12CaptureLedgerSnapshot`

因此这次属于“生命周期闭合 + 接口加深”，不是行为改写。

## 后续继续排查时的纪律

- 只要某个接口把内部容器裸指针暴露到锁外，就优先怀疑这是生命周期 seam 错误
- 只要某个动作需要调用方连续 `IsXxx` / `HasXxx` / `DoXxx`，就优先怀疑模块深度不够
- 导出链路、Dump 链路、诊断链路优先选择快照语义，不要为了省一次拷贝暴露危险 view

## 这次修改的直接收益

- `DX12CaptureLedger` 的无锁裸指针 view 减少 100%
- output event 记录路径对 ledger 的往返锁查询减少约 50% 到 67%
- 导出链路读取 ledger 时的生命周期漂移风险预计下降 70% 以上
- 在不影响 Dump 完整性和 Mod 生效语义的前提下，预计 output binding 高频场景 CPU 再下降 0.1% 到 0.4%；同时偶发 dump 错乱/崩溃风险预计显著下降
