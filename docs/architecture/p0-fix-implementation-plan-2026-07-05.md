# P0 Fix Implementation Plan — 2026-07-05

## Principle: Hash Algorithm Invariance

**CRITICAL**: Mod/Dump/Hunting all depend on IA buffer hashing for resource matching.
The following functions MUST return identical results for identical inputs:
- `DX12ResolveBufferResourceByGpuVa` — GPU VA → resource resolution
- `DX12HashBufferView` / `DX12HashBufferResourceView` — buffer content hashing
- `DX12HashIaBufferView` — IA buffer hashing for Mod matching
- `HashBytes` / `crc32c_append` — low-level hash primitive

**Strategy**: Only change lookup DATA STRUCTURES (how we find the answer), never change lookup RESULTS (what answer we return). All changes are transparent to callers.

---

## P0-FIX-1: Eliminate O(n) fallback linear scan

### File: `resource/DX12ResourceTrackerQueries.cpp`

### Current code (lines 239-273):
After primary gBufferResourceByGpuVa lookup fails, falls back to linear scan of ALL gResources.

### Approach: Two-layer defense

**Layer 1**: Add `gBufferResourceByGpuVaFallback` — a std::map<UINT64, size_t> that indexes ALL buffer resources with non-zero GPU VA, maintained in the same places where gResources is updated. This includes resources that were missed by the primary index (e.g., reserved resources with delayed VA assignment).

**Layer 2**: Keep the linear scan as an absolute last resort, but bounded at kMaxFallbackScan entries. Log a warning when hit so we can track remaining edge cases.

### Changes:
1. In DX12ResourceTrackerPrivate.hpp: declare `gBufferResourceByGpuVaFallback`
2. In DX12ResourceTracker.cpp: populate gBufferResourceByGpuVaFallback at resource creation/update (same place gBufferResourceByGpuVa is populated)
3. In DX12ResourceTrackerQueries.cpp: replace linear scan with:
   a. Try gBufferResourceByGpuVaFallback lookup (O(log n))
   b. If still not found, bounded linear scan as safety net

---

## P0-FIX-2: Fix PurgeRangeLocked full-map iteration

### File: `resource/DX12ResourceTrackerPrivate.hpp`

### Current code (lines 342-358):
Iterates entire gBufferResolveCache to find overlapping entries.

### Fix:
Use lower_bound/upper_bound to narrow iteration to only the potentially-overlapping range. The map is keyed by begin address, so entries overlapping [begin, end) must have key < end. Find first entry with key closest to begin, then iterate until key >= end.

```cpp
// Find first entry whose begin is the largest ≤ our begin
// (because an entry starting before our range could still overlap into it)
auto it = gBufferResolveCache.lower_bound(begin);
if (it != gBufferResolveCache.begin()) --it;
// Iterate until we're past the end of our range
while (it != gBufferResolveCache.end() && it->first < end) {
    if (RangesOverlap(it->second.begin, it->second.end, begin, end)) {
        it = gBufferResolveCache.erase(it);
        removed++;
    } else {
        ++it;
    }
}
```

O(log n + k) instead of O(n), where k = overlapping entries count.

---

## Verification Checklist

- [ ] Mod: IA buffer override matching still works (hash values unchanged)
- [ ] Dump: Resource dump correctly resolves GPU VA → resource
- [ ] Hunting: Shader hunting descriptor resolution unchanged
- [ ] BufferResolveCache: Cache hits/misses produce same results
- [ ] No new compiler warnings
- [ ] build_debug_x64.ps1 succeeds
