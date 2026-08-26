// DX12WaitHooks: WaitForSingleObject/MultipleObjects* hooks for fence/frame-latency probes.
// Split from DX12Wrapper.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <d3d12sdklayers.h>
#include <intrin.h>

class DX12WaitHookState
{
public:
	using WaitForSingleObjectFn = DWORD(WINAPI *)(HANDLE, DWORD);
	using WaitForMultipleObjectsFn = DWORD(WINAPI *)(DWORD, const HANDLE *, BOOL, DWORD);
	using WaitForSingleObjectExFn = DWORD(WINAPI *)(HANDLE, DWORD, BOOL);
	using WaitForMultipleObjectsExFn = DWORD(WINAPI *)(DWORD, const HANDLE *, BOOL, DWORD, BOOL);
	using SignalObjectAndWaitFn = DWORD(WINAPI *)(HANDLE, HANDLE, DWORD, BOOL);

	inline static WaitForSingleObjectFn OrigWaitForSingleObject = nullptr;
	inline static WaitForMultipleObjectsFn OrigWaitForMultipleObjects = nullptr;
	inline static WaitForSingleObjectExFn OrigWaitForSingleObjectEx = nullptr;
	inline static WaitForMultipleObjectsExFn OrigWaitForMultipleObjectsEx = nullptr;
	inline static SignalObjectAndWaitFn OrigSignalObjectAndWait = nullptr;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static bool HookAttempted = false;
};

class DX12WaitHookCallbacks
{
public:
	// A wait that can never last >= 50ms (the logging threshold) and is not on
	// a tracked handle needs no measurement, no snapshot registration and no
	// log line. Every wait call in the process used to pay two QPCs plus ~13
	// locked RMW ops on shared cache lines for this bookkeeping; short waits
	// (0ms polls, semaphore drains) are the most common wait in a game.
	static __forceinline bool CanSkipMeasurement(DWORD waitMs, bool probeHandle)
	{
		return !probeHandle && waitMs < 50000;
	}

	static bool ShouldProbeHandle(HANDLE handle, bool *matchesFenceEvent, bool *matchesFrameLatency)
	{
		return DX12OuterTimingProbeUtils::IsTrackedWaitHandle(
			handle, matchesFenceEvent, matchesFrameLatency);
	}

	static bool ShouldProbeHandles(
		DWORD count,
		const HANDLE *handles,
		HANDLE *matchedHandle,
		bool *matchesFenceEvent,
		bool *matchesFrameLatency)
	{
		if (matchedHandle)
			*matchedHandle = nullptr;
		if (matchesFenceEvent)
			*matchesFenceEvent = false;
		if (matchesFrameLatency)
			*matchesFrameLatency = false;
		if (!handles)
			return false;

		for (DWORD i = 0; i < count; ++i) {
			bool fenceMatch = false;
			bool frameLatencyMatch = false;
			if (!ShouldProbeHandle(handles[i], &fenceMatch, &frameLatencyMatch))
				continue;
			if (matchedHandle)
				*matchedHandle = handles[i];
			if (matchesFenceEvent)
				*matchesFenceEvent = fenceMatch;
			if (matchesFrameLatency)
				*matchesFrameLatency = frameLatencyMatch;
			return true;
		}
		return false;
	}

	static void LogMatchedWaitStack(
		const char *api,
		HANDLE handle,
		DWORD waitResult,
		LONG64 us)
	{
		ID3D12Fence *fence = nullptr;
		UINT64 fenceValue = 0;
		HANDLE fenceEvent = nullptr;
		DX12OuterTimingProbeUtils::SnapshotLastFenceEvent(
			&fence, &fenceValue, &fenceEvent);
		DX12OuterTimingProbeUtils::LogCurrentCallStack(
			"matched_waitable_object",
			api,
			handle ? handle : fenceEvent,
			fence,
			fenceValue,
			us,
			waitResult,
			GetCurrentThreadId(),
			1);
	}

	static DWORD WINAPI HookedWaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
	{
		if (!DX12WaitHookState::OrigWaitForSingleObject)
			return WAIT_FAILED;

		thread_local LONG reentryDepth = 0;
		if (InterlockedIncrement(&reentryDepth) != 1) {
			const DWORD result =
				DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		// Internal handles (e.g. the async log writer's own 100ms poll event)
		// are the wrapper's own sleep, not the game's: pass through without
		// measurement or logging so the hook never feeds its own log.
		if (DX12LogManager::IsInternalEventHandle(hHandle)) {
			const DWORD result =
				DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		bool matchesFenceEvent = false;
		bool matchesFrameLatency = false;
		const bool probeHandle = ShouldProbeHandle(
			hHandle, &matchesFenceEvent, &matchesFrameLatency);
		LONG64 us = 0;
		DWORD result = WAIT_FAILED;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
		} else {
			const LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
			result = DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
			us = DX12OuterTimingProbeUtils::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12OuterTimingProbeUtils::RegisterSystemWait(
				1,
				hHandle,
				nullptr,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			DX12OuterTimingProbeUtils::LogSystemWaitSlice(
				"WaitForSingleObject",
				hHandle,
				nullptr,
				dwMilliseconds,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack("WaitForSingleObject", hHandle, result, us);
		}
		InterlockedDecrement(&reentryDepth);
		return result;
	}

	static DWORD WINAPI HookedWaitForMultipleObjects(
		DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
	{
		if (!DX12WaitHookState::OrigWaitForMultipleObjects)
			return WAIT_FAILED;

		thread_local LONG reentryDepth = 0;
		if (InterlockedIncrement(&reentryDepth) != 1) {
			const DWORD result = DX12WaitHookState::OrigWaitForMultipleObjects(
				nCount, lpHandles, bWaitAll, dwMilliseconds);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		HANDLE matchedHandle = nullptr;
		bool matchesFenceEvent = false;
		bool matchesFrameLatency = false;
		const bool probeHandle = ShouldProbeHandles(
			nCount,
			lpHandles,
			&matchedHandle,
			&matchesFenceEvent,
			&matchesFrameLatency);
		LONG64 us = 0;
		DWORD result = WAIT_FAILED;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForMultipleObjects(
				nCount, lpHandles, bWaitAll, dwMilliseconds);
		} else {
			const LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
			result = DX12WaitHookState::OrigWaitForMultipleObjects(
				nCount, lpHandles, bWaitAll, dwMilliseconds);
			us = DX12OuterTimingProbeUtils::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12OuterTimingProbeUtils::RegisterSystemWait(
				2,
				matchedHandle,
				nullptr,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			DX12OuterTimingProbeUtils::LogSystemWaitSlice(
				bWaitAll ? "WaitForMultipleObjects(all)" : "WaitForMultipleObjects(any)",
				matchedHandle,
				nullptr,
				dwMilliseconds,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack(
					bWaitAll ? "WaitForMultipleObjects(all)" : "WaitForMultipleObjects(any)",
					matchedHandle,
					result,
					us);
		}
		InterlockedDecrement(&reentryDepth);
		return result;
	}

	static DWORD WINAPI HookedWaitForSingleObjectEx(
		HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable)
	{
		if (!DX12WaitHookState::OrigWaitForSingleObjectEx)
			return WAIT_FAILED;

		thread_local LONG reentryDepth = 0;
		if (InterlockedIncrement(&reentryDepth) != 1) {
			const DWORD result = DX12WaitHookState::OrigWaitForSingleObjectEx(
				hHandle, dwMilliseconds, bAlertable);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		if (DX12LogManager::IsInternalEventHandle(hHandle)) {
			const DWORD result = DX12WaitHookState::OrigWaitForSingleObjectEx(
				hHandle, dwMilliseconds, bAlertable);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		bool matchesFenceEvent = false;
		bool matchesFrameLatency = false;
		const bool probeHandle = ShouldProbeHandle(
			hHandle, &matchesFenceEvent, &matchesFrameLatency);
		LONG64 us = 0;
		DWORD result = WAIT_FAILED;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForSingleObjectEx(
				hHandle, dwMilliseconds, bAlertable);
		} else {
			const LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
			result = DX12WaitHookState::OrigWaitForSingleObjectEx(
				hHandle, dwMilliseconds, bAlertable);
			us = DX12OuterTimingProbeUtils::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12OuterTimingProbeUtils::RegisterSystemWait(
				4, hHandle, nullptr, result, lastError, us,
				matchesFenceEvent, matchesFrameLatency);
			DX12OuterTimingProbeUtils::LogSystemWaitSlice(
				"WaitForSingleObjectEx", hHandle, nullptr, dwMilliseconds,
				result, lastError, us, matchesFenceEvent, matchesFrameLatency);
			// Stack capture is expensive (walk + per-frame module lookup +
			// one log line per frame); only pay it for waits on handles we
			// actually track, mirroring the non-Ex hook. The previous
			// condition captured a 24-frame stack for every >=50ms wait,
			// which made every 100ms polling wait in the process emit a
			// multi-line stack burst through the global log lock.
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack("WaitForSingleObjectEx", hHandle, result, us);
		}
		InterlockedDecrement(&reentryDepth);
		return result;
	}

	static DWORD WINAPI HookedWaitForMultipleObjectsEx(
		DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll,
		DWORD dwMilliseconds, BOOL bAlertable)
	{
		if (!DX12WaitHookState::OrigWaitForMultipleObjectsEx)
			return WAIT_FAILED;

		thread_local LONG reentryDepth = 0;
		if (InterlockedIncrement(&reentryDepth) != 1) {
			const DWORD result = DX12WaitHookState::OrigWaitForMultipleObjectsEx(
				nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		HANDLE matchedHandle = nullptr;
		bool matchesFenceEvent = false;
		bool matchesFrameLatency = false;
		const bool probeHandle = ShouldProbeHandles(
			nCount, lpHandles, &matchedHandle, &matchesFenceEvent, &matchesFrameLatency);
		LONG64 us = 0;
		DWORD result = WAIT_FAILED;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForMultipleObjectsEx(
				nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);
		} else {
			const LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
			result = DX12WaitHookState::OrigWaitForMultipleObjectsEx(
				nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);
			us = DX12OuterTimingProbeUtils::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12OuterTimingProbeUtils::RegisterSystemWait(
				5, matchedHandle, nullptr, result, lastError, us,
				matchesFenceEvent, matchesFrameLatency);
			const char *api = bWaitAll ?
				"WaitForMultipleObjectsEx(all)" : "WaitForMultipleObjectsEx(any)";
			DX12OuterTimingProbeUtils::LogSystemWaitSlice(
				api, matchedHandle, nullptr, dwMilliseconds,
				result, lastError, us, matchesFenceEvent, matchesFrameLatency);
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack(api, matchedHandle, result, us);
		}
		InterlockedDecrement(&reentryDepth);
		return result;
	}

	static DWORD WINAPI HookedSignalObjectAndWait(
		HANDLE hObjectToSignal, HANDLE hObjectToWaitOn, DWORD dwMilliseconds, BOOL bAlertable)
	{
		if (!DX12WaitHookState::OrigSignalObjectAndWait)
			return WAIT_FAILED;

		thread_local LONG reentryDepth = 0;
		if (InterlockedIncrement(&reentryDepth) != 1) {
			const DWORD result = DX12WaitHookState::OrigSignalObjectAndWait(
				hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		if (DX12LogManager::IsInternalEventHandle(hObjectToWaitOn)) {
			const DWORD result = DX12WaitHookState::OrigSignalObjectAndWait(
				hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);
			InterlockedDecrement(&reentryDepth);
			return result;
		}

		bool matchesFenceEvent = false;
		bool matchesFrameLatency = false;
		const bool probeHandle = ShouldProbeHandle(
			hObjectToWaitOn, &matchesFenceEvent, &matchesFrameLatency);
		LONG64 us = 0;
		DWORD result = WAIT_FAILED;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigSignalObjectAndWait(
				hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);
		} else {
			const LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
			result = DX12WaitHookState::OrigSignalObjectAndWait(
				hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);
			us = DX12OuterTimingProbeUtils::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12OuterTimingProbeUtils::RegisterSystemWait(
				3,
				hObjectToWaitOn,
				hObjectToSignal,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			DX12OuterTimingProbeUtils::LogSystemWaitSlice(
				"SignalObjectAndWait",
				hObjectToWaitOn,
				hObjectToSignal,
				dwMilliseconds,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack("SignalObjectAndWait", hObjectToWaitOn, result, us);
		}
		InterlockedDecrement(&reentryDepth);
		return result;
	}
};

void DX12WaitHooks::Install()
{
	AcquireSRWLockExclusive(&DX12WaitHookState::HookLock);
	if (DX12WaitHookState::HookAttempted) {
		ReleaseSRWLockExclusive(&DX12WaitHookState::HookLock);
		return;
	}
	DX12WaitHookState::HookAttempted = true;
	ReleaseSRWLockExclusive(&DX12WaitHookState::HookLock);

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
		kernel32 = LoadLibraryW(L"kernel32.dll");
	if (!kernel32) {
		AcquireSRWLockExclusive(&DX12WaitHookState::HookLock);
		DX12WaitHookState::HookAttempted = false;
		ReleaseSRWLockExclusive(&DX12WaitHookState::HookLock);
		return;
	}

	DX12HookManager::InstallExportHook(
		kernel32,
		"WaitForSingleObject",
		reinterpret_cast<void**>(&DX12WaitHookState::OrigWaitForSingleObject),
		reinterpret_cast<void*>(DX12WaitHookCallbacks::HookedWaitForSingleObject));
	DX12HookManager::InstallExportHook(
		kernel32,
		"WaitForMultipleObjects",
		reinterpret_cast<void**>(&DX12WaitHookState::OrigWaitForMultipleObjects),
		reinterpret_cast<void*>(DX12WaitHookCallbacks::HookedWaitForMultipleObjects));
	DX12HookManager::InstallExportHook(
		kernel32,
		"WaitForSingleObjectEx",
		reinterpret_cast<void**>(&DX12WaitHookState::OrigWaitForSingleObjectEx),
		reinterpret_cast<void*>(DX12WaitHookCallbacks::HookedWaitForSingleObjectEx));
	DX12HookManager::InstallExportHook(
		kernel32,
		"WaitForMultipleObjectsEx",
		reinterpret_cast<void**>(&DX12WaitHookState::OrigWaitForMultipleObjectsEx),
		reinterpret_cast<void*>(DX12WaitHookCallbacks::HookedWaitForMultipleObjectsEx));
	DX12HookManager::InstallExportHook(
		kernel32,
		"SignalObjectAndWait",
		reinterpret_cast<void**>(&DX12WaitHookState::OrigSignalObjectAndWait),
		reinterpret_cast<void*>(DX12WaitHookCallbacks::HookedSignalObjectAndWait));
}

void DX12WaitHooks::Shutdown()
{
}
