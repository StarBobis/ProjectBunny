#pragma once

// Hot-path feature gate flags and shared hot-path runtime state.
// Split out of Core/DX12HotPath.h; include that umbrella header from existing code.
#include "../DX12CommandState.h"

enum DX12ModHotPathFlagBits : UINT32
{
	DX12_MOD_HOT_SHADER_OVERRIDES = 1u << 0,
	DX12_MOD_HOT_PRESENT_REPLACEMENT = 1u << 1,
	DX12_MOD_HOT_PRE_SKIN_PROBE = 1u << 2,
	DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK = 1u << 3,
	DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK = 1u << 4,
	DX12_MOD_HOT_IA_RESIDENT_STATE = 1u << 5,
	DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT = 1u << 6
};
UINT32 DX12ModGetHotPathFlags();
void DX12ActivateCommandListHotPathHooks();

class DX12HotPathState
{
public:
	inline static std::atomic<LONG> skipAll{ 1 };
	inline static std::atomic<LONG> skipBindings{ 1 };
	inline static std::atomic<LONG> skipGraphicsBindings{ 1 };
	inline static std::atomic<LONG> trackResourceMetadata{ 0 };
	inline static std::atomic<LONG> stateMask{ -1 };
	inline static std::atomic<UINT64> loggedHookWorkFlags{ UINT64_MAX };

	// A single flags word replaces nine independent hot-path checks.
	// Update publishes it from Present-time frame actions and state transitions.
	inline static std::atomic<UINT64> HookWorkFlags{ 0 };

	// Individual work bits.
	static constexpr UINT64 FLAG_NEEDS_MOD_WORK          = 1ull << 0;
	static constexpr UINT64 FLAG_HUNT_ENABLED            = 1ull << 1;
	static constexpr UINT64 FLAG_FRAME_ANALYSIS_ACTIVE   = 1ull << 2;
	static constexpr UINT64 FLAG_SHADER_DUMP_ACTIVE      = 1ull << 3;
	static constexpr UINT64 FLAG_PRE_SKIN_PROBE          = 1ull << 4;
	static constexpr UINT64 FLAG_PRE_SKIN_BINDING_TRACK  = 1ull << 5;
	static constexpr UINT64 FLAG_SHADER_OVERRIDES         = 1ull << 6;
	static constexpr UINT64 FLAG_PRESENT_REPLACEMENT      = 1ull << 7;
	static constexpr UINT64 FLAG_IA_RESIDENT_STATE        = 1ull << 8;
	static constexpr UINT64 FLAG_SHADER_DESCRIPTOR_TRACK  = 1ull << 9;
	static constexpr UINT64 FLAG_DRAW_SRV_SLOT_REPLACEMENT = 1ull << 10;

	// Combined masks allow one branch for draw or dispatch hook work.
	static constexpr UINT64 MASK_DRAW_WORK =
		FLAG_NEEDS_MOD_WORK | FLAG_HUNT_ENABLED |
		FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE;
	static constexpr UINT64 MASK_DISPATCH_WORK =
		MASK_DRAW_WORK | FLAG_PRE_SKIN_PROBE;

	class SourceState
	{
	public:
		bool huntEnabled = false;
		bool frameAnalysisActive = false;
		bool shaderDumpActive = false;
		bool shaderOverrides = false;
		bool presentReplacement = false;
		bool preSkinProbe = false;
		bool preSkinBindingTrack = false;
		bool shaderDescriptorTrack = false;
		bool iaResidentState = false;
		bool drawSrvSlotReplacement = false;

		static SourceState Capture()
		{
			SourceState state;
			state.huntEnabled = DX12HuntNeedsHotPath();
			state.frameAnalysisActive = DX12FrameAnalysisState::IsActive();
			state.shaderDumpActive = DX12ShaderDumpIsBusy();
			const UINT32 modFlags = DX12ModGetHotPathFlags();
			state.shaderOverrides =
				(modFlags & DX12_MOD_HOT_SHADER_OVERRIDES) != 0;
			state.presentReplacement =
				(modFlags & DX12_MOD_HOT_PRESENT_REPLACEMENT) != 0;
			state.preSkinProbe =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_PROBE) != 0;
			state.preSkinBindingTrack =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK) != 0;
			state.shaderDescriptorTrack =
				(modFlags & DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK) != 0;
			state.iaResidentState =
				(modFlags & DX12_MOD_HOT_IA_RESIDENT_STATE) != 0;
			state.drawSrvSlotReplacement =
				(modFlags & DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT) != 0;
			return state;
		}

		UINT64 BuildHookWorkFlags() const
		{
			UINT64 flags = 0;
			if (NeedsModDrawWork())
				flags |= FLAG_NEEDS_MOD_WORK;
			if (shaderOverrides)
				flags |= FLAG_SHADER_OVERRIDES;
			if (presentReplacement)
				flags |= FLAG_PRESENT_REPLACEMENT;
			if (huntEnabled)
				flags |= FLAG_HUNT_ENABLED;
			if (frameAnalysisActive)
				flags |= FLAG_FRAME_ANALYSIS_ACTIVE;
			if (shaderDumpActive)
				flags |= FLAG_SHADER_DUMP_ACTIVE;
			if (preSkinProbe)
				flags |= FLAG_PRE_SKIN_PROBE;
			if (preSkinBindingTrack)
				flags |= FLAG_PRE_SKIN_BINDING_TRACK;
			if (iaResidentState)
				flags |= FLAG_IA_RESIDENT_STATE;
			if (shaderDescriptorTrack)
				flags |= FLAG_SHADER_DESCRIPTOR_TRACK;
			if (drawSrvSlotReplacement)
				flags |= FLAG_DRAW_SRV_SLOT_REPLACEMENT;
			return flags;
		}

		bool HasAnyWork() const
		{
			return huntEnabled ||
				frameAnalysisActive ||
				shaderDumpActive ||
				shaderOverrides ||
				presentReplacement ||
				preSkinProbe ||
				preSkinBindingTrack ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsHeavyTracking() const
		{
			return huntEnabled || shaderDumpActive;
		}

		bool NeedsRecordWork() const
		{
			return huntEnabled || NeedsModDrawWork();
		}

		bool NeedsModDrawWork() const
		{
			return shaderOverrides ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsBindingState() const
		{
			// Pre-skin keeps its compute bindings resident in the lightweight
			// runtime snapshot and no longer needs the full tracked-binding path.
			return NeedsHeavyTracking();
		}

		bool NeedsGraphicsBindingState() const
		{
			return NeedsHeavyTracking() || shaderDescriptorTrack ||
				drawSrvSlotReplacement;
		}

		const char *ModeName() const
		{
			return huntEnabled ? "Hunting" : "Mod";
		}
	};

	static bool HasAnyFlag(UINT64 flags, UINT64 mask)
	{
		return (flags & mask) != 0;
	}

	static bool NeedsResidentIaState(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED | FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsGraphicsResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_DRAW_SRV_SLOT_REPLACEMENT |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootDescriptorState(UINT64 flags)
	{
		// Descriptor-triggered Mod matching can still query graphics root
		// descriptors from the lightweight runtime snapshot, but draw-time SRV
		// table patching does not need them.
		return HasAnyFlag(
			flags,
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootValueState(UINT64 flags)
	{
		// DX11-style Mod matching for graphics texture replacement only needs the
		// live descriptor heaps/tables/root signature. Outside capture/debug, the
		// only graphics root values that still matter are root descriptors for
		// descriptor-triggered Mod lookups; root constants remain capture-only.
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsTrackedBindingCapture(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentBindingState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsAnyResidentDescriptorState(UINT64 flags)
	{
		return NeedsGraphicsResidentDescriptorState(flags) ||
			NeedsComputeResidentDescriptorState(flags);
	}

	static bool NeedsHuntOnlyPipelineObservation(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED) &&
			!HasAnyFlag(flags, FLAG_SHADER_OVERRIDES);
	}


	static bool NeedsPreSkinProbe(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_PRE_SKIN_PROBE);
	}

	static bool NeedsIaBufferContentTracking(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsTextureHashTracking(UINT64 flags)
	{
		// Only maintain the resource-content hash cache on copy paths when a
		// consumer can actually read it. Generic shader overrides and PreSkin
		// probing do not consume this cache. IA resident tracking only consumes
		// buffer hashes through its separate gate. Including any of them here
		// queues a top-mip GPU readback for every streamed texture upload.
		return HasAnyFlag(
			flags,
			FLAG_HUNT_ENABLED |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_DRAW_SRV_SLOT_REPLACEMENT);
	}

	static bool NeedsBindingEventRecording(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE);
	}

	static void Update()
	{
		const SourceState source = SourceState::Capture();
		if (skipAll.load(std::memory_order_relaxed) && !source.HasAnyWork())
			return;

		// As in DX11, skipAll controls draw, state, and copy hooks.
		// PreSkin only needs dispatch hooks and compute binding tracking;
		// skipBindings and the dispatch-specific check handle that case.
		const LONG skipAll =
			(source.NeedsHeavyTracking() || source.NeedsRecordWork()) ? 0 : 1;
		const LONG skipBindings = source.NeedsBindingState() ? 0 : 1;
		const LONG skipGraphicsBindings =
			source.NeedsGraphicsBindingState() ? 0 : 1;
		const LONG trackResourceMetadata =
			source.NeedsHeavyTracking() ? 1 : 0;
		const UINT64 flags = source.BuildHookWorkFlags();

		// These atomics are independent hot-path gates. Hook threads can tolerate
		// observing the previous frame's gate briefly, so no acquire/release
		// ordering is needed and a full fence is avoided on every refresh.
		DX12HotPathState::skipAll.store(skipAll, std::memory_order_relaxed);
		DX12HotPathState::skipBindings.store(skipBindings, std::memory_order_relaxed);
		DX12HotPathState::skipGraphicsBindings.store(skipGraphicsBindings, std::memory_order_relaxed);
		DX12HotPathState::trackResourceMetadata.store(trackResourceMetadata, std::memory_order_relaxed);
		HookWorkFlags.store(flags, std::memory_order_relaxed);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		const LONG previousMask = stateMask.exchange(mask, std::memory_order_relaxed);
		const UINT64 previousFlags = loggedHookWorkFlags.exchange(flags, std::memory_order_relaxed);
		if (previousMask != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntime::BumpTrackingGeneration();
		}
		if (previousMask != mask || previousFlags != flags) {
			DX12LogManager::Fields("DX12HotPathState",
				"\"mode\":\"%s\",\"flags\":\"0x%llx\",\"skipAll\":%ld,"
				"\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,"
				"\"trackResourceMetadata\":%ld,\"hunt\":%s,\"frameAnalysis\":%s,"
				"\"shaderDump\":%s,\"shaderOverrides\":%s,\"preSkinProbe\":%s,"
				"\"preSkinBindingTrack\":%s,\"iaResidentState\":%s,"
				"\"shaderDescriptorTrack\":%s,\"drawSrvSlotReplacement\":%s",
				source.ModeName(),
				static_cast<unsigned long long>(flags),
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata,
				source.huntEnabled ? "true" : "false",
				source.frameAnalysisActive ? "true" : "false",
				source.shaderDumpActive ? "true" : "false",
				source.shaderOverrides ? "true" : "false",
				source.preSkinProbe ? "true" : "false",
				source.preSkinBindingTrack ? "true" : "false",
				source.iaResidentState ? "true" : "false",
				source.shaderDescriptorTrack ? "true" : "false",
				source.drawSrvSlotReplacement ? "true" : "false");
		}
	}

};
