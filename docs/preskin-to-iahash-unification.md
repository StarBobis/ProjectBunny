# PreSkin → IaHash Unification Plan

## Goal
Replace PreSkin's custom fingerprint/cache/async layer with the IaHash pipeline,
while keeping binding collection and replacement application intact.

## Files to modify

### 1. DX12IaHashState.hpp
Add: `std::vector<DX12IaBufferHash> computeBuffers;`

### 2. DX12DispatchHookFlow.hpp — PreparePreSkinProbe
**Remove**: BuildStableBindingFingerprint, BuildCrossFrameStableBindingFingerprint, DX12ModGetCachedPreSkinProbeResult, gPreSkinAsyncQueue, DX12ModStoreCachedPreSkinningUavMatch
**Add**: Hash compute bindings → IaHashState → L3 cache check → match
**Keep**: CollectPreSkinBindings, DX12ModApplyPreSkinningUavReplacement

### 3. DX12ModRuntime.cpp
**Remove**: MakePreSkinUavMatchCacheKey, gPreSkinUavMatchCache, gPreSkinUavMatchCacheLru, MaxPreSkinUavMatchCacheEntries, DX12ModGetCachedPreSkinProbeResult, DX12ModStoreCachedPreSkinningUavMatch

### 4. DX12PreSkinPlan.hpp
**Remove**: BuildStableBindingFingerprint, BuildCrossFrameStableBindingFingerprint (keep only if referenced by other code)
