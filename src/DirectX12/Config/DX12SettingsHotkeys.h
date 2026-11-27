#pragma once

#include "DX12Config.h"
#include "IniInputChord.h"
#include <algorithm>
#include <mutex>
#include <vector>

// Present polls these bindings even when hunting is off. Input edges survive
// reloading an unchanged binding. Newly assigned chords must first be released.
class DX12SettingsHotkeys
{
public:
	enum class Action { None, Reload, Wipe };

	class Chord
	{
	public:
		std::vector<std::pair<int, bool>> buttons;
		bool down = false;

		bool Parse(const std::wstring &text, bool requirePress)
		{
			buttons.clear();
			if (text.empty())
				return true;
			auto add = [this](const wchar_t *token) {
				bool inverted;
				const int key = migoto::ParseKeyboardButton(token, inverted);
				if (key < 0)
					return false;
				buttons.emplace_back(key, inverted);
				return true;
			};
			// DX11 tries the entire name first for aliases like "Prnt Scrn".
			if ((!add(text.c_str()) && !migoto::VisitInputChord(text.c_str(), add)) ||
				(requirePress && std::none_of(buttons.begin(), buttons.end(),
					[](const auto &button) { return !button.second; }))) {
				buttons.clear();
				return false;
			}
			return true;
		}

		template <class ReadKey>
		bool Active(ReadKey read) const
		{
			return !buttons.empty() && std::all_of(buttons.begin(), buttons.end(),
				[&](const auto &button) { return read(button.first) != button.second; });
		}

		template <class ReadKey>
		bool Pressed(ReadKey read)
		{
			const bool active = Active(read);
			const bool pressed = active && !down;
			down = active;
			return pressed;
		}
	};

	class Bindings
	{
	public:
		Chord reload;
		Chord wipe;
	};

	static Bindings Parse(const Bunny::IniDocument &ini, std::vector<std::wstring> &warnings)
	{
		Bindings bindings;
		std::wstring reload, wipe;
		if (!ini.GetString(L"Hunting", L"reload_config", &reload))
			reload = legacyReload;
		ini.GetString(L"Hunting", L"wipe_user_config", &wipe);
		if (!bindings.reload.Parse(reload, false))
			warnings.push_back(L"Invalid [Hunting] reload_config: " + reload);
		if (!bindings.wipe.Parse(wipe, true))
			warnings.push_back(L"Invalid [Hunting] wipe_user_config: " + wipe);
		return bindings;
	}

	static bool KeyDown(int vk) { return GetAsyncKeyState(vk) < 0; }

	template <class ReadKey>
	void Install(Bindings bindings, ReadKey read)
	{
		std::lock_guard lock(mutex);
		for (auto member : { &Bindings::reload, &Bindings::wipe }) {
			auto &updated = bindings.*member;
			const auto &old = current.*member;
			updated.down = updated.buttons == old.buttons ? old.down : updated.Active(read);
		}
		current = std::move(bindings);
	}

	template <class ReadKey>
	Action Poll(ReadKey read)
	{
		std::lock_guard lock(mutex);
		const bool reload = current.reload.Pressed(read);
		if (current.wipe.Pressed(read))
			return Action::Wipe;
		return reload ? Action::Reload : Action::None;
	}

	static DX12SettingsHotkeys &Instance()
	{
		static DX12SettingsHotkeys hotkeys;
		return hotkeys;
	}

private:
	static constexpr auto legacyReload = L"no_modifiers no_decimal VK_F10";
	DX12SettingsHotkeys() { current.reload.Parse(legacyReload, false); }
	std::mutex mutex;
	Bindings current;
};
