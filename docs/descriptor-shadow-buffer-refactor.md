# DX12 描述符追踪重构方案 v2

## 日期
2025-07-17

## 约束修正
- **禁止异步操作**: DX12 Hook 领域不允许异步，`DX12DescriptorCollectPipeline` 的 Starter/Worker/Queue 体系是死代码，必须删除
- **纯同步优化**: 所有优化必须在同步热路径上完成，通过数据结构替换降本增效

---

## 1. 当前架构问题

### 1.1 死代码
`DX12DescriptorCollectPipeline` (.hpp 291行 + .cpp 83行) 中:
- `Start()` / `Stop()` / `WorkerLoop()` / `EnqueueDescriptorTask()` 等 40+ 个方法全部为空实现
- `DX12DescriptorTaskRingBufferUtils` (MPSC无锁队列, 65536容量) — 无人使用
- `DX12DescriptorTlsBatchUtils` — 无人使用

这些代码编译进二进制，占用空间，且误导维护者。

### 1.2 性能瓶颈
`RecordRuntimeDescriptorCopyRange` (DX12ResourceTracker.cpp:1880):
- 每帧调用 **6,502次**，总耗时 **37,368 us (22.4%)**
- 每次在全局 `gDescriptorLock` 排他锁内执行:
  - hash map find x2 (`gRuntimeDescriptorByCpuHandle` + `gDescriptorByCpuHandle`)
  - hash map insert x1
  - DescriptorRecord clone + release (内存分配/释放)
  - BumpDescriptorQuerySerial (atomic CAS)

### 1.3 根因
架构用 `std::unordered_map<SIZE_T, DescriptorRecord>` 追踪每个描述符句柄的中间状态。Hash map 的 O(1) 常数过大，且每次操作伴随内存分配。

---

## 2. 重构方案: Shadow Heap Buffer + 范围标记

### 2.1 核心思路

**用 flat array 替代 hash map，用 heap offset 索引替代 hash key。**

```
当前 (Hash Map):
  gRuntimeDescriptorByCpuHandle[handle] = clone(record)
  → hash(SIZE_T) → bucket chain → alloc node → copy DescriptorRecord → insert

改为 (Shadow Buffer):
  shadow[ (handle - heapBase) / descriptorSize ] = record
  → 直接数组索引 O(1)，无 hash，无 alloc
```

### 2.2 数据结构设计

```cpp
class DX12DescriptorHeapShadowUtils {
public:
    struct HeapShadow {
        ID3D12DescriptorHeap* heap = nullptr;
        D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = {};
        UINT descriptorSize = 0;
        UINT capacity = 0;                     // Max descriptors in this heap
        std::vector<DescriptorRecord> records; // Flat array, indexed by offset
        std::vector<bool> valid;               // Which slots have meaningful data
        UINT64 lastUpdateSerial = 0;
    };

    // Called once per SetDescriptorHeaps — allocates/resizes shadow
    static void BindHeap(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_TYPE type);

    // Replaces RecordRuntimeDescriptorCopyRange — O(count) raw memcpy, no lock
    static void RecordCopyRange(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_CPU_DESCRIPTOR_HANDLE destStart,
        D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
        UINT count);

    // Replaces FindLatestDescriptorLocked — O(1) array index
    static bool ResolveDescriptor(
        D3D12_CPU_DESCRIPTOR_HANDLE handle,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        DescriptorRecord* outRecord);
};
```

### 2.3 同步数据流

```
SetDescriptorHeaps(cbvSrvHeap, samplerHeap):
  → DX12DescriptorHeapShadowUtils::BindHeap(cbvSrvHeap)   // alloc shadow array
  → DX12DescriptorHeapShadowUtils::BindHeap(samplerHeap)   // alloc shadow array
  → 全量 memcpy heap 内容到 shadow buffer (一次性 ~1-2ms)

CreateShaderResourceView(handle, resourceDesc):
  → DX12DescriptorHeapShadowUtils::WriteSlot(handle, record)
  → records[(handle-cpuBase)/descriptorSize] = record       // O(1) array write
  → valid[(handle-cpuBase)/descriptorSize] = true
  → BumpDescriptorQuerySerial()

CopyDescriptors(dest, src, count):
  → DX12DescriptorHeapShadowUtils::RecordCopyRange(dest, src, count)
  → for i in 0..count:
      records[dest_offset+i] = records[src_offset+i]        // O(count) array copy
      valid[dest_offset+i] = valid[src_offset+i]
  → BumpDescriptorQuerySerial()                             // 仅一次 bump

Draw/Dispatch 查询 (Mod/Hunt/Dump):
  → DX12DescriptorHeapShadowUtils::ResolveDescriptor(handle, &out)
  → offset = (handle - cpuBase) / descriptorSize
  → if valid[offset]: out = records[offset]                  // O(1) array read
```

### 2.4 锁策略变化

| 操作 | 当前锁 | 重构后锁 |
|------|--------|----------|
| Create*View (RecordDescriptorMerged) | gDescriptorLock 排他 | heap-shadow 级别 SRWLOCK 共享/排他 |
| CopyDescriptors (RecordRuntimeCopyRange) | gDescriptorLock 排他 | 无需锁（array copy，无 alloc） |
| Query (FindLatestDescriptorLocked) | gDescriptorLock 共享 | 无需锁或 per-heap 共享锁 |
| BumpDescriptorQuerySerial | gDescriptorLock 保护下 | atomic CAS，无需锁 |

---

## 3. 删除清单

| 文件 | 操作 | 原因 |
|------|------|------|
| `descriptor/DX12DescriptorCollectPipeline.hpp` (291行) | **删除** | 死代码，异步Pipeline空壳 |
| `descriptor/DX12DescriptorCollectPipeline.cpp` (83行) | **删除** | 死代码 |
| `utils/DX12DescriptorTaskRingBufferUtils.hpp` | **删除** | 仅被 Pipeline 引用，无人使用 |
| `utils/DX12DescriptorTlsBatchUtils.hpp` | **删除** | 仅被 Pipeline 引用，无人使用 |
| `resource/DX12ResourceTrackerHooks.cpp` | **修改** | 直接调用 ShadowUtils 替代 Pipeline::RecordDescriptor |
| `resource/DX12ResourceTracker.cpp` | **修改** | 用 ShadowUtils 替代 RecordRuntimeDescriptorCopyRange 全局函数 |
| `resource/DX12ResourceTrackerPrivate.hpp` | **修改** | 移除 Pipeline 相关声明，新增 Shadow 数据结构 |
| CMakeLists.txt / vcxproj | **修改** | 移除已删除文件的编译条目 |

---

## 4. 兼容性分析

| 子系统 | Hash算法 | 查询接口 | 数据内容 | 兼容性 |
|--------|----------|----------|----------|--------|
| **Mod** | 不变 | `FindLatestDescriptorLocked` → `ShadowUtils::ResolveDescriptor` | DescriptorRecord 结构体字段不变 | ✅ |
| **Dump** | 不变 | `FindDescriptorSummaryByCpuHandle*` 系列 | 返回值不变 | ✅ |
| **Hunt** | 不变 | 同上，Hash 输入不变 | DescriptorRecord.valueId 计算不变 | ✅ |
| **PreSkin** | 不变 | 走独立锁路径 `gModLock`/`gPreSkinLock` | 不受影响 | ✅ |

## 5. 量化预期

| 指标 | 优化前 | 优化后 | 降幅 |
|------|--------|--------|------|
| record_runtime_copy_range 每帧耗时 | 37,368 us | ~2,000 us | **降低 94.6%** |
| 每帧 hash map 操作次数 | ~19,000 | 0 | **消除 100%** |
| 全局锁竞争 | gDescriptorLock 排他锁 6,502次/帧 | 无全局锁 | **消除 100%** |
| DescriptorRecord 内存分配/释放 | ~13,000次/帧 | 0次（array 复用） | **消除 100%** |
| API scope CPU 占比 | 30.6% (51ms) | ~10% (16ms) | **降低 67%** |
| 预期 FPS | 4.6 FPS | 7-9 FPS | **提升 52-96%** |

## 6. 实施步骤

1. **创建 `DX12DescriptorHeapShadowUtils.hpp`** — 新的描述符追踪工具类
2. **修改 `DX12ResourceTrackerHooks.cpp`** — SetDescriptorHeaps hook 调用 BindHeap
3. **修改 `DX12ResourceTracker.cpp`** — RecordDescriptorMerged 和 RecordRuntimeDescriptorCopyRange 改用 ShadowUtils
4. **修改查询路径** — FindLatestDescriptorLocked 等改用 ShadowUtils::ResolveDescriptor
5. **删除死代码** — DX12DescriptorCollectPipeline + RingBuffer + TlsBatch
6. **编译验证** — `build_debug_x64.ps1`
7. **功能验证** — 日志确认 Mod/Dump/Hunt 行为不变
8. **性能验证** — 对比优化前后帧分析报告
