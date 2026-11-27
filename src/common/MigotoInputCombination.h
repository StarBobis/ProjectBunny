#pragma once

#include "IniInputChord.h"
#include "IniXInput.h"
#include <cwctype>
#include <vector>

namespace migoto {

class InputCombination
{
public:
	bool Empty() const { return keys.empty() && pads.empty(); }
	bool operator==(const InputCombination &rhs) const { return keys == rhs.keys && pads == rhs.pads; }

	unsigned Ports() const
	{
		unsigned mask = 0;
		for (const auto &button : pads)
			mask |= button.Ports();
		return mask;
	}

	bool UsesGuide() const
	{
		return std::any_of(pads.begin(), pads.end(), [](const auto &pad) {
			return pad.button == XInputButtonBinding::guide;
		});
	}

	template <class ReadKey>
	bool Active(ReadKey read, const XInputSnapshot &controllers) const
	{
		return !Empty() && std::all_of(keys.begin(), keys.end(),
			[&](const auto &key) { return read(key.first) != key.second; }) &&
			std::all_of(pads.begin(), pads.end(), [&](const auto &pad) { return pad.Down(controllers); });
	}

	bool Parse(const std::wstring &text, bool requirePress = false)
	{
		keys.clear();
		pads.clear();
		if (text.empty())
			return true;
		auto add = [this](const wchar_t *token) {
			bool inverted;
			int vk = ParseKeyboardButton(token, inverted);
			// DX12's existing [Key] aliases include ESC, VK_A and VK_0x41.
			const wchar_t *name = inverted ? token + 3 : token;
			if (vk < 0 && !_wcsnicmp(name, L"VK_", 3)) {
				name += 3;
				vk = ParseVKey(name);
			}
			if (vk < 0 && !_wcsicmp(name, L"esc"))
				vk = VK_ESCAPE;
			if (vk >= 0) {
				keys.emplace_back(vk, inverted);
				return true;
			}
			XInputButtonBinding pad;
			if (!pad.Parse(token))
				return false;
			pads.push_back(pad);
			return true;
		};
		// Single names such as "Prnt Scrn" take priority over chord tokens.
		if (!add(text.c_str()) && !VisitInputChord(NormalizeSeparators(text).c_str(), add)) {
			keys.clear();
			pads.clear();
			return false;
		}
		if (requirePress && std::none_of(keys.begin(), keys.end(), [](const auto &k) { return !k.second; }) &&
			std::none_of(pads.begin(), pads.end(), [](const auto &p) { return !p.inverted; })) {
			keys.clear();
			pads.clear();
			return false;
		}
		std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) {
			return a.second != b.second ? a.second < b.second : a.first < b.first;
		});
		keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
		std::sort(pads.begin(), pads.end());
		pads.erase(std::unique(pads.begin(), pads.end()), pads.end());
		return true;
	}

private:
	std::vector<std::pair<int, bool>> keys;
	std::vector<XInputButtonBinding> pads;

	// A trigger comparison is one button within a mixed chord, even if the
	// user put spaces on both sides of '>'. Other device tokens stay separate.
	static std::wstring NormalizeSeparators(const std::wstring &text)
	{
		std::wstring result;
		bool space = false;
		for (const auto c : text) {
			if (std::iswspace(c)) {
				space = true;
				continue;
			}
			if (space && !result.empty() && result.back() != L'>' && c != L'>')
				result += L' ';
			result += c;
			space = false;
		}
		return result;
	}
};

} // namespace migoto
