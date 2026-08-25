// Input handling for the overlay and hunting hotkeys.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12Overlay.h"

class DX12Input
{
public:
	inline static volatile LONG ReloadInProgress = 0;

	static void CompleteReload()
	{
		const DWORD start = GetTickCount();
		DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"begin_sync\"");
		const bool ok = DX12ModRuntimeReload();
		const DWORD elapsed = GetTickCount() - start;
		if (ok) {
			wchar_t status[128];
			swprintf_s(status, L"Reload Success TimeConsume: %.3f S",
				static_cast<double>(elapsed) / 1000.0);
			DX12OverlayStatusState::SetTemporary(status, 3000);
		}
		DX12LogManager::Fields("DX12ModRuntimeReload",
			"\"status\":\"done_sync\",\"result\":\"%s\",\"elapsedMs\":%lu",
			ok ? "ok" : "failed", elapsed);
		DX12ModRefreshRuntimeStateAfterReload();
		InterlockedExchange(&ReloadInProgress, 0);
	}

	static void RequestModReload()
	{
		if (InterlockedCompareExchange(&ReloadInProgress, 1, 0) != 0) {
			DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"already_running\"");
			DX12OverlayStatusState::SetWarning(L"Reload already running");
			return;
		}

		DX12OverlayStatusState::SetStatus(L"Reload running");
		// Keep DX11-style semantics: the reload and resident runtime refresh happen
		// as one operation instead of waiting for a later Present tick.
		CompleteReload();
	}
};
