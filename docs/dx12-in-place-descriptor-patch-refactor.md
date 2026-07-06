# DX12 Draw SRV In-Place Descriptor Patch Refactor

## Root Cause: SetDescriptorHeaps Invalidates Other Root Tables

### Bug Chain

```
ApplyRequests:
1. CopyDescriptorsSimple(table.original → tempHeap)      // copy patched table only
2. CopyDescriptorsSimple(replacementSrv → tempHeap[idx])  // patch entry
3. SetDescriptorHeaps(tempHeap, samplerHeap)              // ← BREAKS ALL OTHER TABLES!
4. SetGraphicsRootDescriptorTable(root=1, patchedGpu)     // rebind patched table
5. Draw → shader reads SRV from tempHeap ✓
          BUT CBV/UAV from originalHeap → INVALID! ✗

Root Signature:
  Parameter 0: CBV descriptor table → GPU handle 0xORIGINAL_HEAP_offset
  Parameter 1: SRV descriptor table → GPU handle 0xORIGINAL_HEAP_offset (patched to tempHeap)

After SetDescriptorHeaps(tempHeap):
  Parameter 0's GPU handle references ORIGINAL heap → driver may reject / read garbage
  Parameter 1's GPU handle references TEMP heap → corrected via SetGraphicsRootDescriptorTable
  
Result: Shader reads correct SRV but wrong CBV → constant buffer values garbage → model black
```

### Fix: In-Place Descriptor Patching

DO NOT change descriptor heaps. DO NOT rebind root parameter tables.

Instead:
1. Save the original descriptor value from the original heap
2. Write the replacement SRV into the original heap at the target offset
3. Execute the draw call (shader reads all bindings from original heap → all correct)
4. Restore the saved original descriptor back to the original heap

```cpp
// Before draw:
// Read original from game's shader-visible heap → save
D3D12_CPU_DESCRIPTOR_HANDLE targetCpu = {};
targetCpu.ptr = binding.tableCpuStart + binding.descriptorOffset * heapIncrement;
ReadDescriptorFromHeap(device, targetCpu, &savedOriginal);

// Write replacement to game's heap
device->CopyDescriptorsSimple(1, targetCpu, replacementCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

// Draw (game's own heap, no changes needed)

// After draw: restore
device->CopyDescriptorsSimple(1, targetCpu, savedOriginalCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
```

### Why This Is Safe

1. `CopyDescriptorsSimple` from the D3D12 spec: "The source and destination may share the same heap."
2. Writing to a shader-visible heap is allowed for command lists that haven't been submitted yet.
3. The draw call happens on the same command list, at recording time, before Submit.
4. No heap switching means all root parameter tables continue to work.

### Changes Required

1. **Modify `DX12DrawSrvReplacementUtils::ApplyRequests`**: Remove temp heap allocation, remove SetDescriptorHeaps, remove SetGraphicsRootDescriptorTable. Replace with in-place descriptor read/save → write replacement → draw → restore loop.

2. **Modify `DX12DrawSrvReplacementUtils::Restore`**: Instead of restoring root parameter tables, just copy saved descriptors back to original heap.

3. **Add `DX12DrawSrvPatchTable.restoreCpu`**: Store the saved original descriptor handle for restoration.

4. **Remove unused**: `TempHeap`, `EnsurePreSkinDescriptorRingLocked` usage in this path.

### Risk

- Writing to shader-visible heap: Technically requires the heap to be in a state where writes are allowed. On NVIDIA/AMD GPUs, writes to shader-visible heaps during command list recording work correctly. Verified against ReShade and RenoDX approaches.
- Concurrency: If another command list references the same heap while being executed on the GPU queue, modifying the heap could cause issues. But since we do this BEFORE the command list is submitted (in the recording phase), the GPU hasn't seen the heap's state yet. Safe.
