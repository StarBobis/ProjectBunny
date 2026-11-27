#pragma once

#include "vkeys.h"

namespace migoto {

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
