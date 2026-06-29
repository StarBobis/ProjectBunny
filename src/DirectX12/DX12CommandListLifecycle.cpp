#include "DX12CommandListLifecycle.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12ModRuntime.h"
#include "DX12ShaderHunt.h"

void DX12CommandListLifecycle::Register(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntime::Register(commandList);
	if (DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList) ||
	    DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingTracker::RegisterCommandList(commandList);
		DX12BindingTracker::ResetCommandList(commandList, initialState);
	}
}

void DX12CommandListLifecycle::Reset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	if (DX12CommandListStateCapture::ShouldTrackBindingsCached(commandList) ||
	    DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, initialState);
	if (DX12CommandListStateCapture::ShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntime::RememberPipelineState(commandList, initialState);
	if (DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::ResetIa(commandList);
	if (DX12CommandListStateCapture::ShouldTrackPsoStateCached(commandList) ||
	    DX12CommandListStateCapture::ShouldTrackHuntIaCached(commandList))
		DX12HuntResetCommandList(commandList, initialState);
}

void DX12CommandListLifecycle::ClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	if (DX12CommandListStateCapture::ShouldRecordBindingEventsCached(commandList))
		DX12BindingTracker::RecordStateEvent(commandList, "clear_state");
	if (DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, pipelineState);
	if (DX12CommandListStateCapture::ShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
	if (DX12CommandListStateCapture::ShouldTrackHuntIaCached(commandList))
		DX12CommandListRuntime::ResetIa(commandList);
	if (DX12HuntIsEnabled())
		DX12HuntResetCommandList(commandList, pipelineState);
}


