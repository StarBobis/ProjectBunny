# DX12 Frame Timeline Attribution Plan

Date: 2026-07-05

## Purpose

The latest log shows a frame interval near 280 ms, while known DX12 hot-path
timing accounts for only about 51 ms and `PresentWait` accounts for about
0.2 ms. The missing time must be attributed before more architectural work can
be ranked correctly.

This phase adds a DX11-style per-frame accumulator around the outer DX12
boundaries. It does not change command recording, resource identity, IA hash
inputs, shader hashes, or mod matching semantics.

## Official API Constraints

- `IDXGISwapChain::Present` and `IDXGISwapChain1::Present1` are the frame
  presentation boundary. The runtime may synchronize presentation with the
  selected output.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
- `ID3D12CommandQueue::ExecuteCommandLists` is the explicit submission
  boundary. Multiple lists submitted together can reduce fixed submission cost,
  while separate submissions preserve ordering between calls.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
- `ID3D12Fence::GetCompletedValue` is a read-only fence query and can be used
  for diagnostics without changing queue synchronization.
  Reference:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue

## First-Principles Diagnosis

A frame interval is the wall-clock distance between two presentation
boundaries. If hot-path timers and Present wait do not sum to the frame
interval, the missing time is either:

- outside the current hooks between two Present calls,
- inside Present wrapper work not covered by the hot-path timers,
- inside queue submission wrappers,
- inside queue/fence waits,
- inside background worker drain waits,
- or inside application/driver CPU work that our hook layer does not currently
  classify.

The current architecture lacks a frame-level time conservation check. This
causes local optimizations to be ranked before the largest unmeasured region is
known.

## Implementation

Restore `DX12OuterTimingProbeUtils` as an accumulator instead of a no-op stub.
Existing call sites already wrap:

- Present enter, slices, and leave.
- ExecuteCommandLists wrapper and flow slices.
- Queue Signal and Wait.
- Fence and event wait slices.
- Background worker task and wait slices.

The probe will aggregate those samples with interlocked counters and emit one
`DX12FrameTimeline` line at each Present leave.

## Logged Fields

The frame line must include:

- frame wall time from previous Present leave to current Present leave.
- time outside the Present wrapper.
- Present wrapper wall time.
- Present original call time.
- Present post-original runtime time.
- `DX12ModBeginFrame` time.
- profiling and hot-path flush time.
- queue wrapper and queue flow total time.
- queue original submission time.
- queue notify/register/unwrap/rebuild slices.
- queue Signal/Wait time.
- fence/event wait time.
- background worker task and wait time.

## Expected Result

Behavior change:

```text
None.
```

Hash stability:

```text
No IA hash change.
No shader hash change.
No mod INI semantic change.
```

Diagnostic improvement:

```text
Unattributed frame time should drop from about 228 ms to a classified
Present/outside/queue/fence/worker bucket.
```

Performance expectation:

```text
Runtime performance gain: 0%.
Diagnostic coverage gain: expected from about 18% to near full frame-boundary
attribution.
```
