#include "DX12Input.h"

#include <Windows.h>

#include "DX12ModRuntime.h"
#include "DX12LogManager.hpp"
#include "DX12OverlayStatusState.hpp"

static volatile LONG gReloadInProgress = 0;

static DWORD WINAPI DX12ReloadThread(void*)
{
	const DWORD start = GetTickCount();
	DX12LogManager::JsonFunc("DX12ModRuntimeReload", "\"status\":\"thread_begin\"");
	const bool ok = DX12ModRuntimeReload();
	const DWORD elapsed = GetTickCount() - start;
	if (ok) {
		wchar_t status[128];
		swprintf_s(status, L"Reload Success TimeConsume: %.3f S",
			static_cast<double>(elapsed) / 1000.0);
		DX12OverlayStatusState::SetTemporary(status, 3000);
	}
	DX12LogManager::JsonFunc("DX12ModRuntimeReload",
		"\"status\":\"thread_done\",\"result\":\"%s\",\"elapsedMs\":%lu",
		ok ? "ok" : "failed", elapsed);
	InterlockedExchange(&gReloadInProgress, 0);
	return 0;
}

void DX12RequestModReload()
{
	if (InterlockedCompareExchange(&gReloadInProgress, 1, 0) != 0) {
		DX12LogManager::JsonFunc("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"already_running\"");
		DX12OverlayStatusState::SetWarning(L"Reload already running");
		return;
	}

	DX12OverlayStatusState::SetStatus(L"Reload running");
	HANDLE thread = CreateThread(nullptr, 0, DX12ReloadThread, nullptr, 0, nullptr);
	if (thread) {
		CloseHandle(thread);
		return;
	}

	InterlockedExchange(&gReloadInProgress, 0);
	DX12LogManager::JsonFunc("DX12ModRuntimeReload",
		"\"status\":\"failed\",\"reason\":\"create_thread\",\"error\":%lu", GetLastError());
	DX12OverlayStatusState::SetError(L"Reload failed: cannot start thread");
}


