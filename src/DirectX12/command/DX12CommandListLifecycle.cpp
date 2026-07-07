#include "DX12CommandListLifecycle.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12ShaderHunt.hpp"

void DX12CommandListLifecycle::Register(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntime::Register(commandList);
	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningUavProbe()) {
		const bool inserted = DX12BindingTracker::RegisterCommandList(commandList);
		if (inserted)
			DX12BindingTracker::ResetCommandList(commandList, initialState);
	}
}

void DX12CommandListLifecycle::Reset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.trackBindings ||
	    DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, initialState);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, initialState);
	if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntime::ResetIa(commandList);
	if (tracking.trackPsoState || tracking.trackHuntIa)
		DX12HuntResetCommandList(commandList, initialState);
}

void DX12CommandListLifecycle::ClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RecordStateEvent(commandList, "clear_state");
	if (DX12ModNeedsPreSkinningUavProbe())
		DX12BindingTracker::ResetCommandList(commandList, pipelineState);
	if (tracking.trackPsoState)
		DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
	if (tracking.trackHuntIa)
		DX12CommandListRuntime::ResetIa(commandList);
	if (DX12HuntIsEnabled())
		DX12HuntResetCommandList(commandList, pipelineState);
}


