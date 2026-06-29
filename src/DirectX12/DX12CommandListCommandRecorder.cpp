#include "DX12CommandListCommandRecorder.hpp"

#include "DX12CommandListLifecycle.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12ResourceTracker.h"
#include "DX12ShaderHunt.h"

void DX12CommandListCommandRecorder::RecordResourceBarrier(
	UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers)
{
	// ResourceBarrier is the explicit D3D12 synchronization point. This module
	// only records observed barriers today; future state validation can grow
	// here without making hook entry points understand resource-state policy.
	if (DX12CommandListStateCapture::ShouldTrackBindings())
		DX12RecordResourceBarrier(numBarriers, barriers);
}

void DX12CommandListCommandRecorder::RecordExecuteBundle(
	ID3D12GraphicsCommandList *bundle,
	ID3D12PipelineState *initialState)
{
	// Bundle execution records command-list state into the parent stream. Keep
	// bundle registration here so hook code does not need to know lifecycle
	// details for secondary command lists.
	DX12CommandListLifecycle::Register(bundle, initialState);
}

void DX12CommandListCommandRecorder::RecordExecuteIndirect(
	ID3D12GraphicsCommandList *commandList)
{
	// ExecuteIndirect can expand into draw or dispatch work on the GPU. Shader
	// hunting treats it as both graphics and compute usage without inspecting
	// the command signature on the CPU hot path.
	DX12HuntRecordExecuteIndirect(commandList);
}

