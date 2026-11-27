#pragma once

#include <windows.h>
#include <iterator>
#include <string_view>

namespace migoto {

struct InputWindowSource {
	HWND (WINAPI *foreground)() = GetForegroundWindow;
	DWORD (WINAPI *processId)() = GetCurrentProcessId;
};

inline bool ForegroundAllowsInput(bool checkWindow, std::wstring_view additionalTitle, InputWindowSource source = {})
{
	if (!checkWindow)
		return true;
	HWND window = source.foreground();
	DWORD owner = 0;
	if (!window || !GetWindowThreadProcessId(window, &owner))
		return false;
	if (owner == source.processId())
		return true;
	if (additionalTitle.empty())
		return false;
	wchar_t title[512];
	const int length = GetWindowTextW(window, title, static_cast<int>(std::size(title)));
	return length > 0 && additionalTitle == std::wstring_view(title, length);
}

} // namespace migoto
