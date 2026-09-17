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

// Wait hooks exist only for TIMING-build fence/frame-latency probes. In a
// RELEASE build no hook is installed and no wait in the process pays anything.
#if DX12_TIMING_BUILD
namespace
{
	// Long-wait witness. The freeze watchdog proves a stall happened but not
	// what the thread blocked on: once the stack snapshot is taken the thread's
	// registers are gone (the crash is raised from another thread), so a
	// module-only dump cannot name the waited-on object. This thread closes
	// that hole - it publishes the handles of any wait that has been running
	// longer than a second, which is exactly the moment a hang becomes visible,
	// while the waiting thread is still parked in the kernel with its arguments
	// intact on the stack.
	constexpr DWORD kLongWaitThresholdMs = 1200;
	constexpr unsigned int kMaxWaitSlots = 32;

	struct LongWaitSlot
	{
		volatile LONG active = 0;
		volatile LONG reported = 0;
		volatile LONG threadId = 0;
		volatile LONG kind = 0;		// 0 = single object, 1 = multiple objects
		volatile LONG count = 0;
		volatile LONG startTick = 0;
		volatile LONG waitMs = 0;
		HANDLE handles[4] = {};
		char api[40] = {};
	};

	LongWaitSlot gLongWaits[kMaxWaitSlots];
	volatile LONG gLongWaitThreadStarted = 0;

	DWORD WINAPI LongWaitWatchdog(void *)
	{
		for (;;) {
			Sleep(250);
			const LONG now = static_cast<LONG>(GetTickCount());
			for (unsigned int i = 0; i < kMaxWaitSlots; ++i) {
				LongWaitSlot &slot = gLongWaits[i];
				if (!slot.active || slot.reported)
					continue;
				const LONG elapsed = now - slot.startTick;
				if (elapsed < static_cast<LONG>(kLongWaitThresholdMs))
					continue;
				InterlockedExchange(&slot.reported, 1);
				DX12_LOG_FLUSH("DX12LongWait",
					"\"event\":\"blocked\",\"api\":\"%s\",\"threadId\":%ld,"
					"\"elapsedMs\":%ld,\"waitMs\":%ld,\"count\":%ld",
					slot.api, slot.threadId, elapsed, slot.waitMs, slot.count);
				for (LONG h = 0; h < slot.count && h < 4; ++h) {
					DX12_LOG_FLUSH("DX12LongWait",
						"\"event\":\"handle\",\"api\":\"%s\",\"slot\":%ld,"
						"\"handle\":\"%p\"",
						slot.api, h, slot.handles[h]);
				}
			}
		}
		return 0;
	}

	// Returns the slot index, or -1 when all slots are busy (a wait nesting
	// deeper than the table is not worth logging).
	int AcquireLongWaitSlot(const char *api, DWORD waitMs, DWORD count,
		const HANDLE *handles, HANDLE single)
	{
		if (InterlockedCompareExchange(&gLongWaitThreadStarted, 1, 0) == 0) {
			HANDLE thread = CreateThread(
				nullptr, 0, &LongWaitWatchdog, nullptr, 0, nullptr);
			if (thread)
				CloseHandle(thread);
		}
		for (unsigned int i = 0; i < kMaxWaitSlots; ++i) {
			LongWaitSlot &slot = gLongWaits[i];
			if (InterlockedCompareExchange(&slot.active, 1, 0) != 0)
				continue;
			slot.reported = 0;
			slot.threadId = static_cast<LONG>(GetCurrentThreadId());
			slot.kind = handles ? 1 : 0;
			slot.count = handles ? static_cast<LONG>(count) : 1;
			slot.startTick = static_cast<LONG>(GetTickCount());
			slot.waitMs = static_cast<LONG>(waitMs);
			for (LONG h = 0; h < slot.count && h < 4; ++h)
				slot.handles[h] = handles ? handles[h] : single;
			strncpy_s(slot.api, api ? api : "", _TRUNCATE);
			return static_cast<int>(i);
		}
		return -1;
	}

	void ReleaseLongWaitSlot(int index)
	{
		if (index >= 0)
			InterlockedExchange(&gLongWaits[index].active, 0);
	}
}

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
		return DX12DiagProf::IsTrackedWaitHandle(
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
		DX12DiagProf::SnapshotLastFenceEvent(
			&fence, &fenceValue, &fenceEvent);
		DX12DiagProf::LogCurrentCallStack(
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
		if (DX12DiagLog::IsInternalEventHandle(hHandle)) {
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
		// See HookedWaitForMultipleObjects: the witness is what names the object
		// a hang is parked on, which the stack dump cannot recover.
		const bool witness = dwMilliseconds == INFINITE ||
			dwMilliseconds >= kLongWaitThresholdMs;
		const int slot = witness ?
			AcquireLongWaitSlot("WaitForSingleObject", dwMilliseconds, 1,
				nullptr, hHandle) : -1;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
		} else {
			const LARGE_INTEGER begin = DX12DiagProf::Now();
			result = DX12WaitHookState::OrigWaitForSingleObject(hHandle, dwMilliseconds);
			us = DX12DiagProf::ElapsedUs(begin);
		}
		ReleaseLongWaitSlot(slot);
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12DiagProf::RecordSystemWait(
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
		// Only waits that can actually be long get a witness slot: an INFINITE
		// wait or one with a timeout past the reporting threshold. Everything
		// else returns before the watchdog could ever look at it.
		const bool witness = dwMilliseconds == INFINITE ||
			dwMilliseconds >= kLongWaitThresholdMs;
		const int slot = witness ?
			AcquireLongWaitSlot(
				bWaitAll ? "WaitForMultipleObjects(all)" : "WaitForMultipleObjects(any)",
				dwMilliseconds, nCount, lpHandles, nullptr) : -1;
		if (CanSkipMeasurement(dwMilliseconds, probeHandle)) {
			result = DX12WaitHookState::OrigWaitForMultipleObjects(
				nCount, lpHandles, bWaitAll, dwMilliseconds);
		} else {
			const LARGE_INTEGER begin = DX12DiagProf::Now();
			result = DX12WaitHookState::OrigWaitForMultipleObjects(
				nCount, lpHandles, bWaitAll, dwMilliseconds);
			us = DX12DiagProf::ElapsedUs(begin);
		}
		ReleaseLongWaitSlot(slot);
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			const char *api =
				bWaitAll ? "WaitForMultipleObjects(all)" : "WaitForMultipleObjects(any)";
			DX12DiagProf::RecordSystemWait(
				api,
				matchedHandle,
				nullptr,
				dwMilliseconds,
				result,
				lastError,
				us,
				matchesFenceEvent,
				matchesFrameLatency);
			if (matchesFenceEvent || matchesFrameLatency)
				LogMatchedWaitStack(api, matchedHandle, result, us);
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

		if (DX12DiagLog::IsInternalEventHandle(hHandle)) {
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
			const LARGE_INTEGER begin = DX12DiagProf::Now();
			result = DX12WaitHookState::OrigWaitForSingleObjectEx(
				hHandle, dwMilliseconds, bAlertable);
			us = DX12DiagProf::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12DiagProf::RecordSystemWait(
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
			const LARGE_INTEGER begin = DX12DiagProf::Now();
			result = DX12WaitHookState::OrigWaitForMultipleObjectsEx(
				nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);
			us = DX12DiagProf::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			const char *api = bWaitAll ?
				"WaitForMultipleObjectsEx(all)" : "WaitForMultipleObjectsEx(any)";
			DX12DiagProf::RecordSystemWait(
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

		if (DX12DiagLog::IsInternalEventHandle(hObjectToWaitOn)) {
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
			const LARGE_INTEGER begin = DX12DiagProf::Now();
			result = DX12WaitHookState::OrigSignalObjectAndWait(
				hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);
			us = DX12DiagProf::ElapsedUs(begin);
		}
		if (probeHandle || us >= 50000) {
			const DWORD lastError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
			DX12DiagProf::RecordSystemWait(
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
#endif // DX12_TIMING_BUILD

void DX12WaitHooks::Install()
{
#if DX12_TIMING_BUILD
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
#endif // DX12_TIMING_BUILD
}

void DX12WaitHooks::Shutdown()
{
}
