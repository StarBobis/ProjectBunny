#pragma once

#include <d3d12.h>

#include "DX12CommandListCommandRecorder.hpp"
#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12HotPathState.hpp"
#include "DX12Profiling.hpp"
#include "DX12ShaderHunt.hpp"
#include "utils/DX12ResidentMaintenanceTelemetryUtils.hpp"

class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePipelineState(
			commandList, runtimeState, pipelineState);
	}

	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12PipelineState *pipelineState)
	{
		if (DX12HotPathState::skipBindings && DX12HuntIsEnabled() &&
		    !DX12ModHasActiveShaderOverrides()) {
			DX12_PROFILE_FAST_FORWARD();
			if (runtimeState)
				DX12CommandListRuntime::RememberPipelineState(runtimeState, pipelineState);
			else
				DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
			DX12HuntObservePipelineState(pipelineState);
			return pipelineState;
		}

		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetPipelineState", commandList,
			" pso=%p active=%p", pipelineState, activePipelineState);
		return activePipelineState;
	}

	static bool CaptureResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureCall(numBarriers);
		if (DX12InternalReplayState::IsActive()) {
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedInternalReplay();
			return false;
		}
		if (DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedHotPath();
			return false;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::ResourceBarrier", commandList,
			" count=%u", numBarriers);
		DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierRecorded(numBarriers);
		return true;
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
				" topology=%u", static_cast<UINT>(topology));
			DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
			return;
		}

		if (DX12HuntIsEnabled()) {
			if (runtimeState)
				DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			else
				DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
		if (DX12ModHasActiveShaderOverrides()) {
			if (runtimeState)
				DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			else
				DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureIndexBuffer(commandList, runtimeState, view);
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
				" gpu=0x%llx size=%u format=%u",
				view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
				view ? view->SizeInBytes : 0,
				view ? static_cast<UINT>(view->Format) : 0);
			DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			if (runtimeState)
				DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			else
				DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
		if (DX12ModHasActiveShaderOverrides()) {
			if (runtimeState)
				DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			else
				DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, views);
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
				" start=%u count=%u", startSlot, count);
			DX12CommandListStateCapture::VertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			if (runtimeState)
				DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			else
				DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
		if (DX12ModHasActiveShaderOverrides()) {
			if (runtimeState)
				DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			else
				DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
	}
};
