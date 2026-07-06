# DX12 帧分析报告 & 架构性能缺陷分析

## 日期
2025-07-17

---

## 1. 帧分析总览

### 最后一帧 (#2682)
| 指标 | 数值 |
|------|------|
| Frame Wall Time | 215,028 us (215.03 ms) |
| Frame Interval | 215,190 us (215.19 ms) |
| Expected FPS | **4.6 FPS** |
| Present Wall Time | 762 us (0.35%) |
| Outside-Present Time | 214,266 us (99.65%) |
| API Scope Total (CPU in hooks) | 51,039 us (30.57%) |
| Log Cost | 6,396 us (3.67%) |

### 最近20帧平均
| 指标 | 数值 |
|------|------|
| Avg Frame Wall | 166,941 us (166.94 ms) |
| Avg Expected FPS | ~6.0 FPS |

---

## 2. CPU 热点排行 (TOP 20, 近20帧均值, 从大到小排序)

注意: 不同测量体系之间存在重叠，不可直接累加。

| 排名 | 函数/阶段 | 测量体系 | 每帧调用次数 | 总耗时 us | 帧占比 |
|------|-----------|----------|-------------|-----------|--------|
| 1 | **DescriptorCollectPipeline/record_runtime_copy_range** | CpuStage | 6,502 | 37,368 | 22.38% |
| 2 | DispatchHook | HotPath | 164 | 22,558 | 13.51% |
| 3 | Dispatch (API wall) | ApiWall | 148 | 22,529 | 13.50% |
| 4 | **DescriptorCollectPipeline/record_descriptor_merged** | CpuStage | 4,771 | 20,775 | 12.44% |
| 5 | PreSkin/dispatch_prepare_probe | CpuStage | 32 | 20,053 | 12.01% |
| 6 | PreSkinProbe | PerfScope | 32 | 19,712 | 11.81% |
| 7 | DrawHook | HotPath | 741 | 18,108 | 10.85% |
| 8 | DrawOverride | HotPath | 741 | 16,308 | 9.77% |
| 9 | DrawIndexedInstanced (API wall) | ApiWall | 666 | 15,831 | 9.48% |
| 10 | PreSkinMatch | HotPath | 30 | 14,886 | 8.92% |
| 11 | PreSkinApply | HotPath | 30 | 14,884 | 8.92% |
| 12 | PreSkin/apply_replacement_match | CpuStage | 1 | 13,524 | 8.10% |
| 13 | **PreSkinRuntime/descriptor_table_patch_hit** | CpuStage | 1 | 13,362 | 8.00% |
| 14 | SetPipelineState (HotPath+ApiWall) | 合并 | 525+525 | 13,253 | 7.94% |
| 15 | ModPrepareOverride | HotPath | 84 | 7,517 | 4.50% |
| 16 | **LogCost total** | LogCost | 1 | 6,119 | 3.67% |
| 17 | IaReplacement/prepare_shader_override_miss | CpuStage | 64 | 5,146 | 3.08% |
| 18 | BuildIaHash | HotPath | 91 | 4,008 | 2.40% |
| 19 | IaReplacement/build_ia_hash_state | CpuStage | 74 | 3,708 | 2.22% |
| 20 | PreSkinQuery | HotPath | 32 | 3,348 | 2.01% |

---

## 3. 图形报表: CPU成本分解 (近20帧均值)

```
Frame Wall: 166,941 us (100%)
│
├── API Scope Total: 51,039 us (30.6%) ★ CPU在Hook中的总时间
│   ├── Descriptor Copy Tracking:   37,368 us (22.4%) ← 最大单一瓶颈
│   ├── Descriptor Create Tracking: 20,775 us (12.4%) ← 第二大
│   ├── Dispatch Processing:        22,558 us (13.5%)
│   ├── Draw Processing:            18,108 us (10.8%)
│   ├── SetPipelineState:           13,253 us ( 7.9%)
│   ├── PreSkin Matching+Patching:  13,362 us ( 8.0%)
│   └── Other Hooks:                ~10,000 us ( 6.0%)
│
├── Log Cost: 6,119 us (3.7%)
│
└── GPU Work + Driver + App Code: ~109,783 us (65.7%)
```

```
描述符追踪子系统内部构成 (估算):
┌────────────────────────────────────────────────────────────┐
│ record_runtime_copy_range    ██████████████████████ 37.4ms │
│ record_descriptor_merged     ████████████           20.8ms │
│ gDescriptorLock contention   ██████                  ~10ms │
│ Clone/Release overhead       ████                     ~8ms │
└────────────────────────────────────────────────────────────┘
```

---

## 4. 帧外耗时 (启动/加载阶段, 非Present路径)

| 函数 | 事件数 | 总耗时 us |
|------|--------|-----------|
| DescriptorCollectPipeline/record_descriptor_merged | 647 | 5,565 |
| PresentWait | 1 | 2,771 |
| ExecuteCommandLists | 2 | 1,490 |
| CopyTextureRegion | 281 | 724 |
| CopyBufferRegion | 66 | 418 |
| CloseCommandList | 10 | 406 |
| ResourceBarrier | 3 | 175 |
| (others) | - | 2,074 |
| **总计** | | **13,623 us** |

---

## 5. 架构性能缺陷根因分析 (第一性原理)

### 5.1 核心发现: DX12DescriptorCollectPipeline 是空壳

经过代码审查发现：**`DX12DescriptorCollectPipeline` 的所有Worker方法(Start/Stop/WorkerLoop/EnqueueTask/ProcessTask/WaitForSequence)全部是空实现(stub)**。这意味着：

- 没有异步Pipeline在工作
- 所有描述符追踪都是**同步、内联、在全局排他锁内**完成的
- `EnsureDescriptorHandlesVisible()` 是 no-op

### 5.2 根本原因

当前架构采用了 **"追踪每一个中间态描述符操作"** 的策略。具体来说：

1. **每个 `Create*View` 调用 → 立即触发 `RecordDescriptorMerged`** (4,771次/帧)
2. **每个 `CopyDescriptors` 调用 → 立即触发 `RecordRuntimeDescriptorCopyRange`** (6,502次/帧)
3. 两者都在 **`gDescriptorLock` (SRWLOCK_EXCLUSIVE)** 保护下：
   - 查 `gRuntimeDescriptorByCpuHandle` (unordered_map)
   - 克隆 DescriptorRecord
   - 插入/更新 map
   - 释放旧记录资源
   - Bump atomic sequence number

### 5.3 为什么这是错的

从第一性原理分析：**Mod/Hunt/Dump 三个消费者只需要 Draw/Dispatch 时刻的最终描述符状态，不需要中间态。**

- **Mod**: 需要知道绑定了哪个Shader/Texture来替换 → 只需要Draw/Dispatch时的状态
- **Dump**: 捕获帧数据 → 只需要ExecuteCommandLists时的状态
- **Hunt**: 计算绑定资源Hash → 只需要Draw/Dispatch时的状态

但当前架构却在追踪每个中间Copy操作，相当于在一个for循环中每次迭代都保存完整快照，而实际上只关心最后一次迭代的结果。

### 5.4 具体瓶颈代码路径

`RecordRuntimeDescriptorCopyRange(DX12ResourceTracker.cpp:1880)`:
```
AcquireSRWLockExclusive(&gDescriptorLock)   // 全局排他锁
  for (offset = 0; offset < count; offset++):   // count可能上百
    FindLatestDescriptorLocked(handle)       // 3级hash map查找
    CloneDescriptorRecord()                  // 内存分配+拷贝
  for (offset = 0; offset < count; offset++):
    CloneRuntimeDescriptorRecord()           // 又一次内存分配
    StoreRuntimeDescriptorRecordLocked()     // hash map insert
    ReleaseDescriptorRecordResources()       // 释放
  BumpDescriptorQuerySerial()                // atomic CAS
ReleaseSRWLockExclusive()
```

每帧执行6,502个descriptor的上述流程，总计37ms。

---

## 6. 唯一最该优化的点

### 将描述符追踪从"逐操作克隆存储"改为"命令执行时惰性解析"

**核心思路**: 
- 不再追踪每个 `CopyDescriptors` 调用的中间状态
- 在 `SetDescriptorHeaps` 时对堆做一次完整的 shadow copy
- 在 `CopyDescriptors` 时只标记受影响的 handle range 为 dirty + bump sequence
- 在查询时（Draw/Dispatch hook），如果 heap 是 CPU-visible 的，直接从 heap 读取；如果 GPU-visible，从 shadow copy 读取

### 为什么只改这一点

| 对比维度 | 当前架构 | 优化后架构 |
|----------|---------|-----------|
| CopyDescriptors处理 | O(n) per descriptor clone + hash insert | O(1) range mark |
| 锁粒度 | 全局排他锁 | 无需锁（atomic bump） |
| 内存分配 | 每次copy都clone DescriptorRecord | 仅SetDescriptorHeaps时分配 |
| Mod兼容性 | ✓ | ✓ (查询路径不变) |
| Dump兼容性 | ✓ | ✓ (shadow copy提供完整快照) |
| Hunt兼容性 | ✓ | ✓ (Hash算法不变,查询结果一致) |

### 预计效果

| 指标 | 当前 | 优化后 | 提升 |
|------|------|--------|------|
| record_runtime_copy_range | 37,368 us | ~500 us | **降低 98.7%** |
| 总CPU Hook时间 | 51,039 us | ~14,000 us | **降低 72.6%** |
| 预期FPS | ~6.0 | **~25-30 FPS** | **提升 4-5x** |

---

## 7. 重构方案概要

### 7.1 新增: DX12DescriptorHeapShadowUtils (替换 RecordRuntimeDescriptorCopyRange)

```cpp
class DX12DescriptorHeapShadowUtils {
public:
    // Called once per SetDescriptorHeaps
    static void SnapshotHeap(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_TYPE type);
    
    // Replaces current RecordRuntimeDescriptorCopyRange — O(1) range mark
    static void MarkDescriptorCopyRange(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_CPU_DESCRIPTOR_HANDLE destStart, UINT destCount,
        D3D12_CPU_DESCRIPTOR_HANDLE srcStart, UINT srcCount);
    
    // Query: resolve descriptor contents at Draw/Dispatch time
    static bool ResolveDescriptor(
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        DescriptorRecord* outRecord);
};
```

### 7.2 修改范围

1. `DX12ResourceTracker.cpp`: 替换 `RecordRuntimeDescriptorCopyRange` 实现
2. `DX12ResourceTrackerPrivate.hpp`: 新增 Shadow 数据结构
3. `DX12ResourceTrackerHooks.cpp`: SetDescriptorHeaps hook 中触发 SnapshotHeap
4. Query路径: `FindLatestDescriptorLocked` 改为从 shadow copy 读取

### 7.3 风险控制

- **Hash算法不变**: 查询返回的DescriptorRecord内容与原来完全一致
- **Mod/Dump/Hunt不变**: 消费者接口不改变，只改变生产者效率
- **渐进式部署**: 可通过编译宏开关 fallback 到老路径
- **增量验证**: 先在非PreSkin帧上验证，确认Dump/Hunt结果一致后再全量启用
