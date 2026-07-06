# P1 Deep Performance Optimizations Plan

## Date
2025-07-06

## Status
In Progress — P1-2 ✅, P1-3 ✅, P1-1 pending

---

## P1-1: CollectPreSkinBindings — Eliminate Per-Call Vector Heap Allocations

### Root Cause
`DX12BindingTracker::CollectPreSkinBindings` (L570-1002) creates 4 `std::vector` every call plus 3-5 more per descriptor-range iteration. In PreSkin mode, this is called every dispatch.

### Fix
- Promotes `localUavs`, `localSrvs`, `localCbvs`, `localConstants` to `thread_local static` with `.clear()` + `.reserve(kMaxBindings)` reuse
- For per-range vectors (`cpuHandles`, `descriptors`, `found`, `pendingDescriptors`), use stack arrays when range count ≤ 16
- Bounds: kMaxUavs=64, kMaxSrvs=256, kMaxCbvs=16, kMaxConstants=16

### Files
- `src/DirectX12/binding/DX12BindingTracker.cpp`

### Expected Impact
- Eliminate ~10-20 heap allocations per PreSkin dispatch
- Save ~1-5us per dispatch

---

## P1-2: CanonicalResource — Cache COM QI Result

### Root Cause
`CanonicalResource()` (in ResourceTracker) calls `QueryInterface(IID_IUnknown)` every invocation.
`FindTrackedResourceIndexLocked` calls this internally, which is called from every resource barrier/record.
Same `ID3D12Resource*` pointer maps to the same canonical identity for its lifetime.

### Fix
Use `std::unordered_map<ID3D12Resource*, ID3D12Resource*> gCanonicalCache` guarded by lock.
On insert: QI once, store mapping.
On resource destroy: remove from cache.

### Files
- `src/DirectX12/resource/DX12ResourceTracker.cpp` or `.hpp`

### Expected Impact
- Save COM QI overhead per resource barrier/record: ~100ns each

---

## P1-3: BuildEvent — Split Copy by Event Kind

### Root Cause
`BuildEvent()` (L1292) always copies ALL 10 sub-arrays regardless of event kind:
- Draw events don't need `computeTables`/`computeDescriptors`/`computeConstants` (~5KB each)
- Dispatch events don't need `graphicsTables`/`graphicsDescriptors`/`graphicsConstants`/vertexBuffer* (~5KB each)

### Fix
Add kind-based conditional memcpy in BuildEvent:
- Kind starts with "draw_" → skip compute sub-arrays
- Kind == "dispatch" → skip graphics sub-arrays + vertex buffer arrays

### Files
- `src/DirectX12/binding/DX12BindingTracker.cpp`

### Expected Impact
- ~50% reduction in BuildEvent memcpy per event
- ~20-40KB saved per build
