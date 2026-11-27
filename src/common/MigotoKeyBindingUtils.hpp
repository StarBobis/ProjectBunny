#pragma once

#include <Windows.h>

#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#include "DirectX12/DX12Runtime.h"
#include "MigotoCommandVmUtils.hpp"
#include "MigotoInputCombination.h"
#include "IniKeyCycle.h"
#include "IniInputDelay.h"
#include "IniOverrideTransition.h"
#include <map>

namespace Bunny {

class MigotoKeyBindingUtils
{
public:
	enum class BindingType
	{
		Activate,
		Hold,
		Toggle,
		Cycle
	};

	class VariableValues
	{
	public:
		std::wstring name;
		std::wstring iniNamespace;
		std::vector<std::wstring> values;
		std::wstring target;
		std::optional<float> savedValue;
	};

	class Trigger
	{
	public:
		std::wstring text;
		bool reverse = false;
		migoto::InputCombination combination;
		bool wasDown = false;
		migoto::InputDelayState delayState;
	};

	struct TransitionSettings
	{
		std::vector<int> durations;
		std::vector<migoto::TransitionType> curves;

		size_t Count() const { return (std::max)(durations.size(), curves.size()); }
	};

	struct FrameState
	{
		std::map<std::wstring, migoto::OverrideTransitionValue> pending;
		std::map<std::wstring, migoto::OverrideSavedValue> saves;

		float Target(const std::wstring &name) const
		{
			const auto it = pending.find(name);
			if (it != pending.end() && it->second.time != -1)
				return it->second.target;
			return static_cast<float>(MigotoCommandVmUtils::LookupVariable({}, name));
		}

		void Update(ULONGLONG now)
		{
			for (auto it = pending.begin(); it != pending.end();) {
				auto &transition = it->second;
				MigotoCommandVmUtils::SetScopedGlobal({}, it->first, transition.Update(now));
				if (transition.time == -1)
					it = pending.erase(it);
				else
					++it;
			}
		}
	};

	class Binding
	{
	public:
		std::wstring section;
		std::wstring originalSection;
		std::wstring condition;
		std::wstring conditionNamespace;
		BindingType type = BindingType::Activate;
		std::vector<Trigger> triggers;
		std::vector<VariableValues> variables;
		std::vector<std::wstring> runCommandLists;
		bool active = false;
		migoto::KeyCycleCursor cycle;
		std::vector<std::wstring> cycleConditions;
		TransitionSettings press, release;
		migoto::InputDelays delays;
	};

	// Optional per-press diagnostic sink. Registered by the DX12 runtime in
	// diagnostic (TIMING) builds so that hotkey edges, condition gating and
	// the resulting variable state can be traced from d3d12_log.txt. A null
	// sink keeps production overhead at exactly zero.
	using KeyPressSink = void (*)(
		const wchar_t *section, const wchar_t *keyText, int type,
		const wchar_t *direction, const wchar_t *conditionText,
		int conditionActive, const wchar_t *variablesState);
	inline static KeyPressSink g_keyPressSink = nullptr;

	static void SetKeyPressSink(KeyPressSink sink)
	{
		g_keyPressSink = sink;
	}

	static void ParseSections(const IniDocument &ini, std::vector<Binding> *bindings,
		std::vector<std::wstring> *warnings = nullptr)
	{
		if (!bindings)
			return;
		bindings->clear();
		std::vector<const IniSection *> sections;
		for (const auto &section : ini.Sections()) {
			if (StartsWithI(section.name, L"Key"))
				sections.push_back(&section);
		}
		// DX11 registers listeners in case-insensitive section-name order.
		// A pre callback can change the condition of the next listener.
		std::sort(sections.begin(), sections.end(), [](const IniSection *a, const IniSection *b) {
			return _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
		});
		for (const auto *keySection : sections) {
			const auto &section = *keySection;
			Binding binding;
			binding.section = section.name;
			binding.originalSection = section.originalName.empty() ? section.name : section.originalName;
			std::wstring type;
			if (ini.GetString(section.name.c_str(), L"type", &type))
				binding.type = ParseBindingType(type);

			bool havePressDelay = false, haveReleaseDelay = false;
			for (const IniEntry &entry : section.entries) {
				if (!entry.hasEquals)
					continue;
				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
				std::wstring value = IniDocumentText::Trim(entry.value);
				if (key == L"delay" || key == L"release_delay") {
					const bool press = key == L"delay";
					bool &seen = press ? havePressDelay : haveReleaseDelay;
					if (!seen) {
						seen = true; // DX11 uses the first scalar setting in the section.
						const auto delay = MigotoCommandVmUtils::ReadStaticIniInteger(IniDocumentText::ToLower(value), entry.iniNamespace);
						if (!delay && warnings)
							warnings->push_back(section.name + L": invalid " + key + L": " + value);
						(press ? binding.delays.press : binding.delays.release) = delay.value_or(0);
					}
					continue;
				}
				if (key == L"transition" || key == L"release_transition" ||
					key == L"transition_type" || key == L"release_transition_type") {
					ReadTransitions(binding, key, value, entry.iniNamespace, warnings);
					continue;
				}
				if (key == L"condition") {
					binding.condition = IniDocumentText::ToLower(value);
					binding.conditionNamespace = entry.iniNamespace;
					if (binding.type == BindingType::Cycle) {
						binding.cycleConditions = SplitCommaValues(binding.condition);
						for (auto &condition : binding.cycleConditions) {
							if (!condition.empty() && !MigotoCommandVmUtils::ExpressionParser::ValidSyntax(condition)) {
								if (warnings)
									warnings->push_back(section.name + L": invalid cycle condition: " + condition);
								condition.clear();
							}
						}
					}
					continue;
				}
				if (binding.type == BindingType::Cycle && (key == L"wrap" || key == L"smart")) {
					const auto flag = MigotoCommandVmUtils::ReadStaticIniBoolean(IniDocumentText::ToLower(value), entry.iniNamespace);
					if (!flag && warnings)
						warnings->push_back(section.name + L": invalid " + key + L": " + value);
					(key == L"wrap" ? binding.cycle.wrap : binding.cycle.smart) = flag.value_or(true);
					continue;
				}
				if (key == L"key" || key == L"back") {
					const bool reverse = key == L"back";
					if (reverse && binding.type != BindingType::Cycle)
						continue;
					Trigger trigger{value, reverse};
					if (!trigger.combination.Parse(value)) {
						if (warnings)
							warnings->push_back(section.name + L": invalid " + key + L": " + value);
					} else if (!trigger.combination.Empty()) {
						binding.triggers.push_back(std::move(trigger));
					}
					continue;
				}
				if (key == L"run") {
					std::vector<std::wstring> values = SplitCommaValues(value);
					for (const std::wstring &item : values) {
						if (!item.empty()) {
							binding.runCommandLists.push_back(
								IniDocumentText::ResolveNamespacedSectionReference(
									item, L"CommandList", entry.iniNamespace));
						} else if (binding.type == BindingType::Cycle) {
							binding.runCommandLists.emplace_back();
						}
					}
					continue;
				}
				if (!key.empty() && key[0] == L'$') {
					VariableValues variable;
					variable.name = MigotoCommandVmUtils::ExtractVariableName(key);
					variable.iniNamespace = entry.iniNamespace;
					MigotoCommandVmUtils::ExpressionContext context;
					context.iniNamespace = variable.iniNamespace;
					if (MigotoCommandVmUtils::IsLockedGlobal(context, variable.name))
						continue;
					variable.target = MigotoCommandVmUtils::ResolveGlobalName(variable.name, variable.iniNamespace);
					variable.values = SplitCommaValues(value);
					if (!variable.name.empty())
						binding.variables.push_back(variable);
					continue;
				}
			}

			auto &triggers = binding.triggers;
			if (!triggers.empty()) {
				// DX11 registers all cycle-back callbacks before forward callbacks.
				if (binding.type == BindingType::Cycle)
					std::stable_partition(triggers.begin(), triggers.end(), [](const Trigger &t) { return t.reverse; });
				bindings->push_back(std::move(binding));
			}
		}
	}

	template <class ReadKey, class Run, class Clock = decltype(&GetTickCount64)>
	static void ProcessBindings(std::vector<Binding> &bindings, FrameState &state,
		ReadKey read, const migoto::XInputSnapshot &controllers, Run run, Clock clock = GetTickCount64)
	{
		std::vector<const std::wstring *> post;
		migoto::VirtualKeySnapshot keyboard;
		auto downKey = [&](int vk) {
			if (keyboard.Down(vk, read))
				return true;
			switch (vk) {
			case VK_CONTROL: return keyboard.Down(VK_LCONTROL, read) || keyboard.Down(VK_RCONTROL, read);
			case VK_SHIFT: return keyboard.Down(VK_LSHIFT, read) || keyboard.Down(VK_RSHIFT, read);
			case VK_MENU: return keyboard.Down(VK_LMENU, read) || keyboard.Down(VK_RMENU, read);
			default: return false;
			}
		};
		for (Binding &binding : bindings) {
			for (Trigger &trigger : binding.triggers) {
				const bool down = trigger.combination.Active(downKey, controllers);
				if (!trigger.delayState.EventDue(down, trigger.wasDown, binding.delays, clock))
					continue;
				if (!down && (binding.type != BindingType::Hold || !binding.active))
					continue;
				if (binding.type == BindingType::Cycle && !SelectCycleStep(binding, state, trigger.reverse))
					continue;
				const bool allowed = !down || ConditionActive(binding);
				if (allowed) {
					const bool releases = !down || (binding.type == BindingType::Toggle && binding.active);
					const bool saves = !binding.active && (binding.type == BindingType::Hold || binding.type == BindingType::Toggle);
					const size_t index = binding.type == BindingType::Cycle ? binding.cycle.index : 0;
					ScheduleVariables(binding, state, index, releases, saves, clock());
					binding.active = !releases;
					const auto &command = AtStep(binding.runCommandLists, index);
					if (!command.empty()) {
						run(command, releases);
						if (!releases && !saves)
							post.push_back(&command);
					}
				}
				if (down && g_keyPressSink)
					EmitKeyPressDiag(binding, trigger, allowed);
			}
		}
		if (!state.pending.empty())
			state.Update(clock());
		for (const auto *command : post)
			run(*command, true);
	}

	static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
	{
		return IniDocumentText::ToLower(value).rfind(IniDocumentText::ToLower(prefix), 0) == 0;
	}

	static BindingType ParseBindingType(const std::wstring &value)
	{
		std::wstring type = IniDocumentText::ToLower(IniDocumentText::Trim(value));
		if (type == L"hold")
			return BindingType::Hold;
		if (type == L"toggle")
			return BindingType::Toggle;
		if (type == L"cycle")
			return BindingType::Cycle;
		return BindingType::Activate;
	}

	static std::vector<std::wstring> SplitCommaValues(const std::wstring &value)
	{
		std::vector<std::wstring> values;
		size_t offset = 0;
		std::wstring_view field;
		while (migoto::NextKeyCycleField(std::wstring_view(value), offset, field))
			values.push_back(IniDocumentText::Trim(std::wstring(field)));
		return values;
	}

	static bool ConditionActive(const Binding &binding)
	{
		const auto &condition = CurrentCondition(binding);
		if (condition.empty())
			return true;
		MigotoCommandVmUtils::ExpressionContext context;
		context.iniNamespace = binding.conditionNamespace;
		const auto value = MigotoCommandVmUtils::EvaluateExpression(context, condition);
		return binding.type == BindingType::Cycle ? value != 0 : MigotoCommandVmUtils::Truthy(value);
	}

	static bool SelectCycleStep(Binding &binding, const FrameState &state, bool back)
	{
		const auto count = CycleValueCount(binding);
		if (binding.cycle.smart)
			binding.cycle.Resync(count, [&](size_t index) { return CycleStepMatches(binding, state, index); });
		return binding.cycle.Advance(count, back);
	}

	static const std::wstring &AtStep(const std::vector<std::wstring> &values, size_t index)
	{
		static const std::wstring empty;
		return values.empty() ? empty : values[(std::min)(index, values.size() - 1)];
	}

	static const std::wstring &CurrentCondition(const Binding &binding)
	{
		if (binding.type != BindingType::Cycle)
			return binding.condition;
		const auto index = binding.cycle.index == migoto::KeyCycleCursor::unset ? 0 : binding.cycle.index;
		return AtStep(binding.cycleConditions, index);
	}

	static void ReadTransitions(Binding &binding, const std::wstring &key, const std::wstring &text,
		const std::wstring &iniNamespace, std::vector<std::wstring> *warnings)
	{
		auto &settings = key.starts_with(L"release_") ? binding.release : binding.press;
		const bool curve = key.ends_with(L"_type");
		const bool cycle = binding.type == BindingType::Cycle;
		const auto values = cycle ? SplitCommaValues(text) : std::vector<std::wstring>{text};
		if (curve)
			settings.curves.clear();
		else
			settings.durations.clear();
		for (const auto &field : values) {
			const auto value = IniDocumentText::ToLower(field);
			bool valid = true;
			if (curve) {
				const bool cosine = value == L"cosine";
				valid = cosine || value.empty() || value == L"linear";
				settings.curves.push_back(cosine ? migoto::TransitionType::COSINE : migoto::TransitionType::LINEAR);
			} else {
				int duration = 0;
				if (cycle) {
					// DX11 cycle fields use %i and accept a numeric prefix.
					wchar_t *end = nullptr;
					const long parsed = std::wcstol(value.c_str(), &end, 0);
					valid = value.empty() || end != value.c_str();
					duration = static_cast<int>(parsed);
				} else if (!value.empty()) {
					const auto number = MigotoCommandVmUtils::ReadStaticIniInteger(value, iniNamespace);
					valid = number.has_value();
					duration = number.value_or(0);
				}
				settings.durations.push_back(duration);
			}
			if (!valid && warnings)
				warnings->push_back(binding.section + L": invalid " + key + L": " + field);
		}
	}

	static void ScheduleVariables(Binding &binding, FrameState &state, size_t index, bool release, bool save, ULONGLONG now)
	{
		const auto &settings = release ? binding.release : binding.press;
		const int duration = settings.durations.empty() ? 0 : settings.durations[(std::min)(index, settings.durations.size() - 1)];
		const auto curve = settings.curves.empty() ? migoto::TransitionType::LINEAR : settings.curves[(std::min)(index, settings.curves.size() - 1)];
		for (auto &variable : binding.variables) {
			const auto &expression = AtStep(variable.values, index);
			if (expression.empty() || MigotoCommandVmUtils::IsLockedGlobal({}, variable.target))
				continue;
			float target;
			if (release) {
				if (!variable.savedValue)
					continue;
				target = *variable.savedValue;
				variable.savedValue.reset();
				auto saved = state.saves.find(variable.target);
				if (saved != state.saves.end() && !saved->second.Restore(&target))
					state.saves.erase(saved);
			} else {
				if (save) {
					variable.savedValue = state.Target(variable.target);
					state.saves[variable.target].Save(*variable.savedValue);
				}
				MigotoCommandVmUtils::ExpressionContext context;
				context.iniNamespace = variable.iniNamespace;
				target = static_cast<float>(MigotoCommandVmUtils::EvaluateExpression(context, expression));
			}
			const float current = static_cast<float>(MigotoCommandVmUtils::LookupVariable({}, variable.target));
			state.pending[variable.target].Schedule(current, target, now, duration, curve);
		}
	}

	static size_t CycleValueCount(const Binding &binding)
	{
		size_t count = (std::max)({binding.cycleConditions.size(), binding.press.Count(), binding.release.Count()});
		for (const VariableValues &variable : binding.variables) {
			if (variable.values.size() > count)
				count = variable.values.size();
		}
		if (binding.runCommandLists.size() > count)
			count = binding.runCommandLists.size();
		return count;
	}

	static bool CycleStepMatches(const Binding &binding, const FrameState &state, size_t index)
	{
		MigotoCommandVmUtils::ExpressionContext context;
		for (const auto &variable : binding.variables) {
			const auto &expression = AtStep(variable.values, index);
			if (expression.empty())
				continue;
			context.iniNamespace = variable.iniNamespace;
			const auto expected = MigotoCommandVmUtils::EvaluateExpression(context, expression);
			if (state.Target(variable.target) != static_cast<float>(expected))
				return false;
		}
		return true;
	}

	static void EmitKeyPressDiag(const Binding &binding, const Trigger &trigger, bool conditionActive)
	{
		if (!g_keyPressSink)
			return;
		std::wostringstream variables;
		MigotoCommandVmUtils::ExpressionContext context;
		size_t written = 0;
		for (const VariableValues &variable : binding.variables) {
			if (variable.name.empty())
				continue;
			if (written)
				variables << L" ";
			context.iniNamespace = variable.iniNamespace;
			variables << variable.name << L"="
					  << MigotoCommandVmUtils::LookupVariable(context, variable.name);
			if (++written >= 32)
				break;
		}
		const std::wstring variablesState = variables.str();
		g_keyPressSink(
			binding.section.c_str(), trigger.text.c_str(),
			static_cast<int>(binding.type), trigger.reverse ? L"back" : L"forward",
			CurrentCondition(binding).c_str(), conditionActive ? 1 : 0,
			variablesState.c_str());
	}
};

} // namespace Bunny
