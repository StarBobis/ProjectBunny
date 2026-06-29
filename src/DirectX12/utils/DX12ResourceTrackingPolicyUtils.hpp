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
		return true;
	}

	static bool NeedsFullDescriptorHistory()
	{
		return true;
	}

	static bool NeedsFullResourceMetadata()
	{
		return true;
	}

	static bool NeedsLiveResourceIndex()
	{
		return true;
	}

	static bool NeedsBufferResourceTracking()
	{
		return true;
	}

	static bool NeedsTextureResourceTracking()
	{
		return true;
	}

	static bool NeedsAnyResourceCreationTracking()
	{
		return true;
	}
};
