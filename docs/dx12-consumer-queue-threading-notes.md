# DX12 Consumer Queue Threading Notes

## 2026-06-30

- Do not move DXGI `Present`, command queue execution, or command-list mutation into the consumer queue. These APIs carry caller-thread ordering, window-message, or command-list instance ownership assumptions.
- Safe queue candidates are CPU metadata tasks that either copy plain descriptor structs immediately or retain COM objects before enqueue, then update our own tracker state under `gResourceLock` or `gDescriptorLock`.
- When queueing metadata that later feeds draw/dispatch lookup, add a drain at the read boundary. Otherwise the hot path can record a PSO index and immediately fail to resolve its root signature because the consumer has not processed the task yet.
- Keep public APIs stable and route them through queue-backed implementations with an immediate fallback. This makes call sites less fragile and keeps future queue expansion centralized.
- Logs can show the queue work is already off-thread while the hook wrapper remains hot. In that case, check for synchronous `JsonFuncFlush` and repeated policy predicates on the caller thread before moving more D3D-facing work.
- Descriptor view creation should keep the real D3D12 `Create*View` call on the caller thread. Only queue descriptor metadata after designing batching/backpressure; raw per-descriptor tasks are too bursty for the shared consumer queue.
- Failed attempt: moving all `Create*View` descriptor records into the shared resource collect queue caused descriptor bursts to grow pending work past 65k while the worker stayed behind. Do not put per-descriptor creation traffic on the same single consumer queue without coalescing, backpressure, or a dedicated descriptor pipeline.
- If descriptor metadata is revisited, prefer batching/range coalescing or a bounded latest-value cache keyed by CPU handle instead of one queue task per descriptor.
- Follow-up architecture: descriptor metadata now enters through `DX12DescriptorCollectPipeline` instead of `DX12ResourceCollectQueue`. The first adapter is intentionally synchronous, with drain hooks at descriptor read boundaries, so future batching or latest-value coalescing can be added without putting descriptor bursts back into the shared resource FIFO.
