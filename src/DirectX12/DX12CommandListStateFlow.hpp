#pragma once

#include <d3d12.h>

#include "DX12CommandListCommandRecorder.hpp"
#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12HotPathState.hpp"
#include "DX12Profiling.hpp"
#include "DX12ShaderHunt.hpp"

class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		if (DX12HotPathState::Get().skipBindings && DX12HuntIsEnabled() &&
		    !DX12ModHasActiveShaderOverrides()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
			DX12HuntSetPipelineState(commandList, pipelineState);
			return pipelineState;
		}

		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateCapture::PipelineState(commandList, pipelineState);
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
		if (DX12InternalReplayState::IsActive())
			return false;
		if (DX12HotPathState::Get().skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			return false;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::ResourceBarrier", commandList,
			" count=%u", numBarriers);
		DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
		return true;
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (!DX12HotPathState::Get().skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
				" topology=%u", static_cast<UINT>(topology));
			DX12CommandListStateCapture::PrimitiveTopology(commandList, topology);
			return;
		}

		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (!DX12HotPathState::Get().skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
				" gpu=0x%llx size=%u format=%u",
				view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
				view ? view->SizeInBytes : 0,
				view ? static_cast<UINT>(view->Format) : 0);
			DX12CommandListStateCapture::IndexBuffer(commandList, view);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetIndexBuffer(commandList, view);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (!DX12HotPathState::Get().skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
				" start=%u count=%u", startSlot, count);
			DX12CommandListStateCapture::VertexBuffers(commandList, startSlot, count, views);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		if (DX12HuntIsEnabled()) {
			DX12Profiling::RecordIaHuntIaUpdate();
			DX12HuntSetVertexBuffers(commandList, startSlot, count, views);
		}
		if (DX12ModHasActiveShaderOverrides())
			DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
	}
};
