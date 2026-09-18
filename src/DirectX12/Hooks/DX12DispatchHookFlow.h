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
#include <optional>
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
	bool cbvsFound = false;
	bool rootConstantsFound = false;
	bool applied = false;
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	UINT64 bindingDependencyFingerprint = 0;
	// Binding-identity probe memo for the current dispatch. Every negative
	// verdict cache and the resident-snapshot load need the same answer ("are
	// the cached bindings still the live ones?"), and within one dispatch that
	// answer cannot change, so it is computed at most once. ScratchProbe()
	// clears both fields at the start of every PreSkin probe.
	bool bindingIdentityChecked = false;
	bool bindingIdentityValid = false;
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

// Why the Execute-level negative verdict was not reused. Every term of its
// guard is a snapshot field written by the last completed probe or by the
// collector, so a window of these counters names the single term that keeps
// 96% of the skinning dispatches paying for a full probe. They exist because
// the verdict's own counters cannot answer it: a rejected guard records
// nothing, and the four "hit" rows only ever count the winners.
enum class DX12ExecVerdictBlock : unsigned char
{
	NoShaderHash = 0,   // probe->computeShaderHash == 0: not a candidate at all
	ProbeNotValid,     // no completed probe to reuse: probeResultValid == false
	ProbeApplied,      // the last probe applied a replacement, so its verdict is a positive
	EpochMismatch,     // the state's content epochs moved since the verdict
	BindingsInvalid,   // bindingsValid / bindingsUseRootValues / heap / root sig
	IdentityProbe,     // the binding-identity probe rejected the live bindings
	Fingerprint,       // bindings changed since the verdict was stored
	Count,
};

class DX12ExecVerdictDiag
{
public:
	static void Report(DX12ExecVerdictBlock reason,
		const DX12SimulateContext *state, UINT64 shaderHash)
	{
		DX12_PROF_COUNT(sNames[static_cast<size_t>(reason)]);
		// One line per window: the first rejection's exact terms, so a counter
		// that fires for two different reasons cannot be misread.
		if (sLogged || !state || !DX12DiagProf::Enabled())
			return;
		sLogged = true;
		DX12_LOG("DX12Perf",
			"\"scope\":\"ExecVerdictBlock\",\"reason\":\"%s\",\"cs\":\"%016llx\","
			"\"probeValid\":%d,\"probeApplied\":%d,\"probeCsHash\":\"%016llx\","
			"\"probeReload\":%llu,\"reload\":%llu,\"probeIaEpoch\":%llu,"
			"\"iaEpoch\":%llu,\"probeContentGen\":%llu,\"contentGen\":%llu,"
			"\"bindingsValid\":%d,\"useRootValues\":%d,\"heapEq\":%d,\"rootSigEq\":%d,"
			"\"probeFingerprint\":\"%016llx\"",
			sNames[static_cast<size_t>(reason)],
			static_cast<unsigned long long>(shaderHash),
			state->resolvedCompute.probeResultValid ? 1 : 0,
			state->resolvedCompute.probeApplied ? 1 : 0,
			static_cast<unsigned long long>(state->resolvedCompute.probeCsHash),
			static_cast<unsigned long long>(state->resolvedCompute.probeReloadGeneration),
			static_cast<unsigned long long>(DX12ModGetReloadGenerationFast()),
			static_cast<unsigned long long>(state->resolvedCompute.probeIaContentEpoch),
			static_cast<unsigned long long>(state->iaContentEpoch),
			static_cast<unsigned long long>(state->resolvedCompute.probeBufferContentGeneration),
			static_cast<unsigned long long>(DX12GetIaHashContentGeneration()),
			state->resolvedCompute.bindingsValid ? 1 : 0,
			state->resolvedCompute.bindingsUseRootValues ? 1 : 0,
			state->resolvedCompute.bindingsHeap == state->cbvSrvUavHeap ? 1 : 0,
			state->resolvedCompute.bindingsRootSignature == state->computeRootSignature ? 1 : 0,
			static_cast<unsigned long long>(
				state->resolvedCompute.probeBindingDependencyFingerprint));
	}

private:
	static constexpr const char *sNames[] = {
		"Rm.ExecVerdictBlockedCsHashZero",
		"Rm.ExecVerdictBlockedProbeNotValid",
		"Rm.ExecVerdictBlockedProbeApplied",
		"Rm.ExecVerdictBlockedEpoch",
		"Rm.ExecVerdictBlockedBindings",
		"Rm.ExecVerdictBlockedIdentityProbe",
		"Rm.ExecVerdictBlockedFingerprint",
	};
	static_assert(
		sizeof(sNames) / sizeof(sNames[0]) ==
			static_cast<size_t>(DX12ExecVerdictBlock::Count),
		"every rejection reason needs its own counter");
	inline static thread_local bool sLogged = false;
};

// Publishes one probe's outcome into the command-list state so a later
// dispatch can reuse the verdict instead of re-deriving it. The publisher is
// state-agnostic on purpose: the geometry-keyed negative verdict must publish
// its result too, or the state never carries a completed probe and every
// dispatch below rebuilds the geometry key (measured: 3303 dispatches per
// window, 78 stores per window, and 96% of dispatches exiting through the
// geometry cache before any store ran).
class DX12PreSkinProbeStatePublisher
{
public:
	template <typename State>
	static void Publish(
		State *state,
		UINT64 csHash,
		UINT64 reloadGeneration,
		UINT64 iaContentEpoch,
		UINT64 bufferContentGeneration,
		UINT64 bindingDependencyFingerprint,
		bool applied,
		bool handlingSkip,
		bool hasDispatch,
		UINT groupsX,
		UINT groupsY,
		UINT groupsZ)
	{
		if (!state)
			return;
		state->resolvedCompute.probeBindingDependencyFingerprint =
			bindingDependencyFingerprint;
		state->resolvedCompute.probeCsHash = csHash;
		state->resolvedCompute.probeReloadGeneration = reloadGeneration;
		state->resolvedCompute.probeIaContentEpoch = iaContentEpoch;
		state->resolvedCompute.probeBufferContentGeneration = bufferContentGeneration;
		state->resolvedCompute.probeResultValid = true;
		state->resolvedCompute.probeApplied = applied;
		state->resolvedCompute.probeDispatchHandlingSkip = handlingSkip;
		state->resolvedCompute.probeDispatchHasDispatch = hasDispatch;
		state->resolvedCompute.probeGroupsX = groupsX;
		state->resolvedCompute.probeGroupsY = groupsY;
		state->resolvedCompute.probeGroupsZ = groupsZ;
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
	// One-shot latch for the "the zero-work verdict path finally fired" log
	// line. Render-thread only, so a plain thread-local flag is enough.
	inline static thread_local bool sExecuteVerdictHitLogged = false;
	inline static thread_local bool sIdentityProbeFailLogged = false;


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
		if (!(flagsWord & DX12HotPathState::MASK_DISPATCH_WORK)) {
			DX12_PROF_COUNT("FastForward");
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

		DX12_PROF_STAGE_SCOPE(DispatchHook);

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
		DX12_PROF_FRAME_DISPATCH();

		if (DX12InternalReplayState::IsActive()) {
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		// Match DX11's compute path: when dispatch only needs logging/hunting or
		// capture, stay on the straight-through path and skip PSO policy rebuilds.
		if (!needsDispatchDecision) {
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			if (DX12HotPathState::skipBindings && huntEnabled) {
				DX12_PROF_COUNT("FastForward");
				DX12HuntRecordDispatch(commandList);
				if (DX12HuntShouldSkipDispatch(commandList)) {
					return;
				}
				originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
					return;
				}
			}
			DX12_PROF_COUNT("FastForward");
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
			needsPreSkinProbe && dispatchDecision->NeedsPreSkinProbe();
		const bool decisionNeedsShaderOverrideWork =
			hasShaderOverrides && dispatchDecision->NeedsShaderOverrideWork();
		const bool needsDispatchWork =
			decisionNeedsShaderOverrideWork || decisionNeedsPreSkinProbe;

		if (DX12HotPathState::skipBindings && huntEnabled && !needsDispatchWork) {
			DX12_PROF_COUNT("FastForward");
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			DX12HuntRecordDispatch(commandList);
			if (DX12HuntShouldSkipDispatch(commandList)) {
				return;
			}
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
				return;
			}
		}
		if (!needsDispatchWork) {
			if (needsPreSkinProbe && dispatchDecision->IsPreSkinRejected())
				DX12_PROF_COUNT("Rm.DispatchDecisionFastRejects");
			DX12_PROF_COUNT("FastForward");
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}

		Execute(
			commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
			runtimeState, originalDispatch, dispatchDecision);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12SimulateContext *runtimeState,
		DX12DispatchHookFlowDispatch::Fn originalDispatch,
		const DX12CompiledDispatchDecision *dispatchDecision)
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
		// ExecuteHook resolves the decision before entering this function: the
		// hot path stays on the command-list-local immutable decision instead of
		// falling back to DX12ModGetPipelineStateSkipFlags(), which takes gModLock
		// again. The decision is non-null by construction at the only call site.
		if (hasShaderOverrides && dispatchDecision->hasShaderOverrideSkip)
			return;

		// Everything below this point is the PreSkin candidate path. Test the
		// decision before touching the guard or the fallback state: the two
		// guarded objects below are only observable on this path, and the
		// fallback state is a whole DX12SimulateContext (tens of KB of plain
		// arrays, dominated by the two 64-entry root-constant tables) that a
		// non-candidate dispatch has no use for.
		const bool decisionNeedsPreSkinProbe =
			needsPreSkinProbe && dispatchDecision->NeedsPreSkinProbe();
		if (!decisionNeedsPreSkinProbe) {
			if (needsPreSkinProbe && dispatchDecision->IsPreSkinRejected())
				DX12_PROF_COUNT("Rm.DispatchDecisionFastRejects");
			DX12ModRestorePreSkinningUavReplacement(commandList);
			DX12ModFlushDrawSrvReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12_PROF_COUNT("Rm.DispatchDecisionPreSkinCandidateDispatches");

		DX12DispatchHookFlowGuard guard(InDispatchHookFlow);

		// Only constructed when the command list really has no resident runtime
		// state. Declaring it as a value here cost a full DX12SimulateContext
		// (~111KB of plain arrays) of zero-initialisation on every pre-skin
		// candidate dispatch, for a fallback the wrapper path never takes.
		//
		// The budget below is a stack limit rather than a taste call: this
		// object is frame-resident, the thread that reaches it has a 1MB stack,
		// and a member that pushed the frame past it is exactly how the IA-state
		// negative cache took startup down with c00000fd. Shrink the offending
		// member instead of raising this.
		static_assert(sizeof(DX12SimulateContext) <= 256 * 1024,
			"DX12SimulateContext is frame-resident in the dispatch hook");
		std::optional<DX12SimulateContext> fallbackState;
		DX12SimulateContext *effectiveState = runtimeState;
		if (!effectiveState) {
			DX12_LOG_TEXT("DX12DispatchHookFlowExecute: no runtimeState, using fallback\n");
			fallbackState.emplace();
			fallbackState->pipelineState = DX12CommandListRuntime::GetPipelineState(commandList);
			effectiveState = &*fallbackState;
		}

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
		// bindings (and therefore the verdict) to differ. The binding-identity
		// probe below proves the bindings themselves - it re-reads every
		// observed descriptor slot's value id at its current ring position and
		// re-compares the root descriptors and root constants - which covers
		// every binding mutation the serials covered, plus the re-binds and ring
		// rotations they rejected. The stored binding-dependency fingerprint
		// additionally pins the verdict to *this* snapshot: a collector run that
		// returns through the geometry negative cache replaces the snapshot
		// without refreshing the verdict, and reusing a verdict computed on
		// other bindings would skip a replacement those bindings need. Across a
		// command-list Reset the counters restart, but Reset already invalidates
		// probeResultValid and bindingsValid, so the verdict can never alias a
		// previous recording.
		const DX12ExecVerdictBlock blockReason = EvaluatePreSkinExecVerdict(
			commandList, effectiveState, &probe,
			dispatchDecision->computeShaderHash, outerReloadGeneration);
		if (blockReason == DX12ExecVerdictBlock::Count) {
			probe.computeShaderHash = dispatchDecision->computeShaderHash;
			probe.computeBindingSerial = effectiveState->computeBindingSerial;
			probe.applied = false;
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
			DX12_PROF_COUNT("Rm.PreSkinVerdictDispatchHits");
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"probe_cache_dispatch_binding_identity_negative_hit",
				0,
				1,
				0);
			// The zero-work verdict path only ever fired zero times before the
			// geometry-keyed negative hit started publishing its result, so the
			// first reuse is worth one line: it states which guard terms are
			// true when the path does become reachable.
			if (!sExecuteVerdictHitLogged && DX12DiagProf::Enabled()) {
				sExecuteVerdictHitLogged = true;
				DX12_LOG("DX12Perf",
					"\"scope\":\"ExecVerdictHit\",\"cs\":\"%016llx\",\"probeCsHash\":\"%016llx\","
					"\"reload\":%llu,\"iaEpoch\":%llu,\"contentGen\":%llu,"
					"\"bindingsValid\":%d,\"heapEq\":%d,\"rootSigEq\":%d",
					static_cast<unsigned long long>(dispatchDecision->computeShaderHash),
					static_cast<unsigned long long>(effectiveState->resolvedCompute.probeCsHash),
					static_cast<unsigned long long>(effectiveState->resolvedCompute.probeReloadGeneration),
					static_cast<unsigned long long>(effectiveState->resolvedCompute.probeIaContentEpoch),
					static_cast<unsigned long long>(effectiveState->resolvedCompute.probeBufferContentGeneration),
					effectiveState->resolvedCompute.bindingsValid ? 1 : 0,
					effectiveState->resolvedCompute.bindingsHeap == effectiveState->cbvSrvUavHeap ? 1 : 0,
					effectiveState->resolvedCompute.bindingsRootSignature == effectiveState->computeRootSignature ? 1 : 0);
			}
			DX12ModRestorePreSkinningUavReplacement(commandList);
			originalDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		// Report the decision already evaluated above, rather than scanning
		// every guard (including the content generation) a second time.
		DX12ExecVerdictDiag::Report(blockReason, effectiveState,
			dispatchDecision->computeShaderHash);
		DX12_DIAG_QPC(prepareProbeBegin);
		PreparePreSkinProbe(commandList, effectiveState, &probe);
		const std::vector<DX12CurrentComputeUavBinding> &probeUavs =
			probe.cachedUavs ? *probe.cachedUavs : probe.uavs;
		const std::vector<DX12CurrentComputeUavBinding> &probeSrvs =
			probe.cachedSrvs ? *probe.cachedSrvs : probe.srvs;
		const std::vector<DX12CurrentComputeUavBinding> &probeCbvs =
			probe.cachedCbvs ? *probe.cachedCbvs : probe.cbvs;
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"dispatch_prepare_probe",
			0,
			static_cast<LONG64>(probeUavs.size() + probeSrvs.size() + probeCbvs.size()),
			DX12_DIAG_ELAPSED_US(prepareProbeBegin));
		UINT dispatchGroupX = threadGroupCountX;
		UINT dispatchGroupY = threadGroupCountY;
		UINT dispatchGroupZ = threadGroupCountZ;
		if (probe.applied && probe.dispatchOverride.handlingSkip) {
			if (probe.dispatchOverride.hasDispatch) {
				dispatchGroupX = probe.dispatchOverride.groupsX;
				dispatchGroupY = probe.dispatchOverride.groupsY;
				dispatchGroupZ = probe.dispatchOverride.groupsZ;
				// Publish the resize alias before the dispatch so draws
				// recorded on other lists still resolve the resized output.
				DX12ModPublishPreSkinOutputResizeEarly(commandList);
				DX12_DIAG_QPC(dispatchBegin);
				originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
				DX12_PROF_CPU_STAGE(
					"PreSkin",
					"dispatch_override_original",
					0,
					1,
					DX12_DIAG_ELAPSED_US(dispatchBegin));
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
			DX12_DIAG_QPC(finishBegin);
			FinishPreSkinProbe(commandList);
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"dispatch_finish_probe",
				0,
				static_cast<LONG64>(probeUavs.size()),
				DX12_DIAG_ELAPSED_US(finishBegin));
			return;
		}
		DX12_DIAG_QPC(dispatchOriginalBegin);
		if (probe.applied)
			DX12ModPublishPreSkinOutputResizeEarly(commandList);
		originalDispatch(commandList, dispatchGroupX, dispatchGroupY, dispatchGroupZ);
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"dispatch_original",
			0,
			1,
			DX12_DIAG_ELAPSED_US(dispatchOriginalBegin));
		DX12_DIAG_QPC(finishOriginalBegin);
		FinishPreSkinProbe(commandList);
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"dispatch_finish_probe",
			0,
			static_cast<LONG64>(probeUavs.size()),
			DX12_DIAG_ELAPSED_US(finishOriginalBegin));
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
		probe.cbvsFound = false;
		probe.rootConstantsFound = false;
		probe.applied = false;
		probe.computeShaderHash = 0;
		probe.computeBindingSerial = 0;
		probe.bindingDependencyFingerprint = 0;
		probe.bindingIdentityChecked = false;
		probe.bindingIdentityValid = false;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

	// Binding-identity revalidation for the PreSkin negative-verdict caches and
	// the resident binding snapshot.
	//
	// Those caches used to be validated with the store-time binding counters
	// (probeBindingSerial / bindingsDescriptorVersion). The game re-binds
	// compute root tables and rewrites the descriptor ring while binding the
	// identical descriptors, which bumps both counters, so the keys never
	// matched once in a 17MB run and every candidate dispatch fell through to
	// the full binding collector. DX12BindingTracker::TryGetRuntimePreSkinBindings
	// already validates this snapshot by binding *identity* (same root
	// signature, same root-descriptor identity, same root-constant content,
	// same per-slot descriptor value ids read at the current ring positions),
	// so the hook reuses that entry point through its public wrapper instead of
	// duplicating the validation.
	//
	// Identity is strictly stronger than the serials it replaces: a different
	// root signature, heap, root descriptor, root constant, observed slot value
	// or a gained/lost used root all fail it. Only a state whose bindings are
	// provably the cached ones can pass, so no state the serials rejected for a
	// real binding change is accepted now.
	//
	// The wrapper resolves the command list's resident state itself, so the
	// probe is only meaningful when that state is the one being probed: a
	// command list without resident state has no snapshot to validate. The
	// cheap root / root-constant predicates gate the call - when they already
	// fail, the full validation cannot succeed either - so a missing snapshot
	// costs two comparisons instead of a descriptor pass.
	static bool ProbePreSkinBindingIdentity(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (probe->bindingIdentityChecked)
			return probe->bindingIdentityValid;
		probe->bindingIdentityChecked = true;
		if (!runtimeState) {
			DX12_PROF_COUNT("Rm.PreSkinIdentityProbeNoState");
			return false;
		}
		if (DX12CommandListRuntime::GetStatePtrFast(commandList) != runtimeState) {
			DX12_PROF_COUNT("Rm.PreSkinIdentityProbeStateMismatch");
			return false;
		}
		if (!DX12BindingTracker::ValidateRuntimePreSkinBindingState(runtimeState)) {
			DX12_PROF_COUNT("Rm.PreSkinVerdictBindingStateInvalid");
			// One line per window with the snapshot's own state at the moment
			// the validator rejected it: whether a snapshot exists at all, and
			// what it holds, is the difference between "never collected" and
			// "collected and then invalidated".
			if (!sIdentityProbeFailLogged && DX12DiagProf::Enabled()) {
				sIdentityProbeFailLogged = true;
				const DX12SimulateContextComputeTemplate &cache =
					runtimeState->resolvedCompute;
				DX12_LOG("DX12Perf",
					"\"scope\":\"IdentityProbeFail\",\"bindingsValid\":%d,"
					"\"rootSig\":\"%p\",\"cachedRootSig\":\"%p\",\"usedMaskValid\":%d,"
					"\"observedSlots\":%zu,\"uavs\":%zu,\"srvs\":%zu,\"cbvs\":%zu,"
					"\"rootConstants\":%zu,\"dependencyFingerprint\":\"%016llx\"",
					cache.bindingsValid ? 1 : 0,
					reinterpret_cast<const void *>(runtimeState->computeRootSignature),
					reinterpret_cast<const void *>(cache.bindingsRootSignature),
					runtimeState->computeRootUsedMaskValid ? 1 : 0,
					cache.bindingsObservedSlots.size(),
					cache.uavs.size(),
					cache.srvs.size(),
					cache.cbvs.size(),
					cache.rootConstants.size(),
					static_cast<unsigned long long>(cache.bindingsDependencyFingerprint));
			}
			return false;
		}
		DX12_DIAG_QPC(identityProbeBegin);
		DX12_PROF_COUNT("Rm.PreSkinVerdictIdentityProbeRuns");
		probe->bindingIdentityValid =
			DX12BindingTracker::GetSimulateContextPreSkinBindings(
				commandList,
				&probe->cachedUavs,
				&probe->cachedSrvs,
				&probe->cachedCbvs,
				&probe->cachedRootConstants,
				&probe->bindingDependencyFingerprint,
				&probe->uavsFound);
		if (probe->bindingIdentityValid)
			DX12_PROF_COUNT("Rm.PreSkinVerdictIdentityProbeHits");
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"binding_identity_probe",
			0,
			1,
			DX12_DIAG_ELAPSED_US(identityProbeBegin));
		return probe->bindingIdentityValid;
	}

	// Count is the success sentinel; all other values name the first rejecting
	// guard. Keep acceptance and diagnostics on this single short-circuit path.
	static DX12ExecVerdictBlock EvaluatePreSkinExecVerdict(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *state,
		DX12DispatchPreSkinProbe *probe,
		UINT64 shaderHash,
		UINT64 reloadGeneration)
	{
		if (!shaderHash)
			return DX12ExecVerdictBlock::NoShaderHash;
		if (!state->resolvedCompute.probeResultValid)
			return DX12ExecVerdictBlock::ProbeNotValid;
		if (state->resolvedCompute.probeApplied)
			return DX12ExecVerdictBlock::ProbeApplied;
		if (state->resolvedCompute.probeCsHash != shaderHash ||
			state->resolvedCompute.probeReloadGeneration != reloadGeneration ||
			state->resolvedCompute.probeIaContentEpoch != state->iaContentEpoch ||
			state->resolvedCompute.probeBufferContentGeneration !=
				DX12GetIaHashContentGeneration())
			return DX12ExecVerdictBlock::EpochMismatch;
		if (!state->resolvedCompute.bindingsValid ||
			state->resolvedCompute.bindingsUseRootValues ||
			state->resolvedCompute.bindingsHeap != state->cbvSrvUavHeap ||
			state->resolvedCompute.bindingsRootSignature != state->computeRootSignature)
			return DX12ExecVerdictBlock::BindingsInvalid;
		if (!ProbePreSkinBindingIdentity(commandList, state, probe))
			return DX12ExecVerdictBlock::IdentityProbe;
		if (state->resolvedCompute.probeBindingDependencyFingerprint !=
			probe->bindingDependencyFingerprint)
			return DX12ExecVerdictBlock::Fingerprint;
		return DX12ExecVerdictBlock::Count;
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		DX12DispatchPreSkinProbe *probe)
	{
		if (!probe || !(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRE_SKIN_PROBE)) {
			DX12_LOG_TEXT("PreparePreSkinProbe: exit needsProbe failed\n");
			return;
		}

		DX12_DIAG_QPC(t0);

		// Match DX11 by reading the precomputed CS hash from RuntimeState.
		DX12_DIAG_QPC(csHashBegin);
		probe->computeShaderHash = (runtimeState && runtimeState->cachedCsHashValid) ?
			runtimeState->cachedCsHash :
			GetCachedCsHash(runtimeState ? runtimeState->pipelineState : nullptr);
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"prepare_get_cs_hash",
			0,
			probe->computeShaderHash ? 1 : 0,
			DX12_DIAG_ELAPSED_US(csHashBegin));
		if (!probe->computeShaderHash) {
			DX12_LOG_TEXT("PreparePreSkinProbe: exit no cs hash\n");
			return;
		}
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
		//
		// The key below is every match input except the bindings themselves. The
		// bindings are covered by the binding-identity probe, never by the
		// store-time binding serial: the game re-binds its compute root tables on
		// every dispatch, so serial equality never held (measured: 0 hits) while
		// the identity of the bindings was in fact unchanged.
		const UINT64 earlyReloadGeneration = DX12ModGetReloadGenerationFast();
		const bool preSkinProbeKeyMatches = runtimeState &&
			runtimeState->resolvedCompute.probeResultValid &&
			!runtimeState->resolvedCompute.probeApplied &&
			runtimeState->resolvedCompute.probeCsHash == probe->computeShaderHash &&
			runtimeState->resolvedCompute.probeReloadGeneration == earlyReloadGeneration &&
			runtimeState->resolvedCompute.probeIaContentEpoch == runtimeState->iaContentEpoch &&
			runtimeState->resolvedCompute.probeBufferContentGeneration ==
				DX12GetIaHashContentGeneration();
		if (preSkinProbeKeyMatches)
			DX12_PROF_COUNT("Rm.PreSkinVerdictKeyMatches");

		// The Execute-level verdict cache may already have paid for this on the
		// current dispatch; otherwise it is evaluated here, once, for both
		// negative-verdict caches and for the resident-snapshot load below. A
		// hit publishes the cached binding vectors into the probe, so it *is*
		// the resident snapshot load for this dispatch.
		const bool preSkinIdentityValid =
			ProbePreSkinBindingIdentity(commandList, runtimeState, probe);
		// The verdict must belong to the snapshot the probe just validated:
		// only a collector run that returns before the verdict is refreshed (the
		// geometry negative cache) can replace the snapshot, and its
		// binding-dependency fingerprint is that generation marker. Reusing a
		// verdict computed on other bindings would skip a replacement those
		// bindings need.
		const bool preSkinVerdictMatchesSnapshot = runtimeState &&
			runtimeState->resolvedCompute.probeBindingDependencyFingerprint ==
				probe->bindingDependencyFingerprint;
		if (preSkinProbeKeyMatches && preSkinIdentityValid &&
			preSkinVerdictMatchesSnapshot) {
			probe->applied = false;
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
			DX12_PROF_COUNT("Rm.PreSkinVerdictEarlyHits");
			DX12_PROF_CPU_STAGE(
				"PreSkin", "probe_cache_early_negative_hit", 0, 1,
				DX12_DIAG_ELAPSED_US(t0));
			return;
		}
		// Same verdict key, now also requiring the hook's own provenance gates
		// on the cached snapshot. The identity probe subsumes them; they are
		// kept so this branch can never accept a state the probe rejected.
		if (preSkinProbeKeyMatches &&
			runtimeState->resolvedCompute.bindingsValid &&
			!runtimeState->resolvedCompute.bindingsUseRootValues &&
			runtimeState->resolvedCompute.bindingsHeap == runtimeState->cbvSrvUavHeap &&
			runtimeState->resolvedCompute.bindingsRootSignature ==
				runtimeState->computeRootSignature &&
			preSkinIdentityValid &&
			preSkinVerdictMatchesSnapshot) {
			probe->applied = false;
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
			DX12_PROF_COUNT("Rm.PreSkinVerdictDescriptorHits");
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"probe_cache_binding_identity_negative_hit",
				0,
				1,
				DX12_DIAG_ELAPSED_US(t0));
			return;
		}

		UINT64 templateStableBindingFingerprint = 0;
		// No `if (!probe->uavsFound)` guard here: ScratchProbe() clears
		// uavsFound at the top of every dispatch and nothing between it and this
		// point writes it, so the test was always true. The block is kept as a
		// plain scope (and uavsFound stays as the collector's own result field,
		// read below when choosing between the cached and scratch binding lists).
		{
			DX12_PROF_STAGE_SCOPE(PreSkinBindQuery);
			DX12_DIAG_QPC(collectBegin);
			// The identity probe above already validated this snapshot and
			// published its vectors, fingerprint and found-any flag into the
			// probe, so accepting the resident snapshot no longer needs the
			// descriptor version / table base equality: those are exactly the
			// ring-rotation signals the game changes while binding identical
			// descriptors, and the probe re-reads every observed slot's value id
			// at its current ring position instead. The snapshot is still not
			// re-collected on every Dispatch, and invalidation on real descriptor
			// writes or ring rotation is preserved by those value ids.
			const bool residentSnapshotLoaded = preSkinIdentityValid;
			if (residentSnapshotLoaded) {
				DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
				DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
				DX12_PROF_COUNT("Rm.PreSkinVerdictResidentHits");
				DX12_PROF_CPU_STAGE(
					"PreSkin", "resident_binding_snapshot_hit", 0,
					static_cast<LONG64>(
						probe->cachedUavs->size() +
						probe->cachedSrvs->size() +
						probe->cachedCbvs->size()),
					DX12_DIAG_ELAPSED_US(collectBegin));
			}

			// Consume the validated recording-context template directly. The
			// collector remains the cold path that materializes a missing template.
			bool computeTemplateLoaded = residentSnapshotLoaded;
			// ProbePreSkinBindingIdentity already ran the tracker's resident
			// snapshot lookup on this exact command-list state whenever it could
			// apply. That lookup only mutates the cache after a full successful
			// validation, so a failed probe cannot be turned into a hit by
			// calling it again on unchanged state; and when the probe could not
			// run at all, the command list's resident state is not the state
			// being probed, so a lookup would hand out another state's snapshot.
			// Both cases go straight to the collector, exactly as the negative
			// answer would have.
			if (!computeTemplateLoaded && !probe->bindingIdentityChecked) {
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
				DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
				DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
				DX12_PROF_COUNT("Rm.PreSkinVerdictTemplateHits");
				DX12_PROF_CPU_STAGE(
					"PreSkin",
					"probe_cache_template_negative_hit",
					0,
					1,
					DX12_DIAG_ELAPSED_US(t0));
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
					DX12_PROF_COUNT("Rm.PreSkinPlanFingerprintBuilds");
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
					DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
					DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
					DX12_PROF_COUNT("Rm.PreSkinVerdictTemplateFingerprintHits");
					DX12_PROF_CPU_STAGE(
						"PreSkin",
						"probe_cache_template_fingerprint_negative_hit",
						0,
						1,
						DX12_DIAG_ELAPSED_US(t0));
					return;
				}
			}
			if (!computeTemplateLoaded) {
				probe->uavsFound = DX12BindingTracker::CollectPreSkinBindings(
					commandList, &probe->uavs, &probe->srvs, &probe->cbvs,
					&probe->rootConstants, &probe->bindingDependencyFingerprint,
					!probe->bindingIdentityChecked);
			}
			const std::vector<DX12CurrentComputeUavBinding> &uavs =
				probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
			const std::vector<DX12CurrentComputeUavBinding> &srvs =
				probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
			const std::vector<DX12CurrentComputeUavBinding> &cbvs =
				probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"collect_bindings",
				0,
				static_cast<LONG64>(uavs.size() + srvs.size() + cbvs.size()),
				DX12_DIAG_ELAPSED_US(collectBegin));
		}
		const std::vector<DX12CurrentComputeUavBinding> &uavs =
			probe->cachedUavs ? *probe->cachedUavs : probe->uavs;
		const std::vector<DX12CurrentComputeUavBinding> &srvs =
			probe->cachedSrvs ? *probe->cachedSrvs : probe->srvs;
		const std::vector<DX12CurrentComputeUavBinding> &cbvs =
			probe->cachedCbvs ? *probe->cachedCbvs : probe->cbvs;
		const std::vector<DX12CurrentRootConstants> &rootConstants =
			probe->cachedRootConstants ? *probe->cachedRootConstants : probe->rootConstants;
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
			// Publish this completed negative verdict. Without it the state
			// never carries a valid probe, so the Execute-level verdict guard
			// falls through on every dispatch and the geometry key above is
			// rebuilt (hash resolve included) forever: measured 3303 dispatches
			// against 78 stores per window, with 96% of exits taken here.
			// A negative verdict may only be reused once the binding-identity
			// probe has re-proved the bindings, which the guard already
			// requires, so publishing it cannot let a replacement be skipped.
			DX12PreSkinProbeStatePublisher::Publish(
				runtimeState,
				probe->computeShaderHash,
				earlyReloadGeneration,
				runtimeState ? runtimeState->iaContentEpoch : 0,
				DX12GetIaHashContentGeneration(),
				probe->bindingDependencyFingerprint,
				false,
				false,
				false,
				0,
				0,
				0);
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
			DX12_PROF_COUNT("Rm.PreSkinVerdictGeometryHits");
			DX12_PROF_CPU_STAGE(
				"PreSkin", "probe_cache_geometry_negative_hit", 0, 1,
				DX12_DIAG_ELAPSED_US(t0));
			DX12_LOG("DX12Perf",
				"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit_geometry\",\"us\":%lld",
				static_cast<unsigned long long>(probe->computeShaderHash),
				static_cast<long long>(DX12_DIAG_ELAPSED_US(t0)));
			return;
		}

		DX12_DIAG_QPC(fingerprintBegin);
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
			DX12_PROF_COUNT("Rm.PreSkinPlanFingerprintBuilds");
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
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"build_binding_fingerprint",
			0,
			static_cast<LONG64>(uavs.size() + srvs.size() + cbvs.size()),
			DX12_DIAG_ELAPSED_US(fingerprintBegin));

		// Negative hits can skip matching. Positive hits still reapply through
		// the existing path so required D3D12 commands are emitted.
		bool positiveCacheHit = false;
		DX12PreSkinDispatchOverride cachedDispatchOverride;
		DX12PreSkinProbeCacheEntry cacheEntry;
		DX12_PROF_COUNT("Rm.PreSkinPlanCacheLookups");
		DX12_DIAG_QPC(cacheLookupBegin);
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
				runtimeState->resolvedCompute.probeFingerprint =
					computeIaFingerprint;
				runtimeState->resolvedCompute.probeBindingSerial = probeBindingSerial;
				DX12PreSkinProbeStatePublisher::Publish(
					runtimeState,
					computeShaderHash,
					reloadGeneration,
					probeIaContentEpoch,
					DX12GetIaHashContentGeneration(),
					probe->bindingDependencyFingerprint,
					applied,
					dispatchOverride.handlingSkip,
					dispatchOverride.hasDispatch,
					dispatchOverride.groupsX,
					dispatchOverride.groupsY,
					dispatchOverride.groupsZ);
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
					probe->bindingDependencyFingerprint;
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
				probe->bindingDependencyFingerprint &&
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
				    probe->bindingDependencyFingerprint &&
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
			    probe->bindingDependencyFingerprint, &cacheEntry)) {
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				cacheEntry.applied ? "probe_cache_positive_hit" : "probe_cache_negative_hit",
				0,
				1,
				DX12_DIAG_ELAPSED_US(cacheLookupBegin));
			if (cacheEntry.applied)
				DX12_PROF_COUNT("Rm.PreSkinPlanCachePositiveHits");
			else
				DX12_PROF_COUNT("Rm.PreSkinPlanCacheNegativeHits");
			DX12_LOG_TEXT("PreparePreSkinProbe: cache_hit applied=%d fingerprint=%llu\n",
				cacheEntry.applied ? 1 : 0,
				static_cast<unsigned long long>(computeIaFingerprint));
			if (!runtimeProbeCacheHit) {
				storeRuntimeProbeCache(
					probe->computeShaderHash,
					cacheEntry.applied,
					cacheEntry.dispatchOverride);
			}
			if (!cacheEntry.applied) {
				probe->applied = false;
				DX12_LOG("DX12Perf",
					"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":false,\"cache\":\"negative_hit\",\"us\":%lld",
					static_cast<unsigned long long>(probe->computeShaderHash),
					static_cast<long long>(DX12_DIAG_ELAPSED_US(t0)));
				return;
			}
			positiveCacheHit = true;
			cachedDispatchOverride = cacheEntry.dispatchOverride;
			DX12_LOG_TEXT("PreparePreSkinProbe: positive cache requires command list reapply fingerprint=%llu\n",
				static_cast<unsigned long long>(computeIaFingerprint));
		} else {
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"probe_cache_miss",
				0,
				1,
				DX12_DIAG_ELAPSED_US(cacheLookupBegin));
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheMisses");

			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"async_warmup_bypassed",
				0,
				1,
				0);
		}

		static const std::vector<DX12CurrentComputeUavBinding> kEmptyComputeBindings;
		static const std::vector<DX12CurrentRootConstants> kEmptyRootConstants;
		DX12_PROF_STAGE_SCOPE(PreSkinMatch);
		DX12_PROF_STAGE_SCOPE(PreSkinApply);
		DX12_DIAG_QPC(applyBegin);
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
			probe->bindingDependencyFingerprint,
			&matchPhaseMiss);
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			probe->applied ? "apply_replacement_match" : "apply_replacement_miss",
			0,
			static_cast<LONG64>(uavs.size() + srvs.size() + cbvs.size()),
			DX12_DIAG_ELAPSED_US(applyBegin));
		// Only match-phase misses go to the geometry cache: they depend solely
		// on the inputs folded into negativeMatchKey. A positive that failed
		// its descriptor patch is content-dependent and must keep re-probing.
		if (matchPhaseMiss && negativeMatchKey)
			DX12ModStorePreSkinNegativeMatch(negativeMatchKey);
		if (positiveCacheHit && probe->applied && !probe->dispatchOverride.hasDispatch &&
		    cachedDispatchOverride.hasDispatch) {
			probe->dispatchOverride = cachedDispatchOverride;
		}
		const bool updateSharedProbeCache =
			(!runtimeProbeCacheHit && !positiveCacheHit) || !probe->applied;
		if (updateSharedProbeCache) {
			DX12_DIAG_QPC(storeCacheBegin);
			DX12ModStoreCachedPreSkinningUavMatch(
				commandList, probe->computeShaderHash, computeIaFingerprint,
				probe->bindingDependencyFingerprint,
				probe->applied, &probe->dispatchOverride);
			DX12_PROF_CPU_STAGE(
				"PreSkin",
				"store_probe_cache",
				0,
				1,
				DX12_DIAG_ELAPSED_US(storeCacheBegin));
			DX12_PROF_COUNT("Rm.PreSkinPlanCacheStores");
		}
		storeRuntimeProbeCache(
			probe->computeShaderHash,
			probe->applied,
			probe->dispatchOverride);

		DX12_LOG("DX12Perf",
			"\"scope\":\"PreSkinProbe\",\"cs\":\"%016llx\",\"applied\":%s,\"cache\":\"%s\",\"us\":%lld",
			static_cast<unsigned long long>(probe->computeShaderHash),
			probe->applied ? "true" : "false",
			positiveCacheHit ? "positive_reapply" : "miss",
			static_cast<long long>(DX12_DIAG_ELAPSED_US(t0)));
	}

	static void FinishPreSkinProbe(ID3D12GraphicsCommandList *commandList)
	{
		// Keep PreSkin descriptor/table mutations scoped to the current
		// dispatch. Deferred reuse is a CPU optimization, but if our tracked
		// command-list state drifts from the actual restored bindings it can
		// poison later matching and leave patched tables active for unrelated
		// Unity work.
		DX12_DIAG_QPC(restoreBegin);
		DX12ModRestorePreSkinningUavReplacement(commandList);
		DX12_PROF_CPU_STAGE(
			"PreSkin",
			"restore_replacement",
			0,
			1,
			DX12_DIAG_ELAPSED_US(restoreBegin));
	}
};

