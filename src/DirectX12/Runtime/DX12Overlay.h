// In-game overlay status state: one transient notice line rendered as green
// text inside the game window by DX12NativeOverlay. Split from DX12Runtime.h;
// keep declarations in the layer that owns them.
#pragma once

#include "DX12VmFrameVariablesUtils.h"

// Every notice is transient: the in-game text overlay has no status bar, so
// nothing is sticky. Progress feedback (shader dump, reload, ...) re-posts
// itself while it runs, which keeps the notice alive exactly as long as the
// operation it describes. The one exception is the startup banner, which the
// PSO-creation path would otherwise re-post for the whole session.
class DX12OverlayStatusState
{
public:
	SRWLOCK lock;
	wchar_t status[512] = L"";
	COLORREF statusColor = RGB(0, 255, 0);
	DWORD statusExpireTick = 0;
	// Latched by SetStatusOnce: the one notice that must not come back.
	bool announced = false;

	DX12OverlayStatusState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12OverlayStatusState &Get()
	{
		static DX12OverlayStatusState state;
		return state;
	}

	static void NotifyLocked(const wchar_t *status, COLORREF color, DWORD durationMs)
	{
		DX12OverlayStatusState &state = Get();
		if (status && status[0])
			wcsncpy_s(state.status, status, _TRUNCATE);
		state.statusColor = color;
		state.statusExpireTick = GetTickCount() + (durationMs ? durationMs : 3000);
	}

	static void SetStatus(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		NotifyLocked(status, RGB(0, 255, 0), 4000);
		ReleaseSRWLockExclusive(&state.lock);
	}

	// A notice worth showing once per process. The "hook alive | cached shaders
	// ready" banner is posted from the PSO-creation path, which the game runs
	// continuously while streaming a scene, so as a plain SetStatus it came back
	// every few seconds during play; the DX11 build shows the equivalent once at
	// startup.
	static void SetStatusOnce(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		if (!state.announced) {
			state.announced = true;
			NotifyLocked(status, RGB(0, 255, 0), 4000);
		}
		ReleaseSRWLockExclusive(&state.lock);
	}

	static void SetWarning(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		NotifyLocked(status, RGB(255, 220, 0), 6000);
		ReleaseSRWLockExclusive(&state.lock);
	}

	static void SetError(const wchar_t *status)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		NotifyLocked(status, RGB(255, 64, 64), 8000);
		ReleaseSRWLockExclusive(&state.lock);
	}

	static void SetTemporary(const wchar_t *status, DWORD durationMs)
	{
		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		NotifyLocked(status, RGB(0, 255, 0), durationMs);
		ReleaseSRWLockExclusive(&state.lock);
	}

	// Returns the notice while it is still alive. Once expired the slot is
	// cleared so a stale notice can never resurface after a later expiry.
	static bool GetTransientStatus(wchar_t *status, size_t statusCount, COLORREF *color)
	{
		if (!status || statusCount == 0)
			return false;

		DX12OverlayStatusState &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		const bool alive = state.status[0] && state.statusExpireTick &&
			GetTickCount() < state.statusExpireTick;
		if (alive) {
			wcsncpy_s(status, statusCount, state.status, _TRUNCATE);
			if (color)
				*color = state.statusColor;
		} else if (state.status[0] && state.statusExpireTick &&
			GetTickCount() >= state.statusExpireTick) {
			state.status[0] = 0;
			state.statusExpireTick = 0;
		}
		ReleaseSRWLockExclusive(&state.lock);
		return alive;
	}
};
