#pragma once

// Draw hook flow.
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

class DX12DrawHookFlow
{
public:

	// Per-PSO noop fast path for the command-list wrapper: when the compiled
	// draw decision proves the current PSO can never match any override, the
	// draw can be forwarded without entering the generic draw interpreter.
	// This is exactly the interpreter-internal NoopNeverMatches forward, minus
	// the entry tax (flag re-reads, telemetry counts, decision validation).
	// Safe only when no per-draw bookkeeping can apply, hence the gates:
	//   - hunt / frame analysis / shader dump need per-draw recording;
	//   - shader descriptor tracking and draw-SRV slot replacement record
	//     binding events before the interpreter's noop forward;
	//   - redirected static IA / template IA bindings need the restore checks
	//     the interpreter performs before forwarding;
	//   - a pending present replacement must be recorded first;
	//   - a capture request that has not activated yet must not miss draws.
	// The decision itself is validated exactly like
	// precompiledDecisionStillCurrent (PSO, reload generation, globals).
	template <typename Fn>
	static bool TryForwardNoopDraw(
		DX12SimulateContext *runtimeState,
		UINT64 drawFlags,
		Fn &&executeOriginal)
	{
		if (!runtimeState)
			return false;
		// A pending deferred draw-SRV restore must flush before any draw; the
		// generic path routes through the draw executor, which flushes.
		if (drawFlags & DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE)
			return false;
		if (drawFlags & (DX12HotPathState::FLAG_HUNT_ENABLED |
				DX12HotPathState::FLAG_FRAME_ANALYSIS_ACTIVE |
				DX12HotPathState::FLAG_SHADER_DUMP_ACTIVE |
				DX12HotPathState::FLAG_SHADER_DESCRIPTOR_TRACK |
				DX12HotPathState::FLAG_DRAW_SRV_SLOT_REPLACEMENT))
			return false;
		if (DX12FrameAnalysisState::IsCaptureRequested())
			return false;
		const DX12ActiveIaState &ia = runtimeState->ia;
		if (ia.indexBufferStaticReplaced || ia.vertexBufferStaticReplacedMask)
			return false;
		if (runtimeState->staticTemplateIaApplied)
			return false;
		if ((drawFlags & DX12HotPathState::FLAG_PRESENT_REPLACEMENT) &&
				DX12ModPresentReplacementPendingForCurrentPresent())
			return false;
		const DX12CompiledDrawDecision &decision = runtimeState->drawDecision;
		if (!decision.resolved ||
				decision.pipelineState != runtimeState->pipelineState ||
				decision.kind != DX12CompiledDrawDecisionKind::NoopNeverMatches)
			return false;
		if (decision.reloadGeneration != DX12ModGetReloadGenerationFast())
			return false;
		const bool activeShaderOverrides =
			(drawFlags & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
		if (decision.hasActiveShaderOverrides != activeShaderOverrides)
			return false;
		// Keep frame-draw accounting identical to the interpreter path
		// (needsModWork is necessarily set when this fast path is reached).
		DX12RuntimeState::IncrementFrameDraws();
		executeOriginal();
		return true;
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		if (!(flagsWord & DX12HotPathState::MASK_DRAW_WORK) &&
			!DX12Profiling::ShouldCollectCounters()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks, flagsWord);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawHookTotal);
		const bool needsModWork = (flagsWord & DX12HotPathState::FLAG_NEEDS_MOD_WORK) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;
		const bool frameCapturePendingOrActive =
			DX12FrameAnalysisState::IsCaptureRequested() ||
			DX12FrameAnalysisState::IsCapturing();
		const DX12CompiledDrawDecision *residentDrawDecision = nullptr;

		if (!(flagsWord & DX12HotPathState::MASK_DRAW_WORK)) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		// DX11-style resident PSO rejection: when shader overrides are the only
		// draw-driven feature for this PSO, a state proven unable to match must
		// not enter the generic draw interpreter (counters, logging, IA keys, or
		// cache layers). Reload generation keeps the shortcut conservative across
		// hot reloads while still allowing global resident descriptor/IA features
		// to coexist with the fast reject.
		DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
			commandList, runtimeState, callbacks);
		if (DX12Profiling::ShouldCollectCounters() || needsModWork)
			DX12RuntimeState::IncrementFrameDraws();

		if (DX12IaReplacementDrawScope::IsExecuting()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		// A compiled NoopNeverMatches decision is a PSO-level proof that no
		// draw on this PSO can reach an override.  Keep that decision at the
		// SetPipelineState/draw boundary instead of paying the DX11-style
		// generic draw interpreter for every draw in the main scene.  Preserve
		// the two static-IA restore checks from HandleDrawOverrides before
		// forwarding: they are normally no-ops, but a PSO switch can require one
		// restore before this non-matching draw is recorded. Hunt/capture paths
		// retain their per-draw bookkeeping below.
		if (needsModWork) {
			DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathAttempt();
			if (huntEnabled) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathHuntBlocked();
			} else if (frameCapturePendingOrActive) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathCaptureBlocked();
			} else if (flagsWord & DX12HotPathState::FLAG_SHADER_DUMP_ACTIVE) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathShaderDumpBlocked();
			} else if (!runtimeState) {
				DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNoRuntime();
			} else {
				const bool activeShaderOverrides =
					(flagsWord & DX12HotPathState::FLAG_SHADER_OVERRIDES) != 0;
				const DX12CompiledDrawDecision *decision =
					DX12CommandListRuntime::EnsureDrawDecision(
						runtimeState, activeShaderOverrides);
				residentDrawDecision = decision;
				if (!decision) {
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNoDecision();
				} else if (decision->kind == DX12CompiledDrawDecisionKind::NoopNeverMatches) {
					// The generic draw path performs this watchdog before matching.
					// For a PSO-proven Noop draw it is only needed immediately before
					// forwarding, avoiding a duplicate check on every non-noop draw.
					DX12IaReplacementExecutor::RestoreStaleStaticIaBindings(
						commandList, runtimeState, callbacks);
					if (runtimeState->staticTemplateIaApplied) {
						DX12IaReplacementExecutor::RestoreStaticTemplateIa(
							commandList, runtimeState, callbacks);
					}
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathForwarded();
					DX12_PROFILE_FAST_FORWARD();
					DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
					DX12_HOTPATH_TIME_END(DrawHookTotal);
					return;
				} else {
					DX12ResidentMaintenanceTelemetryUtils::RecordDrawNoopFastPathNonNoopDecision();
				}
			}
		}

		if (DX12HotPathState::skipBindings && huntEnabled && !needsModWork) {
			DX12_PROFILE_FAST_FORWARD();
			if (ShouldSkipHuntDraw(commandList, draw)) {
				DX12_HOTPATH_TIME_END(DrawHookTotal);
				return;
			}
			RecordHuntDraw(commandList, draw);
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		const bool shouldRecordBindingEvents =
			DX12HotPathState::NeedsBindingEventRecording(flagsWord) &&
			(runtimeState ?
				DX12CommandListRuntime::GetTrackingState(runtimeState).recordBindingEvents :
				DX12CommandListStateCapture::ShouldRecordBindingEvents());
		if (shouldRecordBindingEvents)
			RecordBindingDraw(commandList, draw);
		if (huntEnabled) {
			if (ShouldSkipHuntDraw(commandList, draw))
				return;
			RecordHuntDraw(commandList, draw);
		}
		if (!needsModWork) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		DX12DrawHookFlow::Execute(
			commandList, draw, runtimeState, callbacks, residentDrawDecision, flagsWord);
		DX12_HOTPATH_TIME_END(DrawHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks,
		const DX12CompiledDrawDecision *residentDrawDecision = nullptr,
		UINT64 flagsWord = DX12HotPathState::HookWorkFlags)
	{
		if (runtimeState &&
		    (flagsWord & DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			DX12IaReplacementExecutor::ExecutePresent(commandList, *runtimeState, callbacks);
		if (runtimeState &&
		    DX12IaReplacementExecutor::HandleDrawOverrides(
				commandList, draw, *runtimeState, callbacks, residentDrawDecision))
			return;
		DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
	}

	static bool ShouldSkipHuntDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		return DX12HuntShouldSkipDraw(
			commandList, draw.indexed,
			draw.indexed ? 0 : draw.vertexCount,
			draw.indexed ? draw.indexCount : 0,
			draw.indexed ? 0 : draw.firstVertex,
			draw.indexed ? draw.firstIndex : 0,
			draw.indexed ? draw.baseVertex : 0);
	}

	static void RecordHuntDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		DX12HuntRecordDraw(
			commandList, draw.indexed,
			draw.indexed ? 0 : draw.vertexCount,
			draw.indexed ? draw.indexCount : 0,
			draw.indexed ? 0 : draw.firstVertex,
			draw.indexed ? draw.firstIndex : 0,
			draw.indexed ? draw.baseVertex : 0);
	}

	static void RecordBindingDraw(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		if (draw.indexed) {
			DX12BindingTracker::RecordDrawIndexedInstanced(
				commandList, draw.indexCount, draw.instanceCount,
				draw.firstIndex, draw.baseVertex, draw.firstInstance);
			return;
		}
		DX12BindingTracker::RecordDrawInstanced(
			commandList, draw.vertexCount, draw.instanceCount,
			draw.firstVertex, draw.firstInstance);
	}
};
