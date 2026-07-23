#pragma once

#include "Runtime/DX12ExecutionAnalysis.hpp"

class DX12Overlay
{
public:
	static constexpr UINT ShowMessage = WM_APP + 0x512;
	static constexpr UINT HideMessage = WM_APP + 0x513;
	static constexpr UINT DestroyMessage = WM_APP + 0x514;
	static constexpr UINT PositionMessage = WM_APP + 0x515;
	static constexpr UINT RepaintMessage = WM_APP + 0x516;

	static DWORD WINAPI Thread(void*)
	{
		return 0;
	}

	static void EnsureWindow()
	{
	}

	static void Show()
	{
	}

	static void Hide()
	{
	}

	static void RequestRepaint()
	{
	}

	static bool UpdateWindowForSwapChain(IDXGISwapChain *swapChain)
	{
		(void)swapChain;
		return false;
	}

	static void Close()
	{
	}
};



class DX12OverlayStatusState
{
public:
	SRWLOCK lock;
	HWND overlayWindow = nullptr;
	wchar_t status[512] = L"3DMigoto DX12 hook alive";
	COLORREF statusColor = RGB(0, 255, 0);
	DWORD statusExpireTick = 0;

	DX12OverlayStatusState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12OverlayStatusState &Get()
	{
		static DX12OverlayStatusState state;
		return state;
	}

	static void SetWindow(HWND hwnd)
	{
		Get().overlayWindow = hwnd;
	}

	static HWND GetWindow()
	{
		return Get().overlayWindow;
	}

	static void SetStatusColorLocked(const wchar_t *status, COLORREF color)
	{
		DX12OverlayStatusState &state = Get();
		if (status && status[0])
			wcsncpy_s(state.status, status, _TRUNCATE);
		else
			wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
		state.statusColor = color;
		state.statusExpireTick = 0;
	}

	static void InvalidateStatus()
	{
		DX12Overlay::RequestRepaint();
	}

	static void SetStatus(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(0, 255, 0));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetWarning(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(255, 220, 0));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetError(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		SetStatusColorLocked(status, RGB(255, 64, 64));
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void SetTemporary(const wchar_t *status, DWORD durationMs)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		if (status && status[0])
			wcsncpy_s(state.status, status, _TRUNCATE);
		else
			wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
		state.statusColor = RGB(0, 255, 0);
		state.statusExpireTick = durationMs ? GetTickCount() + durationMs : 0;
		ReleaseSRWLockExclusive(&state.lock);
		InvalidateStatus();
	}

	static void ClearExpired()
	{
		bool changed = false;
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		if (state.statusExpireTick) {
			const DWORD now = GetTickCount();
			if (static_cast<LONG>(now - state.statusExpireTick) >= 0) {
				wcsncpy_s(state.status, L"3DMigoto DX12 hook alive", _TRUNCATE);
				state.statusColor = RGB(0, 255, 0);
				state.statusExpireTick = 0;
				changed = true;
			}
		}
		ReleaseSRWLockExclusive(&state.lock);
		if (changed)
			InvalidateStatus();
	}

	static void GetStatus(wchar_t *status, size_t statusCount)
	{
		if (!status || statusCount == 0)
			return;

		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		wcsncpy_s(status, statusCount, state.status, _TRUNCATE);
		ReleaseSRWLockShared(&state.lock);
	}

	static COLORREF GetColor()
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		COLORREF color = state.statusColor;
		ReleaseSRWLockShared(&state.lock);
		return color;
	}
};



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
		// DX11 applies config reload on the render thread during Present, so the
		// new resident Mod state is visible on the very next frame. Running DX12
		// reload asynchronously adds an extra Present-sized latency bubble before
		// HotPathState is refreshed, which is what makes normal Mod mode feel
		// ~200 ms late compared with DX11.
		CompleteReload();
	}
};
