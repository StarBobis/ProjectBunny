#pragma once

#include <windows.h>
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <tuple>

namespace migoto {

struct XInputSlot
{
	XINPUT_STATE state{};
	bool connected = false;
};

struct XInputSnapshot
{
	std::array<XInputSlot, XUSER_MAX_COUNT> slots{};
};

class XInputPoller
{
public:
	XInputSnapshot snapshot;

	template <class ReadState>
	void Update(std::uint64_t second, unsigned mask, ReadState read, bool probeAll = false)
	{
		for (DWORD i = 0; i < snapshot.slots.size(); ++i) {
			auto &slot = snapshot.slots[i];
			if (!(mask & (1u << i))) {
				slot.connected = false;
				continue;
			}
			// DX11 probes one absent port per second. Connected devices are
			// sampled on every dispatch, and a failed read releases their keys.
			if (!probeAll && !slot.connected && (second == lastSecond || second % XUSER_MAX_COUNT != i))
				continue;
			slot.connected = read(i, &slot.state) == ERROR_SUCCESS;
		}
		lastSecond = second;
	}

private:
	std::uint64_t lastSecond = 0;
};

using XInputReader = DWORD (WINAPI *)(DWORD, XINPUT_STATE *);

inline XInputReader GuideReader(HMODULE library)
{
	return library ? reinterpret_cast<XInputReader>(GetProcAddress(library, MAKEINTRESOURCEA(100))) : nullptr;
}

// The linked XInput 9.1.0 reader is always available. Only GUIDE needs
// the optional extended reader supplied by XInput 1.3 or 1.4.
class XInputGuideLibrary
{
public:
	XInputReader read = XInputGetState;

	template <class Report>
	void Enable(DWORD loadFlags, Report report)
	{
		if (module)
			return;
		module = LoadLibraryExW(L"xinput1_3.dll", nullptr, loadFlags);
		if (module)
			report("Loaded xinput1_3.dll for guide button support");
		else {
			module = LoadLibraryExW(L"xinput1_4.dll", nullptr, loadFlags);
			if (module)
				report("Loaded xinput1_4.dll for guide button support");
		}
		if (auto extended = GuideReader(module))
			read = extended;
		else
			report("Unable to load extended XInput reader: Guide button will not be available");
	}

	void Disable()
	{
		read = XInputGetState;
		if (module) {
			FreeLibrary(module);
			module = nullptr;
		}
	}

private:
	HMODULE module = nullptr;
};

struct XInputButtonBinding
{
	int controller = -1;
	WORD button = 0;
	BYTE leftTrigger = 0;
	BYTE rightTrigger = 0;
	bool inverted = false;

	static constexpr WORD guide = 0x0400;
	static constexpr unsigned allPorts = (1u << XUSER_MAX_COUNT) - 1;

	unsigned Ports() const { return controller < 0 ? allPorts : 1u << controller; }

	auto Fields() const { return std::tie(controller, button, leftTrigger, rightTrigger, inverted); }
	bool operator==(const XInputButtonBinding &other) const { return Fields() == other.Fields(); }
	bool operator<(const XInputButtonBinding &other) const { return Fields() < other.Fields(); }

	bool Down(const XInputSnapshot &pads) const
	{
		auto active = [this](const XInputSlot &slot) {
			if (!slot.connected)
				return false;
			const auto &pad = slot.state.Gamepad;
			const bool pressed = (pad.wButtons & button) ||
				(leftTrigger && pad.bLeftTrigger >= leftTrigger) ||
				(rightTrigger && pad.bRightTrigger >= rightTrigger);
			return pressed != inverted;
		};
		if (controller >= 0)
			return active(pads.slots[controller]);
		return std::any_of(pads.slots.begin(), pads.slots.end(), active);
	}

	// DX11 historically accepts suffixes after a trigger name and uses
	// decimal-prefix conversion. Keep its existing registration behavior.
	// Strict bindings reject leftover text so a malformed chord cannot fire.
	enum class Syntax { Strict, Legacy };
	bool Parse(const wchar_t *text, Syntax syntax = Syntax::Strict)
	{
		*this = {};
		if (!_wcsnicmp(text, L"no_", 3)) {
			inverted = true;
			text += 3;
		}
		if (_wcsnicmp(text, L"XB", 2))
			return false;
		text += 2;
		if (*text >= L'1' && *text <= L'4')
			controller = *text++ - L'1';
		if (*text++ != L'_')
			return false;
		for (const auto &named : buttons) {
			if (!_wcsicmp(text, named.first)) {
				button = named.second;
				return true;
			}
		}
		const size_t length = wcslen(text);
		const bool legacy = syntax == Syntax::Legacy;
		BYTE *trigger;
		if (length >= 12 && !_wcsnicmp(text, L"LEFT_TRIGGER", legacy ? 11 : 12)) {
			trigger = &leftTrigger;
			text += 12;
		} else if (length >= 13 && !_wcsnicmp(text, L"RIGHT_TRIGGER", legacy ? 12 : 13)) {
			trigger = &rightTrigger;
			text += 13;
		} else {
			return false;
		}
		while (*text == L' ')
			++text;
		long threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
		if (*text == L'>') {
			const wchar_t *number = text + 1;
			wchar_t *end = nullptr;
			errno = 0;
			threshold = std::wcstol(number, &end, 10);
			if (!legacy && (end == number || *end || errno == ERANGE))
				return false;
		} else if (*text && !legacy) {
			return false;
		}
		// Convert DX11's strict '>' to an inclusive byte threshold. At
		// saturation even '>255' can be reached by pressing the trigger fully.
		*trigger = threshold >= 254 ? 255 : static_cast<BYTE>(threshold + 1);
		return true;
	}

private:
	inline static constexpr std::pair<const wchar_t *, WORD> buttons[] = {
		{L"DPAD_UP", XINPUT_GAMEPAD_DPAD_UP}, {L"DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
		{L"DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {L"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
		{L"START", XINPUT_GAMEPAD_START}, {L"BACK", XINPUT_GAMEPAD_BACK},
		{L"LEFT_THUMB", XINPUT_GAMEPAD_LEFT_THUMB}, {L"RIGHT_THUMB", XINPUT_GAMEPAD_RIGHT_THUMB},
		{L"LEFT_SHOULDER", XINPUT_GAMEPAD_LEFT_SHOULDER}, {L"RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER},
		{L"A", XINPUT_GAMEPAD_A}, {L"B", XINPUT_GAMEPAD_B}, {L"X", XINPUT_GAMEPAD_X},
		{L"Y", XINPUT_GAMEPAD_Y}, {L"GUIDE", guide},
	};
};

} // namespace migoto
