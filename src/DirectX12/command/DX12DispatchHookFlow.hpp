#pragma once

#include <d3d12.h>

#include <cstring>
#include <unordered_map>
#include <vector>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12HotPathState.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12PreSkinPlan.hpp"
#include "DX12Profiling.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12LogManager.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

#if defined(_DEBUG)
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do { stmt; } while (0)
#else
#define DX12_DISPATCH_DEBUG_VERBOSE_LOG(stmt) do {} while (0)
#endif

class DX12DispatchHookFlowDispatch
{
public:
	using Fn = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
};

class DX12DispatchPreSkinProbe
{
public:
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;
	const std::vector<DX12CurrentComputeUavBinding> *cachedUavs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedSrvs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedCbvs = nullptr;
	const std::vector<DX12CurrentRootConstants> *cachedRootConstants = nullptr;
	bool uavsFound = false;
	bool srvsFound = false;
	bool cbvsFound = false;
	bool rootConstantsFound = false;
	bool applied = false;
	bool shouldLog = false;
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	UINT64 bindingDependencyFingerprint = 0;
	UINT originalVertexCount = 0;
	UINT overrideVertexCount = 0;
	DX12PreSkinDispatchOverride dispatchOverride;
};

class DX12DispatchHookFlowGuard
{
public:
	bool &active;

	explicit DX12DispatchHookFlowGuard(bool &value) : active(value) { active = true; }
	~DX12DispatchHookFlowGuard() { active = false; }
	DX12DispatchHookFlowGuard(const DX12DispatchHookFlowGuard&) = delete;
	DX12DispatchHookFlowGuard &operator=(const DX12DispatchHookFlowGuard&) = delete;
};

class DX12DispatchHookFlow
{
public:
	inline static thread_local bool InDispatchHookFlow = false;

	static bool TryExecuteSkipAll(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		if (!DX12HotPathState::skipAll)
			return false;
		if (originalDispatch)
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		return true;
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			originalDispatch);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		if (!originalDispatch)
			return;

		DX12_HOTPATH_TIME_BEGIN(DispatchHookTotal);

		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool hasShaderOverrides =
			(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		const bool needsPreSkinProbe = (flagsWord & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		if (DX12Profiling::ShouldCollectCounters() || hasShaderOverrides || needsPreSkinProbe)
			DX12RuntimeState::IncrementFrameDispatches();

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		DX12CompiledDispatchDecision fallbackDecision;
		const DX12CompiledDispatchDecision *dispatchDecision =
			DX12CommandListRuntime::EnsureDispatchDecision(
				runtimeState, hasShaderOverrides, needsPreSkinProbe);
		if (!dispatchDecision) {
			ID3D12PipelineState *pipelineState =
				DX12CommandListRuntime::GetPipelineState(commandList);
			fallbackDecision = DX12CommandListRuntime::BuildDispatchDecision(
				pipelineState, hasShaderOverrides, needsPreSkinProbe);
			dispatchDecision = &fallbackDecision;
		}
		const bool decisionNeedsPreSkinProbe =
			needsPreSkinProbe && dispatchDecision && dispatchDecision->NeedsPreSkinProbe();
		const bool decisionNeedsShaderOverrideWork =
			hasShaderOverrides && dispatchDecision && dispatchDecision->NeedsShaderOverrideWork();
		const bool needsDispatchWork =
			decisionNeedsShaderOverrideWork || decisionNeedsPreSkinProbe;

		if (DX12HotPathState::skipBindings && huntEnabled && !needsDispatchWork) {
			DX12_PROFILE_FAST_FORWARD();
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				DX12_HOTPATH_TIME_END(DispatchHookTotal);
				return;
			}
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::Dispatch", commandList,
			" groups=%u,%u,%u", threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		if (tracking.recordBindingEvents)
			DX12BindingTracker::RecordDispatch(
				commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		if (huntEnabled) {
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				DX12_HOTPATH_TIME_END(DispatchHookTotal);
				return;
			}
		}
		if (!needsDispatchWork) {
			if (needsPreSkinProbe && dispatchDecision && dispatchDecision->IsPreSkinRejected())
				DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionFastReject();
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		Execute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, originalDispatch, dispatchDecision);
		DX12_HOTPATH_TIME_END(DispatchHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12CommandListRuntimeState *runtimeState,
		DX12DispatchHookFlowDispatch::Fn originalDispatch,
		const DX12CompiledDispatchDecision *dispatchDecision = nullptr)
	{
		if (!originalDispatch)
			return;
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool hasShaderOverrides =
			(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		const bool needsPreSkinProbe =
			(flagsWord & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
		if (InDispatchHookFlow) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		bool shouldSkipDispatch = false;
		if (hasShaderOverrides) {
			if (dispatchDecision)
				shouldSkipDispatch = dispatchDecision->hasShaderOverrideSkip;
			else if (runtimeState)
				shouldSkipDispatch = DX12ModShouldSkipPipelineState(runtimeState->pipelineState, true);
		}
		if (shouldSkipDispatch)
			return;

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

		// Design debt: this keeps old behavior for untracked command lists. The cleaner module seam is
		// to guarantee runtime state before entering dispatch flow, then remove this fallback entirely.
		DX12CommandListRuntimeState fallbackState = {};
		DX12CommandListRuntimeState *effectiveState = runtimeState;
		if (!effectiveState) {
#if defined(_DEBUG)
			DX12LogManager::Text("DX12DispatchHookFlowExecute: no runtimeState, using fallback\n");
#endif
			fallbackState.pipelineState = DX12CommandListRuntime::GetPipelineState(commandList);
			effectiveState = &fallbackState;
		}

		DX12CompiledDispatchDecision localDecision;
		const DX12CompiledDispatchDecision *effectiveDecision = dispatchDecision;
		if (!effectiveDecision && effectiveState) {
			localDecision = DX12CommandListRuntime::BuildDispatchDecision(
				effectiveState->pipelineState, hasShaderOverrides, needsPreSkinProbe);
			effectiveDecision = &localDecision;
		}
		const bool decisionNeedsPreSkinProbe =
			needsPreSkinProbe && effectiveDecision && effectiveDecision->NeedsPreSkinProbe();
		if (!needsPreSkinProbe || !effectiveState || !decisionNeedsPreSkinProbe) {
			if (needsPreSkinProbe && effectiveDecision && effectiveDecision->IsPreSkinRejected())
				DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionFastReject();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidateDispatch();

		DX12DispatchPreSkinProbe &probe = ScratchProbe();
		LARGE_INTEGER prepareProbeBegin = DX12OuterTimingProbeUtils::Now();
		PreparePreSkinProbe(commandList, effectiveState, &probe);
		const std::vector<DX12CurrentComputeUavBinding> &probeUavs =
			probe.cachedUavs ? *probe.cachedUavs : probe.uavs;
		const std::vector<DX12CurrentComputeUavBinding> &probeSrvs =
			probe.cachedSrvs ? *probe.cachedSrvs : probe.srvs;
		const std::vector<DX12CurrentComputeUavBinding> &probeCbvs =
			probe.cachedCbvs ? *probe.cachedCbvs : probe.cbvs;
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_prepare_probe",
			0,
			probeUavs.size() + probeSrvs.size() + probeCbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(prepareProbeBegin));
		UINT dispatchGroupX = threadGroupCountX;
		UINT dispatchGroupY = threadGroupCountY;
		UINT dispatchGroupZ = threadGroupCountZ;
		if (probe.applied && probe.dispatchOverride.handlingSkip) {
			if (probe.dispatchOverride.hasDispatch) {
				dispatchGroupX = probe.dispatchOverride.groupsX;
				dispatchGroupY = probe.dispatchOverride.groupsY;
				dispatchGroupZ = probe.dispatchOverride.groupsZ;
				LARGE_INTEGER dispatchBegin = DX12OuterTimingProbeUtils::Now();
				originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin",
					"dispatch_override_original",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(dispatchBegin));
			}
			LARGE_INTEGER finishBegin = DX12OuterTimingProbeUtils::Now();
			FinishPreSkinProbe(
				commandList,
				threadGroupCountX, threadGroupCountY, threadGroupCountZ,
				dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"dispatch_finish_probe",
				0,
				probeUavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(finishBegin));
			return;
		}
		LARGE_INTEGER dispatchBegin = DX12OuterTimingProbeUtils::Now();
		originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_original",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(dispatchBegin));
		LARGE_INTEGER finishBegin = DX12OuterTimingProbeUtils::Now();
		FinishPreSkinProbe(
			commandList,
			threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			dispatchGroupX, dispatchGroupY, dispatchGroupZ, probe);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"dispatch_finish_probe",
			0,
			probeUavs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(finishBegin));
	}

	// Match the DX11 current-compute-shader model: the CS hash lives in the
	// runtime state and this fallback cache is only used outside wrapper mode.
	static uint64_t GetCachedCsHash(ID3D12PipelineState *pipelineState)
	{
		if (!pipelineState)
			return 0;
		// Fallback cache for command lists without an embedded runtime state.
		static std::unordered_map<ID3D12PipelineState*, uint64_t> cache;
		static UINT64 lastReloadGen = 0;
		UINT64 reloadGen = DX12ModGetReloadGeneration();
		if (reloadGen != lastReloadGen) {
			cache.clear();
			lastReloadGen = reloadGen;
		}
		std::unordered_map<ID3D12PipelineState*, uint64_t>::iterator it = cache.find(pipelineState);
		if (it != cache.end())
			return it->second;
		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) || !shaderInfo.hasCS)
			return 0;
		cache[pipelineState] = shaderInfo.cs;
		return shaderInfo.cs;
	}

	static bool HasConfiguredPreSkinCs(
		ID3D12GraphicsCommandList *commandList,
		const DX12CommandListRuntimeState *runtimeState)
	{
		ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState :
			DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState)
			return false;
		uint64_t csHash = (runtimeState && runtimeState->cachedCsHashValid) ?
			runtimeState->cachedCsHash : GetCachedCsHash(pipelineState);
		return csHash != 0 && DX12ModShouldTrackPreSkinBindingsForCs(csHash);
	}

	static DX12DispatchPreSkinProbe &ScratchProbe()
	{
		static thread_local DX12DispatchPreSkinProbe probe;
		probe.uavs.clear();
		probe.srvs.clear();
		probe.cbvs.clear();
		probe.rootConstants.clear();
		probe.cachedUavs = nullptr;
		probe.cachedSrvs = nullptr;
		probe.cachedCbvs = nullptr;
		probe.cachedRootConstants = nullptr;
		probe.uavsFound = false;
		probe.srvsFound = false;
		probe.cbvsFound = false;
		probe.rootConstantsFound = false;
		probe.applied = false;
		probe.shouldLog = false;
		probe.computeShaderHash = 0;
		probe.computeBindingSerial = 0;
		probe.bindingDependencyFingerprint = 0;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

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

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRE_SKIN_PROBE)) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
#endif
			return;
		}

		LARGE_INTEGER t0, t1, freq;
		QueryPerformanceCounter(&t0);

		// Match DX11 by reading the precomputed CS hash from RuntimeState.
		LARGE_INTEGER csHashBegin = DX12OuterTimingProbeUtils::Now();
		probe->computeShaderHash = (runtimeState && runtimeState->cachedCsHashValid) ?
			runtimeState->cachedCsHash :
			GetCachedCsHash(runtimeState ? runtimeState->pipelineState : nullptr);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"prepare_get_cs_hash",
			0,
			probe->computeShaderHash ? 1 : 0,
			DX12OuterTimingProbeUtils::ElapsedUs(csHashBegin));
		if (!probe->computeShaderHash) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
#endif
			LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
			return;
		}
		probe->computeBindingSerial = runtimeState ? runtimeState->computeBindingSerial : 0;

		LARGE_INTEGER csConfigBegin = DX12OuterTimingProbeUtils::Now();
		if (!DX12ModShouldProbePreSkinningForCs(probe->computeShaderHash)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"prepare_cs_config_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(csConfigBegin));
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit cs not configured\n");
#endif
			LogPreSkinDispatchProbeReason(
				"cs_not_configured", probe->computeShaderHash, 0, 0);
			return;
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"prepare_cs_config_hit",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(csConfigBegin));

		probe->computeBindingSerial =
			DX12BindingTracker::GetComputeBindingSerial(commandList);
		if (runtimeState)
			runtimeState->computeBindingSerial = probe->computeBindingSerial;

		// Most candidate dispatches are repeated with no compute-binding or IA
		// mutation between them. A proven negative result is therefore reusable
		// before descriptor summaries are materialized. Positive results still go
		// through the exact-location path so replacement commands target the
		// currently bound descriptor table.
		const UINT64 earlyReloadGeneration = DX12ModGetReloadGenerationFast();
		if (runtimeState &&
			runtimeState->cachedPreSkinProbeResultValid &&
			!runtimeState->cachedPreSkinProbeApplied &&
			runtimeState->cachedPreSkinProbeCsHash == probe->computeShaderHash &&
			runtimeState->cachedPreSkinProbeReloadGeneration == earlyReloadGeneration &&
			runtimeState->cachedPreSkinProbeBindingSerial == probe->computeBindingSerial &&
			runtimeState->cachedPreSkinProbeIaContentEpoch == runtimeState->iaContentEpoch) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin", "probe_cache_early_negative_hit", 0, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
			return;
		}

		if (!probe->uavsFound) {
			DX12_HOTPATH_TIME_BEGIN(PreSkinBindQuery);
			LARGE_INTEGER collectBegin = DX12OuterTimingProbeUtils::Now();

			// One dependency-aware cache owns PreSkin collection. It validates
			// exact descriptor slots and overlapping root-descriptor GPUVA ranges.
			probe->cachedUavs = nullptr;
			probe->cachedSrvs = nullptr;
			probe->cachedCbvs = nullptr;
			probe->cachedRootConstants = nullptr;
			probe->uavsFound = DX12BindingTracker::CollectPreSkinBindings(
				commandList, &probe->uavs, &probe->srvs, &probe->cbvs,
				&probe->rootConstants, &probe->bindingDependencyFingerprint);
			const std::vector<DX12CurrentComputeUavBinding> &uavs =
				probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
			const std::vector<DX12CurrentComputeUavBinding> &srvs =
				probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
			const std::vector<DX12CurrentComputeUavBinding> &cbvs =
				probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"collect_bindings",
				0,
				uavs.size() + srvs.size() + cbvs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(collectBegin));
			DX12_HOTPATH_TIME_END(PreSkinBindQuery);
		}
		const std::vector<DX12CurrentComputeUavBinding> &uavs =
			probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
		const std::vector<DX12CurrentComputeUavBinding> &srvs =
			probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
		const std::vector<DX12CurrentComputeUavBinding> &cbvs =
			probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
		const std::vector<DX12CurrentRootConstants> &rootConstants =
			probe->cachedRootConstants ? *probe->cachedRootConstants : probe->rootConstants;
		probe->srvsFound = !srvs.empty();
		probe->cbvsFound = !cbvs.empty();
		probe->rootConstantsFound = !rootConstants.empty();

		LARGE_INTEGER fingerprintBegin = DX12OuterTimingProbeUtils::Now();
		UINT64 computeIaFingerprint = 0;
		if (runtimeState && probe->bindingDependencyFingerprint &&
				runtimeState->cachedPreSkinDependencyFingerprint ==
					probe->bindingDependencyFingerprint &&
				runtimeState->cachedPreSkinBindingFingerprint) {
			computeIaFingerprint = runtimeState->cachedPreSkinBindingFingerprint;
		} else {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
			computeIaFingerprint = DX12PreSkinPlanUtils::BuildStableBindingFingerprint(
				uavs, srvs, cbvs, rootConstants);
			if (computeIaFingerprint == 0)
				computeIaFingerprint = 1;
			if (runtimeState && probe->bindingDependencyFingerprint) {
				runtimeState->cachedPreSkinDependencyFingerprint =
					probe->bindingDependencyFingerprint;
				runtimeState->cachedPreSkinBindingFingerprint = computeIaFingerprint;
			}
		}
		computeIaFingerprint = DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(
			computeIaFingerprint,
			runtimeState ? runtimeState->iaContentEpoch : 0);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"build_binding_fingerprint",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(fingerprintBegin));

		// Negative hits can skip matching. Positive hits still reapply through
		// the existing path so required D3D12 commands are emitted.
		bool positiveCacheHit = false;
		DX12PreSkinDispatchOverride cachedDispatchOverride;
		DX12PreSkinProbeCacheEntry cacheEntry;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
		LARGE_INTEGER cacheLookupBegin = DX12OuterTimingProbeUtils::Now();
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		const UINT64 probeBindingSerial = probe->computeBindingSerial;
		const UINT64 probeIaContentEpoch = runtimeState ? runtimeState->iaContentEpoch : 0;
		const auto storeRuntimeProbeCache =
			[runtimeState, reloadGeneration, computeIaFingerprint,
			 probeBindingSerial, probeIaContentEpoch](
				uint64_t computeShaderHash,
				bool applied,
				const DX12PreSkinDispatchOverride &dispatchOverride) {
				if (!runtimeState)
					return;
				runtimeState->cachedPreSkinProbeFingerprint =
					computeIaFingerprint;
				runtimeState->cachedPreSkinProbeCsHash = computeShaderHash;
				runtimeState->cachedPreSkinProbeReloadGeneration = reloadGeneration;
				runtimeState->cachedPreSkinProbeBindingSerial = probeBindingSerial;
				runtimeState->cachedPreSkinProbeIaContentEpoch = probeIaContentEpoch;
				runtimeState->cachedPreSkinProbeResultValid = true;
				runtimeState->cachedPreSkinProbeApplied = applied;
				runtimeState->cachedPreSkinProbeDispatchHandlingSkip =
					dispatchOverride.handlingSkip;
				runtimeState->cachedPreSkinProbeDispatchHasDispatch =
					dispatchOverride.hasDispatch;
				runtimeState->cachedPreSkinProbeGroupsX = dispatchOverride.groupsX;
				runtimeState->cachedPreSkinProbeGroupsY = dispatchOverride.groupsY;
				runtimeState->cachedPreSkinProbeGroupsZ = dispatchOverride.groupsZ;
			};
		bool runtimeProbeCacheHit = false;
		if (runtimeState &&
			runtimeState->cachedPreSkinProbeResultValid &&
			runtimeState->cachedPreSkinProbeFingerprint ==
				computeIaFingerprint &&
			runtimeState->cachedPreSkinProbeCsHash == probe->computeShaderHash &&
			runtimeState->cachedPreSkinProbeReloadGeneration == reloadGeneration) {
			cacheEntry.applied = runtimeState->cachedPreSkinProbeApplied;
			cacheEntry.dispatchOverride.handlingSkip =
				runtimeState->cachedPreSkinProbeDispatchHandlingSkip;
			cacheEntry.dispatchOverride.hasDispatch =
				runtimeState->cachedPreSkinProbeDispatchHasDispatch;
			cacheEntry.dispatchOverride.groupsX =
				runtimeState->cachedPreSkinProbeGroupsX;
			cacheEntry.dispatchOverride.groupsY =
				runtimeState->cachedPreSkinProbeGroupsY;
			cacheEntry.dispatchOverride.groupsZ =
				runtimeState->cachedPreSkinProbeGroupsZ;
			runtimeProbeCacheHit = true;
		}
		if (runtimeProbeCacheHit || DX12ModGetCachedPreSkinProbeResult(
			    commandList, probe->computeShaderHash, computeIaFingerprint, &cacheEntry)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				cacheEntry.applied ? "probe_cache_positive_hit" : "probe_cache_negative_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cacheLookupBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(
				cacheEntry.applied);
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: cache_hit applied=%d fingerprint=%llu\n",
				cacheEntry.applied ? 1 : 0,
				static_cast<unsigned long long>(computeIaFingerprint));
#endif
			if (!runtimeProbeCacheHit) {
				storeRuntimeProbeCache(
					probe->computeShaderHash,
					cacheEntry.applied,
					cacheEntry.dispatchOverride);
			}
			if (!cacheEntry.applied) {
				probe->applied = false;
				QueryPerformanceCounter(&t1);
				QueryPerformanceFrequency(&freq);
				DX12LogManager::JsonFunc("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
				return;
			}
			positiveCacheHit = true;
			cachedDispatchOverride = cacheEntry.dispatchOverride;
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: positive cache requires command list reapply fingerprint=%llu\n",
				static_cast<unsigned long long>(computeIaFingerprint));
#endif
		} else {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_miss",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cacheLookupBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheMiss();

			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"async_warmup_bypassed",
				0,
				1,
				0);
		}
#if defined(_DEBUG)
		probe->shouldLog = true;
#endif
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: uavsFound=%d uavCount=%zu srvCount=%zu cbvCount=%zu\n",
			probe->uavsFound ? 1 : 0, uavs.size(), srvs.size(), cbvs.size());

		if (!probe->srvsFound) {
			DX12LogManager::Text("PreparePreSkinProbe: warning no_compute_srvs\n");
		}
#endif

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: calling DX12ModApplyPreSkinningUavReplacement\n");
#endif
		DX12_HOTPATH_TIME_BEGIN(PreSkinMatch);
		DX12_HOTPATH_TIME_BEGIN(PreSkinApply);
		LARGE_INTEGER applyBegin = DX12OuterTimingProbeUtils::Now();
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavsFound ? uavs : kEmptyComputeBindings,
			srvs,
			probe->cbvsFound ? cbvs : kEmptyComputeBindings,
			probe->rootConstantsFound ? rootConstants : kEmptyRootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			probe->applied ? "apply_replacement_match" : "apply_replacement_miss",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
		if (positiveCacheHit && probe->applied && !probe->dispatchOverride.hasDispatch &&
		    cachedDispatchOverride.hasDispatch) {
			probe->dispatchOverride = cachedDispatchOverride;
		}
		DX12_HOTPATH_TIME_END(PreSkinApply);
		DX12_HOTPATH_TIME_END(PreSkinMatch);
		if (!positiveCacheHit || probe->applied) {
			LARGE_INTEGER storeCacheBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModStoreCachedPreSkinningUavMatch(
				commandList, probe->computeShaderHash, computeIaFingerprint,
				probe->applied, &probe->dispatchOverride);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"store_probe_cache",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(storeCacheBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheStore();
		}
		storeRuntimeProbeCache(
			probe->computeShaderHash,
			probe->applied,
			probe->dispatchOverride);

		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"%s\",\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
			positiveCacheHit ? "positive_reapply" : "miss",
			static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
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
		const std::vector<DX12CurrentComputeUavBinding> &uavs =
			probe.cachedUavs ? *probe.cachedUavs : probe.uavs;
		const std::vector<DX12CurrentComputeUavBinding> &srvs =
			probe.cachedSrvs ? *probe.cachedSrvs : probe.srvs;
		const std::vector<DX12CurrentComputeUavBinding> &cbvs =
			probe.cachedCbvs ? *probe.cachedCbvs : probe.cbvs;
		const std::vector<DX12CurrentRootConstants> &rootConstants =
			probe.cachedRootConstants ? *probe.cachedRootConstants : probe.rootConstants;
		if (probe.applied) {
			LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"restore_replacement",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
		}
		if (probe.uavsFound) {
			LARGE_INTEGER recordUavBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModRecordComputeUavs(commandList, uavs);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"record_compute_uavs",
				0,
				uavs.size(),
				DX12OuterTimingProbeUtils::ElapsedUs(recordUavBegin));
		}
		if (!probe.applied)
			return;

		DX12_DISPATCH_DEBUG_VERBOSE_LOG(
			DX12LogManager::DebugJsonFunc("DX12DispatchUavProbe",
				"\"found\":%s,\"uavs\":%zu,\"srvs\":%zu,\"cbvs\":%zu,\"rootConstants\":%zu,\"preSkinApplied\":%s,\"handlingSkip\":%s,\"explicitDispatch\":%s,\"cs\":\"%016llx\",\"originalGroups\":\"%u,%u,%u\",\"groups\":\"%u,%u,%u\"",
				probe.uavsFound ? "true" : "false", uavs.size(),
				probe.srvsFound ? srvs.size() : 0,
				probe.cbvsFound ? cbvs.size() : 0,
				probe.rootConstantsFound ? rootConstants.size() : 0,
				probe.applied ? "true" : "false",
				probe.dispatchOverride.handlingSkip ? "true" : "false",
				probe.dispatchOverride.hasDispatch ? "true" : "false",
				static_cast<unsigned long long>(probe.computeShaderHash),
				originalThreadGroupCountX, originalThreadGroupCountY, originalThreadGroupCountZ,
				threadGroupCountX, threadGroupCountY, threadGroupCountZ));
	}
};
