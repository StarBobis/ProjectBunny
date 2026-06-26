#include "DX12State.h"

#include <Shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <unordered_map>
#include <unordered_set>

#include "DX12FrameAnalysis.h"
#include "DX12Json.h"
#include "DX12ModRuntime.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "Nektra/NktHookLib.h"

static HINSTANCE gModule = nullptr;
static FILE *gLog = nullptr;
static unsigned long long gLogLineNo = 0;
static volatile LONG gPresentCount = 0;
static HWND gOverlayWindow = nullptr;
static wchar_t gOverlayStatus[512] = L"";
static COLORREF gOverlayStatusColor = RGB(0, 255, 0);
static DWORD gOverlayStatusExpireTick = 0;
static ID3D12CommandQueue *gCommandQueue = nullptr;
static SRWLOCK gStateLock = SRWLOCK_INIT;
static CNktHookLib gHookMgr;
static std::unordered_set<void*> gHookedFunctions;
static std::unordered_map<void*, void*> gOriginalFunctions;

// Hot-path fast-forward flags — see DX12State.h for full documentation.
volatile LONG gDX12HotPathSkipAll = 0;
volatile LONG gDX12HotPathSkipBindings = 0;
static thread_local UINT tDX12InternalReplayDepth = 0;

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
	// Heavy tracking subsystems that need full BindingTracker data.
	const bool needsHeavyTracking =
		DX12FrameAnalysisIsActive() ||
		DX12FrameAnalysisIsCapturing() ||
		DX12FrameAnalysisIsCaptureRequested() ||
		DX12ShaderDumpIsCapturingFrame() ||
		DX12ShaderDumpIsCaptureRequested() ||
		DX12ShaderDumpIsBusy();

	// Hunt needs draw/dispatch/PSO/IA hooks, but not the full BindingTracker.
	const bool needsHunt = DX12HuntIsEnabled();

	// Mod work that needs draw/dispatch hooks but NOT binding hooks.
	const bool needsModWork =
		DX12ModHasActiveShaderOverrides() ||
		DX12ModHasActiveTextureOverrides() ||
		DX12ModNeedsPresentReplacement() ||
		DX12ModHasAnyActiveOverrides() ||
		DX12ModNeedsPreSkinningUavProbe();

	// Skip-all: only when NOTHING is active.
	InterlockedExchange(&gDX12HotPathSkipAll,
		(needsHeavyTracking || needsHunt || needsModWork) ? 0 : 1);

	// Skip-bindings: only heavy capture/dump paths need the BindingTracker.
	InterlockedExchange(&gDX12HotPathSkipBindings,
		needsHeavyTracking ? 0 : 1);
}

void DX12SetModule(HINSTANCE module)
{
	gModule = module;
}

HINSTANCE DX12GetModule()
{
	return gModule;
}

bool DX12OpenLogFile()
{
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(gModule, path, MAX_PATH))
		return false;

	PathRemoveFileSpecW(path);
	PathAppendW(path, L"d3d12_log.jsonl");
	gLog = _wfsopen(path, L"w", _SH_DENYNO);
	gLogLineNo = 0;
	return gLog != nullptr;
}

static void MakeLogTime(char *buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize)
		return;
	SYSTEMTIME localTime;
	GetLocalTime(&localTime);
	snprintf(buffer, bufferSize,
		"\"%04u-%02u-%02uT%02u:%02u:%02u.%03u+08:00\"",
		localTime.wYear, localTime.wMonth, localTime.wDay,
		localTime.wHour, localTime.wMinute, localTime.wSecond,
		localTime.wMilliseconds);
}

static void WriteJsonLogLine(const char *funcJson, const char *extra, bool flush)
{
	if (!gLog)
		return;
	char timeJson[64] = {};
	MakeLogTime(timeJson, sizeof(timeJson));
	const DWORD tick = GetTickCount();
	const DWORD threadId = GetCurrentThreadId();
	const unsigned long long index = ++gLogLineNo;
	if (extra && extra[0]) {
		fprintf(gLog,
			"{\"index\":%llu,\"time\":%s,\"tickMs\":%lu,\"thread\":%lu,\"func\":%s,%s}\n",
			index, timeJson, tick, threadId, funcJson, extra);
	} else {
		fprintf(gLog,
			"{\"index\":%llu,\"time\":%s,\"tickMs\":%lu,\"thread\":%lu,\"func\":%s}\n",
			index, timeJson, tick, threadId, funcJson);
	}
	if (flush)
		fflush(gLog);
}

void DX12CloseLogFile()
{
	if (gLog) {
		fclose(gLog);
		gLog = nullptr;
	}
}

void DX12Log(const char *fmt, ...)
{
	if (!gLog)
		return;

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
	WriteJsonLogLine(funcJson, fields[0] == ',' ? fields + 1 : fields, false);
	// Do NOT fflush here — per-line sync flushes cause severe CPU stalls on the
	// recording hot path. DX12FlushLog() is called from the Present hook instead.
}

void DX12LogJsonFunc(const char *func, const char *fmt, ...)
{
	if (!gLog)
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

	WriteJsonLogLine(funcJson, extra, false);
	// No fflush here — see DX12Log above.
}

void DX12LogJsonFuncFlush(const char *func, const char *fmt, ...)
{
	if (!gLog)
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

	WriteJsonLogLine(funcJson, extra, true);
}

#if defined(_DEBUG)
void DX12LogDebugJsonFunc(const char *func, const char *fmt, ...)
{
	if (!gLog)
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

	WriteJsonLogLine(funcJson, extra, false);
}

void DX12LogDebugJsonFuncFlush(const char *func, const char *fmt, ...)
{
	if (!gLog)
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

	WriteJsonLogLine(funcJson, extra, true);
}
#endif

void DX12FlushLog()
{
	if (gLog)
		fflush(gLog);
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

void *DX12GetOriginalFunction(void *target)
{
	if (!target)
		return nullptr;

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
		gOverlayStatus[0] = L'\0';
	gOverlayStatusColor = color;
	gOverlayStatusExpireTick = 0;
}

static void InvalidateOverlayStatus()
{
	HWND hwnd = DX12GetOverlayWindow();
	if (hwnd)
		InvalidateRect(hwnd, nullptr, FALSE);
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
		gOverlayStatus[0] = L'\0';
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
			gOverlayStatus[0] = L'\0';
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
			DX12Log("Ignoring non-direct command queue for frame analysis readback: queue=%p type=%d\n",
				queue, static_cast<int>(desc.Type));
			return;
		}
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
