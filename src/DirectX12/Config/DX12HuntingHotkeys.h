#pragma once

#include "DX12Config.h"
#include "MigotoInputCombination.h"
#include <algorithm>
#include <mutex>
#include <vector>

enum class DX12UiActionBits : LONG
{
	HuntToggle = 1 << 0,
	HuntPreviousVS = 1 << 1,
	HuntNextVS = 1 << 2,
	HuntPreviousPS = 1 << 3,
	HuntNextPS = 1 << 4,
	HuntPreviousCS = 1 << 5,
	HuntNextCS = 1 << 6,
	HuntPreviousIB = 1 << 7,
	HuntNextIB = 1 << 8,
	HuntPreviousVB = 1 << 9,
	HuntNextVB = 1 << 10,
	// Profiling HUD. Deliberately outside the hunting gate below: the HUD is
	// useful with hunting off, and switching it on must not enable the heavy
	// tracking that hunting implies.
	ProfHudCycle = 1 << 11,
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
	ProfHudFreeze = 1 << 16,
	HuntMarkVS = 1 << 17,
	HuntMarkPS = 1 << 18,
	HuntMarkCS = 1 << 19,
	HuntMarkIB = 1 << 20,
	HuntMarkVB = 1 << 21,
	WipeUserConfig = 1 << 22,
};

// Present polls these bindings even when hunting is off. Input edges survive
// reloading an unchanged binding. Newly assigned chords must first be released.
class DX12HuntingHotkeys
{
	struct BindingSpec
	{
		DX12UiActionBits action;
		const wchar_t *name;
		const wchar_t *fallback;
		bool repeatable;
	};

	using Action = DX12UiActionBits;
	inline static constexpr BindingSpec specs[] = {
		{ Action::HuntToggle, L"toggle_hunting", L"no_modifiers no_VK_DECIMAL VK_NUMPAD0", false },
		{ Action::HuntPreviousPS, L"previous_pixelshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD1", true },
		{ Action::HuntNextPS, L"next_pixelshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD2", true },
		{ Action::HuntMarkPS, L"mark_pixelshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD3", false },
		{ Action::HuntPreviousVS, L"previous_vertexshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD4", true },
		{ Action::HuntNextVS, L"next_vertexshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD5", true },
		{ Action::HuntMarkVS, L"mark_vertexshader", L"no_modifiers no_VK_DECIMAL VK_NUMPAD6", false },
		{ Action::HuntPreviousIB, L"previous_indexbuffer", L"no_modifiers no_VK_DECIMAL VK_NUMPAD7", true },
		{ Action::HuntNextIB, L"next_indexbuffer", L"no_modifiers no_VK_DECIMAL VK_NUMPAD8", true },
		{ Action::HuntMarkIB, L"mark_indexbuffer", L"no_modifiers no_VK_DECIMAL VK_NUMPAD9", false },
		{ Action::HuntPreviousVB, L"previous_vertexbuffer", L"no_modifiers no_VK_DECIMAL VK_DIVIDE", true },
		{ Action::HuntNextVB, L"next_vertexbuffer", L"no_modifiers no_VK_DECIMAL VK_MULTIPLY", true },
		{ Action::HuntMarkVB, L"mark_vertexbuffer", L"no_modifiers no_VK_DECIMAL VK_SUBTRACT", false },
		{ Action::HuntPreviousCS, L"previous_computeshader", L"no_modifiers VK_DECIMAL VK_NUMPAD1", true },
		{ Action::HuntNextCS, L"next_computeshader", L"no_modifiers VK_DECIMAL VK_NUMPAD2", true },
		{ Action::HuntMarkCS, L"mark_computeshader", L"no_modifiers VK_DECIMAL VK_NUMPAD3", false },
		{ Action::HuntReset, L"done_hunting", L"no_modifiers no_VK_DECIMAL VK_ADD", false },
		{ Action::FrameAnalysis, L"analyse_frame", L"no_modifiers no_VK_DECIMAL VK_F8", false },
		{ Action::ShaderDump, nullptr, L"no_modifiers no_VK_DECIMAL VK_F9", false },
		{ Action::ProfHudCycle, L"monitor_performance", L"no_modifiers no_VK_DECIMAL VK_F11", false },
		{ Action::ProfHudFreeze, L"freeze_performance_monitor", L"no_modifiers no_VK_DECIMAL VK_F12", false },
		{ Action::ModReload, L"reload_config", L"no_modifiers no_VK_DECIMAL VK_F10", false },
		{ Action::WipeUserConfig, L"wipe_user_config", L"", false },
	};

	class Chord
	{
	public:
		migoto::InputCombination combination;
		bool down = false;
		bool waitForRelease = false;
		ULONGLONG lastTick = 0;

		template <class ReadKey>
		bool Pressed(ReadKey read, const migoto::XInputSnapshot &pads, ULONGLONG now,
			bool repeatable, std::optional<int> configuredRate)
		{
			if (combination.Empty())
				return false;
			if (waitForRelease) {
				waitForRelease = combination.Active(read, pads);
				return false;
			}
			const int rate = repeatable ? configuredRate.value_or(6) : 0;
			const bool repeatDue = migoto::InputRepeatDue(lastTick, now, rate);
			// DX11 throttles both press and release for an explicit repeat rate.
			// Older DX12 configs delivered fresh presses immediately.
			if (configuredRate && rate && !repeatDue)
				return false;
			const bool active = combination.Active(read, pads);
			const bool edge = active != down;
			down = active;
			if (!edge && !(active && repeatDue))
				return false;
			if (active || configuredRate)
				lastTick = now;
			return active;
		}
	};

public:
	class Bindings
	{
		friend class DX12HuntingHotkeys;
		std::array<Chord, std::size(specs)> chords;
		std::optional<int> repeatRate;
		unsigned ports = 0;
		bool usesGuide = false;
	public:
		void IncludeDevices(const migoto::InputCombination &combination)
		{
			ports |= combination.Ports();
			usesGuide |= combination.UsesGuide();
		}
	};

	static Bindings Parse(const Bunny::IniDocument &ini, std::vector<std::wstring> &warnings)
	{
		Bindings bindings;
		std::wstring text;
		if (ini.GetString(L"Hunting", L"repeat_rate", &text)) {
			int rate;
			if (migoto::ReadIniIntegerLiteral(text.c_str(), rate))
				bindings.repeatRate = rate;
			else if (const auto value = MigotoCommandVmUtils::ReadStaticIniExpression(text)) {
				migoto::ConvertExpressionToInt(*value, rate);
				bindings.repeatRate = rate;
			} else {
				warnings.push_back(L"Invalid [Hunting] repeat_rate: " + text);
			}
		}
		for (size_t i = 0; i < std::size(specs); ++i) {
			const auto &spec = specs[i];
			if (!spec.name || !ini.GetString(L"Hunting", spec.name, &text))
				text = spec.fallback;
			auto &combination = bindings.chords[i].combination;
			if (!combination.Parse(text, spec.action == Action::WipeUserConfig) && spec.name)
				warnings.push_back(std::wstring(L"Invalid [Hunting] ") + spec.name + L": " + text);
			bindings.IncludeDevices(combination);
		}
		return bindings;
	}

	static bool KeyDown(int vk) { return GetAsyncKeyState(vk) < 0; }

	template <class ReadKey>
	void Install(Bindings bindings, ReadKey read, migoto::XInputReader reader = nullptr)
	{
		std::lock_guard lock(mutex);
		if (bindings.usesGuide && !reader)
			guide.Enable(LOAD_LIBRARY_SEARCH_SYSTEM32, [](const char *msg) { DX12_LOG("DX12Input", "%s", msg); });
		else
			guide.Disable();
		// Probe configured ports on installation, so binding a held controller
		// chord (especially wipe_user_config) cannot create a fresh press.
		controllerReader = reader ? reader : guide.read;
		controllers.Update(GetTickCount64() / 1000, bindings.ports, controllerReader, true);
		migoto::VirtualKeySnapshot snapshot;
		auto down = [&](int vk) { return snapshot.Down(vk, read); };
		for (size_t i = 0; i < bindings.chords.size(); ++i) {
			auto &updated = bindings.chords[i];
			auto &old = current.chords[i];
			if (updated.combination == old.combination)
				updated = std::move(old);
			else
				updated.waitForRelease = updated.combination.Active(down, controllers.snapshot);
		}
		current = std::move(bindings);
	}

	template <class ReadKey>
	LONG Poll(ReadKey read, ULONGLONG now, migoto::XInputSnapshot *sample = nullptr)
	{
		std::lock_guard lock(mutex);
		if (current.ports)
			controllers.Update(now / 1000, current.ports, controllerReader);
		if (sample)
			*sample = controllers.snapshot;
		migoto::VirtualKeySnapshot snapshot;
		auto down = [&](int vk) { return snapshot.Down(vk, read); };
		LONG actions = 0;
		for (size_t i = 0; i < current.chords.size(); ++i) {
			if (current.chords[i].Pressed(down, controllers.snapshot, now, specs[i].repeatable, current.repeatRate))
				actions |= static_cast<LONG>(specs[i].action);
		}
		return actions;
	}

	static DX12HuntingHotkeys &Instance()
	{
		static DX12HuntingHotkeys hotkeys;
		return hotkeys;
	}

private:
	DX12HuntingHotkeys()
	{
		for (size_t i = 0; i < current.chords.size(); ++i)
			current.chords[i].combination.Parse(specs[i].fallback, specs[i].action == Action::WipeUserConfig);
	}
	std::mutex mutex;
	Bindings current;
	migoto::XInputPoller controllers;
	migoto::XInputGuideLibrary guide;
	migoto::XInputReader controllerReader = XInputGetState;
};
