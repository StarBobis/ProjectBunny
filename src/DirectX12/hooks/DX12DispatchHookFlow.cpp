#include <algorithm>
#include "DX12DispatchHookFlow.h"

#include <cstring>
#include <vector>

#include "DX12BindingTracker.h"
#include "DX12ModRuntime.h"
#include "DX12Profiling.hpp"
#include "DX12ShaderDump.h"
#include "DX12LogManager.hpp"

#if defined(_DEBUG)
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do { stmt; } while (0)
#else
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do {} while (0)
#endif

static void LogPreSkinDispatchProbeReason(
	const char *reason,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	size_t uavCount)
{
#if defined(_DEBUG)
	if (reason && (!strcmp(reason, "uav_filter_miss") ||
	    !strcmp(reason, "cached_no_uav_match") ||
	    !strcmp(reason, "cs_not_configured"))) {
		return;
	}
	DX12LogManager::DebugJsonFunc("DX12PreSkinDispatchProbe",
		"\"reason\":\"%s\",\"cs\":\"%016llx\",\"bindingSerial\":%llu,\"uavs\":%zu",
		reason ? reason : "unknown",
		static_cast<unsigned long long>(computeShaderHash),
		static_cast<unsigned long long>(computeBindingSerial),
		uavCount);
#else
	(void)reason;
	(void)computeShaderHash;
	(void)computeBindingSerial;
	(void)uavCount;
#endif
}

struct DX12DispatchPreSkinProbe
{
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;
	bool uavsFound = false;
	bool srvsFound = false;
	bool cbvsFound = false;
	bool rootConstantsFound = false;
	bool applied = false;
	bool shouldLog = false;
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	DX12PreSkinDispatchOverride dispatchOverride;
};

static DX12DispatchPreSkinProbe &GetDispatchPreSkinProbeScratch()
{
	static thread_local DX12DispatchPreSkinProbe probe;
	probe.uavs.clear();
	probe.srvs.clear();
	probe.cbvs.clear();
	probe.rootConstants.clear();
	probe.uavsFound = false;
	probe.srvsFound = false;
	probe.cbvsFound = false;
	probe.rootConstantsFound = false;
	probe.applied = false;
	probe.shouldLog = false;
	probe.computeShaderHash = 0;
	probe.computeBindingSerial = 0;
	probe.originalVertexCount = 0;
	probe.overrideVertexCount = 0;
	probe.dispatchOverride = DX12PreSkinDispatchOverride();
	return probe;
}

static void PreparePreSkinProbe(
	ID3D12GraphicsCommandList *commandList,
	const DX12CommandListRuntimeState &runtimeState,
	DX12DispatchPreSkinProbe *probe)
{
	if (!probe || !DX12ModNeedsPreSkinningUavProbe()) {
		DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
		return;
	}

	DX12PsoShaderInfo shaderInfo = {};
	if (DX12GetPipelineStateShaderInfo(runtimeState.pipelineState, &shaderInfo) &&
	    shaderInfo.hasCS)
		probe->computeShaderHash = shaderInfo.cs;
	if (!probe->computeShaderHash) {
		DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
		LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
		return;
	}

	DX12LogManager::Text("PreparePreSkinProbe: cs=%016llx, checking ShouldProbe\n",
		static_cast<unsigned long long>(probe->computeShaderHash));

	if (!DX12ModShouldProbePreSkinningForCs(probe->computeShaderHash)) {
		DX12LogManager::Text("PreparePreSkinProbe: exit cs not configured\n");
		LogPreSkinDispatchProbeReason(
			"cs_not_configured", probe->computeShaderHash, 0, 0);
		return;
	}

	UINT64 computeBindingSerial =
		DX12BindingGetComputeBindingSerial(commandList);
	DX12LogManager::Text("PreparePreSkinProbe: serial=%llu\n",
		static_cast<unsigned long long>(computeBindingSerial));
	if (computeBindingSerial == 0) {
		DX12LogManager::Text("PreparePreSkinProbe: zero serial, using fallback\n");
		computeBindingSerial = 1;
	} else {
		bool cachedMatch = false;
		if (DX12ModHasCachedPreSkinningUavMatch(
			    commandList, probe->computeShaderHash, computeBindingSerial, &cachedMatch) &&
		    !cachedMatch) {
			DX12LogManager::Text("PreparePreSkinProbe: exit cached no match\n");
			return;
		}
	}
	probe->computeBindingSerial = computeBindingSerial;
#if defined(_DEBUG)
	probe->shouldLog = true;
#endif
	if (!probe->uavsFound)
		probe->uavsFound = DX12BindingGetCurrentComputeAll(commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
	probe->srvsFound = !probe->srvs.empty();
	probe->cbvsFound = !probe->cbvs.empty();
	probe->rootConstantsFound = !probe->rootConstants.empty();
	DX12LogManager::Text("PreparePreSkinProbe: uavsFound=%d uavCount=%zu srvCount=%zu cbvCount=%zu\n",
		probe->uavsFound ? 1 : 0, probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());

	if (!probe->srvsFound) {
		DX12LogManager::Text("PreparePreSkinProbe: warning no_compute_srvs\n");
	}

	static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
	static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
	DX12LogManager::Text("PreparePreSkinProbe: calling DX12ModApplyPreSkinningUavReplacement\n");
	probe->applied = DX12ModApplyPreSkinningUavReplacement(
		commandList, probe->computeShaderHash,
		probe->uavsFound ? probe->uavs : kEmptyComputeBindings,
		probe->srvs,
		probe->cbvsFound ? probe->cbvs : kEmptyComputeBindings,
		probe->rootConstantsFound ? probe->rootConstants : kEmptyRootConstants,
		&probe->originalVertexCount, &probe->overrideVertexCount,
		&probe->dispatchOverride);
	DX12ModStoreCachedPreSkinningUavMatch(
		commandList, probe->computeShaderHash, computeBindingSerial,
		probe->applied);
}

static void FinishPreSkinProbe(
	ID3D12GraphicsCommandList *commandList,
	UINT originalThreadGroupCountX,
	UINT originalThreadGroupCountY,
	UINT originalThreadGroupCountZ,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ,
	const DX12DispatchPreSkinProbe &probe)
{
	if (probe.applied)
		DX12ModRestorePreSkinningUavReplacement(commandList);
	if (probe.uavsFound)
		DX12ModRecordComputeUavs(commandList, probe.uavs);
	if (!probe.applied)
		return;

	DX12_DISPATCH_DEBUG_VERBOSE_LOG(
		DX12LogManager::DebugJsonFunc("DX12DispatchUavProbe",
			"\"found\":%s,\"uavs\":%zu,\"srvs\":%zu,\"cbvs\":%zu,\"rootConstants\":%zu,\"preSkinApplied\":%s,\"handlingSkip\":%s,\"explicitDispatch\":%s,\"cs\":\"%016llx\",\"originalGroups\":\"%u,%u,%u\",\"groups\":\"%u,%u,%u\"",
			probe.uavsFound ? "true" : "false", probe.uavs.size(),
			probe.srvsFound ? probe.srvs.size() : 0,
			probe.cbvsFound ? probe.cbvs.size() : 0,
			probe.rootConstantsFound ? probe.rootConstants.size() : 0,
			probe.applied ? "true" : "false",
			probe.dispatchOverride.handlingSkip ? "true" : "false",
			probe.dispatchOverride.hasDispatch ? "true" : "false",
			static_cast<unsigned long long>(probe.computeShaderHash),
			originalThreadGroupCountX, originalThreadGroupCountY, originalThreadGroupCountZ,
			threadGroupCountX, threadGroupCountY, threadGroupCountZ));
}

void DX12DispatchHookFlowExecute(
	ID3D12GraphicsCommandList *commandList,
	UINT threadGroupCountX,
	UINT threadGroupCountY,
	UINT threadGroupCountZ,
	const DX12CommandListRuntimeState *runtimeState,
	DX12DispatchHookFlowDispatch originalDispatch)
{
	if (!originalDispatch)
		return;
	static thread_local bool sInDispatchHookFlow = false;
	if (sInDispatchHookFlow) {
		originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}
	if (runtimeState && DX12ModHasActiveShaderOverrides() &&
	    DX12ModShouldSkipPipelineState(runtimeState->pipelineState, true))
		return;

	struct DispatchHookFlowGuard {
		bool &active;
		explicit DispatchHookFlowGuard(bool &value) : active(value) { active = true; }
		~DispatchHookFlowGuard() { active = false; }
		DispatchHookFlowGuard(const DispatchHookFlowGuard&) = delete;
		DispatchHookFlowGuard &operator=(const DispatchHookFlowGuard&) = delete;
	} guard(sInDispatchHookFlow);

	// If runtimeState is not available (e.g. untracked command list),
	// create a minimal fallback so the pre-skin probe can still read
	// the pipeline state and compute bindings.
	DX12CommandListRuntimeState fallbackState = {};
	const DX12CommandListRuntimeState *effectiveState = runtimeState;
	if (!effectiveState) {
		DX12LogManager::Text("DX12DispatchHookFlowExecute: no runtimeState, using fallback\n");
		fallbackState.pipelineState = DX12CommandListRuntimeGetPipelineState(commandList);
		effectiveState = &fallbackState;
	}

	if (!DX12ModNeedsPreSkinningUavProbe() || !effectiveState) {
		originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return;
	}

	DX12DispatchPreSkinProbe &probe = GetDispatchPreSkinProbeScratch();
	PreparePreSkinProbe(commandList, *effectiveState, &probe);
	UINT dispatchGroupX = threadGroupCountX;
	UINT dispatchGroupY = threadGroupCountY;
	UINT dispatchGroupZ = threadGroupCountZ;
	if (probe.applied && probe.dispatchOverride.handlingSkip) {
		if (probe.dispatchOverride.hasDispatch) {
			dispatchGroupX = probe.dispatchOverride.groupsX;
			dispatchGroupY = probe.dispatchOverride.groupsY;
			dispatchGroupZ = probe.dispatchOverride.groupsZ;
			originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
		}
		FinishPreSkinProbe(
			commandList,
			threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
		return;
	}
	originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
	FinishPreSkinProbe(
		commandList,
		threadGroupCountX, threadGroupCountY, threadGroupCountZ,
		dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
}


