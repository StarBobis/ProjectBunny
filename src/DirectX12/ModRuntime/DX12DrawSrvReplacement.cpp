// DX12DrawSrvReplacement.cpp: draw SRV replacement entry points.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"



bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	if (!commandList || !replacement || replacement->drawSrvPatches.empty())
		return false;
	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;
	const bool applied = DX12DrawSrvReplacementUtils::ApplyPending(
		device, commandList, replacement, executeOriginalDraw);
	device->Release();
	return applied;
}



void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement)
{
	DX12DrawSrvReplacementUtils::Restore(commandList, replacement);
}



void DX12ModFlushDrawSrvReplacement(ID3D12GraphicsCommandList *commandList)
{
	DX12DrawSrvReplacementUtils::FlushPendingRestore(commandList);
}



void DX12ModDropDrawSrvReplacementForHeapChange(ID3D12GraphicsCommandList *commandList)
{
	DX12DrawSrvReplacementUtils::DropPendingRestore(commandList);
}



void DX12ModForgetDrawSrvReplacementForCommandList(ID3D12GraphicsCommandList *commandList)
{
	DX12DrawSrvReplacementUtils::DropPendingRestore(commandList);
}



bool DX12ModHasPendingDrawSrvRestore()
{
	// Plain volatile probe (same ordering argument as the pre-skin active
	// flag): a same-thread reader always observes its own writes in program
	// order, and a stale cross-thread value only changes how much work a
	// probe does, never its correctness.
	return gDrawSrvPendingRestoreCount != 0;
}
