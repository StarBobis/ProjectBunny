#pragma once

// Dispatch hook flow: pre-skin probes, fingerprint cache, guards.
// Split out of DX12Hooks.h; include that umbrella header from existing code.

#include "../DX12Core.h"

#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

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

class DX12PreSkinStableFingerprintCacheUtils
{
public:
	class Entry
	{
	public:
		UINT64 dependency = 0;
		UINT64 fingerprint = 0;
	};

	static constexpr size_t EntryCount = 1024;
	inline static thread_local std::array<Entry, EntryCount> Entries = {};

	static size_t Slot(UINT64 dependency)
	{
		dependency ^= dependency >> 33;
		dependency *= 0xff51afd7ed558ccdull;
		dependency ^= dependency >> 33;
		return static_cast<size_t>(dependency) & (EntryCount - 1);
	}

	static bool TryLoad(UINT64 dependency, UINT64 *fingerprint)
	{
		if (!dependency || !fingerprint)
			return false;
		const Entry &entry = Entries[Slot(dependency)];
		if (entry.dependency != dependency || !entry.fingerprint)
			return false;
		*fingerprint = entry.fingerprint;
		return true;
	}

	static void Store(UINT64 dependency, UINT64 fingerprint)
	{
		if (!dependency || !fingerprint)
			return;
		Entry &entry = Entries[Slot(dependency)];
		entry.dependency = dependency;
		entry.fingerprint = fingerprint;
	}
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


	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		if (!originalDispatch)
			return;
		if (!(flagsWord & DX12HotPathState::MASK_DISPATCH_WORK) &&
			!DX12Profiling::ShouldCollectCounters()) {
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(
				commandList,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ);
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			originalDispatch,
			flagsWord);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn originalDispatch,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		if (!originalDispatch)
			return;

		DX12_HOTPATH_TIME_BEGIN(DispatchHookTotal);

		const bool hasShaderOverrides =
			(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		const bool needsPreSkinProbe = (flagsWord & DX12HotPathState::FLAG_PRE_SKIN_PROBE) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;
		const bool shouldRecordBindingEvents =
			DX12HotPathState::NeedsBindingEventRecording(flagsWord);
		const bool needsDispatchDecision =
			hasShaderOverrides || needsPreSkinProbe;
		// No-candidate compute PSOs are resident state, just like DX11's current
		// shader. Bypass tracking-state lookup and all decision/cache machinery.
		if constexpr (kVerboseEnabled) {
			if (DX12Profiling::ShouldCollectCounters() || needsDispatchDecision)
				DX12RuntimeState::IncrementFrameDispatches();
		}

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

		// Match DX11's compute path: when dispatch only needs logging/hunting or
		// capture, stay on the straight-through path and skip PSO policy rebuilds.
		if (!needsDispatchDecision) {
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			if (DX12HotPathState::skipBindings && huntEnabled) {
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

						if (shouldRecordBindingEvents) {
				const DX12CommandListTrackingState tracking =
					DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
				if (tracking.recordBindingEvents)
					DX12BindingTracker::RecordDispatch(
						commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			}
			if (huntEnabled) {
				DX12HuntRecordDispatch(commandList);
				if (DX12HuntShouldSkipDispatch(commandList)) {
					DX12_HOTPATH_TIME_END(DispatchHookTotal);
					return;
				}
			}
			DX12_PROFILE_FAST_FORWARD();
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				DX12_HOTPATH_TIME_END(DispatchHookTotal);
				return;
			}
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			DX12_HOTPATH_TIME_END(DispatchHookTotal);
			return;
		}

				if (shouldRecordBindingEvents) {
			const DX12CommandListTrackingState tracking =
				DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
			if (tracking.recordBindingEvents)
				DX12BindingTracker::RecordDispatch(
					commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
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
		DX12SimulateContext *runtimeState,
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
		// ExecuteHook resolves a decision before entering this function.  Keep the
		// hot path on the command-list-local immutable decision instead of falling
		// back to DX12ModShouldSkipPipelineState(), which takes gModLock again.
		if (hasShaderOverrides && dispatchDecision)
			shouldSkipDispatch = dispatchDecision->hasShaderOverrideSkip;
		if (shouldSkipDispatch)
			return;

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

		DX12SimulateContext fallbackState = {};
		DX12SimulateContext *effectiveState = runtimeState;
		if (!effectiveState) {
		#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordDispatchDecisionPreSkinCandidateDispatch();

		DX12DispatchPreSkinProbe &probe = ScratchProbe();
		// A deferred draw-SRV restore must flush before any dispatch is
		// recorded: the PreSkin probe below re-reads the (logically restored)
		// shadow bindings and its apply switches heaps again, while every
		// originalDispatch path in this function observes descriptor-heap
		// state. One flush here covers the negative-cache, override and
		// fail-safe forwards below.
		DX12ModFlushDrawSrvReplacement(commandList);
		const UINT64 outerReloadGeneration = DX12ModGetReloadGenerationFast();
		// A proven negative verdict is reusable without re-collecting bindings
		// only while nothing the match can observe has changed since it was
		// stored. The descriptor version alone cannot prove that: root-constant
		// and root-descriptor writes bump computeBindingSerial but not
		// computeDescriptorBindingVersion, so version equality still allows the
		// bindings (and therefore the verdict) to differ. Requiring the binding
		// serial covers every binding mutation; across a command-list Reset the
		// counters restart, but Reset already invalidates probeResultValid, so
		// the verdict can never alias a previous recording.
		if (effectiveState && effectiveDecision &&
			effectiveState->resolvedCompute.probeResultValid &&
			!effectiveState->resolvedCompute.probeApplied &&
			effectiveDecision->computeShaderHash != 0 &&
			effectiveState->resolvedCompute.probeCsHash ==
				effectiveDecision->computeShaderHash &&
			effectiveState->resolvedCompute.probeReloadGeneration ==
				outerReloadGeneration &&
			effectiveState->resolvedCompute.probeBindingSerial ==
				effectiveState->computeBindingSerial &&
			effectiveState->resolvedCompute.probeIaContentEpoch ==
				effectiveState->iaContentEpoch &&
			effectiveState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
			effectiveState->resolvedCompute.bindingsValid &&
			!effectiveState->resolvedCompute.bindingsUseRootValues &&
			effectiveState->resolvedCompute.bindingsHeap ==
				effectiveState->cbvSrvUavHeap &&
			effectiveState->resolvedCompute.bindingsRootSignature ==
				effectiveState->computeRootSignature &&
			effectiveState->resolvedCompute.bindingsDescriptorVersion ==
				effectiveState->computeDescriptorBindingVersion) {
			probe.computeShaderHash = effectiveDecision->computeShaderHash;
			probe.computeBindingSerial = effectiveState->computeBindingSerial;
			probe.applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_dispatch_descriptor_version_negative_hit",
				0,
				1,
				0);
			DX12ModRestorePreSkinningUavReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
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
		if constexpr (kVerboseEnabled) {
			DX12LogManager::Fields("DX12DispatchExecDetail",
				"applied=%d,skip=%d,hasD=%d,gx=%u,gy=%u,gz=%u,origX=%u,origY=%u,origZ=%u,cs=%llx",
				probe.applied ? 1 : 0,
				probe.dispatchOverride.handlingSkip ? 1 : 0,
				probe.dispatchOverride.hasDispatch ? 1 : 0,
				probe.dispatchOverride.groupsX,
				probe.dispatchOverride.groupsY,
				probe.dispatchOverride.groupsZ,
				threadGroupCountX, threadGroupCountY, threadGroupCountZ,
				static_cast<unsigned long long>(probe.computeShaderHash));
		}
		if (probe.applied && probe.dispatchOverride.handlingSkip) {
			if (probe.dispatchOverride.hasDispatch) {
				dispatchGroupX = probe.dispatchOverride.groupsX;
				dispatchGroupY = probe.dispatchOverride.groupsY;
				dispatchGroupZ = probe.dispatchOverride.groupsZ;
				// Publish the resize alias before the dispatch so draws
				// recorded on other lists still resolve the resized output.
				DX12ModPublishPreSkinOutputResizeEarly(commandList);
				LARGE_INTEGER dispatchBegin = DX12OuterTimingProbeUtils::Now();
				originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin",
					"dispatch_override_original",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(dispatchBegin));
			} else {
				// Fail-safe: the config says "skip the original dispatch and
				// run the override", but no override groups are available.
				// Never leave the skinning CS un-issued with patched
				// descriptors bound: undo the replacement (original tables)
				// and run the original dispatch so the scene degrades to the
				// un-modded result instead of a never-written / partially
				// written skinning buffer (spherical geometry + broken UVs).
				DX12ModRestorePreSkinningUavReplacement(commandList);
				DX12ModFlushDrawSrvReplacement(commandList);
				originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
		if (probe.applied)
			DX12ModPublishPreSkinOutputResizeEarly(commandList);
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
		// This runs on arbitrary game recording threads, so keep the cache
		// thread-local: the previous process-wide static map raced on
		// concurrent find/insert, and the reload check below took gModLock
		// on every call even though a volatile generation read is enough.
		static thread_local std::unordered_map<ID3D12PipelineState*, uint64_t> cache;
		static thread_local UINT64 lastReloadGen = 0;
		const UINT64 reloadGen = DX12ModGetReloadGenerationFast();
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		if (reason && (!strcmp(reason, "uav_filter_miss") ||
		    !strcmp(reason, "cached_no_uav_match") ||
		    !strcmp(reason, "cs_not_configured"))) {
			return;
		}
#else
		(void)reason;
		(void)computeShaderHash;
		(void)computeBindingSerial;
		(void)uavCount;
#endif
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRE_SKIN_PROBE)) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
	#endif
			return;
		}

		LARGE_INTEGER t0 = DX12OuterTimingProbeUtils::Now();

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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
#endif
			LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
			return;
		}
		probe->computeBindingSerial = runtimeState ? runtimeState->computeBindingSerial : 0;

		// Execute only reaches this method after the reload-generation-aware
		// dispatch decision has already confirmed this CS as a PreSkin candidate.

		probe->computeBindingSerial = runtimeState ?
			runtimeState->computeBindingSerial :
			DX12BindingTracker::GetComputeBindingSerial(commandList);

		// Most candidate dispatches are repeated with no compute-binding or IA
		// mutation between them. A proven negative result is therefore reusable
		// before descriptor summaries are materialized. Positive results still go
		// through the exact-location path so replacement commands target the
		// currently bound descriptor table.
		const UINT64 earlyReloadGeneration = DX12ModGetReloadGenerationFast();
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			!runtimeState->resolvedCompute.probeApplied &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
			runtimeState->resolvedCompute.probeBindingSerial == probe->computeBindingSerial &&
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration()) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin", "probe_cache_early_negative_hit", 0, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
			return;
		}
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			!runtimeState->resolvedCompute.probeApplied &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
			runtimeState->resolvedCompute.bindingsValid &&
			!runtimeState->resolvedCompute.bindingsUseRootValues &&
				runtimeState->resolvedCompute.bindingsHeap == runtimeState->cbvSrvUavHeap &&
				runtimeState->resolvedCompute.bindingsRootSignature ==
					runtimeState->computeRootSignature &&
				runtimeState->resolvedCompute.bindingsDescriptorVersion ==
					runtimeState->computeDescriptorBindingVersion &&
				DX12BindingTracker::ValidateRuntimePreSkinBindingState(runtimeState)) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				"probe_cache_descriptor_version_negative_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
			return;
		}

		UINT64 templateStableBindingFingerprint = 0;
		if (!probe->uavsFound) {
			DX12_HOTPATH_TIME_BEGIN(PreSkinBindQuery);
			LARGE_INTEGER collectBegin = DX12OuterTimingProbeUtils::Now();
			// DX12 command lists keep resident binding state. When the descriptor
			// version, heap/root signature, and every previously observed table
			// base are unchanged, the cached snapshot is already authoritative.
			// This avoids re-entering the generic DX11-style binding collector on
			// every Dispatch while preserving invalidation on descriptor writes or
			// descriptor-ring rotation.
			bool residentSnapshotLoaded = false;
			if (runtimeState &&
				runtimeState->resolvedCompute.bindingsValid &&
				!runtimeState->resolvedCompute.bindingsUseRootValues &&
				runtimeState->resolvedCompute.bindingsHeap ==
					runtimeState->cbvSrvUavHeap &&
				runtimeState->resolvedCompute.bindingsRootSignature ==
					runtimeState->computeRootSignature &&
				runtimeState->resolvedCompute.bindingsDescriptorVersion ==
					runtimeState->computeDescriptorBindingVersion) {
				residentSnapshotLoaded = true;
				for (const DX12SimulateContextComputeTemplate::RuntimeObservedSlot &observed :
					runtimeState->resolvedCompute.bindingsObservedSlots) {
					if (observed.root >= MaxRootParameters) {
						residentSnapshotLoaded = false;
						break;
					}
					const RootTableState &table =
						runtimeState->computeRootTables[observed.root];
					if (!table.valid ||
						table.baseDescriptor.ptr !=
							runtimeState->resolvedCompute.bindingsRootTableBases[observed.root]) {
						residentSnapshotLoaded = false;
						break;
					}
				}
			}
			if (residentSnapshotLoaded) {
				DX12SimulateContextComputeTemplate &cached =
					runtimeState->resolvedCompute;
				probe->cachedUavs = &cached.uavs;
				probe->cachedSrvs = &cached.srvs;
				probe->cachedCbvs = &cached.cbvs;
				probe->cachedRootConstants = &cached.rootConstants;
				probe->bindingDependencyFingerprint =
					cached.bindingsDependencyFingerprint;
				probe->uavsFound = cached.bindingsFoundAny;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin", "resident_binding_snapshot_hit", 0,
					cached.uavs.size() + cached.srvs.size() + cached.cbvs.size(),
					DX12OuterTimingProbeUtils::ElapsedUs(collectBegin));
			}

			// Consume the validated recording-context template directly. The
			// collector remains the cold path that materializes a missing template.
			bool computeTemplateLoaded = residentSnapshotLoaded;
			if (!computeTemplateLoaded) {
				probe->cachedUavs = nullptr;
				probe->cachedSrvs = nullptr;
				probe->cachedCbvs = nullptr;
				probe->cachedRootConstants = nullptr;
				computeTemplateLoaded =
					DX12BindingTracker::GetSimulateContextPreSkinBindings(
						commandList,
						&probe->cachedUavs,
						&probe->cachedSrvs,
						&probe->cachedCbvs,
						&probe->cachedRootConstants,
						&probe->bindingDependencyFingerprint,
						&probe->uavsFound);
			}
			if (computeTemplateLoaded &&
				runtimeState &&
				runtimeState->resolvedCompute.probeResultValid &&
				!runtimeState->resolvedCompute.probeApplied &&
				runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
				runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
				runtimeState->resolvedCompute.probeBufferContentGeneration ==
					DX12GetIaHashContentGeneration() &&
				runtimeState->resolvedCompute.probeBindingDependencyFingerprint ==
					probe->bindingDependencyFingerprint) {
				probe->applied = false;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"PreSkin",
					"probe_cache_template_negative_hit",
					0,
					1,
					DX12OuterTimingProbeUtils::ElapsedUs(t0));
				DX12_HOTPATH_TIME_END(PreSkinBindQuery);
				return;
			}
			if (computeTemplateLoaded &&
				runtimeState &&
				runtimeState->resolvedCompute.probeResultValid &&
				!runtimeState->resolvedCompute.probeApplied &&
				runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
				runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
				runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
				runtimeState->resolvedCompute.probeBufferContentGeneration ==
					DX12GetIaHashContentGeneration()) {
				const std::vector<DX12CurrentComputeUavBinding> &templateUavs =
					probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
				const std::vector<DX12CurrentComputeUavBinding> &templateSrvs =
					probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
				const std::vector<DX12CurrentComputeUavBinding> &templateCbvs =
					probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
				const std::vector<DX12CurrentRootConstants> &templateRootConstants =
					probe->cachedRootConstants ? *probe->cachedRootConstants : probe->rootConstants;
				if (!DX12PreSkinStableFingerprintCacheUtils::TryLoad(
						probe->bindingDependencyFingerprint,
						&templateStableBindingFingerprint)) {
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
					templateStableBindingFingerprint =
						DX12PreSkinPlanUtils::BuildCrossFrameStableBindingFingerprint(
							templateUavs,
							templateSrvs,
							templateCbvs,
							templateRootConstants);
					if (templateStableBindingFingerprint == 0)
						templateStableBindingFingerprint = 1;
					DX12PreSkinStableFingerprintCacheUtils::Store(
						probe->bindingDependencyFingerprint,
						templateStableBindingFingerprint);
				}
				const UINT64 templateProbeFingerprint =
					DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(
						templateStableBindingFingerprint,
						runtimeState->iaContentEpoch);
				if (runtimeState->resolvedCompute.probeFingerprint == templateProbeFingerprint) {
					probe->applied = false;
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
					DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
					DX12OuterTimingProbeUtils::LogCpuStage(
						"PreSkin",
						"probe_cache_template_fingerprint_negative_hit",
						0,
						1,
						DX12OuterTimingProbeUtils::ElapsedUs(t0));
					DX12_HOTPATH_TIME_END(PreSkinBindQuery);
					return;
				}
			}
			if (!computeTemplateLoaded) {
				probe->uavsFound = DX12BindingTracker::CollectPreSkinBindings(
					commandList, &probe->uavs, &probe->srvs, &probe->cbvs,
					&probe->rootConstants, &probe->bindingDependencyFingerprint);
			}
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

		// Geometry-keyed negative verdicts: the match phase observes only the
		// CS hash, the reload generation, the live SRV view hashes and the
		// live UAV identities - none of the ring-location or buffer-content
		// generations that keep the legacy verdict caches below from ever
		// hitting across frames. Repeated non-matching dispatches (every
		// non-modded character sharing the skinning CS) skip the fingerprint
		// build, the legacy lookups and the apply/store round trip entirely.
		// The only match input outside the key is the sticky-match slot, so a
		// hit is honored only when no sticky entry could drive a positive.
		const UINT64 negativeMatchKey = DX12ModMakePreSkinNegativeMatchKey(
			probe->computeShaderHash, uavs, srvs);
		bool matchPhaseMiss = false;
		if (negativeMatchKey &&
			!MayMatchPreSkinStickyMatch(probe->computeShaderHash, uavs) &&
			DX12ModTestPreSkinNegativeMatch(negativeMatchKey)) {
			probe->applied = false;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheLookup();
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(false);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin", "probe_cache_geometry_negative_hit", 0, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(t0));
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			DX12LogManager::Fields("DX12Perf",
				"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit_geometry\",\"us\":%lld",
				static_cast<unsigned long long>(probe->computeShaderHash),
				static_cast<long long>(DX12OuterTimingProbeUtils::ElapsedUs(t0)));
#endif
			return;
		}

		LARGE_INTEGER fingerprintBegin = DX12OuterTimingProbeUtils::Now();
		UINT64 computeIaFingerprint = 0;
		if (templateStableBindingFingerprint) {
			computeIaFingerprint = templateStableBindingFingerprint;
		} else if (runtimeState && probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprintDependency ==
					probe->bindingDependencyFingerprint &&
				runtimeState->resolvedCompute.bindingFingerprint) {
			computeIaFingerprint = runtimeState->resolvedCompute.bindingFingerprint;
		} else if (DX12PreSkinStableFingerprintCacheUtils::TryLoad(
				probe->bindingDependencyFingerprint,
				&computeIaFingerprint)) {
		} else {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanFingerprintBuild();
			// Use the cross-frame stable fingerprint: it strips per-frame-volatile
			// fields (GPU VA, resource pointer, descriptor offset) that the game's
			// descriptor arena rewrites every frame, so the plan cache can finally
			// hit across frames. PreSkin replacement results depend on the binding
			// pattern (CS + slot layout + descriptor shape), not on the volatile
			// addresses.
			computeIaFingerprint = DX12PreSkinPlanUtils::BuildCrossFrameStableBindingFingerprint(
				uavs, srvs, cbvs, rootConstants);
			if (computeIaFingerprint == 0)
				computeIaFingerprint = 1;
			if (runtimeState && probe->bindingDependencyFingerprint) {
				runtimeState->resolvedCompute.bindingFingerprintDependency =
					probe->bindingDependencyFingerprint;
				runtimeState->resolvedCompute.bindingFingerprint = computeIaFingerprint;
			}
			DX12PreSkinStableFingerprintCacheUtils::Store(
				probe->bindingDependencyFingerprint,
				computeIaFingerprint);
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
			 probe = probe,
			 probeBindingSerial, probeIaContentEpoch](
				uint64_t computeShaderHash,
				bool applied,
				const DX12PreSkinDispatchOverride &dispatchOverride) {
				if (!runtimeState)
					return;
				runtimeState->resolvedCompute.probeBindingDependencyFingerprint =
					probe ? probe->bindingDependencyFingerprint : 0;
				runtimeState->resolvedCompute.probeFingerprint =
					computeIaFingerprint;
				runtimeState->resolvedCompute.probeCsHash = computeShaderHash;
				runtimeState->resolvedCompute.probeReloadGeneration = reloadGeneration;
				runtimeState->resolvedCompute.probeBindingSerial = probeBindingSerial;
				runtimeState->resolvedCompute.probeIaContentEpoch = probeIaContentEpoch;
				runtimeState->resolvedCompute.probeBufferContentGeneration =
					DX12GetIaHashContentGeneration();
				runtimeState->resolvedCompute.probeResultValid = true;
				runtimeState->resolvedCompute.probeApplied = applied;
				runtimeState->resolvedCompute.probeDispatchHandlingSkip =
					dispatchOverride.handlingSkip;
				runtimeState->resolvedCompute.probeDispatchHasDispatch =
					dispatchOverride.hasDispatch;
				runtimeState->resolvedCompute.probeGroupsX = dispatchOverride.groupsX;
				runtimeState->resolvedCompute.probeGroupsY = dispatchOverride.groupsY;
				runtimeState->resolvedCompute.probeGroupsZ = dispatchOverride.groupsZ;
				DX12SimulateContextComputeTemplate::LocalProbeCacheEntry
					&localEntry =
					runtimeState->resolvedCompute.localProbeCache[
						DX12SimulateContextComputeTemplate::LocalProbeCacheSlot(
							computeIaFingerprint, computeShaderHash)];
				localEntry.valid = true;
				localEntry.reloadGeneration = reloadGeneration;
				localEntry.csHash = computeShaderHash;
				localEntry.fingerprint = computeIaFingerprint;
				localEntry.bindingDependencyFingerprint =
					probe ? probe->bindingDependencyFingerprint : 0;
				localEntry.bufferContentGeneration =
					DX12GetIaHashContentGeneration();
				localEntry.iaContentEpoch = probeIaContentEpoch;
				localEntry.bindingSerial = probeBindingSerial;
				localEntry.applied = applied;
				localEntry.handlingSkip = dispatchOverride.handlingSkip;
				localEntry.hasDispatch = dispatchOverride.hasDispatch;
				localEntry.groupsX = dispatchOverride.groupsX;
				localEntry.groupsY = dispatchOverride.groupsY;
				localEntry.groupsZ = dispatchOverride.groupsZ;
			};
		bool runtimeProbeCacheHit = false;
		if (runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			runtimeState->resolvedCompute.probeFingerprint ==
				computeIaFingerprint &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == reloadGeneration &&
			runtimeState->resolvedCompute.probeBindingDependencyFingerprint ==
				(probe ? probe->bindingDependencyFingerprint : 0) &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration() &&
			runtimeState->resolvedCompute.probeIaContentEpoch == probeIaContentEpoch &&
			// A negative verdict is safe to reuse when the descriptor
			// fingerprint is unchanged, even if the command-list binding serial
			// advanced due to unrelated root-state writes. Positive verdicts
			// still require the serial because they carry commands to reapply.
			(!runtimeState->resolvedCompute.probeApplied ||
			 runtimeState->resolvedCompute.probeBindingSerial == probeBindingSerial)) {
			cacheEntry.applied = runtimeState->resolvedCompute.probeApplied;
			cacheEntry.dispatchOverride.handlingSkip =
				runtimeState->resolvedCompute.probeDispatchHandlingSkip;
			cacheEntry.dispatchOverride.hasDispatch =
				runtimeState->resolvedCompute.probeDispatchHasDispatch;
			cacheEntry.dispatchOverride.groupsX =
				runtimeState->resolvedCompute.probeGroupsX;
			cacheEntry.dispatchOverride.groupsY =
				runtimeState->resolvedCompute.probeGroupsY;
			cacheEntry.dispatchOverride.groupsZ =
				runtimeState->resolvedCompute.probeGroupsZ;
			runtimeProbeCacheHit = true;
		}
		// Local lock-free multi-entry verdict cache: same semantic key as the
		// shared cache (command list + CS hash + stable probe fingerprint +
		// reload generation) but stored in the command-list runtime state that
		// survives Reset, so repeated dispatches across frames skip the global
		// mod lock + map lookup entirely.
		bool localProbeCacheHit = false;
		if (runtimeState) {
			DX12SimulateContextComputeTemplate::LocalProbeCacheEntry &localEntry =
				runtimeState->resolvedCompute.localProbeCache[
					DX12SimulateContextComputeTemplate::LocalProbeCacheSlot(
						computeIaFingerprint, probe->computeShaderHash)];
			if (localEntry.valid &&
			    localEntry.reloadGeneration == reloadGeneration &&
			    localEntry.csHash == probe->computeShaderHash &&
			    localEntry.fingerprint == computeIaFingerprint &&
			    localEntry.bindingDependencyFingerprint ==
				    (probe ? probe->bindingDependencyFingerprint : 0) &&
			    localEntry.bufferContentGeneration ==
				    DX12GetIaHashContentGeneration() &&
			    localEntry.iaContentEpoch == probeIaContentEpoch &&
			    (!localEntry.applied ||
			     localEntry.bindingSerial == probeBindingSerial)) {
				cacheEntry.applied = localEntry.applied;
				cacheEntry.dispatchOverride.handlingSkip =
					localEntry.handlingSkip;
				cacheEntry.dispatchOverride.hasDispatch =
					localEntry.hasDispatch;
				cacheEntry.dispatchOverride.groupsX = localEntry.groupsX;
				cacheEntry.dispatchOverride.groupsY = localEntry.groupsY;
				cacheEntry.dispatchOverride.groupsZ = localEntry.groupsZ;
				localProbeCacheHit = true;
			}
		}
		if (runtimeProbeCacheHit || localProbeCacheHit ||
		    DX12ModGetCachedPreSkinProbeResult(
			    commandList, probe->computeShaderHash, computeIaFingerprint,
			    probe ? probe->bindingDependencyFingerprint : 0, &cacheEntry)) {
			DX12OuterTimingProbeUtils::LogCpuStage(
				"PreSkin",
				cacheEntry.applied ? "probe_cache_positive_hit" : "probe_cache_negative_hit",
				0,
				1,
				DX12OuterTimingProbeUtils::ElapsedUs(cacheLookupBegin));
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinPlanCacheHit(
				cacheEntry.applied);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				DX12LogManager::Fields("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					static_cast<long long>(DX12OuterTimingProbeUtils::ElapsedUs(t0)));
	#endif
				return;
			}
			positiveCacheHit = true;
			cachedDispatchOverride = cacheEntry.dispatchOverride;
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		probe->shouldLog = true;
	#endif

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
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
			&probe->dispatchOverride,
			runtimeState ? runtimeState->device : nullptr,
			computeIaFingerprint,
			probe ? probe->bindingDependencyFingerprint : 0,
			&matchPhaseMiss);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			probe->applied ? "apply_replacement_match" : "apply_replacement_miss",
			0,
			uavs.size() + srvs.size() + cbvs.size(),
			DX12OuterTimingProbeUtils::ElapsedUs(applyBegin));
		// Only match-phase misses go to the geometry cache: they depend solely
		// on the inputs folded into negativeMatchKey. A positive that failed
		// its descriptor patch is content-dependent and must keep re-probing.
		if (matchPhaseMiss && negativeMatchKey)
			DX12ModStorePreSkinNegativeMatch(negativeMatchKey);
		if (positiveCacheHit && probe->applied && !probe->dispatchOverride.hasDispatch &&
		    cachedDispatchOverride.hasDispatch) {
			probe->dispatchOverride = cachedDispatchOverride;
		}
		DX12_HOTPATH_TIME_END(PreSkinApply);
		DX12_HOTPATH_TIME_END(PreSkinMatch);
		const bool updateSharedProbeCache =
			(!runtimeProbeCacheHit && !positiveCacheHit) || !probe->applied;
		if (updateSharedProbeCache) {
			LARGE_INTEGER storeCacheBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModStoreCachedPreSkinningUavMatch(
				commandList, probe->computeShaderHash, computeIaFingerprint,
				probe ? probe->bindingDependencyFingerprint : 0,
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

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"%s\",\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
			positiveCacheHit ? "positive_reapply" : "miss",
			static_cast<long long>(DX12OuterTimingProbeUtils::ElapsedUs(t0)));
	#endif
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
		// Keep PreSkin descriptor/table mutations scoped to the current
		// dispatch. Deferred reuse is a CPU optimization, but if our tracked
		// command-list state drifts from the actual restored bindings it can
		// poison later matching and leave patched tables active for unrelated
		// Unity work.
		LARGE_INTEGER restoreBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModRestorePreSkinningUavReplacement(commandList);
		DX12OuterTimingProbeUtils::LogCpuStage(
			"PreSkin",
			"restore_replacement",
			0,
			1,
			DX12OuterTimingProbeUtils::ElapsedUs(restoreBegin));
		if (!probe.applied)
			return;

	}
};

