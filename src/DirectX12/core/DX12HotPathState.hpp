#pragma once

#include <Windows.h>

#include "DX12CommandListRuntime.h"
#include "DX12FrameAnalysisState.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

void DX12CommandListRuntimeBumpTrackingGeneration();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();

class DX12HotPathState
{
public:
	volatile LONG skipAll = 1;
	volatile LONG skipBindings = 1;
	volatile LONG skipGraphicsBindings = 1;
	volatile LONG trackResourceMetadata = 0;
	volatile LONG stateMask = -1;

	static DX12HotPathState &Get()
	{
		static DX12HotPathState state;
		return state;
	}

	static void Update()
	{
		DX12HotPathState &state = Get();
		if (state.skipAll &&
			!DX12FrameAnalysisState::Get().IsActive() &&
			!DX12FrameAnalysisState::Get().IsCapturing() &&
			!DX12FrameAnalysisState::Get().IsCaptureRequested() &&
			!DX12ShaderDumpIsCapturingFrame() &&
			!DX12ShaderDumpIsCaptureRequested() &&
			!DX12ShaderDumpIsBusy() &&
			!DX12HuntNeedsHotPath() &&
			!DX12ModHasActiveShaderOverrides() &&
			!DX12ModNeedsPresentReplacement() &&
			!DX12ModNeedsPreSkinningUavProbe())
			return;

		const bool huntingMode = DX12RuntimeState::IsHuntingMode();
		const bool needsHeavyTracking =
			huntingMode ||
			DX12ShaderDumpIsCapturingFrame() ||
			DX12ShaderDumpIsBusy();

		const bool needsRecordWork =
			huntingMode ||
			DX12ModHasActiveShaderOverrides() ||
			DX12ModNeedsPresentReplacement() ||
			DX12ModNeedsPreSkinningUavProbe();
		const bool needsBindingState =
			needsHeavyTracking ||
			DX12ModNeedsPreSkinningBindingTracking();
		const bool needsGraphicsBindingState =
			needsHeavyTracking ||
			DX12ModNeedsShaderDescriptorTracking();

		const LONG skipAll = (needsHeavyTracking || needsRecordWork) ? 0 : 1;
		const LONG skipBindings = needsBindingState ? 0 : 1;
		const LONG skipGraphicsBindings = needsGraphicsBindingState ? 0 : 1;
		const LONG trackResourceMetadata = needsHeavyTracking ? 1 : 0;

		InterlockedExchange(&state.skipAll, skipAll);
		InterlockedExchange(&state.skipBindings, skipBindings);
		InterlockedExchange(&state.skipGraphicsBindings, skipGraphicsBindings);
		InterlockedExchange(&state.trackResourceMetadata, trackResourceMetadata);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		if (InterlockedExchange(&state.stateMask, mask) != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntimeBumpTrackingGeneration();
			DX12LogManager::JsonFunc("DX12HotPathState",
				"\"mode\":\"%s\",\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
				huntingMode ? "Hunting" : "Mod",
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
		}
	}

	static bool HooksNeeded()
	{
		return InterlockedCompareExchange(&Get().skipAll, 0, 0) == 0 ||
			DX12HuntNeedsHotPath();
	}
};
