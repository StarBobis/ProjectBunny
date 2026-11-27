#pragma once

#include "vkeys.h"
#include <array>
#include <cstdint>

namespace migoto {

// One high-bit observation per virtual key per dispatch. Uncached readers
// still see the backend's own input source (including DX11's test reader).
class VirtualKeySnapshot
{
public:
	template <class ReadDown>
	bool Down(int vk, ReadDown read)
	{
		if (static_cast<unsigned>(vk) >= states.size())
			return read(vk);
		auto &state = states[vk];
		if (!state)
			state = read(vk) ? 1 : 2;
		return state == 1;
	}

private:
	std::array<unsigned char, 256> states{};
};

// DX11 uses whole milliseconds and schedules from the last delivered event.
inline bool InputRepeatDue(std::uint64_t last, std::uint64_t now, int rate)
{
	return rate != 0 && now >= last + (1000 / rate);
}

inline int ParseKeyboardButton(const wchar_t *token, bool &inverted)
{
	inverted = !_wcsnicmp(token, L"no_", 3);
	return ParseVKey(token + (inverted ? 3 : 0));
}

// Keep the DX11 space-delimited grammar. The backend resolves each token as
// a keyboard or controller button and rejects the whole chord on failure.
template <class AddButton>
bool VisitInputChord(const wchar_t *text, AddButton add)
{
	if (!*text)
		return false;
	while (*text) {
		while (*text == L' ')
			++text;
		const wchar_t *start = text;
		while (*text && *text != L' ')
			++text;
		const std::wstring token(start, text);
		if (!_wcsicmp(token.c_str(), L"no_modifiers")) {
			for (const auto *key : { L"NO_CTRL", L"NO_ALT", L"NO_SHIFT", L"NO_LWIN", L"NO_RWIN" }) {
				if (!add(key))
					return false;
			}
		} else if (!add(token.c_str())) {
			return false;
		}
		if (*text)
			++text;
	}
	return true;
}

} // namespace migoto
