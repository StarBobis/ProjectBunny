// Command queue execution state and flow tracking.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12CommandListHookState.h"

// Frame-boundary breadcrumb, defined in DX12CommandListExecutor.cpp. Thin
// declaration (same pattern as DX12VmFrameVariablesUtils.h): this header is
// pulled in before the mod-runtime umbrella declares the marker, so repeat
// the prototype here.
void DX12MarkStage(const char *stage);

class DX12CommandQueueExecutionState
{
public:
	inline static volatile LONG FirstExecuteLog = 0;
};


class DX12CommandQueueExecutionFlow
{
public:
	using RegisterSubmittedCommandListFn = void (*)(IUnknown*);

	static void RegisterSubmittedCommandLists(
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		if (!registerSubmittedCommandList || !commandLists)
			return;
		for (UINT i = 0; i < numCommandLists; ++i)
			registerSubmittedCommandList(commandLists[i]);
	}

	static void Execute(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original,
		RegisterSubmittedCommandListFn registerSubmittedCommandList)
	{
		DX12_PROFILE_FUNCTION(ExecuteCommandLists);
		DX12MarkStage("queue_execute_enter");
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		// Submission-gap probe: wall clock and this thread's CPU time since the
		// previous ExecuteCommandLists. A large wall gap with a small CPU delta
		// means the recording thread was blocked (fence wait / lock / game-side
		// stall) between submits; a large CPU delta means it was busy recording
		// or in unhooked game code. This locates frame time that the per-API
		// hot-path timers cannot see.
		{
			const ULONGLONG wallNow = GetTickCount64();
			FILETIME createTime = {};
			FILETIME exitTime = {};
			FILETIME kernelTime = {};
			FILETIME userTime = {};
			GetThreadTimes(
				GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime);
			const ULONGLONG cpuNow =
				((static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) |
					kernelTime.dwLowDateTime) / 10 +
				((static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) |
					userTime.dwLowDateTime) / 10;
			thread_local ULONGLONG lastWall = 0;
			thread_local ULONGLONG lastCpu = 0;
			thread_local LONG lastPresent = -1;
			const LONG present = DX12RuntimeState::GetPresentCount();
			if (lastWall != 0 && lastPresent >= 0) {
				const ULONGLONG wallDelta = wallNow - lastWall;
				const ULONGLONG cpuDelta = cpuNow - lastCpu;
				DX12LogManager::Fields("DX12ExecuteGapDiag",
					"\"present\":%ld,\"wallUs\":%llu,\"threadCpuUs\":%llu,"
					"\"framesSince\":%ld",
					present,
					wallDelta * 1000,
					cpuDelta,
					static_cast<LONG>(present - lastPresent));
			}
			lastWall = wallNow;
			lastCpu = cpuNow;
			lastPresent = present;
		}
		volatile LONG &firstExecuteLog = DX12CommandQueueExecutionState::FirstExecuteLog;
		if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
				queue, numCommandLists, DX12HotPathState::skipAll.load(),
				DX12InternalReplayState::IsActive() ? "true" : "false");
	#endif

		// Present publishes feature and capture changes, matching DX11's
		// frame-action timing. Submission only consumes that published state.
		//
		// The notify/register callbacks below run AFTER the original
		// ExecuteCommandLists call, i.e. the command lists are already
		// submitted to the GPU. They must be pure CPU bookkeeping - no
		// D3D12/COM recording, no vtable patching, no locks held across
		// waits - or the driver/D3D12Core can hang (white-screen startup
		// hang observed in ZenlessZoneZero with a post-submit
		// ResourceBarrier replay).
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		const bool shouldNotifySubmitted =
			DX12ModNeedsCommandListSubmissionNotifications();
		const bool shouldRegisterSubmitted =
			DX12HuntIsEnabled() ||
			DX12CaptureLedger::CurrentCaptureSessionId() != 0;
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"read_work_flags",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		// Order any pending mod uploads (mod textures loaded since the
		// previous submit) before this submit's lists: the game queue waits
		// on the upload fence GPU-side, so SRVs referencing freshly uploaded
		// textures never observe uninitialized contents and no CPU thread
		// blocks. A no-op once the upload fence has completed.
		DX12UploadChannelWaitOnQueue(queue);

		if (DX12HotPathState::skipAll) {
			if (original) {
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
					DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
	#endif
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
					DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
	#endif
			}
			// Async content capture: submit readback events queued during
			// recording, ordered after the game's lists via Signal/Wait on
			// the mod-owned copy queue.
			if (DX12AsyncReadbackHasPendingWork())
				DX12AsyncReadbackSubmitPending(queue);
			if (shouldNotifySubmitted) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"mod_notify_submitted_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			if (shouldRegisterSubmitted) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"register_submitted_skip",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_skip",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			DX12MarkStage("queue_execute_leave");
			return;
		}

		if (DX12InternalReplayState::IsActive()) {
			if (original) {
				sliceBegin = DX12OuterTimingProbeUtils::Now();
				original(queue, numCommandLists, commandLists);
				DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
					"original_internal_replay",
					queue,
					numCommandLists,
					DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			}
			// Keep the async readback pipeline moving during capture replay:
			// replay submits never carry the game's normal ExecuteCommandLists.
			if (DX12AsyncReadbackHasPendingWork())
				DX12AsyncReadbackSubmitPending(queue);
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_internal_replay",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			DX12MarkStage("queue_execute_leave");
			return;
		}

				if (!original) {
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"total_no_original",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
			DX12MarkStage("queue_execute_leave");
			return;
		}

	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
	#endif
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				ULONGLONG startTick = GetTickCount64();
#endif
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		original(queue, numCommandLists, commandLists);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"original",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogManager::FieldsFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
	#endif
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		ULONGLONG originalEndTick = GetTickCount64();
#endif
		// Async content capture: submit readback events queued during
		// recording, ordered after the game's lists via Signal/Wait on the
		// mod-owned copy queue (pure queue operations, no recording).
		if (DX12AsyncReadbackHasPendingWork())
			DX12AsyncReadbackSubmitPending(queue);
		if (shouldNotifySubmitted) {
			DX12_HOTPATH_TIME_BEGIN(ExecuteCLNotify);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"mod_notify_submitted",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12_HOTPATH_TIME_END(ExecuteCLNotify);
		}
		if (shouldRegisterSubmitted) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
			DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
				"register_submitted",
				queue,
				numCommandLists,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		}
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
		DX12MarkStage("queue_execute_leave");
	}
};


class DX12CommandQueueState
{
public:
	SRWLOCK lock;
	ID3D12CommandQueue *queue = nullptr;
	volatile LONG nonDirectLogs = 0;

	DX12CommandQueueState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12CommandQueueState &Get()
	{
		static DX12CommandQueueState state;
		return state;
	}

	static void Set(ID3D12CommandQueue *queue)
	{
		DX12CommandQueueState &state = Get();
		if (queue) {
			D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
				if (InterlockedIncrement(&state.nonDirectLogs) <= 8)
					DX12LogManager::Fields("DX12CommandQueueState",
						"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
						queue, static_cast<int>(desc.Type));
				return;
			}
			DX12LogManager::Fields("DX12CommandQueueState",
				"\"event\":\"accept_direct\",\"queue\":\"%p\",\"type\":%d",
				queue, static_cast<int>(desc.Type));
		}

		if (queue)
			queue->AddRef();

		AcquireSRWLockExclusive(&state.lock);
		ID3D12CommandQueue *oldQueue = state.queue;
		state.queue = queue;
		ReleaseSRWLockExclusive(&state.lock);

		if (oldQueue)
			oldQueue->Release();
	}

	static ID3D12CommandQueue *Acquire()
	{
		DX12CommandQueueState &state = Get();
		AcquireSRWLockShared(&state.lock);
		ID3D12CommandQueue *queue = state.queue;
		if (queue)
			queue->AddRef();
		ReleaseSRWLockShared(&state.lock);
		return queue;
	}
};


