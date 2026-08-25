// In-game overlay rendering and status state.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12VmFrameVariablesUtils.h"

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


	static void Show()
	{
	}

	static void Hide()
	{
	}

	static void RequestRepaint()
	{
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


	static void GetStatus(wchar_t *status, size_t statusCount)
	{
		if (!status || statusCount == 0)
			return;

		DX12OverlayStatusState &state = Get();
		AcquireSRWLockShared(&state.lock);
		wcsncpy_s(status, statusCount, state.status, _TRUNCATE);
		ReleaseSRWLockShared(&state.lock);
	}

};


