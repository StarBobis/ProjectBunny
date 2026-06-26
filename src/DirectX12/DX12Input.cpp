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
static bool gNumpad0WasDown = false;
static bool gNumpadAddWasDown = false;
static bool gNumpad3WasDown = false;
static bool gNumpad6WasDown = false;
static bool gNumpad9WasDown = false;
static bool gNumpadSubtractWasDown = false;

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
static RepeatKeyState gRepeatNumpadDivide;
static RepeatKeyState gRepeatNumpadMultiply;
static RepeatKeyState gRepeatNumpad1d; // Decimal+1 CS prev
static RepeatKeyState gRepeatNumpad2d; // Decimal+2 CS next

static void ResetHuntRepeatKeys()
{
	gRepeatNumpad1 = RepeatKeyState();
	gRepeatNumpad2 = RepeatKeyState();
	gRepeatNumpad4 = RepeatKeyState();
	gRepeatNumpad5 = RepeatKeyState();
	gRepeatNumpad7 = RepeatKeyState();
	gRepeatNumpad8 = RepeatKeyState();
	gRepeatNumpadDivide = RepeatKeyState();
	gRepeatNumpadMultiply = RepeatKeyState();
	gRepeatNumpad1d = RepeatKeyState();
	gRepeatNumpad2d = RepeatKeyState();
	gNumpad3WasDown = false;
	gNumpad6WasDown = false;
	gNumpad9WasDown = false;
	gNumpadSubtractWasDown = false;
}

static bool KeyPressed(int vk, bool *wasDown)
{
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool pressed = down && !*wasDown;
	*wasDown = down;
	return pressed;
}

static bool KeyPressedOrRepeated(int vk, RepeatKeyState *state, DWORD now)
{
	if (!state)
		return false;

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
	const DWORD repeatMs = 85;
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

void DX12PollInput()
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
	}

	if (!DX12HuntIsEnabled()) {
		ResetHuntRepeatKeys();
		return;
	}

	// === 快捷键布局 ===
	// PS:  1/2 翻找, 3 复制
	// VS:  4/5 翻找, 6 复制
	// IB:  7/8 翻找, 9 复制
	// VB:  / * 翻找, - 复制
	// CS:  Del+1 翻找, Del+2 翻找, Del+3 复制
	// Num0  toggle, Num+  reset

	if (!decimalDown && KeyPressed(VK_ADD, &gNumpadAddWasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"NumpadAdd\",\"action\":\"HuntResetSelection\"");
		DX12HuntResetSelection();
	}
	if (!decimalDown && KeyPressed(VK_NUMPAD3, &gNumpad3WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad3\",\"action\":\"HuntCopyPS\"");
		DX12HuntCopyPS();
	}
	if (!decimalDown && KeyPressed(VK_NUMPAD6, &gNumpad6WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad6\",\"action\":\"HuntCopyVS\"");
		DX12HuntCopyVS();
	}
	if (!decimalDown && KeyPressed(VK_NUMPAD9, &gNumpad9WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Numpad9\",\"action\":\"HuntCopyIB\"");
		DX12HuntCopyIB();
	}
	if (!decimalDown && KeyPressed(VK_SUBTRACT, &gNumpadSubtractWasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"NumpadSubtract\",\"action\":\"HuntCopyVB\"");
		DX12HuntCopyVB();
	}
	if (decimalDown && KeyPressed(VK_NUMPAD3, &gNumpad3WasDown)) {
		DX12LogJsonFunc("InputHotkey", "\"key\":\"Decimal+Numpad3\",\"action\":\"HuntCopyCS\"");
		DX12HuntCopyCS();
	}
	const DWORD repeatNow = GetTickCount();
	// PS: 1/2 prev/next (no decimal)
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD1, &gRepeatNumpad1, repeatNow))
		DX12HuntPreviousPS();
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD2, &gRepeatNumpad2, repeatNow))
		DX12HuntNextPS();
	// VS: 4/5 prev/next
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD4, &gRepeatNumpad4, repeatNow))
		DX12HuntPreviousVS();
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD5, &gRepeatNumpad5, repeatNow))
		DX12HuntNextVS();
	// IB: 7/8 prev/next
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD7, &gRepeatNumpad7, repeatNow))
		DX12HuntPreviousIB();
	if (!decimalDown && KeyPressedOrRepeated(VK_NUMPAD8, &gRepeatNumpad8, repeatNow))
		DX12HuntNextIB();
	// CS: Decimal+1/2 prev/next
	if (decimalDown && KeyPressedOrRepeated(VK_NUMPAD1, &gRepeatNumpad1d, repeatNow))
		DX12HuntPreviousCS();
	if (decimalDown && KeyPressedOrRepeated(VK_NUMPAD2, &gRepeatNumpad2d, repeatNow))
		DX12HuntNextCS();
	// VB: / and * prev/next
	if (KeyPressedOrRepeated(VK_DIVIDE, &gRepeatNumpadDivide, repeatNow))
		DX12HuntPreviousVB();
	if (KeyPressedOrRepeated(VK_MULTIPLY, &gRepeatNumpadMultiply, repeatNow))
		DX12HuntNextVB();
}
