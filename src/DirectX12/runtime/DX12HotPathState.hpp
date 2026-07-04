#pragma once

#include <Windows.h>

#include "DX12CommandListRuntime.hpp"
#include "DX12FrameAnalysisState.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();

class DX12HotPathState
{
public:
	inline static volatile LONG skipAll = 1;
	inline static volatile LONG skipBindings = 1;
	inline static volatile LONG skipGraphicsBindings = 1;
	inline static volatile LONG trackResourceMetadata = 0;
	inline static volatile LONG stateMask = -1;

	// ── 单一 flags word：替代 9 个独立函数调用，一条 if 判断全部 ──
	// 在 Update() 和 RebuildHookWorkFlags() 中更新，
	// ExecuteCommandLists 入口处也会刷新确保帧级别一致性。
	inline static volatile UINT64 HookWorkFlags = 0;

	// bit 定义
	static constexpr UINT64 FLAG_NEEDS_MOD_WORK          = 1ull << 0;
	static constexpr UINT64 FLAG_HUNT_ENABLED            = 1ull << 1;
	static constexpr UINT64 FLAG_FRAME_ANALYSIS_ACTIVE   = 1ull << 2;
	static constexpr UINT64 FLAG_SHADER_DUMP_ACTIVE      = 1ull << 3;
	static constexpr UINT64 FLAG_PRE_SKIN_PROBE          = 1ull << 4;

	// 汇总 mask：一条 if 即可判断是否有 draw / dispatch 级 hook 工作
	static constexpr UINT64 MASK_DRAW_WORK =
		FLAG_NEEDS_MOD_WORK | FLAG_HUNT_ENABLED |
		FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE;
	static constexpr UINT64 MASK_DISPATCH_WORK =
		MASK_DRAW_WORK | FLAG_PRE_SKIN_PROBE;

	static void RebuildHookWorkFlags()
	{
		UINT64 flags = 0;
		if (DX12ModHasActiveShaderOverrides() || DX12ModNeedsPresentReplacement())
			flags |= FLAG_NEEDS_MOD_WORK;
		if (DX12HuntIsEnabled())
			flags |= FLAG_HUNT_ENABLED;
		if (DX12FrameAnalysisState::IsActive() || DX12FrameAnalysisState::IsCapturing() || DX12FrameAnalysisState::IsCaptureRequested())
			flags |= FLAG_FRAME_ANALYSIS_ACTIVE;
		if (DX12ShaderDumpIsCapturingFrame() || DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy())
			flags |= FLAG_SHADER_DUMP_ACTIVE;
		if (DX12ModNeedsPreSkinningUavProbe())
			flags |= FLAG_PRE_SKIN_PROBE;

		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&HookWorkFlags), static_cast<LONG64>(flags));
	}

	static void Update()
	{
		if (skipAll &&
			!DX12FrameAnalysisState::IsActive() &&
			!DX12FrameAnalysisState::IsCapturing() &&
			!DX12FrameAnalysisState::IsCaptureRequested() &&
			!DX12ShaderDumpIsCapturingFrame() &&
			!DX12ShaderDumpIsCaptureRequested() &&
			!DX12ShaderDumpIsBusy() &&
			!DX12HuntNeedsHotPath() &&
			!DX12ModHasActiveShaderOverrides() &&
			!DX12ModNeedsPresentReplacement() &&
			!DX12ModNeedsPreSkinningUavProbe() &&
			!DX12ModNeedsPreSkinningBindingTracking())
			return;

		const bool huntingMode = DX12RuntimeState::IsHuntingMode();
		const bool needsHeavyTracking =
			huntingMode ||
			DX12ShaderDumpIsCapturingFrame() ||
			DX12ShaderDumpIsBusy();

		// DX11 风格: skipAll 只控制 draw/state/copy 级 hook。
		// PreSkin 仅需 dispatch hook + compute binding 跟踪，
		// 通过 skipBindings + dispatch hook 中的独立检查保证。
		const bool needsRecordWork =
			huntingMode ||
			DX12ModHasActiveShaderOverrides() ||
			DX12ModNeedsPresentReplacement();
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

		InterlockedExchange(&DX12HotPathState::skipAll, skipAll);
		InterlockedExchange(&DX12HotPathState::skipBindings, skipBindings);
		InterlockedExchange(&DX12HotPathState::skipGraphicsBindings, skipGraphicsBindings);
		InterlockedExchange(&DX12HotPathState::trackResourceMetadata, trackResourceMetadata);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		if (InterlockedExchange(&stateMask, mask) != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntime::BumpTrackingGeneration();
			DX12LogManager::JsonFunc("DX12HotPathState",
				"\"mode\":\"%s\",\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
				huntingMode ? "Hunting" : "Mod",
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
		}

		// 同步更新 flags word：一条 if 替代 9 个函数调用
		RebuildHookWorkFlags();
	}

	static bool HooksNeeded()
	{
		return InterlockedCompareExchange(&skipAll, 0, 0) == 0 ||
			DX12HuntNeedsHotPath();
	}
};

