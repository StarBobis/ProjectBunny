# DX12 热路径 skipAll 优化计划

## 日期
2025-07-06

## 状态
✅ 已完成 — DX12CommandListHooks.cpp 中 30+ 处 skipAll early-out 添加完毕，HookedDispatch/HookedExecuteIndirect 的 BumpIaContentEpoch 已移到 fast-path 之后。

## 根因分析

从第一性原理出发：当 `skipAll=1` 时，wrapper 的唯一职责是以最小开销将 API 调用透传给真实驱动。当前代码中，有 20+ 个 hook 函数在 skipAll 模式下仍然执行了不必要的操作：

1. **ApiWallScope 构造/析构**（RAII timing probe）
2. **BumpIaContentEpoch**（shard 查找 + epoch 递增）
3. **LogDX12Call** 参数求值
4. **HookedDispatch 中 BumpIaContentEpoch 在 fast-path 判断之前执行**

这些都是设计债务：部分 hooks 已经正确实现了 skipAll early-out 模式，但大部分 pass-through hooks 没有同步跟进。

---

## 优化列表

### 优化 1: HookedDispatch — BumpIaContentEpoch 移到 fast-path 之后

**文件**: `src/DirectX12/command/DX12CommandListHooks.cpp` L655-656

**问题**: `BumpIaContentEpoch` 在 `!(flags & MASK_DISPATCH_WORK)` 之前执行。当 skipAll=1 时 MASK_DISPATCH_WORK=0，99% 的 dispatch 走 fast-path，但每个都白做一次 epoch bump。

**方案**: 将 `BumpIaContentEpoch` 移到 fast-path return 之后，即仅在有实际工作时才 bump。

**预期效果**: 每次 dispatch 在 skipAll 模式下省 ~50ns (shard hash + map find + SRWLock)

---

### 优化 2: 20+ pass-through hooks 添加 skipAll early-out

**文件**: `src/DirectX12/command/DX12CommandListHooks.cpp`

**涉及函数**:

| 函数 | 行号 | 当前额外开销 |
|------|------|-------------|
| HookedCopyBufferRegion | L695-705 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedCopyTextureRegion | L707-716 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedCopyResource | L718-729 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedCopyTiles | L731-747 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedResolveSubresource | L749-761 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedClearUnorderedAccessViewUint | L964-977 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedClearUnorderedAccessViewFloat | L979-994 | ApiWallScope + BumpIaContentEpoch + LogDX12Call |
| HookedExecuteIndirect | L1065-1092 | ApiWallScope + BumpIaContentEpoch 在 skipAll 之前 |
| HookedRSSetViewports | L761-764 | LogDX12Call |
| HookedRSSetScissorRects | L767-770 | LogDX12Call |
| HookedOMSetBlendFactor | L773-776 | LogDX12Call |
| HookedOMSetStencilRef | L779-782 | LogDX12Call |
| HookedSOSetTargets | L899-900 | LogDX12Call |
| HookedDiscardResource | L998-1000 | LogDX12Call |
| HookedBeginQuery | L1003-1007 | LogDX12Call |
| HookedEndQuery | L1009-1015 | LogDX12Call |
| HookedResolveQueryData | L1017-1030 | LogDX12Call |
| HookedSetPredication | L1032-1041 | LogDX12Call |
| HookedCommandListSetMarker | L1043-1047 | LogDX12Call |
| HookedCommandListBeginEvent | L1049-1054 | LogDX12Call |
| HookedCommandListEndEvent | L1056-1059 | LogDX12Call |

**方案**: 参照 `HookedIASetPrimitiveTopology` (L746-754) 的已有模式，在每个 hook 开头添加:
```cpp
if (DX12HotPathState::skipAll) {
    // resolve original → call → return
}
```

**注意**: 
- Copy 类 hooks 仍保留 ApiWallScope + BumpIaContentEpoch 在 skipAll 分支之外
- RSSetViewports 等无 ApiWallScope 的 hooks 只需跳过 LogDX12Call
- ExecuteIndirect 已有 skipAll 检查但 BumpIaContentEpoch 在之前，需要调整顺序

**预期效果**: 
- Copy 类: 每次调用省 ~100-200ns (ApiWallScope + BumpIaContentEpoch + LogDX12Call)
- 简单类: 每次调用省 ~30-50ns (LogDX12Call 参数求值)

---

### 优化 3: 删除三个空操作 hook 函数

**文件**: `src/DirectX12/command/DX12CommandListHooks.cpp` L1097-1138

**问题**: `HookCommandQueue`, `HookCommandList`, `HookCommandListCreation` 三个函数体均为 `(void)ptr;`

**方案**: 保留函数签名（调用方依赖），函数体保持空。

这已经是空函数，无需改动。但 `ActivateCommandQueueHotPathHooks` 中有冗余 AddRef/Release 可以简化。

---

## 修改文件清单

| 文件 | 操作 | 行数 |
|------|------|------|
| `src/DirectX12/command/DX12CommandListHooks.cpp` | 添加 skipAll early-out | ~60 行增加 |
| `docs/hotpath-skipall-optimization.md` | 本计划文档 | - |

## 量化预期

| 场景 | 优化前 | 优化后 | 变化 |
|------|--------|--------|------|
| Dispatch fast-path (skipAll=1) | ~80ns/dispatch | ~25ns/dispatch | -69% |
| CopyBufferRegion (skipAll=1) | ~150ns/call | ~5ns/call | -97% |
| ResolveSubresource (skipAll=1) | ~120ns/call | ~5ns/call | -96% |
| RSSetViewports (skipAll=1) | ~35ns/call | ~5ns/call | -86% |

每帧数千次调用累计，预计整体 CPU 热路径开销降低 5-10% (在 skipAll 场景下)。

## 风险

- **极低**: 变更模式已被 `HookedIASetPrimitiveTopology`, `HookedSetPipelineState`, `HookedResourceBarrier` 等验证
- **向后兼容**: 不影响功能行为，只影响 skipAll=1 时的快速路径
- **线程安全**: skipAll 是 `volatile LONG` 原子读，无锁
