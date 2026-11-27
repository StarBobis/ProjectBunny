#pragma once

#include "IniForegroundWindow.h"

namespace migoto::tests {

// Use a real, hidden Win32 window without changing the user's foreground window.
// Only the foreground handle and the identity of the observing process are supplied.
class InputTestWindow {
	inline static thread_local HWND window = nullptr;
	static HWND WINAPI Foreground() { ++queries; return window; }
	static HWND WINAPI NoWindow() { ++queries; return nullptr; }
	static DWORD WINAPI OtherProcess() { return 0; }
public:
	inline static thread_local unsigned queries = 0;
	InputTestWindow()
	{
		window = CreateWindowExW(0, L"STATIC", L"Bunny Input Audit", WS_POPUP,
			0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		queries = 0;
	}
	~InputTestWindow() { if (window) DestroyWindow(window); window = nullptr; }
	InputTestWindow(const InputTestWindow &) = delete;
	InputTestWindow &operator=(const InputTestWindow &) = delete;
	bool Valid() const { return window != nullptr; }
	bool Rename(const wchar_t *title) { return SetWindowTextW(window, title) != FALSE; }
	static InputWindowSource Own() { return {Foreground, GetCurrentProcessId}; }
	static InputWindowSource Foreign() { return {Foreground, OtherProcess}; }
	static InputWindowSource Absent() { return {NoWindow, GetCurrentProcessId}; }
};

} // namespace migoto::tests
