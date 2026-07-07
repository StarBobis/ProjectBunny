#!/usr/bin/env python3
"""方案F+G+H: IA bypass + descriptor cache + combined fast reject."""

HPP = r'C:\Users\Administrator\Desktop\ProjectBunny\src\DirectX12\mod\DX12ShaderOverrideRuntime.hpp'

with open(HPP, 'r', encoding='utf-8') as f:
    content = f.read()

# ===== 1. Add hasIaDependentConfigs to DX12CompiledPsoOverridePlan =====
old = '''class DX12CompiledPsoOverridePlan
{
public:
\tbool valid = false;
\tbool alwaysMiss = false;
\tUINT64 generation = 0;
\tstd::vector<Bunny::ShaderOverrideConfig> configSnapshot;
};'''

new = '''class DX12CompiledPsoOverridePlan
{
public:
\tbool valid = false;
\tbool alwaysMiss = false;
\tbool hasIaDependentConfigs = false;
\tbool hasDescriptorDependentConfigs = false;
\tUINT64 generation = 0;
\tstd::vector<Bunny::ShaderOverrideConfig> configSnapshot;
};'''

content = content.replace(old, new, 1)

# ===== 2. Modify TryLoadCompiledPsoPlan to also return the flags =====
old = '''static bool TryLoadCompiledPsoPlan(
\tID3D12PipelineState *pipelineState,
\tUINT64 reloadGeneration,
\tstd::vector<Bunny::ShaderOverrideConfig> *configSnapshot,
\tbool *alwaysMiss)
{
\tif (!configSnapshot || !alwaysMiss)
\t\treturn false;
\t*alwaysMiss = false;
\tconfigSnapshot->clear();
\tif (!pipelineState)
\t\treturn false;

\tAcquireSRWLockShared(&gPsoOverridePlansLock);
\tauto it = gPsoOverridePlans.find(pipelineState);
\tif (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
\t\tReleaseSRWLockShared(&gPsoOverridePlansLock);
\t\treturn false;
\t}
\t*alwaysMiss = it->second.alwaysMiss;
\tif (!it->second.alwaysMiss)
\t\t*configSnapshot = it->second.configSnapshot;
\tReleaseSRWLockShared(&gPsoOverridePlansLock);
\treturn true;
}'''

new = '''static bool TryLoadCompiledPsoPlan(
\tID3D12PipelineState *pipelineState,
\tUINT64 reloadGeneration,
\tstd::vector<Bunny::ShaderOverrideConfig> *configSnapshot,
\tbool *alwaysMiss,
\tbool *hasIaDependentConfigs = nullptr,
\tbool *hasDescriptorDependentConfigs = nullptr)
{
\tif (!configSnapshot || !alwaysMiss)
\t\treturn false;
\t*alwaysMiss = false;
\tif (hasIaDependentConfigs) *hasIaDependentConfigs = true;
\tif (hasDescriptorDependentConfigs) *hasDescriptorDependentConfigs = true;
\tconfigSnapshot->clear();
\tif (!pipelineState)
\t\treturn false;

\tAcquireSRWLockShared(&gPsoOverridePlansLock);
\tauto it = gPsoOverridePlans.find(pipelineState);
\tif (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
\t\tReleaseSRWLockShared(&gPsoOverridePlansLock);
\t\treturn false;
\t}
\t*alwaysMiss = it->second.alwaysMiss;
\tif (hasIaDependentConfigs) *hasIaDependentConfigs = it->second.hasIaDependentConfigs;
\tif (hasDescriptorDependentConfigs) *hasDescriptorDependentConfigs = it->second.hasDescriptorDependentConfigs;
\tif (!it->second.alwaysMiss)
\t\t*configSnapshot = it->second.configSnapshot;
\tReleaseSRWLockShared(&gPsoOverridePlansLock);
\treturn true;
}'''

content = content.replace(old, new, 1)

# ===== 3. Modify StoreCompiledPsoPlan to accept target plan and compute flags =====
old = '''static void StoreCompiledPsoPlan(
\tID3D12PipelineState *pipelineState,
\tUINT64 reloadGeneration,
\tconst std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
\t// Never store empty plans: PSOs created before mod loads would cache \"alwaysMiss\"
\t// and never recover when the mod later loads matching configs.
\tif (!pipelineState || configSnapshot.empty())
\t\treturn;

\tAcquireSRWLockExclusive(&gPsoOverridePlansLock);
\tgPsoOverridePlans[pipelineState] = {true, false, reloadGeneration, configSnapshot};
\tReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}'''

new = '''static void StoreCompiledPsoPlan(
\tID3D12PipelineState *pipelineState,
\tUINT64 reloadGeneration,
\tconst std::vector<Bunny::ShaderOverrideConfig> &configSnapshot,
\tbool hasIaDependentConfigs = true,
\tbool hasDescriptorDependentConfigs = false)
{
\t// Never store empty plans: PSOs created before mod loads would cache \"alwaysMiss\"
\t// and never recover when the mod later loads matching configs.
\tif (!pipelineState || configSnapshot.empty())
\t\treturn;

\tDX12CompiledPsoOverridePlan plan;
\tplan.valid = true;
\tplan.alwaysMiss = false;
\tplan.hasIaDependentConfigs = hasIaDependentConfigs;
\tplan.hasDescriptorDependentConfigs = hasDescriptorDependentConfigs;
\tplan.generation = reloadGeneration;
\tplan.configSnapshot = configSnapshot;

\tAcquireSRWLockExclusive(&gPsoOverridePlansLock);
\tgPsoOverridePlans[pipelineState] = plan;
\tReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}'''

content = content.replace(old, new, 1)

# ===== 4. Update RecordGraphicsPipelineState to read target plan =====
old = '''\t// 方案D: Eager PSO config compilation + pre-compiled plan store.
\t// Eliminates per-draw lock+lookup+copy for subsequent draws.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
\t}'''

new = '''\t// 方案F: Eager PSO config compilation + plan store with IA/descriptor flags.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);

\t\t// Read target plan from PSO match cache to compute IA dependency flags.
\t\tDX12IaHashTargetPlan targetPlan;
\t\tbool hasIaTargets = true;
\t\tbool hasDescTargets = false;
\t\tif (TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(pipelineState, &targetPlan)) {
\t\t\thasIaTargets = targetPlan.HasIaTargets();
\t\t\thasDescTargets = targetPlan.hasDescriptorTargets;
\t\t}
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot,
\t\t\thasIaTargets, hasDescTargets);
\t}'''

content = content.replace(old, new, 1)

# ===== 5. Update RecordComputePipelineState similarly =====
old = '''\t// 方案D: Eager PSO config compilation for compute pipelines too.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, true, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
\t}'''

new = '''\t// 方案F: Eager PSO config compilation for compute pipelines with IA flags.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, true, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
\t}'''

content = content.replace(old, new, 1)

# ===== 6. Add fast path in DX12ModPrepareShaderOverrideReplacement =====
# When compiled plan says no IA-dependent configs, skip expensive executor work
# and do lightweight check only.

old = '''\tID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
\tconst bool deviceFromCache = (cachedDevice != nullptr);
\tif (!device)
\t\treturn false;
\tstd::vector<DX12PendingResourceViewRequest> pendingResourceViews;
\tDX12CommandListExecutor executor(
\t\tdevice, commandList, iaState,
\t\tvertexCount, indexCount, instanceCount,
\t\tfirstVertex, firstIndex, replacement, &pendingResourceViews);
\texecutor.SetExecutionMode(true, true);
\tfor (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
\t\texecutor.RunShaderOverride(config);
\t}
\tconst bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
\t\tdevice, commandList, iaState, pendingResourceViews, replacement);
\tif (resourcesChanged)
\t\texecutor.MarkExternalChange();
\tLogShaderOverrideCommandListSnapshotLimited(\"pre\", configSnapshot, *replacement);
\tbool changed = executor.Changed() || replacement->skip || !replacement->draws.empty() ||
\t\t!replacement->dispatches.empty();
\tif (!deviceFromCache)
\t\tdevice->Release();

\tif (negativeCacheEnabled && !changed && pendingResourceViews.empty())
\t\tStoreShaderOverrideNegativeCache(negativeCacheKey);

\treturn changed;'''

new = '''\tID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
\tconst bool deviceFromCache = (cachedDevice != nullptr);
\tif (!device)
\t\treturn false;

\t// 方案F+G Fast path: when compiled plan has no IA-dependent configs,
\t// skip the heavy DX12CommandListExecutor and do lightweight work.
\tstd::vector<DX12PendingResourceViewRequest> pendingResourceViews;
\tbool changed = false;

\tif (compiledPlanHit && !hasIaDependentConfigs) {
\t\t// Descriptor-only or direct-apply configs: skip executor construction.
\t\t// Only apply pending resource views which are pre-determined.
\t\tconst bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
\t\t\tdevice, commandList, iaState, pendingResourceViews, replacement);
\t\tchanged = resourcesChanged || replacement->skip;
\t} else {
\t\tDX12CommandListExecutor executor(
\t\t\tdevice, commandList, iaState,
\t\t\tvertexCount, indexCount, instanceCount,
\t\t\tfirstVertex, firstIndex, replacement, &pendingResourceViews);
\t\texecutor.SetExecutionMode(true, true);
\t\tfor (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
\t\t\texecutor.RunShaderOverride(config);
\t\t}
\t\tconst bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
\t\t\tdevice, commandList, iaState, pendingResourceViews, replacement);
\t\tif (resourcesChanged)
\t\t\texecutor.MarkExternalChange();
\t\tLogShaderOverrideCommandListSnapshotLimited(\"pre\", configSnapshot, *replacement);
\t\tchanged = executor.Changed() || replacement->skip || !replacement->draws.empty() ||
\t\t\t!replacement->dispatches.empty();
\t}
\tif (!deviceFromCache)
\t\tdevice->Release();

\tif (negativeCacheEnabled && !changed && pendingResourceViews.empty())
\t\tStoreShaderOverrideNegativeCache(negativeCacheKey);

\treturn changed;'''

content = content.replace(old, new, 1)

# Also update the draw path load call to capture hasIaDependentConfigs
old = '''\tconst bool compiledPlanHit = TryLoadCompiledPsoPlan(
\t\tpipelineState, reloadGeneration, &configSnapshot, &alwaysMiss);
\tif (compiledPlanHit && alwaysMiss)
\t\treturn false;
\tbool cached = compiledPlanHit;'''

new = '''\tbool hasIaDependentConfigs = true;
\tconst bool compiledPlanHit = TryLoadCompiledPsoPlan(
\t\tpipelineState, reloadGeneration, &configSnapshot, &alwaysMiss,
\t\t&hasIaDependentConfigs);
\tif (compiledPlanHit && alwaysMiss)
\t\treturn false;
\tbool cached = compiledPlanHit;'''

content = content.replace(old, new, 1)

with open(HPP, 'w', encoding='utf-8') as f:
    f.write(content)
print('方案F+G+H applied successfully')
