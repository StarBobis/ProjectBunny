#pragma once

#include <Windows.h>
#include <d3d12.h>

void DX12SetModule(HINSTANCE module);
HINSTANCE DX12GetModule();

bool DX12OpenLogFile();
void DX12CloseLogFile();
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
// Call once per frame (from Present) instead of after every log line.
void DX12FlushLog();

DWORD DX12HookFunction(void **original, void *target, void *hook, const char *name);
void *DX12GetOriginalFunction(void *target);
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

// --- Hot-path fast-forward flags ---
// When no mod overrides, no hunting, no frame analysis capture and no shader
// dump are active, this flag is set to true.  Every recording-hook entry point
// checks it FIRST with a single non-atomic read; if set the hook does nothing
// except call the original D3D12 function.
//
// Recalculated once per Present (frame granularity) by DX12HotPathUpdate.
extern volatile LONG gDX12HotPathSkipAll;

// Separate fast-forward for BINDING hooks (SetRoot*, IASet*, SetDescriptorHeaps).
// Set to true when no HEAVY tracking is needed (hunt / frame-analysis / shader-
// dump are all off), even when mod work (texture/shader overrides) is active.
// Binding hooks only need to track state for hunt/capture — mod work only needs
// draw hooks.  This lets ~80 % of API calls (bindings) skip the StateCapture
// path entirely while draw hooks still run the mod matching path.
extern volatile LONG gDX12HotPathSkipBindings;

void DX12HotPathUpdate();

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
