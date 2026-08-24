#pragma once

#include "DX12CommandState.h"

// Hot-path feature gates, profiling state, and hang watchdog.

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
		if (skipAll && !source.HasAnyWork())
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

		DX12HotPathState::skipAll.store(skipAll);
		DX12HotPathState::skipBindings.store(skipBindings);
		DX12HotPathState::skipGraphicsBindings.store(skipGraphicsBindings);
		DX12HotPathState::trackResourceMetadata.store(trackResourceMetadata);
		HookWorkFlags.store(flags);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		const LONG previousMask = stateMask.exchange(mask);
		const UINT64 previousFlags = loggedHookWorkFlags.exchange(flags);
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


#include <dbghelp.h>


// In-process hang watchdog with self-minidump support.
//
// The game is protected by anti-cheat, so external debuggers / Task Manager
// dumps cannot capture the frozen process. Writing the dump from inside the
// wrapper (MiniDumpWriteDump on self) needs no debug privilege and is the same
// mechanism in-process crash handlers use, so anti-cheat cannot block it.
//
// Triggers:
//  - No Present completion for more than ZZMIDX12_HANG_DUMP_MS (default 8000ms)
//    after the first present. This catches the startup/character-load hang
//    where the render thread stops presenting.
//  - No present at all for 30s after watchdog start (freeze before first frame).
//
// The dump is written next to d3d12.dll as hang_YYYYMMDD_HHMMSS.dmp with module
// data segments (covers the wrapper's SRW lock globals), thread info and stacks,
// so WinDbg can resolve which lock/wait the render thread is blocked on.
class DX12HangWatchdog
{
public:
	// Called right after the original Present/Present1 returns (both the
	// normal and the skipAll fast paths). Cheap: one GetTickCount64 + two
	// atomic stores.
	static void NotifyPresentCompleted()
	{
		const ULONGLONG now = GetTickCount64();
		InterlockedExchange64(
			&sLastPresentCompletionTick, static_cast<LONGLONG>(now));
		InterlockedExchange(&sHasSeenPresent, 1);
	}

	static void Start()
	{
		if (InterlockedCompareExchange(&sStarted, 1, 0) != 0)
			return;
		sInitTick = GetTickCount64();
		InterlockedExchange(&sStopRequested, 0);
		sThresholdMs = DefaultThresholdMs();
		sThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
	}

	static void Stop()
	{
		InterlockedExchange(&sStopRequested, 1);
		if (sThread) {
			WaitForSingleObject(sThread, 5000);
			CloseHandle(sThread);
			sThread = nullptr;
		}
		InterlockedExchange(&sStarted, 0);
	}

private:
	static DWORD DefaultThresholdMs()
	{
		DWORD value = 8000;
		char env[32] = {};
		const DWORD len = GetEnvironmentVariableA(
			"ZZMIDX12_HANG_DUMP_MS", env, sizeof(env));
		if (len > 0 && len < sizeof(env)) {
			const int parsed = atoi(env);
			if (parsed > 0)
				value = static_cast<DWORD>(parsed);
		}
		return value;
	}

	static void WriteMinidump(const char *reason)
	{
		wchar_t directory[MAX_PATH] = {};
		const HMODULE module = DX12RuntimeState::GetModule();
		if (!module || !GetModuleFileNameW(module, directory, MAX_PATH))
			return;
		PathRemoveFileSpecW(directory);

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t path[MAX_PATH] = {};
		swprintf_s(path,
			L"%s\\hang_%04u%02u%02u_%02u%02u%02u.dmp",
			directory,
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond);

		HANDLE file = CreateFileW(
			path, GENERIC_WRITE, FILE_SHARE_READ,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"open_failed\",\"reason\":\"%s\",\"path\":\"%S\",\"error\":%lu",
				reason, path, GetLastError());
			return;
		}

		// DataSegs keeps every loaded module's data segment so the SRW lock
		// globals (gModLock/gResourceLock/...) are present in the dump;
		// thread stacks and thread info capture where each thread is blocked.
		const MINIDUMP_TYPE flags = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithDataSegs |
			MiniDumpWithThreadInfo |
			MiniDumpWithProcessThreadData |
			MiniDumpWithHandleData |
			MiniDumpWithUnloadedModules);
		const BOOL ok = MiniDumpWriteDump(
			GetCurrentProcess(), GetCurrentProcessId(), file,
			flags, nullptr, nullptr, nullptr);
		const DWORD error = GetLastError();
		CloseHandle(file);

		if (ok) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"written\",\"reason\":\"%s\",\"path\":\"%S\"",
				reason, path);
		} else {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"write_failed\",\"reason\":\"%s\",\"error\":%lu",
				reason, error);
		}
	}

	static void LogHangDetected(ULONGLONG elapsedMs)
	{
		DX12LogManager::FieldsFlush("DX12HangWatchdog",
			"\"stage\":\"detected\",\"elapsedMs\":%llu,\"presentSeen\":%ld",
			static_cast<unsigned long long>(elapsedMs), sHasSeenPresent);
	}

	static DWORD WINAPI ThreadProc(void *)
	{
		for (; InterlockedCompareExchange(&sStopRequested, 0, 0) == 0;) {
			Sleep(1000);
			const ULONGLONG now = GetTickCount64();
			if (!sHasSeenPresent) {
				// Freeze before the first present (early startup hang).
				if (now - sInitTick > 30000 &&
				    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
					LogHangDetected(now - sInitTick);
					WriteMinidump("no_present");
				}
				continue;
			}
			const LONGLONG last = InterlockedCompareExchange64(
				&sLastPresentCompletionTick, 0, 0);
			if (last == 0)
				continue;
			const ULONGLONG elapsed = now - static_cast<ULONGLONG>(last);
			if (elapsed > sThresholdMs &&
			    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
				LogHangDetected(elapsed);
				WriteMinidump("present_stall");
			}
		}
		return 0;
	}

	inline static volatile LONG sStarted = 0;
	inline static volatile LONG sStopRequested = 0;
	inline static volatile LONG sHasSeenPresent = 0;
	inline static volatile LONG sDumpAttempted = 0;
	inline static volatile LONGLONG sLastPresentCompletionTick = 0;
	inline static ULONGLONG sInitTick = 0;
	inline static DWORD sThresholdMs = 8000;
	inline static HANDLE sThread = nullptr;
};


class DX12Profiling
{
public:
	enum class Mode {
		NONE = 0,
		SUMMARY,
	};

	class Counter
	{
	public:
		volatile LONG calls = 0;
		volatile LONG64 cpuTicks = 0;

		void Reset();
		void AccumulateFrom(const Counter &src);
		void IncrementCalls();
	};

	struct State
	{
		Counter *counter = nullptr;
		LARGE_INTEGER startTime = {};
		bool counted = false;
	};

	Counter DrawInstanced;
	Counter DrawIndexedInstanced;
	Counter Dispatch;
	Counter SetPipelineState;
	Counter SetDescriptorHeaps;
	Counter SetComputeRootSignature;
	Counter SetGraphicsRootSignature;
	Counter SetComputeRootDescriptorTable;
	Counter SetGraphicsRootDescriptorTable;
	Counter SetComputeRootConstantBufferView;
	Counter SetGraphicsRootConstantBufferView;
	Counter SetComputeRootShaderResourceView;
	Counter SetGraphicsRootShaderResourceView;
	Counter SetComputeRootUnorderedAccessView;
	Counter SetGraphicsRootUnorderedAccessView;
	Counter IASetIndexBuffer;
	Counter IASetVertexBuffers;
	Counter IASetPrimitiveTopology;
	Counter ResourceBarrier;
	Counter ExecuteCommandLists;
	Counter Present;
	Counter ResetCommandList;
	Counter CloseCommandList;
	Counter IaDrawOverrideChecks;
	Counter IaHashStateHits;
	Counter IaHashStateMisses;
	Counter IaTextureMayHits;
	Counter IaTextureMayMisses;
	Counter IaPrepareCalls;
	Counter IaApplied;
	Counter IaCandidateTests;
	Counter IaCandidateHits;
	Counter IaHuntIaUpdates;
	Counter IaReplacementDraws;
	Counter IaReplacementIndexedDraws;
	Counter IaReplacementDispatches;
	Counter PreSkinCsTests;
	Counter PreSkinUavTests;
	Counter PreSkinUavHits;
	Counter PreSkinApplied;
	Counter PreSkinDispatchResized;
	Counter AllDraws;
	Counter AllDispatches;
	Counter AllBindings;
	Counter AllCommands;
	Counter FastForwardHits;
	volatile LONG64 BindingQueueTaskUs = 0;
	volatile LONG64 BindingQueueDrainUs = 0;
	volatile LONG64 BindingQueueMaxTaskUs = 0;
	volatile LONG64 BindingQueueMaxDrainUs = 0;
	volatile LONG BindingQueueTasks = 0;
	volatile LONG BindingQueueDrains = 0;
	volatile LONG BindingQueueMaxPending = 0;

	Mode mode = Mode::SUMMARY;
	volatile LONG collectCounters = 0;
	volatile LONG collectTiming = 0;
	volatile LONG framePresentCount = 0;
	LARGE_INTEGER frameStartTime = {};
	LARGE_INTEGER perfFrequency = {};
	wchar_t overlayText[2048] = L"";

	static DX12Profiling &Get();
	static void Init();
	static void BeginFrame();
	static void EndFrame();
	static void Toggle();
	static bool IsActive();
	static bool ShouldCollectCounters();
	static void Start(State *state, Counter *counter);
	static void End(State *state);
	static void RecordIaDrawOverrideCheck();
	static void RecordIaHashStateResult(bool hit);
	static void RecordIaApplied();
	static void RecordIaHuntIaUpdate();
	static void RecordIaReplacementDraw(bool indexed);
	static void RecordIaReplacementDispatch();
	static void RecordPreSkinCsTest(bool hit);
	static void RecordPreSkinUavTest(bool hit);
	static void RecordPreSkinApplied();
	static void FlushFrame();
	static void RecordFastForwardHit();
	static void RecordBindingQueueTaskUs(LONG64 elapsedUs);
	static void RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending);

	void InitState();
	void BeginFrameState();
	void EndFrameState();
	void ToggleState();
	bool IsActiveState() const;
	void ResetFrame();
	void LogFrameStatsState();
	void FlushFrameState();
	double TicksToUs(LONG64 ticks) const;
	const wchar_t *FormatUs(double us, wchar_t *buf, size_t bufSize) const;
};

class DX12Profiler;