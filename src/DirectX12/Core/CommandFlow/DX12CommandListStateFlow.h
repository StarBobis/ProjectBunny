#pragma once

// Command-list state capture/replay flow.
// Split out of Core/DX12CommandFlow.h; include that umbrella header from existing code.
#include "../DX12HotPath.h"

class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePipelineState(
			commandList, runtimeState, pipelineState);
	}

	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12PipelineState *pipelineState)
	{
		// Same-PSO fast path: when the identical PSO is set again, skip all
		// tracking.  Resolve runtimeState if the caller didn't provide one.
		DX12SimulateContext *rs = runtimeState;
		if (!rs) {
			rs = DX12CommandListRuntime::GetStatePtrFast(commandList);
		}
		if (rs && (rs->pipelineState == pipelineState ||
				rs->cachedReplacementPsoKey == pipelineState)) {
			DX12_PROF_COUNT("FastForward");
			return rs->cachedReplacementPso
				? rs->cachedReplacementPso : pipelineState;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		if (DX12HotPathState::skipBindings.load(std::memory_order_relaxed) &&
		    DX12HotPathState::NeedsHuntOnlyPipelineObservation(flags)) {
			DX12_PROF_COUNT("FastForward");
			if (runtimeState)
				DX12CommandListRuntime::RememberPipelineState(runtimeState, pipelineState);
			else
				DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
			DX12HuntObservePipelineState(pipelineState);
			return pipelineState;
		}

		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
				return activePipelineState;
	}

	static bool CaptureResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers)
	{
		if (DX12HotPathState::skipBindings.load(std::memory_order_relaxed)) {
			DX12_PROF_COUNT("FastForward");
			return false;
		}
		DX12_PROF_COUNT("Rm.BarrierCaptureCalls");
		DX12_PROF_ADD_ITEMS("Rm.BarrierCaptureItems", static_cast<LONG64>(numBarriers));
		if (DX12InternalReplayState::IsActive()) {
			DX12_PROF_COUNT("Rm.BarrierCaptureSkippedInternalReplay");
			return false;
		}
		// No ShouldTrackResourceStates() gate here: RecordResourceBarrier's own
		// first statement is exactly that test (DX12CommandListStateCapture.cpp),
		// and it must keep it - DX12ResourceCollectQueue replays queued barriers
		// through the same free function. Evaluating it twice per barrier call
		// only doubled a four-term predicate on a path that runs thousands of
		// times per frame.

				DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
		DX12_PROF_COUNT("Rm.BarrierRecordedCalls");
		DX12_PROF_ADD_ITEMS("Rm.BarrierRecordedItems", static_cast<LONG64>(numBarriers));
		return true;
	}

	static bool CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
	}

	static bool CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings.load(std::memory_order_relaxed)) {
						DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
			return true;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			return DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
		return true;
	}

	static bool CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CaptureIndexBuffer(commandList, runtimeState, view);
	}

	static bool CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings.load(std::memory_order_relaxed)) {
						DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
			return true;
		}

		DX12_PROF_COUNT("FastForward");
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12_PROF_COUNT("IaHuntIaUpdate");
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			return DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
		return true;
	}

	static bool CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, views);
	}

	static bool CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings.load(std::memory_order_relaxed)) {
						DX12CommandListStateCapture::VertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			return true;
		}

		DX12_PROF_COUNT("FastForward");
		const UINT64 flags = DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed);
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12_PROF_COUNT("IaHuntIaUpdate");
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			return DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
		return true;
	}
};
