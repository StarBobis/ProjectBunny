# DX12 Consumer Queue Threading Notes

## 2026-06-30

- Do not move DXGI `Present`, command queue execution, or command-list mutation into the consumer queue. These APIs carry caller-thread ordering, window-message, or command-list instance ownership assumptions.
- Safe queue candidates are CPU metadata tasks that either copy plain descriptor structs immediately or retain COM objects before enqueue, then update our own tracker state under `gResourceLock` or `gDescriptorLock`.
- When queueing metadata that later feeds draw/dispatch lookup, add a drain at the read boundary. Otherwise the hot path can record a PSO index and immediately fail to resolve its root signature because the consumer has not processed the task yet.
- Keep public APIs stable and route them through queue-backed implementations with an immediate fallback. This makes call sites less fragile and keeps future queue expansion centralized.
