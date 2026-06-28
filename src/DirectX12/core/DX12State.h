#pragma once

#include <Windows.h>
#include <d3d12.h>

enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

void DX12SetModule(HINSTANCE module);
HINSTANCE DX12GetModule();

bool DX12OpenLogFile();
void DX12CloseLogFile();
void DX12EndReleaseStartupLogging();
bool DX12DiagnosticsLoggingEnabled();
bool DX12VerboseDiagnosticsEnabled();
void DX12Log(const char *fmt, ...);
void DX12LogJsonFunc(const char *func, const char *fmt, ...);
void DX12LogJsonFuncFlush(const char *func, const char *fmt, ...);
#if defined(_DEBUG)
void DX12LogDebugJsonFunc(const char *func, const char *fmt, ...);
void DX12LogDebugJsonFuncFlush(const char *func, const char *fmt, ...);
#else
#define DX12LogDebugJsonFunc(...) ((void)0)
#define DX12LogDebugJsonFuncFlush(...) ((void)0)
#endif
void DX12FlushLog();

DWORD DX12HookFunction(void **original, void *target, void *hook, const char *name);
void *DX12GetOriginalFunction(void *target);
void DX12FinalizeHooks();
void DX12UnhookAll();

LONG DX12IncrementPresentCount();
LONG DX12GetPresentCount();

void DX12SetOverlayWindow(HWND hwnd);
HWND DX12GetOverlayWindow();

void DX12SetOverlayStatus(const wchar_t *status);
void DX12SetOverlayWarning(const wchar_t *status);
void DX12SetOverlayError(const wchar_t *status);
void DX12SetOverlayStatusTemporary(const wchar_t *status, DWORD durationMs);
void DX12ClearExpiredOverlayStatus();
void DX12GetOverlayStatus(wchar_t *status, size_t statusCount);
COLORREF DX12GetOverlayStatusColor();

void DX12SetCommandQueue(ID3D12CommandQueue *queue);
ID3D12CommandQueue *DX12AcquireCommandQueue();

extern volatile LONG gDX12HotPathSkipAll;
extern volatile LONG gDX12HotPathSkipBindings;
extern volatile LONG gDX12HotPathSkipGraphicsBindings;
extern volatile LONG gDX12HotPathTrackResourceMetadata;

DX12RuntimeMode DX12GetRuntimeMode();
void DX12SetRuntimeMode(DX12RuntimeMode mode);
bool DX12IsModMode();
bool DX12IsHuntingMode();

// Per-vtable original function cache: maps vtable pointer → originals[256].
// Populated during hook installation; read-only afterwards (no lock needed).
void DX12CacheVtableOriginals(void *vtable, size_t maxSlot);

extern volatile LONG gPerFrameTotalDraws;
extern volatile LONG gPerFrameTotalDispatches;

void DX12HotPathUpdate();
bool DX12HotPathHooksNeeded();
bool DX12ShouldLogHookCall(const char *api);

void DX12EnterInternalReplay();
void DX12LeaveInternalReplay();
bool DX12IsInternalReplay();

class DX12InternalReplayScope
{
public:
	DX12InternalReplayScope() { DX12EnterInternalReplay(); }
	~DX12InternalReplayScope() { DX12LeaveInternalReplay(); }
	DX12InternalReplayScope(const DX12InternalReplayScope&) = delete;
	DX12InternalReplayScope &operator=(const DX12InternalReplayScope&) = delete;
};
