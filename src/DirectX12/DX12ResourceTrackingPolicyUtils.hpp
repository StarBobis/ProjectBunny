#pragma once

#include "DX12FrameAnalysisState.hpp"
#include "DX12RuntimeState.hpp"

bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsShaderDescriptorTracking();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();

class DX12ResourceTrackingPolicyUtils
{
public:
	static bool NeedsFrameDumpTracking()
	{
		return DX12FrameAnalysisState::IsActive() ||
			DX12FrameAnalysisState::IsCaptureRequested() ||
			DX12FrameAnalysisState::IsCapturing();
	}

	static bool NeedsShaderDumpTracking()
	{
		return DX12ShaderDumpIsCaptureRequested() ||
			DX12ShaderDumpIsCapturingFrame() ||
			DX12ShaderDumpIsBusy();
	}

	static bool NeedsDumpOrHuntingTracking()
	{
		return DX12RuntimeState::IsHuntingMode() ||
			NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking();
	}

	static bool NeedsDescriptorTracking()
	{
		return NeedsDumpOrHuntingTracking() ||
			DX12ModNeedsPreSkinningUavProbe() ||
			DX12ModNeedsShaderDescriptorTracking();
	}

	static bool NeedsFullDescriptorHistory()
	{
		return NeedsFrameDumpTracking();
	}

	static bool NeedsFullResourceMetadata()
	{
		return NeedsDumpOrHuntingTracking();
	}

	// Runtime recording (gRuntimeDescriptorByCpuHandle) is a fast cache for
	// draw-time descriptor queries.  It is only required when capture/dump/hunt
	// is actively inspecting every descriptor.  During normal Mod operation the
	// Worker pipeline + EnsureDescriptorHandlesVisible provides equivalent data
	// via gDescriptorByCpuHandle / gDescriptorHeaps.
	static bool NeedsRuntimeDescriptorRecording()
	{
		return NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking() ||
			DX12RuntimeState::IsHuntingMode() ||
			NeedsFullDescriptorHistory();
	}
};
