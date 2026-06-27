#include "DX12Input.h"

#include <Windows.h>

#include "DX12FrameAnalysis.h"
#include "DX12ModRuntime.h"
#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"

static bool gF8WasDown = false;
static bool gF9WasDown = false;
static bool gF10WasDown = false;
static bool gF11WasDown = false;
static volatile LONG gReloadInProgress = 0;
static volatile LONG gInputThreadStopRequested = 0;
static volatile LONG gInputThreadActive = 0;
static HANDLE gInputThread = nullptr;
static bool gNumpad0WasDown = false;
static bool gNumpadAddWasDown = false;
static bool gNumpad9WasDown = false;

struct RepeatKeyState
{
	bool down = false;
	DWORD firstDownTick = 0;
	DWORD lastRepeatTick = 0;
};

static RepeatKeyState gRepeatNumpad1;
static RepeatKeyState gRepeatNumpad2;
static RepeatKeyState gRepeatNumpad4;
static RepeatKeyState gRepeatNumpad5;
static RepeatKeyState gRepeatNumpad7;
static RepeatKeyState gRepeatNumpad8;

static void ResetHuntRepeatKeys()
{
	gRepeatNumpad1 = RepeatKeyState();
	gRepeatNumpad2 = RepeatKeyState();
	gRepeatNumpad4 = RepeatKeyState();
	gRepeatNumpad5 = RepeatKeyState();
	gRepeatNumpad7 = RepeatKeyState();
	gRepeatNumpad8 = RepeatKeyState();
}

static bool KeyPressed(int vk, bool *wasDown)
{
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool pressed = down && !*wasDown;
	*wasDown = down;
	return pressed;
}

static bool KeyPressedOrRepeated(int vk, RepeatKeyState *state)
{
	if (!state)
		return false;

	const DWORD now = GetTickCount();
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	if (!down) {
		*state = RepeatKeyState();
		return false;
	}

	if (!state->down) {
		state->down = true;
		state->firstDownTick = now;
		state->lastRepeatTick = now;
		return true;
	}

	const DWORD initialDelayMs = 350;
	const DWORD repeatMs = 125;
	if (now - state->firstDownTick >= initialDelayMs &&
	    now - state->lastRepeatTick >= repeatMs) {
		state->lastRepeatTick = now;
		return true;
	}

	return false;
}

static DWORD WINAPI DX12ReloadThread(void*)
{
	const DWORD start = GetTickCount();
	DX12LogJsonFunc("DX12ModRuntimeReload", "\"status\":\"thread_begin\"");
	const bool ok = DX12ModRuntimeReload();
	const DWORD elapsed = GetTickCount() - start;
	if (ok) {
		wchar_t status[128];
		swprintf_s(status, L"Reload Success TimeConsume: %.3f S",
			static_cast<double>(elapsed) / 1000.0);
		DX12SetOverlayStatusTemporary(status, 3000);
	}
	DX12LogJsonFunc("DX12ModRuntimeReload",
		"\"status\":\"thread_done\",\"result\":\"%s\",\"elapsedMs\":%lu",
		ok ? "ok" : "failed", elapsed);
	InterlockedExchange(&gReloadInProgress, 0);
	return 0;
}

static void RequestDx12Reload()
{
	if (InterlockedCompareExchange(&gReloadInProgress, 1, 0) != 0) {
		DX12LogJsonFunc("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"already_running\"");
		DX12SetOverlayWarning(L"F10 reload already running");
		return;
	}

	DX12SetOverlayStatus(L"F10 reload running");
	HANDLE thread = CreateThread(nullptr, 0, DX12ReloadThread, nullptr, 0, nullptr);
	if (thread) {
		CloseHandle(thread);
		return;
	}

	InterlockedExchange(&gReloadInProgress, 0);
	DX12LogJsonFunc("DX12ModRuntimeReload",
		"\"status\":\"failed\",\"reason\":\"create_thread\",\"error\":%lu", GetLastError());
	DX12SetOverlayError(L"F10 reload failed: cannot start thread");
}

static void DX12PollInputCore()
{
	const bool f8Down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
	if (f8Down && !gF8WasDown) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"F8\",\"action\":\"FrameAnalysisRequest\"");
		if (DX12FrameAnalysisBegin()) {
			DX12FrameAnalysisLogJsonFunc("FrameAnalysisArmed", nullptr);
			DX12FrameAnalysisRequestCapture();
			DX12SetOverlayStatus(L"F8 frame analysis armed");
		} else {
			DX12LogJsonFunc("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
			DX12SetOverlayStatus(L"F8 dump failed: cannot create directory");
		}
	}
	gF8WasDown = f8Down;

	const bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
	if (f9Down && !gF9WasDown) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"F9\",\"action\":\"ShaderDumpRequest\"");
		DX12RequestShaderDump();
	}
	gF9WasDown = f9Down;

	const bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
	if (f10Down && !gF10WasDown) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"F10\",\"action\":\"ModRuntimeReload\"");
		RequestDx12Reload();
	}
	gF10WasDown = f10Down;

	const bool f11Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
	if (f11Down && !gF11WasDown) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"F11\",\"action\":\"ProfilingToggle\"");
		DX12Profiling::Toggle();
	}
	gF11WasDown = f11Down;

	const bool decimalDown = (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
	if (!decimalDown && KeyPressed(VK_NUMPAD0, &gNumpad0WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad0\",\"action\":\"HuntToggle\"");
		DX12HuntToggle();
		ResetHuntRepeatKeys();
	}

	if (!DX12HuntIsEnabled()) {
		ResetHuntRepeatKeys();
		return;
	}

	if (!decimalDown && KeyPressed(VK_ADD, &gNumpadAddWasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"NumpadAdd\",\"action\":\"HuntResetSelection\"");
		DX12HuntResetSelection();
	}
	if (!decimalDown && KeyPressed(VK_NUMPAD9, &gNumpad9WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad9\",\"action\":\"HuntCopySelectedHash\"");
		DX12HuntCopySelectedHash();
	}
	if (KeyPressedOrRepeated(VK_NUMPAD1, &gRepeatNumpad1)) {
		if (decimalDown) {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Decimal+Numpad1\",\"action\":\"HuntPreviousCS\"");
			DX12HuntPreviousCS();
		} else {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad1\",\"action\":\"HuntPreviousPS\"");
			DX12HuntPreviousPS();
		}
	}
	if (KeyPressedOrRepeated(VK_NUMPAD2, &gRepeatNumpad2)) {
		if (decimalDown) {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Decimal+Numpad2\",\"action\":\"HuntNextCS\"");
			DX12HuntNextCS();
		} else {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad2\",\"action\":\"HuntNextPS\"");
			DX12HuntNextPS();
		}
	}
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD4, &gRepeatNumpad4)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad4\",\"action\":\"HuntPreviousVS\"");
		DX12HuntPreviousVS();
	}
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD5, &gRepeatNumpad5)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad5\",\"action\":\"HuntNextVS\"");
		DX12HuntNextVS();
	}
	if (KeyPressedOrRepeated(VK_NUMPAD7, &gRepeatNumpad7)) {
		if (decimalDown) {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Decimal+Numpad7\",\"action\":\"HuntPreviousVB\"");
			DX12HuntPreviousVB();
		} else {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad7\",\"action\":\"HuntPreviousIB\"");
			DX12HuntPreviousIB();
		}
	}
	if (KeyPressedOrRepeated(VK_NUMPAD8, &gRepeatNumpad8)) {
		if (decimalDown) {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Decimal+Numpad8\",\"action\":\"HuntNextVB\"");
			DX12HuntNextVB();
		} else {
			DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad8\",\"action\":\"HuntNextIB\"");
			DX12HuntNextIB();
		}
	}
}

static DWORD WINAPI DX12InputThreadProc(void*)
{
	while (InterlockedCompareExchange(&gInputThreadStopRequested, 0, 0) == 0) {
		DX12PollInputCore();
		Sleep(50);
	}
	InterlockedExchange(&gInputThreadActive, 0);
	return 0;
}

void DX12StartInputThread()
{
	if (gInputThread)
		return;
	InterlockedExchange(&gInputThreadStopRequested, 0);
	InterlockedExchange(&gInputThreadActive, 1);
	gInputThread = CreateThread(nullptr, 0, DX12InputThreadProc, nullptr, 0, nullptr);
	if (!gInputThread) {
		InterlockedExchange(&gInputThreadActive, 0);
		DX12LogJsonFunc("DX12InputThread", "\"status\":\"failed\",\"error\":%lu", GetLastError());
	}
}

void DX12StopInputThread()
{
	InterlockedExchange(&gInputThreadStopRequested, 1);
	if (gInputThread) {
		WaitForSingleObject(gInputThread, 1000);
		CloseHandle(gInputThread);
		gInputThread = nullptr;
	}
	InterlockedExchange(&gInputThreadActive, 0);
}

void DX12PollInput()
{
	if (InterlockedCompareExchange(&gInputThreadActive, 0, 0) != 0)
		return;
	DX12PollInputCore();
}
