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
#include "DX12Profiling.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12LogManager.hpp"

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
		if (!originalDispatch)
			return;

		const bool hasShaderOverrides = DX12ModHasActiveShaderOverrides();
		const bool needsPreSkinProbe = DX12ModNeedsPreSkinningUavProbe();
		const bool needsDispatchWork = hasShaderOverrides || needsPreSkinProbe;
		const bool huntEnabled = DX12HuntIsEnabled();
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList);
		if (DX12Profiling::ShouldCollectCounters() || needsDispatchWork)
			DX12RuntimeState::IncrementFrameDispatches();

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		if (DX12HotPathState::skipBindings && huntEnabled && !needsDispatchWork) {
			DX12_PROFILE_FAST_FORWARD();
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList))
				return;
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
			if (DX12HuntShouldSkipDispatch(commandList))
				return;
		}
		if (!needsDispatchWork) {
			DX12_PROFILE_FAST_FORWARD();
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		if (!hasShaderOverrides && needsPreSkinProbe) {
			const DX12CommandListRuntimeState *runtimeState =
				DX12CommandListRuntime::GetStatePtr(commandList);
			if (!HasConfiguredPreSkinCs(commandList, runtimeState)) {
				DX12_PROFILE_FAST_FORWARD();
				originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				return;
			}
			Execute(
				commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
				runtimeState, originalDispatch);
			return;
		}

		const DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtr(commandList);
		Execute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, originalDispatch);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		const DX12CommandListRuntimeState *runtimeState,
		DX12DispatchHookFlowDispatch::Fn originalDispatch)
	{
		if (!originalDispatch)
			return;
		const bool hasShaderOverrides = DX12ModHasActiveShaderOverrides();
		const bool needsPreSkinProbe = DX12ModNeedsPreSkinningUavProbe();
		if (InDispatchHookFlow) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		if (runtimeState && hasShaderOverrides &&
		    DX12ModShouldSkipPipelineState(runtimeState->pipelineState, true))
			return;

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

		// Design debt: this keeps old behavior for untracked command lists. The cleaner module seam is
		// to guarantee runtime state before entering dispatch flow, then remove this fallback entirely.
		DX12CommandListRuntimeState fallbackState = {};
		const DX12CommandListRuntimeState *effectiveState = runtimeState;
		if (!effectiveState) {
#if defined(_DEBUG)
			DX12LogManager::Text("DX12DispatchHookFlowExecute: no runtimeState, using fallback\n");
#endif
			fallbackState.pipelineState = DX12CommandListRuntime::GetPipelineState(commandList);
			effectiveState = &fallbackState;
		}

		if (!needsPreSkinProbe || !effectiveState) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		DX12DispatchPreSkinProbe &probe = ScratchProbe();
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

	// 缓存 PSO 指针 → CS Hash，避免每 Dispatch 的共享锁 + map 查找
	static uint64_t GetCachedCsHash(ID3D12PipelineState *pipelineState)
	{
		if (!pipelineState)
			return 0;
		static std::unordered_map<ID3D12PipelineState*, uint64_t> cache;
		static volatile LONG cacheGen = 0;
		static UINT64 lastReloadGen = 0;
		UINT64 reloadGen = DX12ModGetReloadGeneration();
		if (reloadGen != lastReloadGen) {
			cache.clear();
			lastReloadGen = reloadGen;
		}
		auto it = cache.find(pipelineState);
		if (it != cache.end())
			return it->second;
		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo) || !shaderInfo.hasCS)
			return 0;
		cache[pipelineState] = shaderInfo.cs;
		if (cache.size() > 512)
			cache.clear();
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
		uint64_t csHash = GetCachedCsHash(pipelineState);
		return csHash != 0 && DX12ModShouldTrackPreSkinBindingsForCs(csHash);
	}

	static DX12DispatchPreSkinProbe &ScratchProbe()
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
		const DX12CommandListRuntimeState &runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !DX12ModNeedsPreSkinningUavProbe()) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit needsProbe failed\n");
#endif
			return;
		}

		LARGE_INTEGER t0, t1, freq;
		QueryPerformanceCounter(&t0);

		probe->computeShaderHash = GetCachedCsHash(runtimeState.pipelineState);
		if (!probe->computeShaderHash) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit no cs hash\n");
#endif
			LogPreSkinDispatchProbeReason("no_pipeline_cs_hash", 0, 0, 0);
			return;
		}

		if (!DX12ModShouldProbePreSkinningForCs(probe->computeShaderHash)) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: exit cs not configured\n");
#endif
			LogPreSkinDispatchProbeReason(
				"cs_not_configured", probe->computeShaderHash, 0, 0);
			return;
		}

		// 帧级缓存：本帧此 CS 已成功替换过 → 后续 dispatch 直接走原始路径
		static thread_local std::unordered_set<uint64_t> s_frameProbedCsHashes;
		static thread_local LONG s_frameProbePresent = -1;
		LONG present = DX12RuntimeState::GetPresentCount();
		if (s_frameProbePresent != present) {
			s_frameProbedCsHashes.clear();
			s_frameProbePresent = present;
		}
		if (s_frameProbedCsHashes.find(probe->computeShaderHash) != s_frameProbedCsHashes.end()) {
			QueryPerformanceCounter(&t1);
			QueryPerformanceFrequency(&freq);
			DX12LogManager::JsonFunc("DX12Perf",
				"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"frame_skip\",\"us\":%lld",
				static_cast<unsigned long long>(probe->computeShaderHash),
				static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
			return;
		}

		UINT64 computeBindingSerial =
			DX12BindingTracker::GetComputeBindingSerial(commandList);
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: serial=%llu\n",
			static_cast<unsigned long long>(computeBindingSerial));
#endif
		if (computeBindingSerial == 0) {
#if defined(_DEBUG)
			DX12LogManager::Text("PreparePreSkinProbe: zero serial, using fallback\n");
#endif
			computeBindingSerial = 1;
		} else {
			// 缓存命中（匹配或未匹配）→ 跳过完整探测，直接复用结果
			DX12PreSkinProbeCacheEntry cacheEntry;
			if (DX12ModGetCachedPreSkinProbeResult(
				    commandList, probe->computeShaderHash, computeBindingSerial, &cacheEntry)) {
				probe->computeBindingSerial = computeBindingSerial;
				probe->applied = cacheEntry.applied;
				probe->dispatchOverride = cacheEntry.dispatchOverride;
#if defined(_DEBUG)
				DX12LogManager::Text("PreparePreSkinProbe: cache_hit applied=%d\n",
					probe->applied ? 1 : 0);
#endif
				QueryPerformanceCounter(&t1);
				QueryPerformanceFrequency(&freq);
				DX12LogManager::JsonFunc("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					probe->applied ? "true" : "false",
					static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart));
				return;
			}
		}
		probe->computeBindingSerial = computeBindingSerial;
#if defined(_DEBUG)
		probe->shouldLog = true;
#endif
		if (!probe->uavsFound)
			probe->uavsFound = DX12BindingTracker::GetCurrentComputeAll(commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		probe->srvsFound = !probe->srvs.empty();
		probe->cbvsFound = !probe->cbvs.empty();
		probe->rootConstantsFound = !probe->rootConstants.empty();
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: uavsFound=%d uavCount=%zu srvCount=%zu cbvCount=%zu\n",
			probe->uavsFound ? 1 : 0, probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());

		if (!probe->srvsFound) {
			DX12LogManager::Text("PreparePreSkinProbe: warning no_compute_srvs\n");
		}
#endif

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
#if defined(_DEBUG)
		DX12LogManager::Text("PreparePreSkinProbe: calling DX12ModApplyPreSkinningUavReplacement\n");
#endif
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
			probe->applied, &probe->dispatchOverride);

		// 本帧此 CS 已处理，后续 dispatch 跳过
		if (probe->applied)
			s_frameProbedCsHashes.insert(probe->computeShaderHash);

		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
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
};
