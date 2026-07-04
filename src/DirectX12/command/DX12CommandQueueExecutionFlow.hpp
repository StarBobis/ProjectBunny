#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListHookSignatures.hpp"
#include "DX12CommandQueueExecutionState.hpp"
#include "DX12HotPathState.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12ShaderHunt.hpp"

class DX12CommandQueueExecutionFlow
{
public:
	class DX12ExecuteSkipLogUtils
	{
	public:
		inline static volatile LONG64 SkipCalls = 0;
		inline static volatile LONG64 SkipCommandLists = 0;
		inline static volatile LONG LastPresentLogged = -1;
		static constexpr LONG PresentInterval = 120;

		static void Record(ID3D12CommandQueue *queue, UINT numCommandLists, const char *stage)
		{
			InterlockedIncrement64(&SkipCalls);
			InterlockedAdd64(&SkipCommandLists, static_cast<LONG64>(numCommandLists));
			const LONG present = DX12RuntimeState::GetPresentCount();
			const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
			const bool shouldLog = last < 0 || present >= last + PresentInterval;
			if (!shouldLog)
				return;
			if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
				return;
			DX12LogManager::JsonFunc("DX12ExecuteCommandListsHeartbeat",
				"\"stage\":\"%s\",\"mode\":\"aggregate_skip\",\"present\":%ld,\"threadId\":%lu,\"queue\":\"%p\",\"calls\":%lld,\"commandLists\":%lld",
				stage ? stage : "",
				present,
				GetCurrentThreadId(),
				queue,
				static_cast<long long>(InterlockedCompareExchange64(&SkipCalls, 0, 0)),
				static_cast<long long>(InterlockedCompareExchange64(&SkipCommandLists, 0, 0)));
		}
	};

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
		volatile LONG &firstExecuteLog = DX12CommandQueueExecutionState::FirstExecuteLog;
		if (InterlockedCompareExchange(&firstExecuteLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"enter\",\"queue\":\"%p\",\"count\":%u,\"skipAll\":%ld,\"internalReplay\":%s",
				queue, numCommandLists, DX12HotPathState::skipAll,
				DX12InternalReplayState::IsActive() ? "true" : "false");

		// 每帧刷新 flags word：确保 draw/dispatch 热路径使用最新状态
		DX12HotPathState::RebuildHookWorkFlags();

		if (DX12HotPathState::skipAll) {
			if (original) {
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "BeforeOriginalSkip");
				if (InterlockedCompareExchange(&firstExecuteLog, 2, 1) == 1)
					DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"before_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
				original(queue, numCommandLists, commandLists);
				DX12ExecuteSkipLogUtils::Record(
					queue, numCommandLists, "AfterOriginalSkip");
				if (InterlockedCompareExchange(&firstExecuteLog, 3, 2) == 2)
					DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
						"\"stage\":\"after_original_skip\",\"queue\":\"%p\",\"count\":%u",
						queue, numCommandLists);
			}
			if (DX12HuntIsEnabled())
				RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
			DX12ExecuteSkipLogUtils::Record(queue, numCommandLists, "ReturnSkip");
			return;
		}

		if (DX12InternalReplayState::IsActive()) {
			if (original)
				original(queue, numCommandLists, commandLists);
			return;
		}

		DX12_PROFILE_SCOPE(ExecuteCommandLists);
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12CommandQueue::ExecuteCommandLists", queue, " count=%u", numCommandLists);
		if (!original)
			return;

		if (InterlockedCompareExchange(&firstExecuteLog, 4, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"before_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "beforeOriginal", queue, numCommandLists);
		ULONGLONG startTick = GetTickCount64();
#endif
		original(queue, numCommandLists, commandLists);
		if (InterlockedCompareExchange(&firstExecuteLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12ExecuteCommandListsFirst",
				"\"stage\":\"after_original_full\",\"queue\":\"%p\",\"count\":%u",
				queue, numCommandLists);
#if defined(_DEBUG)
		ULONGLONG originalEndTick = GetTickCount64();
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterOriginal", queue, numCommandLists,
			nullptr, 0, S_OK, originalEndTick - startTick);
#endif
		DX12ModNotifyCommandListsSubmitted(queue, numCommandLists, commandLists);
		RegisterSubmittedCommandLists(numCommandLists, commandLists, registerSubmittedCommandList);
#if defined(_DEBUG)
		DX12CommandListFlowDiagnostics::LogQueueStage(
			"ExecuteCommandLists", "afterNotify", queue, numCommandLists,
			nullptr, 0, S_OK, GetTickCount64() - originalEndTick);
#endif
	}
};
