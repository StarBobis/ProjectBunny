# Hot Path skipAll Fast-Path Optimization Plan

## Date
2025-07-06

## Status
In Progress

---

## First-Principles Analysis

The DX12 wrapper operates in two modes:
1. **skipAll=1** (default/idle): No mod active, no hunting, no capture. Wrapper should be transparent.
2. **skipAll=0** (active): Mod, hunting, or frame capture is active. Full tracking.

In skipAll=1 mode, the draw/dispatch hooks have a fast-path check (`skipAll || !(MASK_DRAW_WORK)`) that bypasses all tracking. However, **20+ pass-through hook functions lack this check**, and **HookedDispatch runs BumpIaContentEpoch before the check**. This is wasted work per call.

## Optimization Items

### P0-1: Add skipAll Early-Out to 17 Pass-Through Hooks

These hooks have NO early-out check. They always run ApiWallScope + BumpIaContentEpoch + LogDX12Call + Orig* forward, even when skipAll=1:

| Function | Line | ApiWallScope | BumpIaContentEpoch | LogDX12Call |
|---|---|---|---|---|
| HookedCopyBufferRegion | L695-705 | Yes | Yes | Yes |
| HookedCopyTextureRegion | L707-716 | Yes | Yes | Yes |
| HookedCopyResource | L718-729 | Yes | Yes | Yes |
| HookedCopyTiles | L731-747 | Yes | Yes | Yes |
| HookedResolveSubresource | L749-761 | Yes | Yes | Yes |
| HookedRSSetViewports | L761-764 | No | No | Yes |
| HookedRSSetScissorRects | L767-770 | No | No | Yes |
| HookedOMSetBlendFactor | L773-776 | No | No | Yes |
| HookedOMSetStencilRef | L779-782 | No | No | Yes |
| HookedSOSetTargets | L893-900 | No | No | Yes |
| HookedDiscardResource | L998-1000 | No | No | Yes |
| HookedBeginQuery | L1003-1007 | No | No | Yes |
| HookedEndQuery | L1009-1015 | No | No | Yes |
| HookedResolveQueryData | L1017-1030 | No | No | Yes |
| HookedSetPredication | L1032-1041 | No | No | Yes |
| HookedCommandListSetMarker | L1043-1047 | No | No | Yes |
| HookedCommandListBeginEvent | L1049-1054 | No | No | Yes |
| HookedCommandListEndEvent | L1056-1059 | No | No | Yes |

**Fix**: Add `if (DX12HotPathState::skipAll) { Orig*(...); return; }` to each. Reference existing pattern in HookedIASetPrimitiveTopology (L750-754).

**Estimated improvement**: Each call saves 1x ApiWallScope RAII (timer), 1x BumpIaContentEpoch (hash+lock+epoch), varargs formatting. Per-call savings ~200-500ns. For games with heavy copy/clear/resolve usage, this could save 0.5-2ms per frame.

### P0-2: HookedDispatch — Move BumpIaContentEpoch AFTER Fast-Path Check

Current code (L655-656):
```
BumpIaContentEpoch(commandList);  // ALWAYS runs
if (!(flags & MASK_DISPATCH_WORK)) {  // THEN checks fast-path
    original(...);
    return;
}
```

**Fix**: Move BumpIaContentEpoch into the slow-path branch (after the if block).

**Estimated improvement**: Every dispatch that hits the fast-path (~99% in skipAll mode) saves the BumpIaContentEpoch cost. ~100-200ns per dispatch.

### P1-3: CollectPreSkinBindings Eliminate Per-Call Vector Heap Allocations

Current code allocates 4 vectors per call (localUavs, localSrvs, localCbvs, localConstants) plus 5 more per descriptor range iteration (cpuHandles, descriptors, found, etc.).

**Fix**: 
- Promote `localUavs`, `localSrvs`, `localCbvs`, `localConstants` to `thread_local static` with `.clear()` reuse.
- For per-range vectors, use stack arrays for the common case (ranges typically have ≤16 descriptors).

**Estimated improvement**: Eliminates ~10-20 heap allocations per PreSkin dispatch. ~1-5us per dispatch.

### P1-4: BindingTracker — Split CommandListBindingState Snapshot by Event Kind

In RecordDrawInstanced/RecordDrawIndexedInstanced/RecordDispatch:
- Current: `snapshot = state;` copies ~30KB including both graphics AND compute tables, descriptors, constants.
- Draw events only need: graphics tables/descriptors/constants, VB/IB, primitive topology
- Dispatch events only need: compute tables/descriptors/constants

**Fix**: 
- `snapshot = state;` → structured copy of only relevant fields
- Or: build the event directly inside the lock, only copying needed sub-fields
- Compute tables (~5KB) not needed for draw; Graphics tables (~5KB) not needed for dispatch

**Estimated improvement**: ~50% reduction in per-draw/dispatch state-copy (~15KB saved per event copy). For hunting mode (thousands of draws/dispatches): ~1-3ms per frame.

### P2-5: BuildEvent — Skip Unnecessary memcpy for Unused Fields

BuildEvent copies all 10 sub-arrays regardless of event kind:
- For "draw"/"draw_indexed": don't copy computeTables/computeDescriptors/computeConstants
- For "dispatch": don't copy graphicsTables/graphicsDescriptors/graphicsConstants/vertexBuffer*

**Fix**: Add kind-based conditional memcpy in BuildEvent. Use early kind detection already present at L1295.

**Estimated improvement**: ~50% reduction in BuildEvent memcpy per event. ~20-40KB saved per build. ~0.5-1ms per frame in hunting mode.

### P2-6: SnapshotBindingState Callers — Reuse GetOrResolveBindingState TLS Cache

SnapshotBindingState (L1202) always computes ShardFor hash + AcquireSRWLockShared + unordered_map find, even when called repeatedly for the same commandList. Unlike GetOrResolveBindingState which uses TLS cache.

**Fix**: Add TLS caching to SnapshotBindingState, or have its callers use GetOrResolveBindingState first and pass the state pointer.

**Estimated improvement**: ~50-100ns per call. Low priority since callers are not the hottest path.

## Files Modified

| File | Change |
|---|---|
| `src/DirectX12/command/DX12CommandListHooks.cpp` | P0-1, P0-2 |
| `src/DirectX12/binding/DX12BindingTracker.cpp` | P1-3, P1-4, P2-5, P2-6 |
| `src/DirectX12/binding/DX12BindingTracker.hpp` | P1-4 (new snapshot variant declarations) |

## Quantified Expectations

| Scenario | Metric | Before | After | Improvement |
|---|---|---|---|---|
| skipAll=1, heavy copy/clear | Pass-through hooks overhead | ~2-5ms/frame | ~0ms | -100% |
| skipAll=1, dispatch-heavy | BumpIaContentEpoch waste | ~0.1ms/frame | ~0ms | -100% |
| PreSkin mode | Per-dispatch allocations | ~10-20 allocs | ~0 allocs | -100% heap alloc |
| Hunting mode | Per-event state copy | ~110KB/event | ~55KB/event | -50% memcpy |
| Combined | Overall CPU overhead | Baseline | ~3-8ms saved | ~5-15% FPS in skipAll=1 |
