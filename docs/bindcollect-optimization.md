# BindCollect Hotspot Optimization Plan v2

## Hotspot Profile (Frame 2866)
- **Total cost**: 3,098 us/frame (58.1% of hook CPU)
- **Calls per frame**: 47
- **Avg per call**: 65 us, **Max**: 258 us
- **Scenario**: Pure compute, 5,661 bindings/frame, 4,253 fastForwardHits (75%)

---

## Root Cause Analysis (First Principles)

### What BindCollect Does
`CollectComputeAll` (called from `CollectPreSkinBindings`) iterates ALL 64 root parameter slots to collect UAV/SRV/CBV bindings for PreSkin dispatch replacement. For each valid descriptor table:

1. Resolve descriptor heap (CPU/GPU base, increment, count)
2. Lookup EVERY descriptor in the range via `DX12FindDescriptorSummariesByCpuHandle`
3. Classify by kind string ("UAV"/"SRV"/"CBV")
4. Build `DX12CurrentComputeUavBinding` structs

### Why It's Expensive

**A. Blind O(64) scan every call** — THE #1 PROBLEM
```cpp
for (UINT root = 0; root < MaxRootParameters(64); ++root) {
    const RootTableState &table = snapshot.computeTables[root];
    if (!table.valid) continue;   // 90%+ iterations wasted on this
    ...
}
```
D3D12 root signatures typically declare 4-20 parameters, but the code always scans 64. 47 calls × 64 iterations = 3,008 empty-loop checks per frame.

**Contrast with DX11**: In the original 3Dmigoto/DX11 architecture, binding tracking uses shader reflection — you know from the compiled shader exactly which resource slots exist, and you only iterate those. The DX12 equivalent is the **root signature itself**: `DX12RootSignatureSummary::parameters` is a dense vector of all declared parameters. Iterating this vector instead of 0..63 is the D3D12-native equivalent of DX11's shader-reflection-driven iteration.

**B. Per-descriptor hash-table lookup**
`DX12FindDescriptorSummariesByCpuHandle` does a global descriptor tracker hash-table lookup per descriptor. Small batches (4-16 descriptors per table) × multiple tables = fragmented lookups.

**C. std::vector heap allocations in hot loop**
`cpuHandles`, `descriptors`, `found`, `pendingDescriptors` are re-allocated per range per call.

**D. Redundant kind string comparison**
`desc.kind == "UAV"` string comparison when the range type from root signature already tells us.

---

## Optimization Strategy

### Phase 1: Root-Signature-Driven Iteration (Estimated -40%, ~1,240 us)

**Problem**: Blind `for(root=0; root<64; ++root)` wastes >90% of iterations on invalid slots.

**First-principles fix**: The root signature IS the authority on which parameters exist. D3D12 guarantees root parameters are indexed [0, NumParameters). We already have `cachedSummary->parameters` — a `std::vector<DX12RootParameterSummary>` containing exactly the declared parameters.

**Before**:
```cpp
for (UINT root = 0; root < MaxRootParameters; ++root) {
    const RootTableState &table = snapshot.computeTables[root];
    if (!table.valid) continue;
    // ... 100+ lines of descriptor lookup ...
}
// Same for computeRootDescriptors and computeRootConstants
```

**After**:
```cpp
if (cachedSummary) {
    for (const DX12RootParameterSummary &param : cachedSummary->parameters) {
        UINT root = param.rootParameterIndex;
        // Only iterate declared parameters (typically 4-20, not 64)
        const RootTableState &table = snapshot.computeTables[root];
        if (!table.valid) continue;  // still needed for dynamic state
        // ... descriptor lookup ...
    }
} else {
    // Fallback: use tracked max valid root (updated on SetComputeRoot*)
    for (UINT root = 0; root <= trackedMaxComputeRoot && root < MaxRootParameters; ++root) {
        ...
    }
}
```

**Why this is more robust than `maxValidRoot` alone**:
- Root signature is the **authoritative source** — no state to track, no risk of desync
- Works with **gapped parameters** (e.g., root[0], root[3], root[7]) — vector contains all of them
- **Zero maintenance burden** — no need to touch SetComputeRoot* call sites
- Aligned with DX11 pattern: shader reflection tells you which slots exist

**Fallback (no root signature available)**: Track `maxComputeRootIndex` in `CommandListBindingState`, bumped on SetComputeRoot* calls. This is secondary; the primary code path always has a root signature.

### Phase 2: Pre-Allocated Thread-Local Buffers (Estimated -20%, ~620 us)

**Problem**: `std::vector` heap allocations per range iteration.

**Fix**: Replace with thread-local fixed arrays:
```cpp
static thread_local SIZE_T      tlsCpuHandles[256];
static thread_local DX12DescriptorSummary tlsDescriptors[256];
static thread_local uint8_t     tlsFound[256];
static thread_local uint8_t     tlsPending[256];
```
- 256 entries handles worst-case (64 root params × 4 descriptors each)
- Zero heap allocation
- Better cache locality (contiguous stack/TLS memory)

### Phase 3: Unified Bulk Descriptor Lookup (Estimated -25%, ~775 us)

**Problem**: `DX12FindDescriptorSummariesByCpuHandleNoWait` called separately per range — fragmented hash-table traversals.

**Fix**: Two-pass approach:
1. **Pass 1 (collect)**: Scan root signature parameters, collect ALL CPU handles into `tlsCpuHandles[]`
2. **Pass 2 (resolve)**: Single `DX12FindDescriptorSummariesByCpuHandleNoWait(tlsCpuHandles, totalCount, ...)` 
3. **Pass 3 (distribute)**: Map results back to UAV/SRV/CBV vectors using stored range info

- N small hash lookups → 1 large hash lookup
- Better L1/L2 cache utilization (all descriptors resolved together)
- Fewer function call overheads

### Phase 4: Eliminate Kind String Comparison (Estimated -15%, ~465 us)

**Problem**: `descriptors[i].kind == "UAV"` string comparison in hot loop.

**Fix**: Root signature range already specifies `D3D12_DESCRIPTOR_RANGE_TYPE_UAV/SRV/CBV`. Use integral type comparison:
```cpp
// Before:
if (descriptors[idx].kind == "UAV") ...
// After:
if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) ...
```
For the no-root-sig fallback, use `DescriptorKind::UAV_ID` (integer) instead of `DescriptorKind::UAV` (string_view).

---

## Expected Total Impact

| Phase | Change | Mechanism | Est. Savings |
|-------|--------|-----------|-------------|
| P1 | Root-sig-driven iteration | 64→~12 iterations | **-40%** (~1,240 us) |
| P2 | TLS fixed arrays | Eliminate heap alloc | **-20%** (~620 us) |
| P3 | Bulk descriptor lookup | N→1 hash traversal | **-25%** (~775 us) |
| P4 | Integer kind comparison | No string compare | **-15%** (~465 us) |
| **Cumulative** | | | **~3,100 us → ~310 us (90% reduction)** |

After optimization:
- BindCollect: 3,098 → ~310 us/frame
- Hook CPU total: 3,166 → ~400 us/frame
- Frame wall: 264 ms → unchanged (GPU-bound, our CPU overhead negligible)

---

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| P1 | Root sig parameters not 1:1 with snapshot arrays | D3D12 guarantees rootParameterIndex is 0-based and < NumParameters |
| P1 fallback | No root sig = must track maxRoot | Only bump on SetComputeRoot*, never shrink |
| P2 | 256-entry TLS arrays may overflow | Assert + fallback to dynamic allocation |
| P3 | Bulk lookup must preserve correct binding mapping | Store range-to-handle mapping in parallel index array |
| P4 | Kind mismatch if range declared as SRV but descriptor is UAV | D3D12 validation guarantees this; range type == descriptor type |

---

## Implementation Order
1. **P1 first** (largest win, lowest risk) — change loop bounds in `CollectComputeAll` and the inline version
2. **P2 second** — swap vectors for TLS arrays
3. **P4 third** — kind comparison cleanup (small, safe)
4. **P3 last** — bulk lookup (most complex, validate correctness carefully)

---

## Testing
- Build: `build_debug_x64.ps1`
- Run game, verify `d3d12_log.jsonl` BindCollect totalUs drops ~90%
- Verify PreSkin dispatch correctness (no missing UAV/SRV/CBV replacements)
- Check `CompiledDispatchDecision` still correctly resolves bindings
