#pragma once

#include <d3d12.h>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12HotPathState.hpp"
#include "DX12IaReplacementDrawScope.hpp"
#include "DX12IaReplacementDrawExecutor.hpp"
#include "DX12IaReplacementExecutor.hpp"
#include "DX12Profiling.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderHunt.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"

class DX12DrawHookFlow
{
public:
	static bool TryExecuteSkipAll(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!DX12HotPathState::skipAll)
			return false;
		DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
		return true;
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void ExecuteHook(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const DX12IaDrawInvocation &draw,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		DX12_HOTPATH_TIME_BEGIN(DrawHookTotal);
		const UINT64 flagsWord = DX12HotPathState::HookWorkFlags;
		const bool needsModWork = (flagsWord & DX12HotPathState::FLAG_NEEDS_MOD_WORK) != 0;
		const bool huntEnabled = (flagsWord & DX12HotPathState::FLAG_HUNT_ENABLED) != 0;

		if (!(flagsWord & DX12HotPathState::MASK_DRAW_WORK)) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
		}

		if (DX12Profiling::ShouldCollectCounters() || needsModWork)
			DX12RuntimeState::IncrementFrameDraws();

		if (DX12IaReplacementDrawScope::IsExecuting()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12IaReplacementDrawExecutor(commandList, callbacks).ExecuteOriginalDraw(draw);
			DX12_HOTPATH_TIME_END(DrawHookTotal);
			return;
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

		LogDrawCall(commandList, draw);
		if (DX12CommandListStateCapture::ShouldRecordBindingEvents())
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

		DX12DrawHookFlow::Execute(commandList, draw, runtimeState, callbacks);
		DX12_HOTPATH_TIME_END(DrawHookTotal);
	}

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw,
		const DX12CommandListRuntimeState *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (runtimeState && DX12ModNeedsPresentReplacement())
			DX12IaReplacementExecutor::ExecutePresent(commandList, *runtimeState, callbacks);
		if (runtimeState &&
		    DX12IaReplacementExecutor::HandleDrawOverrides(commandList, draw, *runtimeState, callbacks))
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

	static void LogDrawCall(
		ID3D12GraphicsCommandList *commandList,
		const DX12IaDrawInvocation &draw)
	{
		if (draw.indexed) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::DrawIndexedInstanced", commandList,
				" indices=%u instances=%u startIndex=%u baseVertex=%d startInstance=%u",
				draw.indexCount, draw.instanceCount, draw.firstIndex,
				draw.baseVertex, draw.firstInstance);
			return;
		}
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::DrawInstanced", commandList,
			" vertices=%u instances=%u startVertex=%u startInstance=%u",
			draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
	}
};
