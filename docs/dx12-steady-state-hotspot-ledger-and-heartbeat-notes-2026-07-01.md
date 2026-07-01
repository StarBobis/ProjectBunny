# DX12 Steady-State Hotspot Ledger And Heartbeat Notes

## 2026-07-01

- 根因确认：第二阶段 capture ownership 收敛后，常驻 CPU 热点已经从 `BindingEvent` 多 owner 复制，转移成了两类 steady-state 开销叠加：一类是 skip path 的 `Present` / `ExecuteCommandLists` 高频 heartbeat 洪泛，另一类是 CPU descriptor ledger / descriptor batch 中对胖 `DescriptorRecord` 的长期复制与驻留。
- 日志证据：`d3d12_log.jsonl` 中 `DX12ExecuteCommandListsHeartbeat`、`DX12PresentHeartbeat`、`DX12DescriptorCollectPipelineBatch`、`DX12BufferAddressRangeAdded` 都是高频大头，而 `DX12DescriptorCollectPipelineStats` 同时显示 `completedBatches == enqueuedBatches`，说明瓶颈已不是 worker 堵塞，而是 caller/steady-state 长期在做过多无效格式化、复制和常驻记账。
- 结构性修正一：skip path heartbeat 改成“首包强刷 + 周期聚合 + 失败照常打”。这样仍能保留 present/submit 是否持续命中 skip path 的诊断能力，但不再为每一帧、每一次提交都构造多条 JSON 心跳。
- 结构性修正二：`DX12BufferAddressRangeAdded` 改成聚合统计日志，保留 GPUVA 索引真实更新，不再为每个 buffer 注册都输出一条 steady-state 诊断行。GPUVA 解析正确性依赖的是 `gBufferResourceByGpuVa`，不是逐条日志。
- 结构性修正三：CPU descriptor ledger 改成 move-owned 存储，不再在 `StoreCpuDescriptorRecordLocked()` 里把同一份 descriptor 再 clone 一次。heap-external / staging descriptor 仍然被保留，Dump 和后续 `CopyDescriptors*` 溯源能力不变，但 caller 到 ledger 的 ownership 转移少了一次重对象复制与 COM retain。
- 结构性修正四：异步 descriptor pipeline 的 create path 改成直接转移调用方 `DescriptorRecord` 进入 `pendingDescriptorWrites`，去掉“先复制一份 asyncRecord，再把原始 record 留给 fallback”的固定额外拷贝。fallback 仍保留，只是在真正需要时才走。
- 结构性修正五：`DescriptorRecord` 的视图 payload 改成单份 union 存储，而不是同时常驻 `cbv/srv/uav/rtv/dsv/sampler` 六份字段位。查询/哈希/导出仍按 `kindId` 解释对应 payload，但常驻 ledger、pending map、batch vector、slot copy 的对象尺寸显著缩小。
- 正确性边界：这次没有删除 `Create*View` 创建事实，也没有删除 heap-external descriptor ledger，更没有关闭 `CopyDescriptors*` 跟踪；因此不会靠“少记东西”换性能。Dump 完整性和 Mod 生效能力依赖的创建时事实仍然常驻，只是把存储和日志方式压缩了。
- 失败经验：不要把“日志不会导致卡死”误读成“steady-state 高频心跳可以无限泛滥”。日志本身不是死锁根因，但每帧数万次 JSON 格式化和队列入列会实实在在侵占 caller thread，最终延迟命令提交，表现成 GPU 利用率掉到低位。
- 失败经验：不要只盯着策略开关是否过宽。即使策略暂时不能大幅关闭，如果底层数据模型还是胖对象、多次 clone、多处保留所有权，steady-state 仍会烧 CPU。先把 ownership 和 payload 压缩到正确形态，再谈更激进的分层门控。
- 下一阶段建议：继续把 descriptor/resource tracking policy 显式拆成三层接口
  - always-on current facts
  - mod-driven targeted residency
  - dump/hunting full residency/history
  这样可以在不损失 F8 追溯能力的前提下，进一步把普通 Mod 常驻模式从“默认全量创建事实驻留”收缩成“按需升级”。
