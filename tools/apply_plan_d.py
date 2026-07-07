#!/usr/bin/env python3
"""方案D: Add PSO pre-compiled override plan to eliminate per-draw config rediscovery."""
import os

BASE = r'C:\Users\Administrator\Desktop\ProjectBunny\src\DirectX12\mod'
HPP = os.path.join(BASE, 'DX12ShaderOverrideRuntime.hpp')

with open(HPP, 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

# 1. Add DX12CompiledPsoOverridePlan struct before the DX12ShaderOverrideIaTargetPlanUtils class
# Find: "class DX12ShaderOverrideIaTargetPlanUtils"
# Insert before it

new_struct = '''// PSO-Create-Time pre-compiled override plan: snapshots all shader override configs
// for a PSO so draw-time discovery (lock+lookup+copy+iterate) is eliminated.
// Populated in RecordGraphicsPipelineState; consumed in PrepareShaderOverrideReplacement.
class DX12CompiledPsoOverridePlan
{
public:
\tbool valid = false;
\tbool alwaysMiss = false;
\tUINT64 generation = 0;
\tstd::vector<Bunny::ShaderOverrideConfig> configSnapshot;
};

static std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;
static SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;

static bool TryLoadCompiledPsoPlan(
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
}

static void StoreCompiledPsoPlan(
\tID3D12PipelineState *pipelineState,
\tUINT64 reloadGeneration,
\tconst std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
\tif (!pipelineState)
\t\treturn;

\tAcquireSRWLockExclusive(&gPsoOverridePlansLock);
\tgPsoOverridePlans[pipelineState] = {true, configSnapshot.empty(), reloadGeneration, configSnapshot};
\tReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void ClearCompiledPsoPlans()
{
\tAcquireSRWLockExclusive(&gPsoOverridePlansLock);
\tgPsoOverridePlans.clear();
\tReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

'''

insert_before = 'class DX12ShaderOverrideIaTargetPlanUtils'
content = content.replace(insert_before, new_struct + insert_before)

# 2. Modify DX12ModPrepareShaderOverrideReplacement to try compiled plan first
# Find the block that does the PSO config cache lookup and replace with compiled plan check

old_lookup = '''\tconst bool negativeCacheEnabled =
\t\tallowNegativeCache && ShaderOverrideNegativeCacheEnabled();
\tDX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
\tif (negativeCacheEnabled) {
\t\tnegativeCacheKey = MakeShaderOverrideNegativeCacheKey(
\t\t\tpipelineState, iaState,
\t\t\tvertexCount, indexCount, instanceCount, firstVertex, firstIndex);
\t\tif (TryFindShaderOverrideNegativeCache(negativeCacheKey))
\t\t\treturn false;
\t}

\t// Check PSO shader-override config cache FIRST, before acquiring the
\t// device.  Most draws have no matching configs; deferring the
\t// commandList→GetDevice QI (a D3D12 COM call) until we know configs
\t// actually exist eliminates ~400+ unnecessary QI+Release pairs per frame.
\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\tstd::vector<Bunny::ShaderOverrideConfig> configSnapshot;
\tbool cached = false;
\tAcquireSRWLockShared(&gModLock);
\tcached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
\tif (cached)
\t\tCopyShaderOverrideConfigs(configs, &configSnapshot);
\tReleaseSRWLockShared(&gModLock);

\tif (cached && configSnapshot.empty())
\t\treturn false;

\tif (!cached) {
\t\tAcquireSRWLockExclusive(&gModLock);
\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\tCopyShaderOverrideConfigs(configs, &configSnapshot);
\t\tReleaseSRWLockExclusive(&gModLock);
\t}
\tif (configSnapshot.empty())
\t\treturn false;'''

new_lookup = '''\tconst bool negativeCacheEnabled =
\t\tallowNegativeCache && ShaderOverrideNegativeCacheEnabled();
\tDX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
\tif (negativeCacheEnabled) {
\t\tnegativeCacheKey = MakeShaderOverrideNegativeCacheKey(
\t\t\tpipelineState, iaState,
\t\t\tvertexCount, indexCount, instanceCount, firstVertex, firstIndex);
\t\tif (TryFindShaderOverrideNegativeCache(negativeCacheKey))
\t\t\treturn false;
\t}

\t// 方案D: Try PSO pre-compiled plan first (zero-lock, zero-discovery).
\t// Eliminates per-draw lock+lookup+copy for PSOs recorded after mod loading.
\tconst UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
\tstd::vector<Bunny::ShaderOverrideConfig> configSnapshot;
\tbool alwaysMiss = false;
\tconst bool compiledPlanHit = TryLoadCompiledPsoPlan(
\t\tpipelineState, reloadGeneration, &configSnapshot, &alwaysMiss);
\tif (compiledPlanHit && alwaysMiss)
\t\treturn false;
\tbool cached = compiledPlanHit;

\tif (!compiledPlanHit) {
\t\t// Fallback: legacy PSO config cache lookup (first draw after mod reload).
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tAcquireSRWLockShared(&gModLock);
\t\tcached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\tif (cached)
\t\t\tCopyShaderOverrideConfigs(configs, &configSnapshot);
\t\tReleaseSRWLockShared(&gModLock);

\t\tif (cached && configSnapshot.empty())
\t\t\treturn false;

\t\tif (!cached) {
\t\t\tAcquireSRWLockExclusive(&gModLock);
\t\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\t\tCopyShaderOverrideConfigs(configs, &configSnapshot);
\t\t\tReleaseSRWLockExclusive(&gModLock);
\t\t}
\t\tif (configSnapshot.empty())
\t\t\treturn false;
\t}'''

content = content.replace(old_lookup, new_lookup)

# 3. Modify DX12ModRecordGraphicsPipelineState to also store compiled plan
# Find the block after FindShaderOverridesForPsoLocked call
old_record = '''\t// Eager PSO config compilation: compute shader overrides and IA target plan
\t// at PSO-create time so draws skip discovery entirely.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t}

\tgPsoRecords[pipelineState] = record;
\tReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState'''

new_record = '''\t// 方案D: Eager PSO config compilation + pre-compiled plan store.
\t// Eliminates per-draw lock+lookup+copy for subsequent draws.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, false, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
\t}

\tgPsoRecords[pipelineState] = record;
\tReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState'''

content = content.replace(old_record, new_record)

# 4. Also modify compute pipeline state record to store compiled plan
old_compute = '''void DX12ModRecordComputePipelineState(
\tID3D12Device *device, ID3D12PipelineState *pipelineState,
\tconst D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
\tif (!device || !pipelineState || !desc)
\t\treturn;

\tDX12StoredPso record;
\trecord.kind = DX12PsoKind::Compute;
\trecord.device = device;
\trecord.device->AddRef();
\tDeepCopyComputeDesc(desc, &record);

\tAcquireSRWLockExclusive(&gModLock);
\tauto existing = gPsoRecords.find(pipelineState);
\tif (existing != gPsoRecords.end())
\t\tReleaseStoredPso(&existing->second);
\tgPsoRecords[pipelineState] = record;
\tReleaseSRWLockExclusive(&gModLock);
}'''

new_compute = '''void DX12ModRecordComputePipelineState(
\tID3D12Device *device, ID3D12PipelineState *pipelineState,
\tconst D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
\tif (!device || !pipelineState || !desc)
\t\treturn;

\tDX12StoredPso record;
\trecord.kind = DX12PsoKind::Compute;
\trecord.device = device;
\trecord.device->AddRef();
\tDeepCopyComputeDesc(desc, &record);

\tAcquireSRWLockExclusive(&gModLock);
\tauto existing = gPsoRecords.find(pipelineState);
\tif (existing != gPsoRecords.end())
\t\tReleaseStoredPso(&existing->second);

\t// 方案D: Eager PSO config compilation for compute pipelines too.
\tif (DX12ModHasActiveShaderOverrides()) {
\t\tstd::vector<const Bunny::ShaderOverrideConfig*> configs;
\t\tFindShaderOverridesForPsoLocked(pipelineState, true, &configs);
\t\tstd::vector<Bunny::ShaderOverrideConfig> snapshot;
\t\tCopyShaderOverrideConfigs(configs, &snapshot);
\t\tStoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
\t}

\tgPsoRecords[pipelineState] = record;
\tReleaseSRWLockExclusive(&gModLock);
}'''

content = content.replace(old_compute, new_compute)

# 5. Clear compiled plans on mod reload — find ClearPsoDrawFastRejectCache and add ClearCompiledPsoPlans
content = content.replace(
    'DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();',
    'DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();\n\tClearCompiledPsoPlans();'
)

with open(HPP, 'w', encoding='utf-8') as f:
    f.write(content)
print('方案D: PSO Pre-Compiled Override Plan implemented')
print(f'  - Added DX12CompiledPsoOverridePlan struct')
print(f'  - TryLoadCompiledPsoPlan / StoreCompiledPsoPlan')
print(f'  - RecordGraphics: stores plan at PSO-create time')
print(f'  - RecordCompute: stores plan for compute PSOs')
print(f'  - PrepareShaderOverrideReplacement: uses plan to skip lock+lookup')
