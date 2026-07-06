# DX12 描述符追踪重构 — 执行计划

## 日期
2025-07-17

---

## 0. 架构确认

经代码审查确认：当前是**纯 Wrapper (COM 代理) 架构**，不存在活跃的 Hook detour。

```
Game API 调用
  → GraphicsCommandListWrapper (COM Proxy)
    → 同步回调: CopyDescriptors / Create*View / SetDescriptorHeaps
      → RecordRuntimeDescriptorCopyRange / RecordDescriptorMerged  ← 瓶颈在此
    → mOrigCopyDescriptors(mRealCmdList, ...)  ← 调用真实驱动
```

- `DX12DescriptorCollectPipeline` 的 Worker/Queue 体系 = **死代码**（编译进去了但40+方法全是空实现）
- NktHookLib 基础设施就绪但**未激活**
- `HookCommandList()` 函数体是 `(void)commandList;` — 空操作
- 所有拦截路径完全在 wrapper 控制之下 → **重构风险可控**

---

## 1. 要删除的死代码

| 文件 | 行数 | 原因 |
|------|------|------|
| `descriptor/DX12DescriptorCollectPipeline.hpp` | 291 | 异步Pipeline接口+数据结构，全部未使用 |
| `descriptor/DX12DescriptorCollectPipeline.cpp` | 83 | 仅3个函数有转发逻辑，其余全是空壳 |
| `utils/DX12DescriptorTaskRingBufferUtils.hpp` | ~100 | MPSC无锁环形缓冲(65536槽)，仅被Pipeline引用 |
| `utils/DX12DescriptorTlsBatchUtils.hpp` | ~50 | TLS批量工具，仅被Pipeline引用 |

---

## 2. 新增文件

### `utils/DX12DescriptorHeapShadowUtils.hpp`

**设计原则**: 纯同步、flat array 索引、无锁、面向对象、C++17、无 private、无 auto

```cpp
class DX12DescriptorHeapShadowUtils {
public:
    // ---- Nested types ----
    struct HeapShadow {
        ID3D12DescriptorHeap* heap;
        D3D12_DESCRIPTOR_HEAP_TYPE type;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase;
        UINT descriptorSize;
        UINT capacity;
        std::vector<DescriptorRecord> records;   // flat array by heap offset
        std::vector<uint8_t> valid;              // 1 = slot has valid data
        SRWLOCK lock;                            // per-heap fine-grained lock
        UINT64 lastUpdateSerial;
    };

    // ---- Lifetime ----
    static void Initialize();
    static void Shutdown();

    // Called from SetDescriptorHeaps hook — allocates/resizes shadow array
    static void BindHeap(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static void UnbindHeap(ID3D12DescriptorHeap* heap);

    // ---- Producer (hot path) ----
    // Replaces RecordDescriptorMerged  — O(1) array write
    static void WriteDescriptorSlot(
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        DescriptorRecord&& record);

    // Replaces RecordRuntimeDescriptorCopyRange — O(count) array copy, no hash map
    static void RecordCopyRange(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_CPU_DESCRIPTOR_HANDLE destStart,
        D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
        UINT count);

    // ---- Consumer (query path) ----
    // Replaces FindLatestDescriptorLocked — O(1) array index
    static bool ResolveDescriptor(
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        DescriptorRecord* outRecord);

    // Replaces EnsureDescriptorHandlesVisible — no-op (shadow is always up to date)
    static void EnsureVisible(const D3D12_CPU_DESCRIPTOR_HANDLE*, size_t) {}

    // ---- Lock helpers ----
    static void AcquireShared(D3D12_DESCRIPTOR_HEAP_TYPE type);
    static void ReleaseShared(D3D12_DESCRIPTOR_HEAP_TYPE type);
    static void AcquireExclusive(D3D12_DESCRIPTOR_HEAP_TYPE type);
    static void ReleaseExclusive(D3D12_DESCRIPTOR_HEAP_TYPE type);

    // ---- Internal ----
    static HeapShadow* FindShadow(D3D12_CPU_DESCRIPTOR_HANDLE handle, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static UINT HandleToOffset(const HeapShadow& shadow, D3D12_CPU_DESCRIPTOR_HANDLE handle);
};
```

---

## 3. 修改清单（按依赖顺序）

### 3.1 `resource/DX12ResourceTrackerPrivate.hpp`

**操作**: 修改

| 变更 | 说明 |
|------|------|
| 删除 `extern SRWLOCK gDescriptorLock` 声明 | 全局锁废除，改用per-heap锁 |
| 删除 `extern std::unordered_map<SIZE_T, DescriptorRecord> gRuntimeDescriptorByCpuHandle` | hash map 废除 |
| 删除 `extern std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle` | hash map 废除 |
| 删除 `void RecordRuntimeDescriptorCopyRange(...)` 声明 | 移至 ShadowUtils |
| 删除 `void RecordDescriptorCopyRange(...)` 声明 | 移至 ShadowUtils |
| 删除 `void RecordDescriptorMerged(...)` 声明 | 移至 ShadowUtils |
| 新增 `#include "../utils/DX12DescriptorHeapShadowUtils.hpp"` | 引入新工具类 |

### 3.2 `resource/DX12ResourceTracker.cpp`

**操作**: 重写核心段

| 变更 | 说明 |
|------|------|
| 删除 `SRWLOCK gDescriptorLock = SRWLOCK_INIT` | 不再需要全局锁 |
| 删除 `std::unordered_map<...> gRuntimeDescriptorByCpuHandle` | 替换为 shadow array |
| 删除 `std::unordered_map<...> gDescriptorByCpuHandle` | 替换为 shadow array |
| 删除 `#include "../descriptor/DX12DescriptorCollectPipeline.hpp"` | 不再需要 |
| 重写 `RecordDescriptorMerged` (L2096) | → 调用 `ShadowUtils::WriteDescriptorSlot` |
| 重写 `RecordRuntimeDescriptorCopyRange` (L1880) | → 调用 `ShadowUtils::RecordCopyRange` |
| 重写 `FindLatestDescriptorLocked` (L1597) | → 调用 `ShadowUtils::ResolveDescriptor` |
| 重写 `StoreRuntimeDescriptorRecordLocked` (L306) | → 内联到 ShadowUtils |
| 重写 `RecordRuntimeDescriptor` (L1873) | → 调用 `ShadowUtils::WriteDescriptorSlot` |
| 修改所有 `AcquireSRWLockExclusive(&gDescriptorLock)` (13处) | → `ShadowUtils::AcquireExclusive(type)` |
| 修改所有 `AcquireSRWLockShared(&gDescriptorLock)` (13处) | → `ShadowUtils::AcquireShared(type)` |
| 修改所有 `ReleaseSRWLockExclusive/Shared` | → `ShadowUtils::ReleaseExclusive/Shared(type)` |
| 修改 `ClearCpuDescriptorLedgerLocked` | → 遍历 shadow arrays |
| 修改 `InvalidateDescriptorHeapLocked` | → 标记 shadow slots invalid |
| 修改 `IsDescriptorUnchanged` | → 从 shadow array 读取 |
| 修改 `RecordDescriptorInCacheLocked` | → 直接写 shadow array（已统一存储） |

### 3.3 `resource/DX12ResourceTrackerHooks.cpp`

**操作**: 修改调用点

| 变更 | 说明 |
|------|------|
| 删除 `#include "../descriptor/DX12DescriptorCollectPipeline.hpp"` | 不再需要 |
| 新增 `#include "../utils/DX12DescriptorHeapShadowUtils.hpp"` | 直接调用 |
| L361-448: 6处 `RecordDescriptor` 调用 | → `ShadowUtils::WriteDescriptorSlot(handle, type, std::move(record))` |
| L490: `RecordDescriptorCopyRange` | → `ShadowUtils::RecordCopyRange(type, dest, src, count)` |
| L506: `RecordDescriptorCopyRange` (Simple) | → `ShadowUtils::RecordCopyRange(type, dest, src, count)` |
| L313: `CreateDescriptorHeap` hook 中的锁 | → `ShadowUtils::AcquireExclusive(type)` |

### 3.4 `resource/DX12ResourceTrackerQueries.cpp`

**操作**: 修改查询路径

| 变更 | 说明 |
|------|------|
| 删除 `#include "../descriptor/DX12DescriptorCollectPipeline.hpp"` | 不再需要 |
| 4处 `EnsureDescriptorHandlesVisible` 调用 | → `ShadowUtils::EnsureVisible` (no-op，shadow始终最新) |
| 所有 `AcquireSRWLockShared(&gDescriptorLock)` | → `ShadowUtils::AcquireShared(type)` |
| `FindDescriptorSummaryForHandleLocked` | → 使用 `ShadowUtils::ResolveDescriptor` |
| `FindDescriptorSummaryLight` | → 同上 |
| `FindDescriptorSummariesBatch` | → 同上 |
| `FindDescriptorSummaryAtEpoch` | → 同上 |
| `FindDescriptorSummaryAtEpochAndKind` | → 同上 |
| `FindDescriptorSummaryByCpuHandle` | → 同上 |

### 3.5 `resource/DX12ResourceCollectQueue.cpp`

**操作**: 修改锁调用

| 变更 | 说明 |
|------|------|
| L378: `AcquireSRWLockExclusive(&gDescriptorLock)` | → `ShadowUtils::AcquireExclusive(type)` |

### 3.6 `resource/DX12ResourceTrackerReleaseTrackers.hpp`

**操作**: 修改锁调用

| 变更 | 说明 |
|------|------|
| L32: 锁 acquire/release | → `ShadowUtils::AcquireExclusive/ReleaseExclusive` |

### 3.7 CMakeLists.txt / 项目文件

| 变更 | 说明 |
|------|------|
| 移除 `descriptor/DX12DescriptorCollectPipeline.cpp` 编译条目 | 文件删除 |
| 移除 `descriptor/DX12DescriptorCollectPipeline.hpp` 编译条目 (如果是header-only无关) | 文件删除 |

---

## 4. 兼容性保证

| 子系统 | 验证点 | 风险 |
|--------|--------|------|
| **Mod** | `ResolveDescriptor` 返回的 `DescriptorRecord` 字段与原来 `FindLatestDescriptorLocked` 一致 | 零风险 |
| **Dump** | `FindDescriptorSummaryByCpuHandle` 系列函数返回值不变 | 零风险 |
| **Hunt** | Hash 计算使用 `DescriptorRecord.valueId` — 由 `PrepareDescriptorValueId` 计算，逻辑不变 | 零风险 |
| **PreSkin** | 走独立锁路径 `gModLock`/`gPreSkinLock`，不经过 `gDescriptorLock` | 零风险 |

---

## 5. 量化预期

### 5.1 热点消除

| 指标 | 优化前 (per frame) | 优化后 (per frame) | 降幅 |
|------|-------------------|--------------------|------|
| `record_runtime_copy_range` 耗时 | 37,368 us | ~1,500 us | **-96.0%** |
| `record_descriptor_merged` 耗时 | 20,775 us | ~5,000 us | **-75.9%** |
| hash map find/insert 操作 | ~19,000 次 | 0 次 | **-100%** |
| DescriptorRecord clone/alloc | ~13,000 次 | ~6,000 次 (array move) | **-53.8%** |
| 全局排他锁竞争 | 6,502 次/帧 | 0 次（per-heap锁，极少竞争） | **-100%** |

### 5.2 帧率提升

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 描述符追踪 CPU 占比 | 58ms | ~6.5ms | **-88.8%** |
| API scope CPU 总耗时 | 51ms | ~14ms | **-72.5%** |
| API scope CPU 占比 | 30.6% | ~8.4% | - |
| 预期 FPS | 4.6 | **12-18** | **+160~290%** |

> 注意: 帧率瓶颈可能从CPU转移到GPU。如果GPU已经是瓶颈（当前帧215ms中GPU占~65%），CPU优化后的上限约为GPU时间决定的FPS。无论如何，描述符追踪不再是热点。

### 5.3 代码瘦身

| 指标 | 数值 |
|------|------|
| 删除死代码行数 | ~524 行 |
| 净增/减代码 | 预计 **-200 行**（删旧代码 > 新增 ShadowUtils） |

---

## 6. 执行顺序

```
Phase 1: 基础设施
  1. 创建 DX12DescriptorHeapShadowUtils.hpp

Phase 2: 生产者改造
  2. 修改 DX12ResourceTrackerPrivate.hpp （声明/引用替换）
  3. 修改 DX12ResourceTracker.cpp （核心实现替换）
  4. 修改 DX12ResourceTrackerHooks.cpp （调用点替换）
  5. 修改 DX12ResourceCollectQueue.cpp （锁替换）
  6. 修改 DX12ResourceTrackerReleaseTrackers.hpp （锁替换）

Phase 3: 消费者改造
  7. 修改 DX12ResourceTrackerQueries.cpp （查询路径替换）

Phase 4: 死代码清理
  8. 删除 DX12DescriptorCollectPipeline.hpp
  9. 删除 DX12DescriptorCollectPipeline.cpp
  10. 删除 DX12DescriptorTaskRingBufferUtils.hpp
  11. 删除 DX12DescriptorTlsBatchUtils.hpp
  12. 修改 CMakeLists.txt

Phase 5: 验证
  13. build_debug_x64.ps1 编译
  14. 功能验证（Mod/Dump/Hunt行为不变）
  15. 帧分析对比（tools/analyze_frame_log.py）
```

---

## 7. 回滚策略

ShadowUtils 内部通过编译宏 `BUNNY_USE_SHADOW_DESCRIPTOR_TRACKING` 控制：
- 定义宏 → 走新路径 (shadow array)
- 不定义 → fallback 到旧的 hash map 路径（保留旧代码但标记 deprecated）

验证通过后删除 fallback 代码。
