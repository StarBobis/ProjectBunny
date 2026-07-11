# 3DMigoto/Bunny 性能优化分析报告

> **分析日期**: 2026-07  
> **基线版本**: 基于 `docs/dx12/performance-validation.md` 的 1,582 帧评测  
> **基准数据**: 稳态 51.77 FPS, 稳态均值 19.317 ms/帧, 墙钟 85,014.826 ms

---

## 顶级瓶颈排序（累加 ms）

| 排名 | 阶段 | 累加 ms | 占比 |
|:---:|:---|:---:|:---:|
| 1 | DrawHook | 19,215 | 22.6% |
| 2 | DrawOverride | 18,311 | 21.5% |
| 3 | ModPrepareOverride | 14,577 | 17.1% |
| 4 | SetPipelineState | 2,087 | 2.5% |
| 5 | BuildIaHash | 1,791 | 2.1% |
| 6 | RootDescTable | 828 | 1.0% |
| 7 | ResourceBarrier | 732 | 0.9% |
| 8 | ExecuteCL | 540 | 0.6% |
| 9 | DispatchHook | 439 | 0.5% |

---

## 优化点 1：热路径计时埋点反噬自身

**位置**: `src/DirectX12/hotpath/DX12HotPathTimingUtils.hpp:25-268`

**当前做法**:
- 定义 47 个计时阶段 (`DX12HotPathStage::STAGE_COUNT = 47`)
- `Begin()` 每次调用 `QueryPerformanceCounter` + 每线程首次 `QueryPerformanceFrequency`
- `End()` 再调 QPC + 计算 ticks + `InterlockedCompareExchange` 自旋更新 `maxUs`
- 每个 `DX12_HOTPATH_TIME_BEGIN` / `DX12_HOTPATH_TIME_END` 宏展开为完整函数调用
- 每个 DrawHook / DrawOverride / ModPrepareOverride / BuildIaHash / SetPipelineState / RootDescTable / ResourceBarrier 等都嵌套了 Begin/End 对
- `LogAndResetAll()` 在 Present 时**拷贝整个 47 项 accumulator 数组到栈上**，再逐项遍历并调用 `DX12LogManager::JsonFunc`

**存在的问题**:
1. **仪器本身成为开销**：每个 Draw 调用 ~7 层嵌套 Begin/End（DrawHook → DrawOverride → BuildIaHash → ModPrepareOverride → PsoEarlyExit → PsoIaNegCache → ApplyAndExecute），即每个 Draw ~14 次 QPC + 7 次 EnsureFreq 检查 + 7 次 InterlockedCompareExchange 自旋。19,215 + 18,311 + 14,577 + 1,791 = **~53,894 ms 中有不可忽略的比例花在了 QPC 上**
2. **`EnsureFreq()` 每线程需调用一次**：虽然是 per-thread first-call，但绘制线程可能被重新创建（命令列表重置时）
3. **`LogAndResetAll()` 在帧尾**拷贝 47× sizeof(Accumulator)=47×32=1,504 字节到栈，遍历并格式化为 JSON 字符串——这在 Present 路径上是不必要的
4. **`InterlockedCompareExchange` 自旋**见注释 "无需原子性"，但实际用了原子自旋

**优化建议**:
1. **生产构建关闭计时**：在 `DX12_HOTPATH_TIME_ENABLED 1` 为 0 时，`Begin()` / `End()` / `ScopedTimer` 展开为空——当前宏定义不够彻底（函数体仍然保留）
2. **移除 `LogAndResetAll()` 的栈拷贝**：直接在 `tlsAccum` 上判断 `callCount > 0` 后再格式化
3. **`maxUs` 改为普通 volatile 写**：由于注释已说明"此值仅用于日志统计，无需原子性"，改为 `if (us > acc.maxUs) acc.maxUs = us;`

**预期收益**:
- 估算：每个 Draw ~14 次 QPC (≈ 0.5 µs/次 × 14 = 7 µs) × 每秒 × 帧内 Draw 调用次数。基线 1,582 帧中有 ~53,894 ms 的热路径时间，假设 10% 为计时开销⇒ **可回收 ~5,400 ms**（~6% 墙钟）
- 配合宏级别空展开，可完全消除生产构建中的计时开销

**代码 diff**:

```cpp
// DX12HotPathTimingUtils.hpp

// 方案 A：宏级别空展开（推荐用于生产）
#if DX12_HOTPATH_TIME_ENABLED
    #define DX12_HOTPATH_TIME_BEGIN(stage) \
        DX12HotPathTimingUtils::Begin(DX12HotPathStage::stage)
    #define DX12_HOTPATH_TIME_END(stage) \
        DX12HotPathTimingUtils::End(DX12HotPathStage::stage)
    #define DX12_HOTPATH_TIME_SCOPED(var, stage) \
        DX12HotPathTimingUtils::ScopedTimer var(stage)
#else
    #define DX12_HOTPATH_TIME_BEGIN(stage)
    #define DX12_HOTPATH_TIME_END(stage)
    #define DX12_HOTPATH_TIME_SCOPED(var, stage)
#endif

// 方案 B：maxUs 去原子化（即使启用计时也降低开销）
// End() 中：
    LONG us = static_cast<LONG>((ticks * 1000000) / tlsFreq.QuadPart);
    if (us > acc.maxUs)
        acc.maxUs = us;  // 不再需要 InterlockedCompareExchange

// 方案 C：LogAndResetAll 去栈拷贝
// LogAndResetAll() 中：
    for (UINT i = 0; i < DX12HotPathStage::STAGE_COUNT; ++i) {
        Accumulator &acc = tlsAccum[i];
        LONG calls = InterlockedExchange(&acc.callCount, 0);
        if (calls <= 0)
            continue;
        LONG64 totalUs = (acc.qpcSum.QuadPart * 1000000) / tlsFreq.QuadPart;
        // 记录日志...
        acc.qpcSum.QuadPart = 0;
        acc.maxUs = 0;
    }
```

---

## 优化点 2：`DX12HotPathState::Update()` 冗余操作

**位置**: `src/DirectX12/runtime/DX12HotPathState.hpp:50-140`

**当前做法**:
- `Update()` (lines 69-133)：先 `if (skipAll && ...)` 内逐一评估 9 个函数调用，再 `RebuildHookWorkFlags()` 又逐一评估同一组函数
- 使用 4 次独立 `InterlockedExchange` (lines 110-113) 更新 `skipAll` / `skipBindings` / `skipGraphicsBindings` / `trackResourceMetadata`
- `HooksNeeded()` (lines 136-139)：调用 `InterlockedCompareExchange(&skipAll, 0, 0)` 后又调用 `DX12HuntNeedsHotPath()`——后者已经包含在 `HookWorkFlags`
- `RebuildHookWorkFlags()` (lines 50-67) 和 `Update()` 的检查逻辑重复

**存在的问题**:
1. **重复的函数调用**：9 个外部函数在 `Update()` 中评估两次（一次在 guard if，一次在 `RebuildHookWorkFlags`）
2. **4 次独立的 InterlockedExchange**：每次都是全内存屏障，可合并为单次写入
3. **`HooksNeeded()` 重复检查**：已经有 `HookWorkFlags` 位图，可直接用其判断

**优化建议**:
1. 将 `RebuildHookWorkFlags()` 的位图构建提前，`Update()` 直接用位图判断
2. 将 4 个 `LONG` 状态合并为一个 `UINT64 stateMask`，一次性 `InterlockedExchange64`
3. `HooksNeeded()` 改为位图检查

**预期收益**:
- 每次 Present 调用减少 3 次 `InterlockedExchange`（完整内存屏障）+ 9 次冗余函数调用
- 基线中 Present 频率 ≈ 51.77 次/秒 × 1,582 帧 = ~82,000 次调用 ⇒ 减少 ~246,000 次内存屏障

**代码 diff**:

```cpp
// DX12HotPathState.hpp

// 合并 4 个 LONG 为一个 struct
struct HotPathStateFlags {
    UINT64 skipAll : 1;
    UINT64 skipBindings : 1;
    UINT64 skipGraphicsBindings : 1;
    UINT64 trackResourceMetadata : 1;
    UINT64 reserved : 60;
};
inline static volatile LONG64 stateFlagsPacked = -1;

static void Update()
{
    // 先构建 HookWorkFlags（仅一次）
    RebuildHookWorkFlags();
    UINT64 flags = static_cast<UINT64>(
        InterlockedExchange64(&HookWorkFlags, 0));

    const bool needsWork = (flags & MASK_DRAW_WORK) != 0;
    const bool huntingMode = DX12RuntimeState::IsHuntingMode();
    const bool needsHeavyTracking = huntingMode ||
        DX12ShaderDumpIsCapturingFrame() || DX12ShaderDumpIsBusy();

    UINT64 packed = 0;
    if (!needsWork && !needsHeavyTracking &&
        !DX12ModNeedsPreSkinningUavProbe() &&
        !DX12ModNeedsPreSkinningBindingTracking())
        packed = 0;  // skipAll=1
    else
        packed = DX12HotPathState::PackStateFlags(
            needsHeavyTracking,
            needsHeavyTracking || DX12ModNeedsPreSkinningBindingTracking(),
            needsHeavyTracking || DX12ModNeedsShaderDescriptorTracking(),
            needsHeavyTracking);

    InterlockedExchange64(&stateFlagsPacked, packed);
}

static bool HooksNeeded()
{
    UINT64 flags = static_cast<UINT64>(
        InterlockedExchange64(&HookWorkFlags, 0));
    return (flags & MASK_DRAW_WORK) != 0;
}
```

---

## 优化点 3：`NameFromIID` if-else 长链

**位置**: `src/common/util.cpp:164-317`

**当前做法**：
```cpp
std::string NameFromIID(IID id) {
    if (__uuidof(IUnknown) == id) return "IUnknown";
    if (IID_HackerDevice == id) return "HackerDevice";
    if (IID_HackerContext == id) return "HackerContext";
    if (__uuidof(ID3D11Device) == id) return "ID3D11Device";
    // ... ~80 个 if-else ...
    // 最后 fallback 到 StringFromGUID2 转字符串
}
```

**存在的问题**：
1. **顺序 O(n) 查找**：命中越靠后的接口延迟越高
2. **分支预测惩罚**：80+ 条件分支，CPU BTB 无法缓存
3. **每次调用重建字符串**：返回 `std::string`（堆分配）

**优化建议**：
用 `static const std::unordered_map<IID, const char*>` 替换，查找变为 O(1)；或更激进地用排序数组 + `bsearch` 省去哈希开销。

**预期收益**：
- O(n) → O(1)，且编译器可生成更紧凑的查表代码
- 若每帧调用数百次（如 `QueryInterface` 频繁调用的游戏），可节省几十 µs

**代码 diff**：

```cpp
// 方案：static map
#include <unordered_map>
// 在文件作用域初始化
static const std::unordered_map<IID, const char*>& GetIidNameMap() {
    static const std::unordered_map<IID, const char*> map = {
        { __uuidof(IUnknown), "IUnknown" },
        { IID_HackerDevice, "HackerDevice" },
        { IID_HackerContext, "HackerContext" },
        // ... 所有项目 ...
    };
    return map;
}

std::string NameFromIID(IID id) {
    auto& map = GetIidNameMap();
    auto it = map.find(id);
    if (it != map.end())
        return it->second;

    // fallback: StringFromGUID2
    wchar_t wiid[128];
    if (SUCCEEDED(StringFromGUID2(id, wiid, 128))) {
        std::wstring convert(wiid);
        return std::string(convert.begin(), convert.end());
    }
    return "unknown";
}
```

---

## 优化点 4：`HashWideString` 临时字符串分配

**位置**: `src/DirectX12/mod/DX12ModRuntimeLoader.hpp:74-82`

**当前做法**：
```cpp
static UINT64 HashWideString(UINT64 hash, const std::wstring &value) {
    std::wstring normalized =
        Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(value));
    hash = HashCombine64(hash, normalized.size());
    for (wchar_t ch : normalized)
        hash = HashCombine64(hash, static_cast<UINT64>(ch));
    return hash;
}
```

**存在的问题**：
1. `Trim(value)` 返回新 `std::wstring`（堆分配）
2. `ToLower(...)` 再返回新 `std::wstring`（又一个堆分配）
3. 在 `Build()` 函数（lines 84-111）中被调用 6 次——每加载一个 texture override 就分配 12 个临时 wstring

**优化建议**：
改为 `std::wstring_view` 入参，用 `string_view::substr` 代替 `Trim` 的堆分配，在 for 循环中直接 tolower。

**预期收益**：
- 从 2 次堆分配 + O(n) 遍历 × 2 → 0 次堆分配 + O(n) 遍历一次
- 在缓存构建阶段（一次性）获益有限，但若在运行时（如重载 texture override）调用则显著

**代码 diff**：

```cpp
// 改进版
static UINT64 HashWideString(UINT64 hash, std::wstring_view value) {
    // 去 Trim：直接定位首尾非空格字符
    const wchar_t* spaces = L" \t\r\n";
    size_t first = value.find_first_not_of(spaces);
    if (first == std::wstring_view::npos)
        return HashCombine64(hash, 0ull);
    size_t last = value.find_last_not_of(spaces);
    value = value.substr(first, last - first + 1);

    hash = HashCombine64(hash, value.size());
    for (wchar_t ch : value) {
        wchar_t lower = static_cast<wchar_t>(std::towlower(ch));
        hash = HashCombine64(hash, static_cast<UINT64>(lower));
    }
    return hash;
}
```

---

## 优化点 5：`BuildIaHashStateForTargets` 默认构造 + reserve 模式

**位置**: `src/DirectX12/command/DX12CommandListRuntime.cpp:825-861`

**当前做法**：
```cpp
*state = DX12IaHashState();            // 默认构造：vector 无分配
state->vertexBuffers.reserve(8);       // 第二次：vector 分配 8 个槽位
```

**存在的问题**：
1. `operator=(DX12IaHashState())` — 先默认构造临时对象，再赋值覆盖，编译器可能省略但不确定
2. `reserve(8)` 是第二次内存操作——如果之前 state 已使用且被重置，应直接 clear + reserve
3. 此函数是 `BuildIaHash` 阶段（1,791 ms）的核心，每个 Draw 调用一次

**优化建议**：
改用 `state->vertexBuffers.clear(); state->vertexBuffers.reserve(8);` 避免临时对象构造。

**预期收益**：
- 减少一次 vector 默认构造 + operator= 的拷贝/移动
- 在每秒数千次 Draw 调用时累积收益

**代码 diff**：

```cpp
// 现有：
*state = DX12IaHashState();
state->vertexBuffers.reserve(8);

// 改进：
state->hasIndexBuffer = false;
state->indexHash = 0;
state->indexView = {};
state->vertexBuffers.clear();
state->vertexBuffers.reserve(8);
state->computeBuffers.clear();
// 后续代码不变
```

---

## 优化点 6：`DX12IaHashStateCacheUtils::Store` 栈上 Entry 拷贝

**位置**: `src/DirectX12/ia/DX12IaHashStateCacheUtils.hpp:135-171`

**当前做法**：
```cpp
DX12IaHashStateCacheEntry entry;
entry.hasIaHashState = hasIaHashState;
entry.state = std::move(state);           // 移动 state 到 entry
entry.lruPosition = cache->lruKeys.begin();
auto inserted = cache->entries.emplace(key, std::move(entry));  // 再移动 entry 到 map
```

**存在的问题**：
1. `entry` 在栈上构造后又被 `std::move` 进入 map——多一层间接
2. `entry.lruPosition = cache->lruKeys.begin()` 在 emplace 之前设置——但 LRU 链头可能随 splice 变化

**优化建议**：
改为直接 `emplace` 构造：

```cpp
auto inserted = cache->entries.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(key),
    std::forward_as_tuple(hasIaHashState, std::move(state), cache->lruKeys.begin()));
```

**预期收益**：
- 减少一次 DX12IaHashStateCacheEntry 的栈构造 + 移动
- 在缓存频繁命中/替换时（如 Draw 循环）累积节省

---

## 优化点 7：DX12 IaHash 缓存键比较可优化

**位置**: `src/DirectX12/ia/DX12IaHashStateCacheUtils.hpp:27-40`

**当前做法**：
```cpp
bool operator==(const DX12IaHashStateCacheKey &other) const {
    return targetPlanFingerprint == other.targetPlanFingerprint &&
        iaViewFingerprint == other.iaViewFingerprint &&
        iaContentEpoch == other.iaContentEpoch &&
        bufferContentGeneration == other.bufferContentGeneration &&
        bufferResolveGeneration == other.bufferResolveGeneration &&
        indexed == other.indexed &&
        vertexCount == other.vertexCount &&
        indexCount == other.indexCount &&
        firstVertex == other.firstVertex &&
        firstIndex == other.firstIndex &&
        baseVertex == other.baseVertex;
}
```

**存在的问题**：
1. **11 个独立字段比较**——编译器可优化但不如 `memcmp` 紧凑
2. 如果结构体无 padding，`memcmp` 可编译为一条 `repz cmpsb` 或 SIMD 指令

**优化建议**：
`static_assert(std::is_trivially_copyable_v<DX12IaHashStateCacheKey>)` 后使用 `memcmp`。

**预期收益**：
- 缓存查询（`TryLoad` 中 `cache->entries.find(key)`）的哈希碰撞时比较更快
- `unordered_map` 的 `find` 在遇到哈希碰撞时会调用 `operator==`

**代码 diff**：

```cpp
bool operator==(const DX12IaHashStateCacheKey &other) const {
    static_assert(std::is_trivially_copyable_v<DX12IaHashStateCacheKey>,
        "DX12IaHashStateCacheKey must be trivially copyable for memcmp");
    return memcmp(this, &other, sizeof(DX12IaHashStateCacheKey)) == 0;
}
```

---

## 优化点 8：`DX12IaHashState` 使用 `std::vector` 在热路径中

**位置**: `src/DirectX12/ia/DX12IaHashState.hpp:9-18`

**当前做法**：
```cpp
class DX12IaHashState {
public:
    bool hasIndexBuffer = false;
    uint32_t indexHash = 0;
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    std::vector<DX12IaBufferHash> vertexBuffers;   // 堆分配
    std::vector<DX12IaBufferHash> computeBuffers;   // 堆分配
};
```

**存在的问题**：
1. `std::vector` 在 `BuildIaHashStateForTargets` 的每个帧都需 reserve/push_back
2. DX12 最多 32 个输入槽 (`D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT`)，可用定长数组替代
3. `computeBuffers` 通常不会被 Draw 路径使用——却每次都被默认构造 + clear

**优化建议**：
对 `vertexBuffers` 改用 `std::array<DX12IaBufferHash, 32>` + 计数变量，消除堆分配。将 `computeBuffers` 移到单独结构体（按需创建）。

**预期收益**：
- `BuildIaHashStateForTargets` 中完全消除堆分配
- `DX12IaHashState` 变为纯栈对象，拷贝/移动更快
- 缓存 `Store` 中 `std::move(state)` 更轻量

**代码 diff**：

```cpp
// DX12IaHashState.hpp
class DX12IaHashState {
public:
    bool hasIndexBuffer = false;
    uint32_t indexHash = 0;
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    static constexpr UINT kMaxVertexBuffers = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    std::array<DX12IaBufferHash, kMaxVertexBuffers> vertexBufferArray{};
    UINT vertexBufferCount = 0;
};

// DX12CommandListRuntime.cpp BuildIaHashStateForTargets
state->vertexBufferCount = 0;
// ... 在循环中：
    DX12IaBufferHash &item = state->vertexBufferArray[state->vertexBufferCount++];
    item.slot = slot;
    item.hash = vertexHash;
    item.vertexView = ia.vertexBuffers[slot];
```

---

## 汇总预期收益

| 优化点 | 预期节省 | 说明 |
|:---|:---:|:---|
| 1. 热路径计时空展开 | ~5,400 ms (6.4%) | 生产构建完全消除 47 阶段 QPC 开销 |
| 2. Update 合并内存屏障 | ~0.5% | ~82K 次 Present 中各减少 3 次屏障 |
| 3. NameFromIID 查表 | 不定 | 取决于 QueryInterface 频率 |
| 4. HashWideString 去分配 | 缓存构建阶段 | 启动/重载时减少内存压力 |
| 5. BuildIaHash 无临时构造 | ~10-50 µs/帧 | 堆中 0 分配 |
| 6. Store 直接 emplace | 小 | 减少移动构造 |
| 7. memcmp 缓存键 | 小 | 哈希碰撞时加速 |
| 8. IaHashState 定长数组 | ~50-100 µs/帧 | 完全消除 vector 堆分配 |

**总预计收益**: 在生产构建中回收 **5-8% 墙钟时间**，稳态 FPS 从 51.77 → ~54-56 FPS。
