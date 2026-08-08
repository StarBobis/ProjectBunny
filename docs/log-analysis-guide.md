# DX12 日志性能统计指南

> 目的：每次拿到 `d3d12_log.txt` 后按固定流程统计，避免漏掉“隐藏热点”。
> 教训来源：GPU 占用 80% -> 13% 的那次排查中，只看 CpuStage/HotPath 热点列表完全看不到 ~50ms/帧
> 的开销，真正的大头在 `DX12ApiWallTiming`（CopyDescriptors 9.4ms、CreateCBV 4.7ms 等）。

## 1. 日志与构建版本对应

测试目录：`D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12`

- `d3d12_log.txt`：最近一次运行的日志
- `d3d12_log.before_<时间戳>.txt`：构建脚本自动轮转保留的历史日志（不删除）
- `d3d12.dll` / `d3d12.pdb`：当前部署的构建（用 LastWriteTime 与日志时间核对）

统计前先确认：

```powershell
Get-ChildItem 'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12' -Force |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 6 Name, Length, LastWriteTime
```

## 2. 必须统计的 6 类数据（缺一不可）

| 数据 | 日志 func | 回答的问题 |
|---|---|---|
| 帧时间构成 | `DX12FrameTimeline` | 帧时间花在哪：Present 外 / Present 内 / 队列提交 / fence 等待 |
| CPU 阶段 | `DX12CpuStageTiming` | Mod 各逻辑阶段（PreSkin、IaReplacement、RootSig 等）每帧耗时 |
| 热路径阶段 | `DX12HotPathTiming` | DrawHook、SetPipelineState、RootDescTable 等包装阶段 |
| **API 墙钟** | `DX12ApiWallTiming` | **每个 D3D12 API 的包装总耗时（含原始调用）——最容易漏的大头** |
| **创建类拆解** | `DX12CreateTiming` | 同一 API 的 `original` vs `record` 耗时——区分“游戏慢”和“我们慢” |
| 描述符影子 | `DX12DescriptorShadowTiming` | 描述符拷贝量、堆/全局锁竞争、槽位分配 |

### 关键规则

1. **先看 `DX12ApiWallTiming`，再看 `DX12CpuStageTiming`。** CPU 阶段只统计了显式埋点的
   逻辑路径（DrawHook、PreSkin 等），Device 级高频 API（CopyDescriptors、CreateCBV、
   AddRef/Release、CreateCommittedResource）的包装开销只出现在 ApiWall 里。
2. **`DX12CreateTiming` 的 original/record 拆解是判断“我们慢还是游戏慢”的实锤。**
   例：CopyDescriptors 原始 0.29us/次，record 2.9us/次 -> 我们贵了 10 倍。
3. **嵌套计时不能相加。** `dispatch_prepare_probe` 包含 `collect_bindings`；
   `DrawHook` 包含 `ApplyAndExecute`/`ModPrepareOverride`。列热点时以最外层为准。
4. **不同日志的场景段（present 范围）可能不同**，帧级数字直接对比有噪声；
   与场景无关的单次耗时（us/call）才是可靠对比指标。
5. TIMING 构建（`-ReleaseTiming`）才会输出这些数据；Release 正式版没有。

## 3. 现成统计命令（PowerShell，在仓库根目录执行）

### 3.1 帧时间构成

```powershell
$log='D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt'
$tl = rg "func=DX12FrameTimeline" $log | ForEach-Object {
  [pscustomobject]@{
    Present=[long]([regex]::Match($_,'present=(\d+)').Groups[1].Value)
    Wall=[long]([regex]::Match($_,'frameWallUs=(\d+)').Groups[1].Value)
    Outside=[long]([regex]::Match($_,'outsidePresentUs=(\d+)').Groups[1].Value)
    PresentWall=[long]([regex]::Match($_,'presentWallUs=(\d+)').Groups[1].Value)
    Original=[long]([regex]::Match($_,'presentOriginalUs=(\d+)').Groups[1].Value)
    Queue=[long]([regex]::Match($_,'queueEstimatedTotalUs=(\d+)').Groups[1].Value)
    FenceUs=[long]([regex]::Match($_,'fenceTotalUs=(\d+)').Groups[1].Value)
  }
}
# 按 present 范围筛选某段，例如 PreSkin 段 4283..4813：
$seg = $tl | Where-Object { $_.Present -ge 4283 -and $_.Present -le 4813 }
$seg | Measure-Object Wall -Median
```

### 3.2 API 墙钟排行（最重要的隐藏热点）

```powershell
$api = rg "func=DX12ApiWallTiming" $log | ForEach-Object {
  [pscustomobject]@{
    Present=[long]([regex]::Match($_,'present=(\d+)').Groups[1].Value)
    Bucket=[regex]::Match($_,'bucket=([^,]+)').Groups[1].Value
    Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
    TotalUs=[long]([regex]::Match($_,'totalUs=(\d+)').Groups[1].Value)
    MaxUs=[long]([regex]::Match($_,'maxUs=(\d+)').Groups[1].Value)
  }
}
$seg = $api | Where-Object { $_.Present -ge 4283 -and $_.Present -le 4813 }
$seg | Group-Object Bucket | ForEach-Object {
  $r=$_.Group; $f=$r.Count
  [pscustomobject]@{
    Bucket=$_.Name
    CallsPerFrame=[math]::Round((($r|Measure-Object Calls -Sum).Sum)/$f,1)
    AvgUsPerFrame=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/$f,1)
    MaxUs=($r|Measure-Object MaxUs -Maximum).Maximum
  }
} | Sort-Object AvgUsPerFrame -Descending | Format-Table -AutoSize
```

### 3.3 创建类 original/record 拆解

```powershell
rg "func=DX12CreateTiming" $log | ForEach-Object {
  [pscustomobject]@{
    Present=[long]([regex]::Match($_,'present=(\d+)').Groups[1].Value)
    Api=[regex]::Match($_,'api=([^,]+)').Groups[1].Value
    Stage=[regex]::Match($_,'stage=([^,]+)').Groups[1].Value
    Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
    TotalUs=[long]([regex]::Match($_,'totalUs=(\d+)').Groups[1].Value)
  }
} | Group-Object Api,Stage | ForEach-Object {
  $r=$_.Group; $f=$r.Count
  [pscustomobject]@{
    Api=$_.Name.Split(',')[0]; Stage=$_.Name.Split(',')[1]
    UsPerCall=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/(($r|Measure-Object Calls -Sum).Sum),2)
    AvgUsPerFrame=[math]::Round((($r|Measure-Object TotalUs -Sum).Sum)/$f,1)
  }
} | Sort-Object Api,Stage | Format-Table -AutoSize
```

### 3.4 PreSkin 缓存与决策链

```powershell
# preskin_bind_cache 命中率
rg "category=preskin_bind_cache" $log | ForEach-Object {
  [pscustomobject]@{
    Calls=[long]([regex]::Match($_,'calls=(\d+)').Groups[1].Value)
    Hits=[long]([regex]::Match($_,'hits=(\d+)').Groups[1].Value)
  }
} | Measure-Object Calls -Sum, Hits -Sum

# 每次 probe 的决策耗时分布（DX12Perf scope=PreSkinProbe）
rg "func=DX12Perf,scope=PreSkinProbe" $log | ForEach-Object {
  [pscustomobject]@{
    Cache=[regex]::Match($_,'cache=(\w+)').Groups[1].Value
    Us=[long]([regex]::Match($_,'us=(\d+)').Groups[1].Value)
  }
} | Group-Object Cache | ForEach-Object {
  $us=@($_.Group.Us | Sort-Object)
  [pscustomobject]@{
    Cache=$_.Name; Count=$us.Count
    MedianUs=$us[[int]($us.Count/2)]; P95Us=$us[[int]($us.Count*0.95)]
  }
} | Format-Table -AutoSize
```

### 3.5 描述符影子与锁竞争

```powershell
rg "func=DX12DescriptorShadowTiming" $log |
  Select-Object -Last 5   # 看 copyRangeCalls / copySlots / 各类 contention / waitUs
```

## 4. 已确认的隐藏热点结论（截至 2026-08-08 BP 版）

PreSkin 段（present 4283-4813，~18fps）每帧 API 墙钟大头：

| API | 次数/帧 | 每帧 | 结论 |
|---|---|---|---|
| CopyDescriptors | 3249 | 9.4ms | record 2.9us/次 vs original 0.29us，我们贵 10 倍 |
| DrawIndexedInstanced | 973 | 6.8ms | 原始调用+包装，需继续拆解 |
| PSO 创建 | 47 | 6.0ms | 集中在段首 32 帧（编译风暴） |
| 堆创建 | 22 | 5.7ms | 270 帧段 |
| CreateCBV | 3488 | 4.7ms | 1.35us/次，每帧创建 3488 个 |
| ExecuteCommandLists | 7 | 2.5ms | 包装+队列同步 |
| Dispatch | 90 | 2.0ms | 含 PreSkin 决策 |
| ObjectLifetime | 33 | 1.7ms | AddRef/Release 包装 52us/次 |
| ResourceBarrier | 135 | 1.7ms | 12us/次 |
| 资源创建 | 75 | 1.6ms | 21us/次 |

GPU 占用率从 80% 掉到 13% 的根因：CPU 渲染线程被上述包装累计拖慢 ~45-50ms/帧，
GPU 无事可做。修复优先级：CopyDescriptors > CreateCBV > Draw > 包装层杂项。

## 5. >1ms 函数 DX12 机制调查结果（2026-08-08）

> 依据：BP 版日志（18:19）PreSkin 段 API 墙钟 + 源码走查。
> 结论：大部分函数内部已经有 DX12 快速路径，剩余大头是“每调用固定开销”
> （原始函数指针解析 + TIMING 遥测原子操作 + 计时 scope），以及几处真正的
> “一次性全量状态”式实现（堆创建、Reset 全量清零）。

| 函数/API | 每帧 | 单次 | 现状（是否已走 DX12 机制） | DX12 专属机制/建议 | 优先级 |
|---|---|---|---|---|---|
| CopyDescriptors | 9.4ms | record 2.9us | 内部已有无锁堆缓存/扁平值ID memo/RangeMemo/稠密 seqlock 路径 | 遥测改 TLS 批量每帧刷；original 指针进对象缓存；未观察堆跳过记录 | P0 |
| DrawIndexedInstanced | 6.8ms | 7us | bound-draw/IA/PSO 多层负缓存已生效 | 无替换候选判定下沉到 SetPipelineState；Release 版对比剥离计时仪器开销 | P1 |
| PSO 创建 | 6.0ms（32帧） | — | 游戏侧编译为主 | 跟踪异步化/重复 PSO 缓存 | P2 |
| 堆创建 | 5.7ms（270帧） | — | 创建时全量分配影子数组（日志见 257MB） | 影子状态按页懒分配（D3D12 堆内容由应用管理） | P1 |
| CreateCBV | 4.7ms | 1.35us | 已有 TLS 内容哈希+顺序槽+扁平值ID 早退 | 遥测 TLS 批量；original 指针缓存 | P1 |
| ExecuteCommandLists | 2.5ms | 358us | unwrap+执行流 | 零工作时直通；检查 upload/readback 是否插入 GPU 等待 | P1 |
| Dispatch | 2.0ms | 22us | CS 判定 TLS 缓存+本地 probe 缓存已生效 | 无替换候选挂到 PSO 决策一次算好 | P2 |
| ObjectLifetime | 1.7ms | 52us | 生命周期跟踪（锁+map） | 只跟踪 mod 引用对象；引用计数原子化 | P2 |
| ResourceBarrier | 1.7ms | 12us | 显式 barrier 状态登记 | 无状态依赖时整段跳过 | P2 |
| 资源创建 | 1.6ms | 21us | 创建即登记（锁+GPU VA map） | 惰性登记/批量入队 | P2 |
| SetPipelineState | 1.2ms | 3.5us | PSO 缓存已生效 | TLS 同值透传；决策挂 PSO | P2 |
| ResetCommandList | 1.15ms | 68us | `*state=...` 全量清零+大对象移动 | 懒重置：保留容量，只清代际字段 | P1 |
| IASetVertexBuffers | 1.1ms | 1.5us | 延迟资源解析已生效 | 无 IA 替换时快速透传 | P3 |
| SetGraphicsRootSignature | 1.06ms | 4.2us | TLS 快路径已生效 | 遥测批量；original 缓存 | P3 |

注意：TIMING 构建的计时/遥测本身有成本（每调用 QPC+原子操作），
正式性能对比建议用 Release（非计时）版再测一轮，避免把测量开销当成 Mod 开销。

## 6. 已实施的 DX12 专属路径优化（2026-08-08 第二轮）

按上表优先级落地并已构建部署；2026-08-08 第三轮根据同场景测试回退了两项
无收益且有理论风险的变化，最终保留/回退情况如下：

1. **ResetCommandList 懒重置**（`DX12CommandRuntime.inl`）
   - 不再 `*state = DX12SimulateContext()` 全量清零（~60KB/次，其中根常量数组
     43KB），改为 `DX12RuntimeResetStateForRecording` 精确重置：
     - 根常量只清 valid/maxSet/rootParameterIndex，payload 留旧（valid=false 永不被读）；
     - `resolvedDescriptorQueries` 跨 Reset 保留容量（move 出去再回来，只清 valid）；
     - 其余标量/表/描述符/决策模板逐项复位。
2. ~~描述符拷贝跳过非 shader-visible 目标堆~~（**已回退**）
   - 理论上符合 DX12 模型，但同场景 A/B 未测出收益，且存在“CPU 堆将来作为源
     拷贝到 GPU 堆时值ID 传播被破坏”的潜在风险；按 best practice 回退。
3. ~~SetPipelineState 冗余调用跳过~~（**已回退**）
   - 同场景 A/B 未测出收益（该场景连续重复设置不多），为保守起见回退。

同场景测试结论：Reset 懒重置确认有效（-13%/次）；CopyDescriptors/SetPSO 的
帧间差异来自游戏状态方差（“内容真变的槽” 7560 -> 10869/帧），与回退项无关。

调查后确认已符合 DX12 机制、无需改动：
- CopyDescriptors/CreateCBV：内部已有无锁堆缓存、TLS 内容哈希、扁平值ID memo、
  RangeMemo、稠密 seqlock 路径，遥测已是 TLS 分块；
- 原始函数指针解析在 wrapper 模式下已免锁（vtable 直读）；
- 堆创建：影子槽数组已是按需懒分配；
- ExecuteCommandLists：upload/readback 通道已有零工作快速门；
- Draw：多层负缓存 + PSO 编译决策已生效，剩余主要是原始调用+计时仪器。
