#include "DX12CommandListLifecycle.h"

#include "DX12BindingTracker.h"
#include "DX12CommandListRuntime.h"
#include "DX12CommandListStateCapture.h"
#include "DX12ModRuntime.h"
#include "DX12ShaderHunt.h"

void DX12CommandListLifecycleRegister(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	DX12CommandListRuntimeRegister(commandList);
	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList) ||
	    DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingRegisterCommandList(commandList);
		DX12BindingResetCommandList(commandList, initialState);
	}
}

void DX12CommandListLifecycleReset(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	if (DX12CommandListCaptureShouldTrackBindingsCached(commandList) ||
	    DX12ModNeedsPreSkinningUavProbe())
		DX12BindingResetCommandList(commandList, initialState);
	if (DX12CommandListCaptureShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntimeRememberPipelineState(commandList, initialState);
	if (DX12ModHasActiveShaderOverrides())
		DX12CommandListRuntimeResetIa(commandList);
	if (DX12CommandListCaptureShouldTrackPsoStateCached(commandList) ||
	    DX12CommandListCaptureShouldTrackHuntIaCached(commandList))
		DX12HuntResetCommandList(commandList, initialState);
}

void DX12CommandListLifecycleClearState(
	ID3D12GraphicsCommandList *commandList,
	ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	if (DX12CommandListCaptureShouldRecordBindingEventsCached(commandList))
		DX12BindingRecordStateEvent(commandList, "clear_state");
	if (DX12ModNeedsPreSkinningUavProbe())
		DX12BindingResetCommandList(commandList, pipelineState);
	if (DX12CommandListCaptureShouldTrackPsoStateCached(commandList))
		DX12CommandListRuntimeRememberPipelineState(commandList, pipelineState);
	if (DX12CommandListCaptureShouldTrackHuntIaCached(commandList))
		DX12CommandListRuntimeResetIa(commandList);
	if (DX12HuntIsEnabled())
		DX12HuntResetCommandList(commandList, pipelineState);
}
