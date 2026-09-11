// Command queue execution state and flow tracking.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12CommandListHookState.h"

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
		DX12_PROF_SCOPE("ExecuteCommandLists");
		DX12MarkStage("queue_execute_enter");
		DX12_DIAG_QPC(totalBegin);
		// Submission-gap probe: wall clock and this thread's CPU time since the
		// previous ExecuteCommandLists. A large wall gap with a small CPU delta
		// means the recording thread was blocked (fence wait / lock / game-side
		// stall) between submits; a large CPU delta means it was busy recording
		// or in unhooked game code. This locates frame time that the per-API
		// hot-path timers cannot see. TIMING builds only.
		DX12_LOG_EXECUTE_GAP();
		volatile LONG &firstExecuteLog = DX12CommandQueueExecutionState::FirstExecuteLog;
		if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
			DX12_LOG_FLUSH("DX12ExecuteCommandListsFirst",
				"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
				queue, numCommandLists, DX12HotPathState::skipAll.load(),
				DX12InternalReplayState::IsActive() ? "true" : "false");

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
		DX12_DIAG_QPC(sliceBegin);
		const bool shouldNotifySubmitted =
			DX12ModNeedsCommandListSubmissionNotifications();
		const bool shouldRegisterSubmitted =
			DX12HuntIsEnabled() ||
			DX12CaptureLedger::CurrentCaptureSessionId() != 0;
		DX12_PROF_ADD_US_ITEMS(
			"Queue.read_work_flags",
			DX12_DIAG_ELAPSED_US(sliceBegin),
			numCommandLists);

		// Order any pending mod uploads (mod textures loaded since the
		// previous submit) before this submit's lists: the game queue waits
		// on the upload fence GPU-side, so SRVs referencing freshly uploaded
		// textures never observe uninitialized contents and no CPU thread
		// blocks. A no-op once the upload fence has completed.
		DX12UploadChannelWaitOnQueue(queue);

		if (DX12HotPathState::skipAll) {
			if (original) {
				if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
					DX12_LOG_FLUSH("DX12ExecuteCommandListsFirst",
						"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
				DX12_DIAG_QPC_RESET(sliceBegin);
				original(queue, numCommandLists, commandLists);
				DX12_PROF_ADD_US_ITEMS(
					"Queue.original_skip",
					DX12_DIAG_ELAPSED_US(sliceBegin),
					numCommandLists);
				if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
					DX12_LOG_FLUSH("DX12ExecuteCommandListsFirst",
						"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
			}
			// Async content capture: submit readback events queued during
			// recording, ordered after the game's lists via Signal/Wait on
			// the mod-owned copy queue.
			if (DX12AsyncReadbackHasPendingWork())
				DX12AsyncReadbackSubmitPending(queue);
			if (shouldNotifySubmitted) {
				DX12_DIAG_QPC_RESET(sliceBegin);
				DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
				DX12_PROF_ADD_US_ITEMS(
					"Queue.mod_notify_submitted_skip",
					DX12_DIAG_ELAPSED_US(sliceBegin),
					numCommandLists);
			}
			if (shouldRegisterSubmitted) {
				DX12_DIAG_QPC_RESET(sliceBegin);
				RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
				DX12_PROF_ADD_US_ITEMS(
					"Queue.register_submitted_skip",
					DX12_DIAG_ELAPSED_US(sliceBegin),
					numCommandLists);
			}
			DX12_PROF_ADD_US_ITEMS(
				"Queue.total_skip",
				DX12_DIAG_ELAPSED_US(totalBegin),
				numCommandLists);
			DX12MarkStage("queue_execute_leave");
			return;
		}

		if (DX12InternalReplayState::IsActive()) {
			if (original) {
				DX12_DIAG_QPC_RESET(sliceBegin);
				original(queue, numCommandLists, commandLists);
				DX12_PROF_ADD_US_ITEMS(
					"Queue.original_internal_replay",
					DX12_DIAG_ELAPSED_US(sliceBegin),
					numCommandLists);
			}
			// Keep the async readback pipeline moving during capture replay:
			// replay submits never carry the game's normal ExecuteCommandLists.
			if (DX12AsyncReadbackHasPendingWork())
				DX12AsyncReadbackSubmitPending(queue);
			DX12_PROF_ADD_US_ITEMS(
				"Queue.total_internal_replay",
				DX12_DIAG_ELAPSED_US(totalBegin),
				numCommandLists);
			DX12MarkStage("queue_execute_leave");
			return;
		}

				if (!original) {
			DX12_PROF_ADD_US_ITEMS(
				"Queue.total_no_original",
				DX12_DIAG_ELAPSED_US(totalBegin),
				numCommandLists);
			DX12MarkStage("queue_execute_leave");
			return;
		}

		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12_LOG_FLUSH("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
		DX12_DIAG_QPC_RESET(sliceBegin);
		original(queue, numCommandLists, commandLists);
		DX12_PROF_ADD_US_ITEMS(
			"Queue.original",
			DX12_DIAG_ELAPSED_US(sliceBegin),
			numCommandLists);
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12_LOG_FLUSH("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
		// Async content capture: submit readback events queued during
		// recording, ordered after the game's lists via Signal/Wait on the
		// mod-owned copy queue (pure queue operations, no recording).
		if (DX12AsyncReadbackHasPendingWork())
			DX12AsyncReadbackSubmitPending(queue);
		if (shouldNotifySubmitted) {
			DX12_PROF_STAGE_BEGIN(ExecuteCLNotify);
			DX12_DIAG_QPC_RESET(sliceBegin);
			DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
			DX12_PROF_ADD_US_ITEMS(
				"Queue.mod_notify_submitted",
				DX12_DIAG_ELAPSED_US(sliceBegin),
				numCommandLists);
			DX12_PROF_STAGE_END(ExecuteCLNotify);
		}
		if (shouldRegisterSubmitted) {
			DX12_DIAG_QPC_RESET(sliceBegin);
			RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
			DX12_PROF_ADD_US_ITEMS(
				"Queue.register_submitted",
				DX12_DIAG_ELAPSED_US(sliceBegin),
				numCommandLists);
		}
		DX12_PROF_ADD_US_ITEMS(
			"Queue.total",
			DX12_DIAG_ELAPSED_US(totalBegin),
			numCommandLists);
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
					DX12_LOG("DX12CommandQueueState",
						"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
						queue, static_cast<int>(desc.Type));
				return;
			}
			DX12_LOG("DX12CommandQueueState",
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


