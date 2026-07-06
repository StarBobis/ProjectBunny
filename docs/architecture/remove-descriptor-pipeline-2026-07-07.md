# 删除 DescriptorCollectPipeline，合并 Worker 到 Runtime 录制

日期：2026-07-07

## 核心思路

当前 descriptor 录制走两遍：runtime 表一遍，Worker 通过 Ring Buffer 再一遍。
`EnsureDescriptorHandlesVisible` + `WaitForSequence` 又把 Worker 同步回来，
解耦是虚假的。合并为一遍，在同一把 `gDescriptorLock` 内写完两张表。

## 改动清单

### 删除
- `DX12DescriptorCollectPipeline` 整个类（.hpp + .cpp）
- `DX12DescriptorTaskRingBufferUtils.hpp`
- `DX12DescriptorTlsBatchUtils.hpp`
- `DX12LruCacheUtils.hpp`（未使用的）

### 修改
- `RecordDescriptor`：追加 `RecordDescriptorInCacheLocked` 调用
- `RecordDescriptorCopyRange`：合并 runtime + heap/ledger 写入
- `EnsureDescriptorHandlesVisible` → 空函数
- `DrainForSnapshot` → 空函数
- `FindPendingDescriptorHandles` → 返回全 0
- `CopyPendingDescriptorWrites` → 返回全 0

### 不变
- `gRuntimeDescriptorByCpuHandle`、`gDescriptorByCpuHandle`、`gDescriptorHeaps`
- `FindDescriptorSummaryByCpuHandleLight` 查找逻辑
- Hash 算法
