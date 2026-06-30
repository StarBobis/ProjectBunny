# DX12 Always-On Minimal Residency Notes

## 2026-07-01

- Always-on residency rule: ordinary Mod injection should keep only the minimum facts that D3D12 will never let us reconstruct later, such as descriptor create parameters, descriptor copy ranges, resource birth metadata, and command-list binding pointers/handles. Full resource-desc completion, PSO/root summary expansion, and binding reconstruction belong to query, dump, or capture paths.
- Root-signature query consistency rule: `DX12GetRootSignatureSummary()` and `DX12GetPsoRootSignature()` are metadata-map lookups, not descriptor-content reads. If their correctness depends on draining the descriptor pipeline first, the architecture boundary is wrong. Keep root-signature and PSO-root records updated synchronously at record time so query paths can stay lock-only.
- Descriptor latest-value budget rule: always-on `Create*View` traffic can be millions of writes per minute even when replacement rate is near zero. A latest-value buffer keyed by CPU handle is the right default, but it still needs a bounded flush budget so unusual descriptor churn cannot accumulate unbounded pending residency.
- Resource metadata split rule: future dump correctness still needs live resource birth records, but always-on command paths do not need eager `FillResourceInfo()`-style completion on every descriptor create. Keep live resource indices resident; gate full resource metadata completion behind dump/hunting/query demand.
