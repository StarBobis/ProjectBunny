#include "DX12State.h"

#include <Shlwapi.h>
#include <array>
#include <atomic>
#include <deque>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12Overlay.h"
#include "DX12Json.h"
#include "DX12ModRuntime.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "Nektra/NktHookLib.h"

static HINSTANCE gModule = nullptr;
static FILE *gLog = nullptr;
static HANDLE gLogThread = nullptr;
static HANDLE gLogEvent = nullptr;
static SRWLOCK gLogQueueLock = SRWLOCK_INIT;
static std::deque<std::string> gLogQueue;
static volatile LONG gLogDroppedLines = 0;
static volatile LONG gLogStopRequested = 0;
static volatile LONG gPresentCount = 0;
static volatile LONG gHotPathStateMask = -1;
static HWND gOverlayWindow = nullptr;
static wchar_t gOverlayStatus[512] = L"3DMigoto DX12 hook alive";
static COLORREF gOverlayStatusColor = RGB(0, 255, 0);
static DWORD gOverlayStatusExpireTick = 0;
static ID3D12CommandQueue *gCommandQueue = nullptr;
static SRWLOCK gStateLock = SRWLOCK_INIT;
#if !defined(_DEBUG)
static std::atomic<bool> gReleaseStartupLogging{ true };
#endif
static CNktHookLib gHookMgr;
static std::unordered_set<void*> gHookedFunctions;
static std::unordered_map<void*, void*> gOriginalFunctions;
static volatile LONG gHooksFinalized = 0;

static std::unordered_map<void*, std::array<void*, 256>> gVtableOriginals;

void DX12CacheVtableOriginals(void *vtable, size_t maxSlot)
{
	if (!vtable || maxSlot == 0 || maxSlot > 256)
		return;
	void **slots = static_cast<void**>(vtable);
	std::array<void*, 256> originals = {};
	for (size_t i = 0; i < maxSlot; ++i) {
		void *target = slots[i];
		if (!target)
			continue;
		auto it = gOriginalFunctions.find(target);
		if (it != gOriginalFunctions.end())
			originals[i] = it->second;
	}
	gVtableOriginals[vtable] = originals;
}

volatile LONG gDX12HotPathSkipAll = 1;
volatile LONG gDX12HotPathSkipBindings = 1;
volatile LONG gDX12HotPathSkipGraphicsBindings = 1;
volatile LONG gDX12HotPathTrackResourceMetadata = 0;
static thread_local UINT tDX12InternalReplayDepth = 0;

#if defined(_DEBUG)
static constexpr size_t kMaxQueuedLogLines = 262144;
#else
static constexpr size_t kMaxQueuedLogLines = 8192;
#endif

void DX12EnterInternalReplay()
{
	++tDX12InternalReplayDepth;
}

void DX12LeaveInternalReplay()
{
	if (tDX12InternalReplayDepth)
		--tDX12InternalReplayDepth;
}

bool DX12IsInternalReplay()
{
	return tDX12InternalReplayDepth != 0;
}

void DX12HotPathUpdate()
{
	// Fast path: if already skipping all and no activity is present, avoid InterlockedExchange storm
	if (gDX12HotPathSkipAll &&
		!DX12FrameAnalysisIsActive() &&
		!DX12FrameAnalysisIsCapturing() &&
		!DX12FrameAnalysisIsCaptureRequested() &&
		!DX12ShaderDumpIsCapturingFrame() &&
		!DX12ShaderDumpIsCaptureRequested() &&
		!DX12ShaderDumpIsBusy() &&
		!DX12HuntNeedsHotPath() &&
		!DX12ModHasActiveShaderOverrides() &&
		!DX12ModNeedsPresentReplacement() &&
		!DX12ModNeedsPreSkinningUavProbe())
		return;

	const bool needsHeavyTracking =
		DX12FrameAnalysisIsActive() ||
		DX12FrameAnalysisIsCapturing() ||
		DX12FrameAnalysisIsCaptureRequested() ||
		DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() ||
		DX12ShaderDumpIsBusy();

	const bool needsRecordWork =
		DX12HuntNeedsHotPath() ||
		DX12ModHasActiveShaderOverrides() ||
		DX12ModNeedsPresentReplacement() ||
		DX12ModNeedsPreSkinningUavProbe();
	const bool needsBindingState =
		needsHeavyTracking ||
		DX12ModNeedsPreSkinningBindingTracking();
	const bool needsGraphicsBindingState =
		needsHeavyTracking ||
		DX12ModNeedsShaderDescriptorTracking();

	const LONG skipAll = (needsHeavyTracking || needsRecordWork) ? 0 : 1;
	const LONG skipBindings = needsBindingState ? 0 : 1;
	const LONG skipGraphicsBindings = needsGraphicsBindingState ? 0 : 1;
	const LONG trackResourceMetadata =
		(needsHeavyTracking || DX12ModNeedsPreSkinningUavProbe()) ? 1 : 0;

	InterlockedExchange(&gDX12HotPathSkipAll, skipAll);
	InterlockedExchange(&gDX12HotPathSkipBindings, skipBindings);
	InterlockedExchange(&gDX12HotPathSkipGraphicsBindings, skipGraphicsBindings);
	InterlockedExchange(&gDX12HotPathTrackResourceMetadata, trackResourceMetadata);

	const LONG mask =
		(skipAll ? 1 : 0) |
		(skipBindings ? 2 : 0) |
		(skipGraphicsBindings ? 4 : 0) |
		(trackResourceMetadata ? 8 : 0);
	if (InterlockedExchange(&gHotPathStateMask, mask) != mask) {
		DX12LogJsonFunc("DX12HotPathState",
			"\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
			skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
	}
}

bool DX12HotPathHooksNeeded()
{
	return InterlockedCompareExchange(&gDX12HotPathSkipAll, 0, 0) == 0 ||
		DX12HuntNeedsHotPath();
}

static bool DX12EnvironmentFlagEnabled(const wchar_t *name)
{
	wchar_t value[32] = {};
	DWORD chars = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
	if (!chars)
		return false;
	return value[0] == L'1' ||
		value[0] == L'y' || value[0] == L'Y' ||
		value[0] == L't' || value[0] == L'T';
}

static bool DX12HookCallLoggingEnabled()
{
#if defined(_DEBUG)
	return DX12EnvironmentFlagEnabled(L"MIGOTO_DX12_DIAGNOSTIC_LOGS");
#else
	return false;
#endif
}

bool DX12DiagnosticsLoggingEnabled()
{
#if defined(_DEBUG)
	return true;
#else
	return false;
#endif
}

bool DX12ShouldLogHookCall(const char *api)
{
#if defined(_DEBUG)
	if (!api || !api[0] || DX12IsInternalReplay() || !DX12HookCallLoggingEnabled())
		return false;
	return true;
#else
	(void)api;
	return false;
#endif
}

void DX12SetModule(HINSTANCE module)
{
	gModule = module;
}

HINSTANCE DX12GetModule()
{
	return gModule;
}

static DWORD WINAPI DX12LogThreadProc(void*)
{
	for (;;) {
		if (gLogEvent)
			WaitForSingleObject(gLogEvent, 100);
		if (gLogEvent)
			ResetEvent(gLogEvent);

		std::vector<std::string> pending;
		AcquireSRWLockExclusive(&gLogQueueLock);
		while (!gLogQueue.empty()) {
			pending.push_back(std::move(gLogQueue.front()));
			gLogQueue.pop_front();
		}
		const bool stopping = InterlockedCompareExchange(&gLogStopRequested, 0, 0) != 0;
		ReleaseSRWLockExclusive(&gLogQueueLock);

		if (gLog) {
			const LONG dropped = InterlockedExchange(&gLogDroppedLines, 0);
			if (dropped > 0) {
				char droppedLine[256];
				snprintf(droppedLine, sizeof(droppedLine),
					"{\"func\":\"DX12LogQueue\",\"event\":\"DroppedLines\",\"count\":%ld}\n",
					dropped);
				fputs(droppedLine, gLog);
			}
			for (const auto &line : pending)
				fputs(line.c_str(), gLog);
			if (!pending.empty() || stopping)
				fflush(gLog);
		}

		if (stopping && pending.empty())
			break;
	}
	return 0;
}

bool DX12OpenLogFile()
{
#if !defined(_DEBUG)
	gReleaseStartupLogging.store(true, std::memory_order_relaxed);
#endif
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(gModule, path, MAX_PATH))
		return false;

	PathRemoveFileSpecW(path);
	PathAppendW(path, L"d3d12_log.jsonl");
	gLog = _wfsopen(path, L"w", _SH_DENYNO);
	if (!gLog)
		return false;

	InterlockedExchange(&gLogStopRequested, 0);
	gLogEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!gLogEvent) {
		fclose(gLog);
		gLog = nullptr;
		return false;
	}
	gLogThread = CreateThread(nullptr, 0, DX12LogThreadProc, nullptr, 0, nullptr);
	if (!gLogThread) {
		CloseHandle(gLogEvent);
		gLogEvent = nullptr;
		fclose(gLog);
		gLog = nullptr;
		return false;
	}
	return true;
}

#if !defined(_DEBUG)
void DX12EndReleaseStartupLogging()
{
	gReleaseStartupLogging.store(false, std::memory_order_relaxed);
}

static bool DX12ReleaseShouldLogFunction(const char *func)
{
	if (DX12EnvironmentFlagEnabled(L"MIGOTO_DX12_RELEASE_LOGS"))
		return true;
	if (gReleaseStartupLogging.load(std::memory_order_relaxed))
		return true;
	if (!func)
		return false;
	if (!strcmp(func, "Log"))
		return true;
	if (!strcmp(func, "HookInstall"))
		return true;
	if (!strcmp(func, "DeviceHooksReady"))
		return true;
	if (!strcmp(func, "D3D12CreateDevice"))
		return true;
	if (!strncmp(func, "CreateDXGIFactory", 17))
		return true;
	if (!strncmp(func, "IDXGIFactory", 12))
		return true;
	if (!strncmp(func, "IDXGISwapChain", 14))
		return true;
	if (!strncmp(func, "ID3D12CommandQueue", 18))
		return true;
	if (!strcmp(func, "DX12CommandQueueState"))
		return true;
	if (!strcmp(func, "DX12ExecuteCommandListsFirst"))
		return true;
	if (!strcmp(func, "DX12PresentFirst"))
		return true;
	if (!strcmp(func, "DX12HotPathHookGate"))
		return true;
	if (!strcmp(func, "DX12HotPathState"))
		return true;
	if (!strcmp(func, "InputHotkey"))
		return true;
	if (!strcmp(func, "DX12InputThread"))
		return true;
	if (!strcmp(func, "DX12HuntSelect"))
		return true;
	if (!strcmp(func, "DX12HuntIaStats"))
		return true;
	if (!strcmp(func, "DX12HuntToggle"))
		return true;
	if (!strncmp(func, "OverlayWindow", 13))
		return true;
	return false;
}
#else
void DX12EndReleaseStartupLogging()
{
}
#endif

static void MakeLogTime(char *buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize)
		return;
	SYSTEMTIME localTime;
	GetLocalTime(&localTime);
	snprintf(buffer, bufferSize,
		"\"%02u:%02u:%02u.%03u\"",
		localTime.wHour, localTime.wMinute, localTime.wSecond,
		localTime.wMilliseconds);
}

static void EnqueueJsonLogLine(const char *line)
{
	if (!line || !line[0])
		return;

	AcquireSRWLockExclusive(&gLogQueueLock);
	if (gLogQueue.size() < kMaxQueuedLogLines) {
		gLogQueue.emplace_back(line);
	} else {
		InterlockedIncrement(&gLogDroppedLines);
	}
	ReleaseSRWLockExclusive(&gLogQueueLock);
	if (gLogEvent)
		SetEvent(gLogEvent);
}

static void WriteJsonLogLine(const char *func, const char *funcJson, const char *extra, bool flush)
{
	if (!gLog)
		return;

	char timeJson[64] = {};
	MakeLogTime(timeJson, sizeof(timeJson));
	const bool isHookCall = func && strcmp(func, "DX12HookCall") == 0;
	char line[8192];
	if (extra && extra[0]) {
		if (isHookCall) {
			snprintf(line, sizeof(line),
				"{%s,\"time\":%s}\n",
				extra, timeJson);
		} else {
			snprintf(line, sizeof(line),
				"{\"func\":%s,\"time\":%s,%s}\n",
				funcJson, timeJson, extra);
		}
	} else {
		if (isHookCall) {
			snprintf(line, sizeof(line),
				"{\"api\":\"\",\"time\":%s}\n",
				timeJson);
		} else {
			snprintf(line, sizeof(line),
				"{\"func\":%s,\"time\":%s}\n",
				funcJson, timeJson);
		}
	}
	EnqueueJsonLogLine(line);
	if (flush)
		DX12FlushLog();
}

void DX12CloseLogFile()
{
	InterlockedExchange(&gLogStopRequested, 1);
	if (gLogEvent)
		SetEvent(gLogEvent);
	if (gLogThread) {
		WaitForSingleObject(gLogThread, 5000);
		CloseHandle(gLogThread);
		gLogThread = nullptr;
	}
	if (gLogEvent) {
		CloseHandle(gLogEvent);
		gLogEvent = nullptr;
	}
	if (gLog) {
		fclose(gLog);
		gLog = nullptr;
	}
	AcquireSRWLockExclusive(&gLogQueueLock);
	gLogQueue.clear();
	ReleaseSRWLockExclusive(&gLogQueueLock);
}

void DX12Log(const char *fmt, ...)
{
	if (!gLog)
		return;
#if !defined(_DEBUG)
	if (!DX12ReleaseShouldLogFunction("Log"))
		return;
#endif

	va_list args;
	char message[2048];
	char fields[4096];

	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	fields[0] = '\0';
	DX12JsonAppendLogFieldsFromText(fields, sizeof(fields), message);
	char funcJson[16];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), "Log");
	WriteJsonLogLine("Log", funcJson, fields[0] == ',' ? fields + 1 : fields, false);
}

void DX12LogJsonFunc(const char *func, const char *fmt, ...)
{
	if (!gLog)
		return;
#if !defined(_DEBUG)
	if (!DX12ReleaseShouldLogFunction(func))
		return;
#endif

	char funcJson[512];
	char extra[4096];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
	extra[0] = '\0';

	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(extra, sizeof(extra), fmt, args);
		va_end(args);
	}

	WriteJsonLogLine(func ? func : "Unknown", funcJson, extra, false);
}

void DX12LogJsonFuncFlush(const char *func, const char *fmt, ...)
{
	if (!gLog)
		return;
#if !defined(_DEBUG)
	if (!DX12ReleaseShouldLogFunction(func))
		return;
#endif

	char funcJson[512];
	char extra[4096];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
	extra[0] = '\0';

	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(extra, sizeof(extra), fmt, args);
		va_end(args);
	}

	WriteJsonLogLine(func ? func : "Unknown", funcJson, extra, true);
}

#if defined(_DEBUG)
void DX12LogDebugJsonFunc(const char *func, const char *fmt, ...)
{
	if (!gLog)
		return;
	if (!DX12DiagnosticsLoggingEnabled())
		return;

	char funcJson[512];
	char extra[4096];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
	extra[0] = '\0';

	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(extra, sizeof(extra), fmt, args);
		va_end(args);
	}

	WriteJsonLogLine(func ? func : "Unknown", funcJson, extra, false);
}

void DX12LogDebugJsonFuncFlush(const char *func, const char *fmt, ...)
{
	if (!gLog)
		return;
	if (!DX12DiagnosticsLoggingEnabled())
		return;

	char funcJson[512];
	char extra[4096];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
	extra[0] = '\0';

	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(extra, sizeof(extra), fmt, args);
		va_end(args);
	}

	WriteJsonLogLine(func ? func : "Unknown", funcJson, extra, true);
}
#endif

void DX12FlushLog()
{
	if (gLogEvent)
		SetEvent(gLogEvent);
}

static bool RememberHook(void *target)
{
	AcquireSRWLockExclusive(&gStateLock);
	bool inserted = gHookedFunctions.insert(target).second;
	ReleaseSRWLockExclusive(&gStateLock);
	return inserted;
}

static void RememberOriginal(void *target, void *original)
{
	if (!target || !original)
		return;

	AcquireSRWLockExclusive(&gStateLock);
	gOriginalFunctions[target] = original;
	ReleaseSRWLockExclusive(&gStateLock);
}

DWORD DX12HookFunction(void **original, void *target, void *hook, const char *name)
{
	if (!target || !hook)
		return ERROR_PROC_NOT_FOUND;
	if (!RememberHook(target))
		return ERROR_SUCCESS;

	SIZE_T hookId = 0;
	DWORD result = gHookMgr.Hook(&hookId, original, target, hook);
	if (result == ERROR_SUCCESS && original)
		RememberOriginal(target, *original);
	DX12LogJsonFunc(name ? name : "HookInstall",
		"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
		result == ERROR_SUCCESS ? "installed" : "failed",
		target, original ? *original : nullptr, result);
	return result;
}

void DX12FinalizeHooks()
{
	InterlockedExchange(&gHooksFinalized, 1);
}

void *DX12GetOriginalFunction(void *target)
{
	if (!target)
		return nullptr;

	if (gHooksFinalized) {
		auto it = gOriginalFunctions.find(target);
		return it != gOriginalFunctions.end() ? it->second : nullptr;
	}

	AcquireSRWLockShared(&gStateLock);
	auto it = gOriginalFunctions.find(target);
	void *original = it != gOriginalFunctions.end() ? it->second : nullptr;
	ReleaseSRWLockShared(&gStateLock);
	return original;
}

void DX12UnhookAll()
{
	gHookMgr.UnhookAll();
}

LONG DX12IncrementPresentCount()
{
	return InterlockedIncrement(&gPresentCount);
}

LONG DX12GetPresentCount()
{
	return gPresentCount;
}

void DX12SetOverlayWindow(HWND hwnd)
{
	gOverlayWindow = hwnd;
}

HWND DX12GetOverlayWindow()
{
	return gOverlayWindow;
}

static void SetOverlayStatusColorLocked(const wchar_t *status, COLORREF color)
{
	if (status && status[0])
		wcsncpy_s(gOverlayStatus, status, _TRUNCATE);
	else
		wcsncpy_s(gOverlayStatus, L"3DMigoto DX12 hook alive", _TRUNCATE);
	gOverlayStatusColor = color;
	gOverlayStatusExpireTick = 0;
}

static void InvalidateOverlayStatus()
{
	DX12RequestOverlayRepaint();
}

void DX12SetOverlayStatus(const wchar_t *status)
{
	AcquireSRWLockExclusive(&gStateLock);
	SetOverlayStatusColorLocked(status, RGB(0, 255, 0));
	ReleaseSRWLockExclusive(&gStateLock);
	InvalidateOverlayStatus();
}

void DX12SetOverlayWarning(const wchar_t *status)
{
	AcquireSRWLockExclusive(&gStateLock);
	SetOverlayStatusColorLocked(status, RGB(255, 220, 0));
	ReleaseSRWLockExclusive(&gStateLock);
	InvalidateOverlayStatus();
}

void DX12SetOverlayError(const wchar_t *status)
{
	AcquireSRWLockExclusive(&gStateLock);
	SetOverlayStatusColorLocked(status, RGB(255, 64, 64));
	ReleaseSRWLockExclusive(&gStateLock);
	InvalidateOverlayStatus();
}

void DX12SetOverlayStatusTemporary(const wchar_t *status, DWORD durationMs)
{
	AcquireSRWLockExclusive(&gStateLock);
	if (status && status[0])
		wcsncpy_s(gOverlayStatus, status, _TRUNCATE);
	else
		wcsncpy_s(gOverlayStatus, L"3DMigoto DX12 hook alive", _TRUNCATE);
	gOverlayStatusColor = RGB(0, 255, 0);
	gOverlayStatusExpireTick = durationMs ? GetTickCount() + durationMs : 0;
	ReleaseSRWLockExclusive(&gStateLock);
	InvalidateOverlayStatus();
}

void DX12ClearExpiredOverlayStatus()
{
	bool changed = false;
	AcquireSRWLockExclusive(&gStateLock);
	if (gOverlayStatusExpireTick) {
		const DWORD now = GetTickCount();
		if (static_cast<LONG>(now - gOverlayStatusExpireTick) >= 0) {
			wcsncpy_s(gOverlayStatus, L"3DMigoto DX12 hook alive", _TRUNCATE);
			gOverlayStatusColor = RGB(0, 255, 0);
			gOverlayStatusExpireTick = 0;
			changed = true;
		}
	}
	ReleaseSRWLockExclusive(&gStateLock);
	if (changed)
		InvalidateOverlayStatus();
}

void DX12GetOverlayStatus(wchar_t *status, size_t statusCount)
{
	if (!status || statusCount == 0)
		return;

	AcquireSRWLockShared(&gStateLock);
	wcsncpy_s(status, statusCount, gOverlayStatus, _TRUNCATE);
	ReleaseSRWLockShared(&gStateLock);
}

COLORREF DX12GetOverlayStatusColor()
{
	AcquireSRWLockShared(&gStateLock);
	COLORREF color = gOverlayStatusColor;
	ReleaseSRWLockShared(&gStateLock);
	return color;
}

void DX12SetCommandQueue(ID3D12CommandQueue *queue)
{
	if (queue) {
		D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
		if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
			static volatile LONG nonDirectLogs = 0;
			if (InterlockedIncrement(&nonDirectLogs) <= 8)
				DX12LogJsonFunc("DX12CommandQueueState",
					"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
					queue, static_cast<int>(desc.Type));
			return;
		}
		DX12LogJsonFunc("DX12CommandQueueState",
			"\"event\":\"accept_direct\",\"queue\":\"%p\",\"type\":%d",
			queue, static_cast<int>(desc.Type));
	}

	if (queue)
		queue->AddRef();

	AcquireSRWLockExclusive(&gStateLock);
	ID3D12CommandQueue *oldQueue = gCommandQueue;
	gCommandQueue = queue;
	ReleaseSRWLockExclusive(&gStateLock);

	if (oldQueue)
		oldQueue->Release();
}

ID3D12CommandQueue *DX12AcquireCommandQueue()
{
	AcquireSRWLockShared(&gStateLock);
	ID3D12CommandQueue *queue = gCommandQueue;
	if (queue)
		queue->AddRef();
	ReleaseSRWLockShared(&gStateLock);
	return queue;
}
