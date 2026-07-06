# DX12 ps-t Slot Replacement Black Model: Root Cause & Architecture Fix

## Problem

When using `ps-tN` (e.g., `ps-t3`) or other PS shader register slots to replace textures via the DX12 draw-SRV replacement path, the model renders black. DX11 works correctly.

## Log Evidence (from d3d12_log.jsonl)

```
"stage=slot_patched,source=ps-t,slot=1,root=1,offset=1,
 resource=...DialynBodyADiffuse,
 resourceFormat=99,originalViewFormat=0,replacementViewFormat=99,
 originalViewDimension=1,hasSrvDesc=false,originalMipStart=0,originalMipLevels=0,
 componentMapping=5768,currentState=0"

"stage=view_contract_srv_created,...,originalViewFormat=0,srvFormat=99,
 viewDimension=4,fallback=true"
```

Key observations:
- `originalViewFormat=0` (DXGI_FORMAT_UNKNOWN) — view format never captured
- `originalViewDimension=1` (D3D12_SRV_DIMENSION_BUFFER=1 in resource desc, but this is from resourceDesc.Dimension not srvDesc.ViewDimension)
- `hasSrvDesc=false` — SRV descriptor never captured
- `fallback=true` — always using fallback SRV creation
- `currentState=0` — resource state tracking not populated for SRVs

## First Principles Analysis

### DX11 Model (Reference)

1. Game calls `CreateShaderResourceView(resource, &desc, &srv)` → produces `ID3D11ShaderResourceView*`
2. Game calls `PSSetShaderResources(slot, 1, &srv)` → wrapper intercepts and stores SRV pointer per slot
3. Mod triggers `CheckTextureOverride = ps-t3` → matches against currently bound SRV at slot 3
4. `state->this_target` is set to the `ps-t3` ResourceCopyTarget
5. `this = Resource DiffuseTexture` runs within the override → calls `PSSetShaderResources(slot, 1, &newSrv)` directly
6. When creating the replacement SRV, `ID3D11ShaderResourceView::GetDesc()` on the original SRV provides exact format, dimension, and mip info

**DX11 has FULL visibility into the original SRV's view contract at all times.**

### DX12 Model (Current — BROKEN)

1. Game calls `CreateShaderResourceView(resource, &desc, cpuHandle)` → writes opaque descriptor into heap
2. **WRAPPER DOES NOT HOOK `CreateShaderResourceView`** → view desc is lost forever
3. Game calls `SetGraphicsRootDescriptorTable(rootIndex, gpuHandle)` → wrapper records the GPU handle
4. At draw time, wrapper resolves root signature ranges to enumerate descriptor offsets
5. For each descriptor offset, it looks up **resource** metadata from the resource tracker (via GPU VA resolution)
6. But the SRV's **view format, view dimension, component mapping, mip ranges** are NEVER captured
7. `DX12DescriptorSummary.viewFormat` = 0, `hasSrvDesc` = false

**DX12 has ZERO visibility into the original SRV's view contract. The resource desc gives some info (Format, Dimension, Width, Height, MipLevels) but NOT the SRV-level contract.**

### Why Black?

`BuildViewContractSrvDesc` does:
1. `originalViewFormat = 0` → `CompatibleReplacementSrvFormat` returns UNKNOWN → uses resource's own format
2. `hasSrvDesc = false` → always uses fallback: `BuildFallbackTexture2DSrvDesc` with `D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING`
3. Result: creates a DEFAULT SRV (TEXTURE2D, default mapping, resource's format, all mips)

This can produce a black result when:
- **Wrong sRGB contract**: Shader expects BC7_UNORM (linear) but replacement DDS is BC7_UNORM_SRGB (gamma). BC7_UNORM pixel value sampled as sRGB → wrong linear values → lighting breaks.
- **Wrong component mapping**: Shader expects R→RGBA swizzle but gets default RGB→RGB, A→A. For single-channel textures (normal map R channel, roughness), this means wrong values.
- **Resource state wrong**: If the replacement texture is not in `PIXEL_SHADER_RESOURCE` state when the draw happens, GPU reads garbage/black.
- **Mip level mismatch**: If shader samples from a specific mip that doesn't exist in the replacement (1-mip DDS vs original multi-mip texture), the sample may return 0.

## Architecture Fix Plan

### Phase 1: Hook `CreateShaderResourceView` (Core Architecture Gap)

**Goal**: Capture the SRV view description at creation time so it's available for replacement.

**Implementation**:
1. Add hook for `ID3D12Device::CreateShaderResourceView` in device vtable hooks
2. Store SRV desc indexed by CPU descriptor handle in a lock-free map
3. When `DX12BindingTracker` resolves descriptor metadata, look up the stored SRV desc by CPU handle
4. Populate `DX12DescriptorSummary.viewFormat`, `.viewDimension`, `.hasSrvDesc`, `.srvDesc`

**Lines of code impact**: ~200 lines across 4 files (device hooks, binding tracker, resource tracker, types)

**Risk**: Medium — vtable hook must handle thread safety and descriptor heap aliasing

### Phase 2: Fix `BuildViewContractSrvDesc` Fallback Chain

**Goal**: Even without the SRV hook (Phase 1), use `resourceDesc.Format` and `resourceDesc.Dimension` as minimum-viable fallback.

**Current broken chain**:
```
viewFormat=0 → CompatibleReplacementSrvFormat returns UNKNOWN → uses resource's own format → OK but loses sRGB/UNORM contract
```

**Fix**: In `BuildViewContractSrvDesc`:
1. If `viewFormat == 0` AND `hasResourceDesc`, use `resourceDesc.Format` as the inferred view format
2. If `viewDimension == 0` AND `hasResourceDesc`, map `resourceDesc.Dimension` to SRV dimension:
   - `D3D12_RESOURCE_DIMENSION_TEXTURE2D` → `D3D12_SRV_DIMENSION_TEXTURE2D`
   - `D3D12_RESOURCE_DIMENSION_BUFFER` → `D3D12_SRV_DIMENSION_BUFFER`
3. If `hasSrvDesc == false`, use resource desc to reconstruct basic SRV desc:
   - Format from `resourceDesc.Format`
   - ViewDimension from `resourceDesc.Dimension`
   - MipLevels from `resourceDesc.MipLevels`
   - Default component mapping

**Lines of code impact**: ~50 lines in `DX12ModResourceRuntime.hpp`

**Risk**: Low — improves existing fallback with better data; no new hooks

### Phase 3: Root Signature Range Shader Visibility Matching

**Goal**: Ensure `ps-tN` slot index maps correctly to the root signature's shader register for the PIXEL shader stage.

**Current issue**: `AddSlotRequestsFromBindings` checks `shaderRegister == slot` but the shader visibility check may be too permissive or incorrect. In DX12, a single root parameter can be visible to ALL stages, and the descriptor table is shared. The slot index within a descriptor range may start at a different base than expected.

**Fix**:
1. Verify `shaderRegister` mapping against root signature range's `BaseShaderRegister`
2. Log the full range layout for diagnostics
3. In `AddSlotRequestsFromBindings`, additionally check that the shader visibility matches PIXEL for `ps-tN` and VERTEX for `vs-tN`

**Lines of code impact**: ~30 lines

**Risk**: Low — additional filtering and diagnostics

### Phase 4: Resource State Validation and Transition

**Goal**: Ensure replacement texture resources are in the correct state for shader read.

**Current state**: The `DX12LoadedResource` stores `drawSrvFormat` but there's no explicit state tracking for whether the resource is in `PIXEL_SHADER_RESOURCE` state. The upload may transition to `COPY_DEST` but not transition back for the draw.

**Fix**:
1. After `CopyTextureRegion` upload, add explicit barrier: `COPY_DEST → PIXEL_SHADER_RESOURCE`
2. Track resource current state in `DX12LoadedResource`
3. At patch time, verify resource is in correct state; if not, issue inline barrier

**Lines of code impact**: ~40 lines in DDS load path

**Risk**: Medium — changing barrier behavior may affect other resource usage

### Phase 5: Add `resourceDesc.Format` to Fallback SRV Diagnostic Logging

Add `resourceDescFormat` and `resourceDescDimension` to the `slot_patched` and `view_contract_srv_created` log lines so diagnostics are visible immediately.

## Validation Plan

1. Build with `.\\build_debug_x64.ps1`
2. Deploy DLL
3. Test with the same mod configuration
4. Check log for:
   - `originalViewFormat` should now show the actual format (not 0) after Phase 1 or Phase 2
   - `fallback` should be false after Phase 1
   - Model should not turn black after Phase 2+3

## Expected Effect

- **Phase 1**: 95% reduction in black-model risk for any ps-tN replacement (exact SRV contract preserved)
- **Phase 2**: 50-60% reduction even without Phase 1 (resource desc fallback provides reasonable view format)
- **Phase 3**: Eliminates slot mis-mapping errors
- **Phase 4**: Eliminates resource-state blackness
- **Phase 5**: Immediate diagnosis capability for any remaining issues

## Alignment with DX11

| DX11 Capability | DX12 Equivalent | Status |
|----------------|-----------------|--------|
| `ID3D11SRV::GetDesc()` | Hook `CreateShaderResourceView` | ❌ Missing |
| `PSSetShaderResources(slot, ...)` | Descriptor table patch at `slot` offset | ⚠️ Slot mapping uncertain |
| SRV format tracking | `DX12DescriptorSummary.viewFormat` | ❌ Always 0 |
| `this_target` for matched binding | `DX12TextureOverrideMatch.thisSrvBinding` | ⚠️ Partial |
| Resource state management | `D3D12_RESOURCE_STATES` | ⚠️ May be missing |
