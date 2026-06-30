# DX12 Descriptor Lazy Materialization Notes

## 2026-07-01

- Copy-range flush correction: `CopyDescriptors*` does not require globally draining every pending descriptor write first. It only needs source descriptors to become visible before the copy is mirrored, and pending writes to destination handles must be discarded because the CPU-immediate copy overwrites them. Global pre-copy flush turns unrelated descriptor churn into submit-thread CPU hotspots.
- Query drain scope correction: descriptor summary queries should not call a global `DrainForSnapshot()` by default. Materialize only the queried CPU handles, and only force pending copy ranges that touch those handles. Otherwise a single lightweight lookup can accidentally serialize unrelated descriptor traffic from the whole process.
- Descriptor cache invalidation correction: binding query caches should not key descriptor freshness only on history epoch. Ordinary always-on descriptor writes can change current bindings without touching capture-history epoch, so add a dedicated descriptor query serial that bumps when visible descriptor state changes.
- Worker activation correction: a descriptor pipeline with a worker thread must actually wake the worker on queued descriptor writes. Without `StartWorker()` and `wake.notify_one()`, the design silently falls back to caller-thread flushes when budgets or queries trigger visibility, which defeats the purpose of deferred materialization.
- Versioning cost correction: descriptor query serial should bump on committed visible changes, not on every internal clear/replace micro-step. Over-bumping inside slot replacement loops risks reintroducing atomic contention during descriptor copy storms while providing no extra correctness.
