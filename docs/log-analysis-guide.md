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
