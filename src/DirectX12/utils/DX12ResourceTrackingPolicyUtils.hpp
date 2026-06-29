#pragma once

#include "../DX12FrameAnalysisState.hpp"
#include "../DX12RuntimeState.hpp"

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
		return DX12FrameAnalysisState::Get().IsActive() ||
			DX12FrameAnalysisState::Get().IsCaptureRequested() ||
			DX12FrameAnalysisState::Get().IsCapturing();
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
			DX12ModNeedsShaderDescriptorTracking() ||
			DX12ModNeedsPreSkinningUavProbe();
	}

	static bool NeedsFullDescriptorHistory()
	{
		return DX12RuntimeState::IsHuntingMode() ||
			NeedsFrameDumpTracking();
	}

	static bool NeedsFullResourceMetadata()
	{
		return NeedsDumpOrHuntingTracking();
	}

	static bool NeedsBufferResourceTracking()
	{
		return NeedsFullResourceMetadata() ||
			DX12ModHasActiveShaderOverrides() ||
			DX12ModNeedsShaderDescriptorTracking() ||
			DX12ModNeedsPreSkinningUavProbe() ||
			DX12ModNeedsPresentReplacement();
	}

	static bool NeedsTextureResourceTracking()
	{
		return NeedsFullResourceMetadata() ||
			DX12ModNeedsPreSkinningUavProbe() ||
			DX12ModNeedsPresentReplacement();
	}

	static bool NeedsAnyResourceCreationTracking()
	{
		return NeedsBufferResourceTracking() ||
			NeedsTextureResourceTracking();
	}
};
