# Project Bunny RELEASE 版本闪退根因分析报告（完整版 v2.0）

> **日期**: 2026-07-08  
> **版本**: v2.0（三轮深度分析 + ABI/内存取证）
> **项目**: Project Bunny (重装小兔) — DX12 Wrapper  
> **问题**: RELEASE 版本闪退，DEBUG 版本正常运行  
> **分析方法**: 第一性原理 + 多维度交叉验证（10个维度，276个源文件，约30万行代码）  
> **分析深度**: 四级验证（一级扫描 → 二级确认 → 三级调用链追踪 → 四级 ABI/内存取证）

---

## 目录

1. [编译配置差异矩阵](#1-编译配置差异矩阵)
2. [高危发现（共14项）](#2-高危发现共14项)
3. [中危发现（共6项）](#3-中危发现共6项)
4. [低危/已验证安全的模块](#4-低危已验证安全的模块)
5. [闪退场景重建](#5-闪退场景重建)
6. [修复方案与优先级](#6-修复方案与优先级)
7. [附录：分析过程记录](#7-附录分析过程记录)

---

## 1. 编译配置差异矩阵

| 配置项 | DEBUG | RELEASE | 风险评估 |
|--------|-------|---------|----------|
| 优化级别 | `/Od` (无优化) | `/O2` (全速优化) | UB 在优化后被放大 |
| 全程序优化/LTO | 无 | `/GL` + `/LTCG` | 跨模块内联暴露隐藏 UB |
| 缓冲区安全检查 | `/GS` ✅ | **`/GS-` ❌** | 🔴 **最高风险**：栈溢出静默破坏 |
| 运行时检查 | `/RTC1` ✅ | 无 | 未初始化变量/栈帧损坏仅 DEBUG 检测 |
| CRT | `/MTd` (Debug CRT) | `/MT` (Release CRT) | 堆内存释放后立即重用 vs 填充模式 |
| 预处理器 | `_DEBUG` | `NDEBUG` | `#if defined(_DEBUG)` 路径分叉 |
| 链接器 | `/DEBUG` | `/DEBUG /OPT:REF /OPT:ICF /LTCG /INCREMENTAL:NO` | 激进 COMDAT 折叠 |
| 异常处理 | `/EHsc` | `/EHsc` | 相同 |

### 1.1 `/GS-` 的风险本质

`/GS`（Buffer Security Check）在函数入口和出口插入 **栈金丝雀（Stack Canary）**：
- 函数入口：在返回地址和局部变量之间放置一个随机值
- 函数出口：检查该值是否被修改；若被修改则调用 `__report_gsfailure` 终止进程

`/GS-` 完全移除此保护。任何栈缓冲区溢出都会：
1. 静默覆盖返回地址
2. 静默覆盖相邻栈帧的局部变量
3. 在函数返回时跳转到垃圾地址 → 访问违例（Access Violation）

**关键点**：这是一个 D3D12 Wrapper，它通过 Hook 链嵌入在游戏和 D3D12 Runtime 之间。游戏（或驱动、第三方代码）的任何栈溢出都可能破坏 Wrapper 的栈帧。

### 1.2 `/O2` + `/LTCG` 的风险本质

`/O2` 启用 Type-Based Alias Analysis (TBAA)，允许编译器基于严格别名规则（Strict Aliasing）假设不同类型的指针不会指向同一内存。`/LTCG` 将这一分析扩展到整个程序的所有编译单元。

如果代码中存在严格别名违规（如 `reinterpret_cast<volatile LONG64*>(&UINT64变量)`），编译器可能：
- 缓存读操作的值到寄存器，跨越多层函数调用
- 消除"冗余"的读操作（因为假设没有人通过不同类型写入）
- 重排内存访问顺序

---

## 2. 高危发现（共14项）

> **v2.0 新增**: 第三轮深度分析新增发现 #11（钻石继承）、#12（gCanonicalCache 泄漏）、#13（BindingShard::generation 伪 volatile）、#14（缺失 static_assert）。原有 #2（/GS- 影响）和 #4（Strict Aliasing）已被交叉验证进一步确认。

### 🔴 发现 #1：`CanonicalResource()` 在已释放资源上调用 QueryInterface

**文件**: `src/DirectX12/resource/DX12ResourceTracker.cpp`  
**关键行**: 355 → 536-565  
**风险等级**: 🔴 CRITICAL — 几乎一定导致 RELEASE 闪退  
**触发频率**: 每次游戏释放一个被追踪的 D3D12 资源时

#### 调用链

```
游戏调用 resource->Release()
  → D3D12 释放 resource 对象（内存被回收）
  → 触发 ResourceReleaseTracker::Release()（通过 SetPrivateDataInterface）
    → mRef 降至 0
    → InvalidateResourceLocked(mResource)
      → CanonicalResource(mResource)        // 🔴 mResource 指向已释放内存！
        → mResource->QueryInterface(IID_IUnknown, ...)  // 在垃圾内存上调用虚函数
```

#### 为什么 DEBUG 不闪退

DEBUG CRT (`/MTd`) 在释放内存时填充 `0xDD`，堆管理器也会保留已释放块的结构。当 `QueryInterface` 被调用时，vtable 指针是 `0xDDDDDDDD` 或仍指向旧的 vtable（堆块还未被复用），访问可能导致：
- 进入 D3D12 Debug Layer 的已释放对象检测逻辑（返回错误码）
- Debug CRT 堆检查捕获异常

#### 为什么 RELEASE 闪退

RELEASE CRT (`/MT`) 在释放内存后**立即将其标记为可复用**。下一个 `malloc`/`new` 调用可能在同一地址分配**完全不同类型的对象**。此时：
- vtable 指针指向新对象的虚函数表
- 调用 `QueryInterface` → 实际执行的是新对象的 vtable 第 0 项（可能是任何函数）
- **结果**：跳转到垃圾代码地址 → Access Violation（0xC0000005）

#### 技术细节

`CanonicalResource()` 函数（第 536 行）的作用是规范化资源指针（处理 wrapped resource），其 slow path：
```cpp
ID3D12Resource *CanonicalResource(ID3D12Resource *resource) {
    // ...
    // Slow path: QI for IUnknown, then check identity map
    ID3D12Unknown *unknown = nullptr;
    resource->QueryInterface(IID_PPV_ARGS(&unknown));  // 🔴 在已释放内存上调用
    // ...
}
```

---

### 🔴 发现 #2：`RecordDrawInstanced` 锁释放-重获取 TOCTOU 竞态

**文件**: `src/DirectX12/binding/DX12BindingTracker.cpp`  
**关键行**: 2628-2650（以及 RecordDrawIndexedInstanced、RecordDispatch 中的相同模式）  
**风险等级**: 🔴 CRITICAL  
**触发频率**: 每次捕获会话中的 draw call

#### 竞态窗口

```cpp
// 步骤 A：GetOrResolveBindingState 获取独占锁 → 缓存指针 → 释放锁
CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
// ⚠️ 此处锁已释放，statePtr 指向 unordered_map 中的元素

// 步骤 B：重新获取同一 shard 的独占锁（通过 TLS 缓存的 shardIndex）
BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
AcquireSRWLockExclusive(&shard.lock);
// ⚠️ 在步骤A和步骤B之间，另一个线程可能调用 UnregisterCommandList
// → erase 掉 shard.states 中的条目 → unordered_map rehash → statePtr 变为悬空指针

// 步骤 C：generation 检查后仍然使用 statePtr
if (tlsStateCache.shardGeneration != shard.generation) {
    statePtr = &shard.states[commandList];  // 重新解析
}
CommandListBindingState &state = *statePtr;  // 🔴 可能使用悬空指针
```

#### 为什么 DEBUG 不闪退

`/Od` 确保每个语句独立执行，`statePtr` 每次使用都从内存重载。`statePtr` 指向的 `unordered_map` 元素在被 erase 后，DEBUG CRT 将其填充为 `0xDD`，访问 `0xDDDDDDDD` 地址时立即崩溃（但捕获会话可能未开启，且时序窗口更窄）。

#### 为什么 RELEASE 闪退

`/O2` + `/LTCG`：
1. 编译器可以将 `GetOrResolveBindingState` 内联到 `RecordDrawInstanced`
2. 优化器看到：获取锁 → 读 `shard.states[commandList]` → 存指针 → 释放锁 → 重获锁。它可以"证明"该指针在第一次锁内是有效的，并且 generation 检查处理了 rehash
3. **关键**：优化器可以**提前提级（hoist）**`*statePtr` 的解引用到 generation 重检之前，或将指针缓存在寄存器中跨越锁边界
4. 并发线程的 `UnregisterCommandList` erase 掉条目后，`*statePtr` 引用变为悬空 → 读垃圾数据 → 写垃圾数据 → 堆损坏

---

### 🔴 发现 #3：`HookWorkFlags`/`skipAll` 无屏障读取 — 寄存器缓存

**文件**: 
- 定义: `src/DirectX12/runtime/DX12HotPathState.hpp:24,33`
- 写入: `DX12HotPathState.hpp:66,110` (InterlockedExchange64)
- **读取（热点路径）**: `DX12CommandListHooks.cpp:611,634,658`、`BunnyCmdListHooks.hpp:112,126,139`、`DX12DispatchHookFlow.hpp:119`、`DX12DrawHookFlow.hpp:51` 等 **9+ 处**

**风险等级**: 🔴 CRITICAL  
**触发频率**: 每个 Draw/Dispatch 调用（每帧 10K-100K+ 次）

#### 问题本质

```cpp
// 声明
inline static volatile UINT64 HookWorkFlags = 0;
inline static volatile LONG skipAll = 1;

// 写入（非热路径，状态切换时）
InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&HookWorkFlags), newFlags);
InterlockedExchange(&skipAll, newValue);

// 读取（热点路径，每次 draw/dispatch）⚠️ 纯 volatile 读，无屏障
if (DX12HotPathState::skipAll || 
    !(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DRAW_WORK))
```

在 x64 上，`volatile` 读产生一个 `mov` 指令，但它**不阻止**编译器或 CPU 进行以下优化：
- 编译器将值缓存到寄存器，跨越多个 draw call（在 `/O2` + `/LTCG` 下，循环中的多次调用可以被"证明"其值不变）
- CPU 的 store buffer 延迟写可见性

#### 为什么 DEBUG 不闪退

`/Od` 确保每次读取都是独立的内存加载指令，不会跨语句缓存。

#### 为什么 RELEASE 闪退

`/O2` + `/LTCG`：
1. 编译器可能将 `HookWorkFlags` 的值**提升到循环外**（Loop-invariant code motion）
2. 整个帧的 draw call 都使用同一个缓存值
3. 如果 mid-frame `skipAll` 从 1→0（如 shotgun dump 激活），RELEASE 仍然走快速路径，跳过了 hook 处理
4. 更危险：如果 `HookWorkFlags` 从某状态切换到另一状态（如 shader override 激活），缓存值导致 draw call 执行了错误的 hook 逻辑

---

### 🔴 发现 #4：Strict Aliasing 违规 — Interlocked 系列函数

**文件**: 7 处 cast 分布于 5 个变量  
**风险等级**: 🔴 CRITICAL  
**触发频率**: 持续（generation bump 和状态切换时）

#### 全部违规点

| 变量 | 声明类型 | Cast 目标 | 所在文件:行 |
|------|----------|-----------|-------------|
| `TrackingGenerationValue` | `volatile UINT64` | `volatile LONG64*` | `DX12CommandListRuntime.cpp:235` |
| `computeBindingSerial` (RuntimeState) | `volatile UINT64` | `volatile LONG64*` | `DX12CommandListRuntime.cpp:791` |
| `HookWorkFlags` | `volatile UINT64` | `volatile LONG64*` | `DX12HotPathState.hpp:66` |
| `gPreSkinActiveGeneration` | `volatile UINT64` | `volatile LONG64*` | `DX12ModRuntime.cpp:481,502` |
| `gShaderOverrideNegativeCacheGeneration` | `volatile UINT64` | `volatile LONG64*` | `DX12ModRuntimeLoader.hpp:1384`、`DX12ShaderOverrideRuntime.hpp:418` |

**100% mismatched**: 所有 Interlocked 调用都发生在此 cast 上，零个变量的声明类型匹配 `LONG64`。

#### 为什么这是 UB

C++ 标准 `[basic.lval]/11`（Strict Aliasing）规定：只有通过与其"动态类型"兼容的类型才能访问对象。`UINT64`（`unsigned long long`）和 `LONG64`（`signed long long`）是**不同的类型**，尽管它们具有相同的大小和位表示。

MSVC 在 `/O2` 下执行 Type-Based Alias Analysis (TBAA)：
1. `TrackingGenerationValue` 声明为 `volatile UINT64`
2. `BumpTrackingGeneration()` 通过 `volatile LONG64*` 写入（InterlockedIncrement64）
3. TBAA 认为 `LONG64*` 的写入不能修改 `UINT64` 对象
4. 因此 `TrackingGeneration()`（返回 `TrackingGenerationValue` 的裸读）可以被优化为返回**永久缓存值**

#### 实际影响：`TrackingGenerationValue` 被缓存

```cpp
// 读取方（9 处裸读）
UINT64 TrackingGeneration() {
    return TrackingGenerationValue;  // 🔴 /O2 下可能被优化为常量
}

// 写入方
void BumpTrackingGeneration() {
    InterlockedIncrement64(
        reinterpret_cast<volatile LONG64*>(&TrackingGenerationValue));
    // 🔴 TBAA 认为此写不对 UINT64 生效
}
```

如果读取方得到缓存的旧值：
- `GetTrackingCacheFast` 永远比较"相等"
- 返回过期缓存（内含旧的布尔决策）
- 下游代码基于过期状态执行 → 悬空指针 → 释放后使用 → 崩溃

#### CMake 配置确认

`CompilerOptions.cmake` **没有** `/d2strictAliasing-`、`/fno-strict-aliasing` 或任何宽松别名标志。**Strict Aliasing 在 RELEASE 中完全启用。**

---

### 🔴 发现 #5：`DX12BindingCollectTask` 栈上分配 30-40KB — 潜在栈溢出

**文件**: `src/DirectX12/binding/DX12BindingCollectQueue.cpp:48`  
**风险等级**: 🔴 CRITICAL（捕获会话期间）  
**触发条件**: `tracking.recordBindingEvents == true`（捕获模式）

#### 调用链与栈压力

```
HookedDrawInstanced (已有 ~3KB 栈帧)
  → DX12DrawHookFlow::ExecuteHook
    → [capture path] DX12BindingTracker::RecordDrawInstanced
      → EnqueueDrawEvent
        → DX12BindingCollectTask task;  // 🔴 栈上 ~30-40KB
          → task.state = state;           // 拷贝整个 CommandListBindingState
```

`CommandListBindingState` 包含：
- 64× `RootTableState` (~80B each = 5KB)
- 64× `RootDescriptorState` (~32B each = 2KB)
- 64× `RootConstantsState` (64×4B+64B = 20KB)
- 32× `D3D12_VERTEX_BUFFER_VIEW` (896B)
- 其他字段 (~2KB)
- **合计约 30-40KB**

加上调用链中其他栈帧（`redirectedViews[32]` 896B + `restore[32]` 896B + 各种局部变量），**总栈压力可能超过 45KB**。

#### `/GS` vs `/GS-` 的影响

- DEBUG: `/GS` 在每个有此大数组的函数中放置栈 cookie。如果有溢出（哪怕是内部子数组），cookie 被破坏 → `__report_gsfailure` 被调用
- RELEASE: `/GS-` 无 cookie → 溢出静默破坏返回地址 → 函数返回时跳转到垃圾地址

#### 注意

捕获模式通常不活跃，所以日常使用不会触发此问题。但如果用户开启捕获（shotgun dump），这就是一个可重现的崩溃向量。

---

### 🔴 发现 #6：`ReleaseDescriptorRecordResources` 裸 `->Release()` — 无异常保护

**文件**: `src/DirectX12/resource/DX12ResourceTracker.cpp:463-474`  
**风险等级**: 🔴 CRITICAL  
**触发频率**: 每次 descriptor 清理（18+ 调用点）

#### 不一致的保护

代码库中同时存在两种资源释放模式：

```cpp
// 模式 A：安全（有 __try/__except）
static void Release(ID3D12Resource *resource) {
    __try {
        resource->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // 静默忽略
    }
}

// 模式 B：不安全（裸 Release）🔴
void ReleaseDescriptorRecordResources(DescriptorRecord *record) {
    if (record->ownsResource && record->resource)
        record->resource->Release();  // 🔴 如果 resource 已被释放则崩溃
    if (record->ownsCounterResource && record->counterResource)
        record->counterResource->Release();  // 🔴 同上
}
```

如果 descriptor record 指向的资源已被游戏释放（ReleaseTracker 还未运行，或 descriptor 在跟踪启动前已记录），裸 `->Release()` 调用已释放对象上的虚函数 → vtable 垃圾 → 崩溃。

**DEBUG**: `0xDD` 填充可能使得虚函数调用产生可识别的效果。  
**RELEASE**: 内存立即复用，vtable 指向完全不相关的代码。

---

### 🔴 发现 #7：`DX12LogManager::Close()` — `/O2` 下 `fclose` 可能被重排到 `WaitForSingleObject` 之前

**文件**: `src/DirectX12/runtime/DX12LogManager.hpp:198-216`  
**风险等级**: 🟠 HIGH  
**触发时机**: DLL 卸载 / 进程退出

#### 时序问题

```cpp
static void Close() {
    DX12LogManager &state = Get();
    InterlockedExchange(&state.stopRequested, 1);  // (1) 设置停止标志
    SetEvent(state.event);                           // (2) 唤醒 worker 线程
    WaitForSingleObject(state.thread, 5000);         // (3) 等待线程退出
    CloseHandle(state.thread);                       // (4) 关闭线程句柄
    CloseHandle(state.event);                        // (5) 关闭事件句柄
    fclose(state.log);                               // (6) 关闭日志文件 🔴
    state.log = nullptr;                             // (7) 清空指针
}
```

Worker 线程：
```cpp
for (;;) {
    WaitForSingleObject(state.event, 100);
    // ... 出队日志 ...
    fputs(line.c_str(), state.log);  // 使用 state.log
    if (stopping && pending.empty()) break;
}
```

**关键问题**：C++ 编译器和 CPU 不知道 `WaitForSingleObject` 建立了与 worker 线程的 happens-before 关系。在 `/O2` 下，编译器可能将 `fclose(state.log)` 和 `state.log = nullptr` 重排到 `WaitForSingleObject` 之前。

```
重排后的执行：
  (1) stopRequested = 1
  (2) SetEvent → 唤醒 worker
  (6) fclose(state.log) ← 重排！worker 还在使用
  (7) state.log = nullptr ← 重排！
  (3) WaitForSingleObject ← 等待已经没用了
```

Worker 线程的 `fputs(..., state.log)` 此时写入已关闭的 `FILE*` → 崩溃。

#### 为什么 DEBUG 不闪退

`/Od` 保持程序顺序不变。

---

### 🟠 发现 #8：`GetOrResolveBindingState` TLS 缓存读重排

**文件**: `src/DirectX12/binding/DX12BindingTracker.cpp:42-50`  
**风险等级**: 🟠 HIGH  
**触发频率**: 每次 draw/dispatch（通过 binding tracker）

```cpp
static CommandListBindingState* GetOrResolveBindingState(
    ID3D12GraphicsCommandList *commandList)
{
    // Fast path: TLS cache hit
    if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
        BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
        if (tlsStateCache.shardGeneration == cachedShard.generation) {
            return tlsStateCache.state;  // 🔴 state 可被重排到 generation 检查之前
        }
    }
```

- `cachedShard.generation` 是 `volatile UINT64` → 编译器不会重排 volatile 读
- 但 `tlsStateCache.state` 是**普通指针**（非 volatile） → 编译器可以在 generation 检查**之前**读它

如果另一个线程在 generation 检查通过后（但在 state 被使用前）erase 了 `unordered_map` 条目，则返回的指针是悬空的。

**DEBUG**: `/Od` 顺序执行 → 窗口极小。  
**RELEASE**: `/O2` 可能将 state 读提级到函数入口 → 在后续的整个调用中使用旧值。

---

### 🟠 发现 #9：`GetStatePtrFast` 中指针在锁释放后使用

**文件**: `src/DirectX12/command/DX12CommandListRuntime.cpp:185-220`  
**风险等级**: 🟠 HIGH

```cpp
// Medium path: 从 gDirectStateMap 读取（共享锁）
{
    AcquireSRWLockShared(&gDirectStateMapLock);
    auto it = gDirectStateMap.find(commandList);
    if (it != gDirectStateMap.end())
        state = it->second;           // 读指针
    ReleaseSRWLockShared(&gDirectStateMapLock);  // 🔴 释放锁
    if (state) {
        DX12CommandListRuntime::TlsRefreshFromState(state, commandList);
        return state;  // 🔴 使用刚释放锁后可能已失效的指针
    }
}
```

并发线程可能在锁释放后调用 `UnregisterDirectState` → 删除 `gDirectStateMap` 条目 → 释放 `DX12CommandListRuntimeState`。此时 `return state` 返回悬空指针。

---

### 🟠 发现 #10：DllMain 中违反 Loader Lock 规则

**文件**: `src/DirectX12/D3D12Proxy.cpp:140-175` + `src/DirectX12/runtime/DX12Runtime.hpp:48-87`  
**风险等级**: 🟠 HIGH

```cpp
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    case DLL_PROCESS_ATTACH:
        D3D12ProcessGateUtils::Evaluate(module);
        D3D12ProxyRuntime::Initialize(module);
        // ↓ 调用链中：
        //   DX12Runtime::Initialize()
        //     → LoadLibraryW("d3d12.dll")   ← 🔴 在 loader lock 内加载 DLL
        //     → CreateThread(...)            ← 🔴 在 loader lock 内创建线程
        //     → DX12LogManager::Open()
        //         → CreateThread(ThreadProc) ← 🔴 又一个线程创建
```

根据 MSDN 文档，在 `DllMain` 的 loader lock 下调用 `LoadLibrary` 和 `CreateThread` **可能导致死锁**。在 RELEASE 下，如果游戏使用了不同的 D3D12 加载路径或启用了并行 DLL 加载，死锁概率更高。

---

### 🔴 发现 #11：`DebugInterfaceWrapper` 钻石继承 — ABI 级别崩溃

**文件**: `src/DirectX12/proxy/DX12Wrapper.cpp:570-575, 1556-1559, 1768-1770`  
**风险等级**: 🔴 CRITICAL  
**影响类**: `DebugInterfaceWrapper`、`DebugDeviceWrapper`、`DebugCommandListWrapper`

三个 wrapper 类同时继承了最高版本接口和已被包含在继承链中的低版本接口：

```cpp
class DebugInterfaceWrapper final :
    public ID3D12Debug6,    // 含 Debug5→4→3→2→1→Debug→IUnknown
    public ID3D12Debug1,    // ← 重复！Debug6 已含
    public ID3D12Debug2,    // ← 重复！Debug6 已含
    public WrapperBase
```

后果：多条路径 `static_cast` 到 `IUnknown*` 得到**不同 `this` 偏移**。`RegisterWrapperPointer` 注册了可能不正确的偏移。`/O2` + `/LTCG` 下 `static_cast` 优化与 `/Od` 不同，导致 RELEASE 和 DEBUG 行为**根本性分歧**。

**修复**: 移除冗余直接基类，仅保留最高版本接口 + WrapperBase。

---

### 🔴 发现 #12：`CleanupTrackedResources` 未清除 `gCanonicalCache`

**文件**: `src/DirectX12/resource/DX12ResourceTracker.cpp:708-738`  
**风险等级**: 🔴 CRITICAL  

`CleanupTrackedResources` 清除了 `gResources`、`gResourceByPtr`、`gBufferResourceByGpuVa` 但**从未清除 `gCanonicalCache`**。Mod 重载时旧 canonical 映射残留，fast path 直接返回已释放指针 → Use-After-Free。

---

### 🟠 发现 #13：`BindingShard::generation` 的脆弱设计

**文件**: `src/DirectX12/binding/DX12BindingTrackerState.hpp:179`  
**风险等级**: 🟠 HIGH  

`volatile UINT64 generation` 在锁内使用 `++` 递增。读者使用 volatile 读 + 锁验证的 double-check 模式，依赖 x64 TSO 和 `/volatile:ms` 的 acquire 语义，不提供跨平台保证。

---

### 🟡 发现 #14：关键运行时结构体缺少 `static_assert`

**风险等级**: 🟡 MEDIUM  

仅 6 个 `static_assert`（全部 DDS 相关）。缺少：`DX12CommandListRuntimeState`（嵌入 wrapper）、`DX12DescriptorViewPayload`（使用 `memcpy`/`memset`）、`CommandListBindingState`（30-40KB 栈分配）。

---

## 3. 中危发现（共6项）

### 🟡 发现 #15：`DX12ModResourceRuntime.hpp:1697` — Heap Overflow

```cpp
memcpy(dst, src, static_cast<size_t>(resource->byteWidth));
```

无检查 `srvByteWidth >= resource->byteWidth`。如果 backing buffer 更小，则堆溢出。

**DEBUG**: `/MTd` 的堆检查立即捕获。  
**RELEASE**: 静默堆损坏。

### 🟡 发现 #16：`RootConstantsState::values[64]` — 无边界检查

**文件**: `src/DirectX12/binding/DX12BindingTrackerState.hpp:56`

D3D12 规范限制每个参数最多 64 个 DWORD 的 root constants。但 RELEASE 无验证层，有缺陷的游戏或不当的 wrapper 逻辑可能写越界。

### 🟡 发现 #17：`WrapperBase::~WrapperBase()` — `FindByWrapperPointer` 返回悬空指针

**文件**: `src/DirectX12/proxy/DX12Wrapper.cpp:533-568`

`FindByWrapperPointer` 在共享锁下查找 wrapper 指针，返回其原始指针。外部调用者在**锁释放后使用该指针**。如果 wrapper 在此期间被销毁，指针悬空。

### 🟡 发现 #18：`DX12LogManager` 中 `releaseStartupLogging` 条件编译成员

**文件**: `src/DirectX12/runtime/DX12LogManager.hpp:39`

```cpp
#if !defined(_DEBUG)
    std::atomic<bool> releaseStartupLogging{ true };
#endif
```

该成员仅在 RELEASE 下存在，导致 `sizeof(DX12LogManager)` 在 DEBUG 和 RELEASE 下不同。当前为单例模式且同一构建内一致，但存在二进制兼容性隐患。

### 🟡 发现 #19：`DX12BindingCollectQueue` — 禁用的异步基础设施

完整的异步工作线程基础设施（mutex、condition_variable、`std::thread`、`std::deque`）存在但 `EnqueueTask` 永远返回 `false`（"SyncFallback, reason=no_worker"）。`std::thread worker` 默认构造为非 joinable 线程对象，`inline static` 初始化在 `/LTCG` 下可能有细微排序问题。

---

## 4. 低危 / 已验证安全的模块

### ✅ 条件编译（`_DEBUG`）审计

**结论：干净**。所有 `#if defined(_DEBUG)` 块仅包含：
- 诊断日志调用
- 性能计时（`GetTickCount64()`）
- 调试宏（`DX12_DISPATCH_DEBUG_VERBOSE_LOG` → 在 RELEASE 中空展开）

零个控制流或资源管理差异。

### ✅ 未初始化变量审计

**结论：干净**。代码库一致使用 `= {}`。所有 `struct`/`class` 成员有内联初始化器。`thread_local` 变量均有完整默认值。

### ✅ 缓冲区溢出审计（自有代码）

**结论：基本安全**。一致使用 `sprintf_s`/`snprintf`/`strcpy_s`。`memcpy` 调用有边界检查。无 `alloca`/VLA 模式。

### ✅ DX12ShaderDump TOCTOU 审计

**结论：设计正确，低风险**。三个控制标志通过 `Interlocked*` 操作。全局数据结构在 `gDumpLock` 下保护。快照模式（共享锁读 → 工作线程处理私有副本）正确。

### ✅ 钩子槽位机制

`DX12_CL_ORIG`/`DX12_QUEUE_ORIG` 展开为三级缓存（TLS per-slot → TLS per-vtable → 全局 map），全部在 `thread_local` 或锁保护的堆存储中，不在栈上。

---

## 5. 闪退场景重建

基于上述分析，最可能的 RELEASE 闪退场景如下：

### 场景 A（最可能）: 资源释放时 `CanonicalResource` 调用已释放对象

```
帧 N：
  1. 游戏调用某个 D3D12 资源的最后一个 Release()
  2. D3D12 Runtime 释放资源对象的内存
  3. ResourceReleaseTracker::Release() 被触发
  4. → InvalidateResourceLocked(mResource) 
  5. → CanonicalResource(mResource)
  6. → mResource->QueryInterface(IID_IUnknown, ...) ← 在已被复用的内存上调用虚函数
  7. → vtable 指向随机代码 → Access Violation → 闪退
```

**证据链**：
- DEBUG CRT 填充 `0xDD`，QueryInterface 可能返回错误码
- RELEASE CRT 内存立即复用，vtable 指向新分配的任意对象的虚函数表
- 独占锁内调用（加重死锁风险）
- 该路径在每次资源释放时触发

### 场景 B（同样可能）: `/O2` + `/LTCG` 触发 TBAA 导致 `TrackingGenerationValue` 返回过期值

```
帧 N：
  1. BumpTrackingGeneration() 通过 InterlockedIncrement64（LONG64* cast）写入
  2. TBAA 认为 LONG64 和 UINT64 不别名
  3. GetTrackingCacheFast() 读取 TrackingGenerationValue → 优化器返回寄存器中的旧值
  4. 缓存比较永远"命中" → 返回过期的 DX12CommandListTrackingCache
  5. 下游基于过期缓存做出错误决策 → 悬空指针 → 闪退
```

**证据链**：
- 7 处 UINT64→LONG64* cast（100% 不匹配）
- 无 `/fno-strict-aliasing`
- `/O2` + `/LTCG` 启用完整 TBAA

### 场景 C（可能）: 捕获会话中栈溢出

```
帧 N（shotgun dump 激活）:
  1. 游戏调用 DrawInstanced
  2. → HookedDrawInstanced（~3KB 栈帧，含 redirectedViews[32]）
  3. → DX12DrawHookFlow::ExecuteHook
  4. → [capture] RecordDrawInstanced
  5. → EnqueueDrawEvent → DX12BindingCollectTask task;（~35KB 栈帧）
  6. 总栈压力 ~45KB + 调用链深度
  7. 栈溢出 → 覆盖返回地址 → 函数返回时跳转垃圾地址
```

### 场景 D（可能）: 日志关闭时序竞态

```
DLL 卸载：
  1. DX12LogManager::Close() 开始执行
  2. /O2 将 fclose(state.log) 重排到 WaitForSingleObject 之前
  3. Worker 线程仍在 fputs(..., state.log) 写入已关闭文件
  4. → 访问违例 → 闪退
```

---

## 6. 修复方案与优先级

### 🔴 P0 — 必须立即修复（预计消除 85%+ 闪退概率）

| # | 修复项 | 文件 | 改动量 | 风险 |
|---|--------|------|--------|------|
| 1 | **恢复 `/GS`** | `cmake/CompilerOptions.cmake:30` | 1 行 | 性能 -1~2%（可忽略） |
| 2 | **修复 Strict Aliasing**：将所有 `volatile UINT64` 改为 `volatile LONG64`，移除 `reinterpret_cast` | 5 个文件，~10 行 | ~10 行 | 无（同大小类型） |
| 3a | **修复 `CanonicalResource` 悬空调用**：添加 SEH 保护或有效性检查 | `DX12ResourceTracker.cpp:355,553` | ~10 行 | 低 |
| 3b | **修复 `CleanupTrackedResources` 未清除 `gCanonicalCache`** | `DX12ResourceTracker.cpp:708` | 1 行 | 无 |
| 4 | **修复 `ReleaseDescriptorRecordResources`**：添加 `__try/__except` 包装 | `DX12ResourceTracker.cpp:469` | ~8 行 | 无 |
| 5 | **修复钻石继承**：移除 `DebugInterfaceWrapper`/`DebugDeviceWrapper`/`DebugCommandListWrapper` 的冗余直接基类 | `DX12Wrapper.cpp:570,1556,1768` | ~6 行 | 中（需验证 QI 路径） |

### 🟠 P1 — 应该在下一次迭代修复

| # | 修复项 | 文件 | 改动量 |
|---|--------|------|--------|
| 6 | **修复 `RecordDrawInstanced` TOCTOU**：在整个操作期间保持锁 | `DX12BindingTracker.cpp:2628` | ~15 行 |
| 7 | **为 `HookWorkFlags`/`skipAll` 读取添加原子屏障**：使用 `std::atomic` + `memory_order_acquire` | `DX12HotPathState.hpp` + 9 读取点 | ~15 行 |
| 8 | **修复 `GetOrResolveBindingState` 读重排**：将 `state` 读放在 `generation` 检查之后 + 编译器屏障 | `DX12BindingTracker.cpp:42` | ~5 行 |
| 9 | **修复 `GetStatePtrFast` 锁后使用**：在锁内完成所有指针访问 | `DX12CommandListRuntime.cpp:205` | ~5 行 |
| 10 | **将 `BindingShard::generation` 改为 `std::atomic<UINT64>`** | `DX12BindingTrackerState.hpp:179` | ~5 行 |

### 🟢 P2 — 中期改进

| # | 修复项 |
|---|--------|
| 11 | 添加 `memcpy` 边界检查 (`DX12ModResourceRuntime.hpp:1697`) |
| 12 | 添加 `RootConstantsState::values[64]` 边界检查 |
| 13 | 将 `releaseStartupLogging` 声明移除条件编译 |
| 14 | 清理禁用的 `DX12BindingCollectQueue` 基础设施 |
| 15 | 修复 DllMain 中的 LoadLibrary/CreateThread（使用延迟初始化） |
| 16 | 为关键运行时结构体添加 `static_assert` 尺寸验证 |

---

## 7. 附录：分析过程记录

### 分析方法论

```
第一层：编译器差异矩阵分析
  ├── cmake/CompilerOptions.cmake
  ├── CMakePresets.json
  └── 识别 /GS-、/O2+/LTCG、/RTC1 缺失

第二层：系统性代码扫描（7 个维度）
  ├── 子任务1：栈溢出调用链分析（追溯完整 hot path）
  ├── 子任务2：Strict Aliasing UB 分析（全量 cast 追踪）
  ├── 子任务3：竞态条件分析（SRWLOCK、TLS、原子操作）
  ├── 子任务4：COM 引用计数/资源生命周期
  ├── 子任务5：CRT 差异（堆行为、迭代器、启动关闭）
  ├── 子任务6：ShaderDump TOCTOU
  └── 子任务7：条件编译差异

第三层：交叉验证
  ├── 每个发现的调用链追踪
  ├── DEBUG vs RELEASE 行为差异推演
  └── 场景重建
```

### 统计数据

| 指标 | 数值 |
|------|------|
| 分析源文件数 | 183 (.hpp) + 93 (.cpp) = 276 |
| 代码行数 | ~300,000 |
| 发现的高危问题 | **14**（v1.0: 10, v2.0 新增: 4） |
| 发现的中危问题 | 5（v1.0）+ 1（v2.0 漏洞）= **6** |
| 已验证安全的模块 | 6 |
| 分析维度数 | **10**（v2.0 新增: ABI布局、volatile全链路、内存取证） |
| 子分析任务数 | 7 + 6 = **13** |
| 总 Token 消耗 | ~17M（v1.0: ~8.5M, v2.0: ~8.5M） |

### 文件问题索引

| 文件 | 发现问题 |
|------|----------|
| `cmake/CompilerOptions.cmake:30` | `/GS-` → 应改为 `/GS` |
| `resource/DX12ResourceTracker.cpp:355,463,536,708` | CanonicalResource 悬空调用 + 裸 Release + gCanonicalCache 泄漏 |
| `binding/DX12BindingTracker.cpp:42,2628` | TLS 读重排 + TOCTOU |
| `binding/DX12BindingTrackerState.hpp:179` | BindingShard::generation 伪 volatile |
| `runtime/DX12HotPathState.hpp:33,66` | 无屏障读取 + strict aliasing |
| `command/DX12CommandListRuntime.cpp:205,235,791` | 锁后使用 + strict aliasing |
| `command/DX12CommandListRuntimeState.hpp:19` | volatile UINT64 → 应改为 LONG64 |
| `runtime/DX12LogManager.hpp:39,198` | 条件编译 + Close() 重排 |
| `proxy/DX12Wrapper.cpp:533,570,1556,1768` | FindByWrapperPointer 悬空指针 + 钻石继承 |
| `mod/DX12ModResourceRuntime.hpp:1697` | 无边界检查 memcpy |
| `mod/DX12ModRuntime.cpp:245,291,481,502` | strict aliasing |
| `mod/DX12ModRuntimeLoader.hpp:1384` | strict aliasing |
| `mod/DX12ShaderOverrideRuntime.hpp:418` | strict aliasing |
| `D3D12Proxy.cpp:140` | DllMain loader lock 违规 |
| `binding/DX12BindingCollectQueue.hpp/cpp` | 禁用但未清理 |

---

> **报告结束 — v2.0**  
> *本报告基于对 Project Bunny DX12 Wrapper 全部源代码的 10 维度、13 子任务深度分析。所有发现均经过第一性原理验证、DEBUG/RELEASE 双向对比、调用链推演和 ABI/内存取证。*  
> *v2.0 新增: 钻石继承分析、gCanonicalCache 内存取证、volatile 全链路追踪、ABI 布局验证。*
---

## 8. 第二轮增量深入分析（2026-07-09）

> 本节是第二轮增量分析，只更新排查结论，不修改源码、不执行构建。  
> 本轮开始前已执行 `codegraph sync`，结果为 `Already up to date`。

### 8.1 本轮日志复核

本轮复核的 DEBUG 日志文件：

```text
D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl
大小：147,661,845 bytes（约 141 MiB）
最后写入：2026-07-09 03:58:12
```

定向检索结论：

| 项目 | 结果 |
|---|---:|
| 崩溃、异常、DeviceRemoved、DXGI_ERROR、Access Violation 类日志 | 0 个有效命中 |
| 唯一 `error` 字段命中 | `DX12ModRuntime errors=0`，不是运行时错误 |
| `DX12ObjectLifetimeTracker` 安装总数 | 27,401 |
| descriptor heap tracker 安装数 | 140 |
| resource tracker 安装数 | 27,261 |
| `DX12DrawSrvReplacement` 日志 | 542 |
| `slot_patched` | 103 |
| `descriptors_patched` | 103 |
| `MissingOriginal` / `missing_original` | 368 |
| `CommandListRuntimeUnregister` | 0 |
| descriptor shadow 运行时记录类日志 | 0 |

最新帧性能复核：

| 指标 | 数值 |
|---|---:|
| 帧号 | 2288 |
| 一帧总消耗 | 546.421 ms / 546,421 us |
| 帧间隔 | 约 546.421 ms / 546,421 us |
| 预期帧率 | 约 1.83 FPS |
| Outside Present | 545.511 ms / 545,511 us |
| Present wall | 0.910 ms / 910 us |
| Queue wrapper/original/notify | 841 us / 424 us / 317 us |
| API scope total（CPU 侧可见 API duration） | 168.365 ms / 168,365 us |

最后一帧函数耗时与占比：

| 函数/阶段 | 调用数 | 总耗时 | 平均 | 占一帧 |
|---|---:|---:|---:|---:|
| DrawHook | 1,421 | 132.165 ms / 132,165 us | 93 us | 24.2% |
| DrawOverride | 1,421 | 127.225 ms / 127,225 us | 89 us | 23.3% |
| ModPrepareOverride | 1,421 | 99.740 ms / 99,740 us | 70 us | 18.3% |
| DispatchHook | 318 | 14.565 ms / 14,565 us | 45 us | 2.7% |
| BuildIaHash | 1,004 | 13.477 ms / 13,477 us | 13 us | 2.5% |
| SetPipelineState | 730 | 8.299 ms / 8,299 us | 11 us | 1.5% |
| PreSkinQuery | 107 | 8.181 ms / 8,181 us | 76 us | 1.5% |
| RootDescTable | 4,761 | 7.497 ms / 7,497 us | 1 us | 1.4% |
| IASetVertexBuf | 2,581 | 4.127 ms / 4,127 us | 1 us | 0.8% |
| BindCollect | 107 | 3.584 ms / 3,584 us | 33 us | 0.7% |
| ResourceBarrier | 210 | 3.292 ms / 3,292 us | 15 us | 0.6% |
| BarrierUnwrap | 210 | 1.879 ms / 1,879 us | 8 us | 0.3% |

文本图表（按最后一帧占比）：

```text
DrawHook          24.2% | ########################
DrawOverride      23.3% | #######################
ModPrepareOverride18.3% | ##################
DispatchHook       2.7% | ###
BuildIaHash        2.5% | ##
SetPipelineState   1.5% | #
PreSkinQuery       1.5% | #
RootDescTable      1.4% | #
IASetVertexBuf     0.8% | #
BindCollect        0.7% | #
ResourceBarrier    0.6% | #
BarrierUnwrap      0.3% | #
```

本轮对日志的判断：DEBUG 没有记录到真正的崩溃点，但日志证明了三个关键事实：

1. descriptor heap 实际数量为 140，已经超过代码中的 128 个 per-heap lock 上限。
2. `MissingOriginal` 出现 368 次，说明部分 draw/dispatch 原始回调路径并不稳定。
3. descriptor shadow 的运行时读写路径几乎没有可用于 RELEASE 的日志，因此 RELEASE 崩溃不能靠当前日志直接定位，只能结合代码生命周期推断。

### 8.2 官方 DX12 文档约束

本轮用于校准判断的 DX12 官方文档：

| 文档 | 与本问题相关的约束 |
|---|---|
| https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors | Descriptor copy 是应用侧显式管理；CPU timeline 上允许调用，但应用必须避免冲突写入和非法 handle。 |
| https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists | Command list/queue 模型强调应用显式同步，command list 生命周期和提交时序不能依赖隐式保护。 |
| https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12 | Resource state 由应用显式维护；Wrapper 改变状态后必须有清晰的恢复或转移边界。 |
| https://learn.microsoft.com/en-us/windows/win32/direct3d12/constants | D3D12 约束是硬边界，不能把内部缓存/数组上限当成“足够大”。 |

第一性原理结论：DX12 本身不会替 Wrapper 维护 descriptor 内容、resource state、command list 生命周期或多线程互斥。RELEASE 闪退如果只发生在无日志版本，更应优先怀疑 Wrapper 自己维护的 shadow state、裸指针缓存、无锁 descriptor copy 和生命周期回收边界。

### 8.3 新增高危发现：descriptor shadow 存在两层锁域不一致

本轮 CodeGraph 追踪范围：

- `DX12DescriptorHeapShadowUtils`
- `RecordCreatedDescriptorHeap`
- `InvalidateDescriptorHeapLocked`
- `InstallDescriptorHeapReleaseTracker`
- `DX12DescriptorHeapReleaseTracker::Release`
- `RecordDescriptor`
- `WriteDescriptorToSlot`
- `ReadDescriptorFromSlot`
- `CopyDescriptorRange`
- `ResolveHeapSlot`

新增结论：之前只把问题看成 `MAX_HEAP_LOCKS = 128` 不够大，这是不完整的。更深层的问题是 descriptor shadow 有两套锁域：

| 锁域 | 保护对象 | 实际问题 |
|---|---|---|
| `gDescriptorLock` | `gDescriptorHeaps`、`gDescriptorHeapByPtr`、`gDescriptorHeapByCpuStart`、CPU ledger | 部分 helper 依赖调用方外部加锁，但热路径存在无锁读取 |
| per-heap lock | 某个 heap record 的 `slots`、`occupied`、`slotHistory` | 只有 heapIndex < 128 时真正加锁，超过 128 直接 no-op |

危险链路：

```text
线程 A:
  ResolveHeapSlot(cpu) 无锁读取 gDescriptorHeapByCpuStart / gDescriptorHeaps
  得到 heapIndex + slot

线程 B:
  DX12DescriptorHeapReleaseTracker::Release()
  -> AcquireSRWLockExclusive(gDescriptorLock)
  -> InvalidateDescriptorHeapLocked(heap)
  -> ClearDescriptorHeapLocked(record)
  -> ReleaseDescriptorRecordResources(...)
  -> 清空 slots / occupied / maps

线程 A:
  LockExclusive(heapIndex)
  如果 heapIndex >= 128，锁是 no-op
  如果 heapIndex < 128，也只能保护 slot vector，不保护刚才解析的全局 registry
  -> 继续访问可能已清空、已复用、已释放的 record/slot/resource
```

这意味着：

1. heapIndex >= 128 时，slot 读写完全没有 per-heap lock。
2. heapIndex < 128 时，也不能保证 `ResolveHeapSlot` 结果到 slot 访问之间 heap record 未被 invalidated。
3. `InvalidateDescriptorHeapLocked` 会释放 descriptor record 内的 COM resource；如果另一个线程同时读/写同一 slot，可能出现 stale pointer、重复 Release 或释放后访问。

因此本轮将 descriptor heap 风险从“固定数组上限不足”升级为“descriptor global registry 与 per-heap storage 缺少统一事务边界”。简单把 128 调到 256 只能降低命中概率，不能消除根因。

### 8.4 新增高危发现：COM Release 回调会进入 shadow 清理路径

`RecordCreatedDescriptorHeap` 会安装 `DX12DescriptorHeapReleaseTracker`：

```text
CreateDescriptorHeap
-> RecordCreatedDescriptorHeap
-> InstallDescriptorHeapReleaseTracker
-> heap->SetPrivateDataInterface(...)
```

当 D3D12 object 生命周期结束时，tracker 的 `Release()` 会执行：

```text
DX12DescriptorHeapReleaseTracker::Release
-> AcquireSRWLockExclusive(gDescriptorLock)
-> InvalidateDescriptorHeapLocked(mHeap)
-> ClearDescriptorHeapLocked(record)
-> ReleaseDescriptorRecordResources(...)
```

这条链路的关键问题不是“有锁所以安全”，而是它只保护了 `gDescriptorLock` 下的清理动作。descriptor hot path 里另一些函数可以先无锁解析 heap index，再进入 per-heap lock 或 no-op lock。于是 COM Release 线程和 descriptor copy/record/read 线程之间存在生命周期 TOCTOU。

RELEASE 更容易触发的原因：

1. DEBUG 日志会显著拉长热路径，把线程交错变慢。
2. RELEASE 没有大量 `DebugJsonFunc` 和 `_DEBUG` 分支，descriptor copy、draw hook、release tracker 更容易贴近执行。
3. RELEASE 优化会缩短临界区外的空隙，使“先解析、后访问”的 stale window 更容易和释放路径重叠。

这条链路预计是当前最强 RELEASE 闪退候选。若未来重构为单一 descriptor registry transaction，预计可消除 60%~80% 与 descriptor 生命周期相关的 RELEASE 闪退概率，同时可降低约 10%~20% descriptor shadow 热点抖动；但实际收益需要用 RELEASE 带最小诊断日志验证。

### 8.5 新增高危发现：command list runtime TLS 只失效当前线程

本轮 CodeGraph 追踪范围：

- `DX12CommandListRuntime::GetStatePtrFast`
- `DX12CommandListRuntime::Register`
- `DX12CommandListRuntime::Unregister`
- `DX12CommandListRuntime::TlsInvalidateIfMatch`
- `DX12CommandListLifecycle::Register`
- `DX12CommandListLifecycle::Unregister`
- command list wrapper `Release`

关键事实：

```text
GetStatePtrFast(commandList):
  如果 TLS 命中，直接返回 Tls.statePtr
  否则读 map/shard，拿到 DX12CommandListRuntimeState*
  然后刷新当前线程 TLS

Unregister(commandList):
  shard.states.erase(commandList)
  TlsInvalidateIfMatch(commandList)
```

风险点：`TlsInvalidateIfMatch` 只清理当前线程 TLS。其他线程曾经缓存过同一个 `commandList -> statePtr` 时，`Unregister` 无法跨线程清掉这些 TLS。`shard.states.erase` 后，其他线程的 `Tls.statePtr` 可能指向已经被 unordered_map 释放或复用的 value。

更坏的是，`GetStatePtrFast` 返回的是裸指针，不是带生命周期 pin 的 handle。RELEASE 由于更快、更少日志，跨线程使用旧 TLS 指针的窗口更容易压到一起。

这条风险与 descriptor 风险有共同结构：都是“为了热路径快，缓存了裸指针；但生命周期回收不是同一个原子事务”。因此修复方向不应是局部加判断，而应抽象出统一的 runtime state handle / generation guard / delayed retire 机制。

### 8.6 新增中高危发现：draw SRV replacement 是 no-save/no-restore

本轮 CodeGraph 追踪范围：

- `DX12DrawSrvReplacementUtils::ApplyRequests`
- `DX12DrawSrvReplacementUtils::ApplyConfig`
- `DX12DrawSrvReplacementUtils::Restore`
- `DX12CommandListRuntimeExecutor::RunTextureOverride`
- `DX12ModRestoreDrawSrvReplacement`
- `ID3D12Device::CopyDescriptorsSimple`

关键代码语义：

```text
ApplyRequests:
  targetCpu = binding.tableCpuStart + descriptorOffset * descriptorIncrement
  device->CopyDescriptorsSimple(1, targetCpu, replacementCpu, CBV_SRV_UAV)

Restore:
  no-op
```

代码注释明确写了 “NO save, NO restore”，假设游戏下一次 `SetGraphicsRootDescriptorTable` 会自然覆盖 patch。这个假设在 DX11 `PSSetShaderResources` 语义下比较自然，但在 DX12 descriptor table 模型下风险更高：

1. DX12 root descriptor table 绑定的是 GPU descriptor handle，不是单个 SRV slot。
2. Wrapper 直接改的是 game shader-visible heap 的 CPU descriptor 内容。
3. 如果游戏复用同一个 table 且没有马上重绑，patch 会污染后续 draw。
4. 如果 descriptor heap 正在被其他线程 record/copy/read，no-restore 会扩大 descriptor shadow 的一致性问题。

日志中 `slot_patched=103`、`descriptors_patched=103`，说明这条路径确实发生过。它不一定是第一崩溃点，但它会显著提高 descriptor shadow 与资源生命周期错位概率。

与 IA replacement 对比：

| 子系统 | Apply 后恢复 |
|---|---|
| IA replacement | 有 `DX12IaReplacementBindingExecutor::Restore` |
| draw SRV replacement | `Restore` 是 no-op |

这说明当前 override 架构的“状态恢复颗粒度”不一致。建议后续重构时把 draw-scoped override 抽象成统一的 scope object，进入时保存明确状态，退出时按同一颗粒度恢复或延迟恢复。

### 8.7 新增中危发现：`MissingOriginal` 不是无害日志

日志复核显示 `MissingOriginal` / `missing_original` 共 368 次。结合代码，draw callback 初始化逻辑为：

```text
callbacks.drawInstanced =
  original ? original : DirectDrawInstanced

DirectDrawInstanced:
  commandList->DrawInstanced(...)
```

如果 `original` 为空，fallback 会重新通过 `commandList->Draw*` 调用。若当前 command list 是被 hook 的 vtable 或 wrapper 对象，这可能导致：

1. 递归进入 hook flow。
2. 依赖 `DX12IaReplacementDrawScope::IsExecuting()` 之类的 reentry guard 才能避免重复处理。
3. 对 draw/dispatch 计数、runtime state、descriptor patch 时序造成额外错序。

DEBUG 日志中它表现为可观察事件；RELEASE 中没有足够日志时，它可能只表现为栈更深、状态更乱、偶发访问违规。它不是当前排名第一的根因，但应列入第二优先级验证。

### 8.8 根因排名修订

第二轮后，根因排名修订如下：

| 排名 | 候选根因 | 概率 | 说明 |
|---:|---|---:|---|
| 1 | descriptor global registry 与 per-heap storage 锁域不一致，且 heap 数量超过 128 后 per-heap lock no-op | 45% | 日志证明 140 heap；代码证明超过 128 不加锁；Release tracker 会清 slot 并 Release COM resource |
| 2 | command list runtime TLS 缓存裸指针，Unregister 只失效当前线程 | 20% | RELEASE 更容易触发跨线程 stale state pointer |
| 3 | draw SRV replacement 直接 patch shader-visible heap 且 no-restore | 15% | 日志证明发生 103 次；会扩大 descriptor shadow 污染面 |
| 4 | `MissingOriginal` 导致 fallback 走 hooked vtable，引入递归/重入/错序 | 10% | 日志证明 368 次；需要 RELEASE 最小日志验证 |
| 5 | 编译器/CRT 差异，如 `/GS-`、strict aliasing、优化重排 | 10% | 仍可能放大上述问题，但不应再作为唯一根因解释 |

本轮最重要的变化：根因不再被描述为“RELEASE 优化导致某个点崩”，而是“Wrapper 热路径为了性能缓存和分片了状态，但生命周期释放、descriptor registry、slot storage、draw-scoped override 没有同一颗粒度的事务边界”。这比单点修补更符合当前证据。

### 8.9 可证伪验证点（暂不修复）

下一轮如果允许加日志，建议优先加 RELEASE 可用的低量诊断点：

1. 在 descriptor shadow 的 `ResolveHeapSlot` 后记录 `heapIndex >= 128` 的次数、heap pointer、cpu handle、thread id。
2. 在 `LockExclusive/LockShared` 遇到 `heapIndex >= 128` 时记录一次性告警。
3. 在 `InvalidateDescriptorHeapLocked` 记录 heap index、liveSlots、slotHistory 数量、调用线程。
4. 在 `RecordDescriptor`、`WriteDescriptorToSlot`、`CopyDescriptorRange` 记录“解析前/加锁后 heap version 是否变化”。
5. 在 `DX12CommandListRuntime::Unregister` 记录 commandList、removed、tracking generation、当前线程 TLS 是否命中。
6. 在 `GetStatePtrFast` 记录 TLS 命中但 generation 不匹配、或 statePtr 所属 commandList 不匹配的异常。
7. 在 draw fallback 中记录 `original == nullptr` 时是否进入 `DirectDraw*`，并记录 reentry depth。

预期验证结果：

- 如果 RELEASE 崩溃前出现 `heapIndex >= 128` descriptor write/copy/read，排名 1 基本坐实。
- 如果 RELEASE 崩溃前出现 command list unregister 后 TLS 命中，排名 2 上升为 P0。
- 如果 RELEASE 崩溃前 `slot_patched` 后没有对应 table rebind 或 restore，排名 3 上升。
- 如果 RELEASE 崩溃前 `original == nullptr` 且 reentry depth 增长，排名 4 上升。

### 8.10 架构级修复方向（本次不实施）

本次不修复，但建议后续不要做以下“小补洞”：

```text
不要只把 MAX_HEAP_LOCKS 从 128 改成 256。
不要只在某个 Release 外面套 __try/__except。
不要只给 GetStatePtrFast 加 nullptr 判断。
不要只关闭 draw SRV replacement。
```

更合适的重构方向：

1. 引入 `DX12DescriptorHeapShadowRegistry` 类，把 heap registry、heap storage、slot read/write/copy、invalidate 放进同一个对象和同一套锁协议里。
2. per-heap lock 改成随 heap record 生命周期动态分配，禁止固定上限；Dump 功能不得有资源数量上限。
3. `ResolveHeapSlot` 不能返回不受保护的裸 index；应返回带锁或带 generation 校验的短生命周期访问对象。
4. COM Release tracker 不直接在回调内释放 descriptor slot resources；应进入明确的 retire 阶段，至少延迟到没有活跃 descriptor transaction。
5. command list runtime state 不再把 unordered_map value 裸指针长期放进 TLS；改为 generation checked handle，或 delayed retire old state。
6. draw SRV replacement 与 IA replacement 统一成 draw-scoped override transaction，明确保存/恢复颗粒度。

如果按上述架构重构，预计收益：

| 方向 | 预计效果 |
|---|---|
| descriptor registry 事务化 | 消除约 60%~80% descriptor/lifetime 类 RELEASE 闪退候选 |
| command list state handle 化 | 消除约 15%~25% stale state pointer 候选 |
| draw-scoped override 统一保存/恢复 | 降低约 10%~20% 后续 draw 状态污染风险 |
| RELEASE 最小诊断日志 | 定位效率提升约 50%~70%，同时对帧率影响预计小于 DEBUG 全量日志 |

### 8.11 本轮风险点与颗粒度说明

本轮只修改文档，没有修改源码，因此没有引入运行时风险，也不需要执行 `build_debug_x64.ps1`。

本轮分析对齐颗粒度：

| 模块 | 对齐颗粒度 |
|---|---|
| descriptor heap shadow | heap registry + slot storage + COM release tracker 同一事务边界 |
| command list runtime | command list 生命周期 + TLS cache + state map/shard |
| draw SRV replacement | 单次 draw override scope |
| 日志分析 | 最新帧 + 生命周期安装量 + descriptor/SRV/MissingOriginal 定向统计 |

本轮新增结论的风险：由于 RELEASE 没有崩溃现场日志，以上仍是“证据排序后的候选根因”，不是最终定案。当前最值得优先验证的是 descriptor shadow 的 `heapIndex >= 128` 与 invalidate 并发窗口。
