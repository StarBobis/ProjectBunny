#pragma once

#include <Windows.h>

#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#include "DirectX12/DX12Runtime.h"
#include "MigotoCommandVmUtils.hpp"
#include "MigotoInputCombination.h"

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
		double savedValue = 0.0;
		bool hasSavedValue = false;
	};

	class Trigger
	{
	public:
		std::wstring text;
		bool reverse = false;
		migoto::InputCombination combination;
		bool wasDown = false;
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
		size_t cycleIndex = static_cast<size_t>(-1);
	};

	class RuntimeAction
	{
	public:
		std::wstring commandList;
		bool includePost = false;
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
		for (const IniSection &section : ini.Sections()) {
			if (!StartsWithI(section.name, L"Key"))
				continue;

			Binding binding;
			binding.section = section.name;
			binding.originalSection = section.originalName.empty() ? section.name : section.originalName;
			std::wstring type;
			if (ini.GetString(section.name.c_str(), L"type", &type))
				binding.type = ParseBindingType(type);

			for (const IniEntry &entry : section.entries) {
				if (!entry.hasEquals)
					continue;
				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
				std::wstring value = IniDocumentText::Trim(entry.value);
				if (key == L"condition") {
					binding.condition = IniDocumentText::ToLower(value);
					binding.conditionNamespace = entry.iniNamespace;
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

	template <class ReadKey>
	static void ProcessBindings(std::vector<Binding> *bindings, std::vector<RuntimeAction> *actions,
		ReadKey read, const migoto::XInputSnapshot &controllers)
	{
		if (!bindings || !actions)
			return;
		actions->clear();
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
		for (Binding &binding : *bindings) {
			for (Trigger &trigger : binding.triggers) {
				const bool down = trigger.combination.Active(downKey, controllers);
				if (down == trigger.wasDown)
					continue;
				trigger.wasDown = down;
				if (!down) {
					if (binding.type == BindingType::Hold && binding.active)
						DeactivateBinding(&binding, actions, true);
					continue;
				}
				const bool allowed = ConditionActive(binding);
				if (allowed)
					Press(binding, trigger.reverse, actions);
				if (g_keyPressSink)
					EmitKeyPressDiag(binding, trigger, allowed);
			}
		}
	}

	static void Press(Binding &binding, bool reverse, std::vector<RuntimeAction> *actions)
	{
		switch (binding.type) {
		case BindingType::Cycle:
			ActivateCycle(&binding, actions, reverse ? -1 : 1);
			break;
		case BindingType::Hold:
			// A second held trigger runs the activate action but must not replace
			// the value saved by the first. Either release deactivates the preset.
			ActivateBinding(&binding, actions, !binding.active, binding.active);
			break;
		case BindingType::Toggle:
			if (binding.active)
				DeactivateBinding(&binding, actions, true);
			else
				ActivateBinding(&binding, actions, true, false);
			break;
		case BindingType::Activate:
			ActivateBinding(&binding, actions, false, true);
			break;
		}
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
		size_t start = 0;
		for (;;) {
			size_t comma = value.find(L',', start);
			std::wstring item = comma == std::wstring::npos
				? value.substr(start)
				: value.substr(start, comma - start);
			values.push_back(IniDocumentText::Trim(item));
			if (comma == std::wstring::npos)
				break;
			start = comma + 1;
		}
		return values;
	}

	static bool ConditionActive(const Binding &binding)
	{
		if (binding.condition.empty())
			return true;
		MigotoCommandVmUtils::ExpressionContext context;
		context.iniNamespace = binding.conditionNamespace;
		return MigotoCommandVmUtils::Truthy(
			MigotoCommandVmUtils::EvaluateExpression(context, binding.condition));
	}

	static void ActivateBinding(
		Binding *binding, std::vector<RuntimeAction> *actions,
		bool saveVariables, bool includePost)
	{
		if (!binding || !actions)
			return;
		if (saveVariables)
			SaveVariables(binding);
		ApplyVariables(binding, 0);
		AppendRunAction(*binding, 0, includePost, actions);
		binding->active = true;
	}

	static void DeactivateBinding(
		Binding *binding, std::vector<RuntimeAction> *actions, bool includePost)
	{
		if (!binding || !actions)
			return;
		RestoreVariables(binding);
		AppendRunAction(*binding, CurrentCycleIndex(*binding), includePost, actions);
		binding->active = false;
	}

	// Steps the cycle by one. direction > 0 walks forward (the "key ="
	// combination), direction < 0 walks backward (the "back =" combination).
	// Stepping is wrap-around and re-syncs to the current variable values
	// first, which matches the DX11 defaults (smart=true, wrap=true).
	static void ActivateCycle(
		Binding *binding, std::vector<RuntimeAction> *actions, int direction)
	{
		if (!binding || !actions)
			return;
		const size_t count = CycleValueCount(*binding);
		if (!count)
			return;
		const size_t matched = FindCurrentCycleIndex(*binding, count);
		size_t next;
		if (direction > 0) {
			next = matched == static_cast<size_t>(-1)
				? ((binding->cycleIndex == static_cast<size_t>(-1)) ? 0 : ((binding->cycleIndex + 1) % count))
				: ((matched + 1) % count);
		} else {
			next = matched == static_cast<size_t>(-1)
				? ((binding->cycleIndex == static_cast<size_t>(-1)) ? count - 1 : ((binding->cycleIndex + count - 1) % count))
				: ((matched + count - 1) % count);
		}
		binding->cycleIndex = next;
		ApplyVariables(binding, next);
		AppendRunAction(*binding, next, true, actions);
	}

	static void SaveVariables(Binding *binding)
	{
		if (!binding)
			return;
		MigotoCommandVmUtils::ExpressionContext context;
		for (VariableValues &variable : binding->variables) {
			context.iniNamespace = variable.iniNamespace;
			variable.savedValue = MigotoCommandVmUtils::LookupVariable(context, variable.name);
			variable.hasSavedValue = true;
		}
	}

	static void RestoreVariables(Binding *binding)
	{
		if (!binding)
			return;
		for (VariableValues &variable : binding->variables) {
			if (!variable.hasSavedValue)
				continue;
			MigotoCommandVmUtils::ExpressionContext context;
			context.iniNamespace = variable.iniNamespace;
			MigotoCommandVmUtils::SetScopedGlobal(context, variable.name, variable.savedValue);
			variable.hasSavedValue = false;
		}
	}

	static void ApplyVariables(Binding *binding, size_t index)
	{
		if (!binding)
			return;
		MigotoCommandVmUtils::ExpressionContext context;
		for (VariableValues &variable : binding->variables) {
			if (index >= variable.values.size() || variable.values[index].empty())
				continue;
			context.iniNamespace = variable.iniNamespace;
			if (MigotoCommandVmUtils::IsLockedGlobal(context, variable.name))
				continue;
			double value = MigotoCommandVmUtils::EvaluateExpression(context, variable.values[index]);
			MigotoCommandVmUtils::SetScopedGlobal(context, variable.name, value);
		}
	}

	static size_t CycleValueCount(const Binding &binding)
	{
		size_t count = 0;
		for (const VariableValues &variable : binding.variables) {
			if (variable.values.size() > count)
				count = variable.values.size();
		}
		if (binding.runCommandLists.size() > count)
			count = binding.runCommandLists.size();
		return count;
	}

	static size_t FindCurrentCycleIndex(const Binding &binding, size_t count)
	{
		MigotoCommandVmUtils::ExpressionContext context;
		for (size_t i = 0; i < count; ++i) {
			bool anyCompared = false;
			bool allMatch = true;
			for (const VariableValues &variable : binding.variables) {
				if (i >= variable.values.size() || variable.values[i].empty())
					continue;
				context.iniNamespace = variable.iniNamespace;
				anyCompared = true;
				double current = MigotoCommandVmUtils::LookupVariable(context, variable.name);
				double expected = MigotoCommandVmUtils::EvaluateExpression(context, variable.values[i]);
				if (std::fabs(current - expected) > 0.000001) {
					allMatch = false;
					break;
				}
			}
			if (anyCompared && allMatch)
				return i;
		}
		return static_cast<size_t>(-1);
	}

	static size_t CurrentCycleIndex(const Binding &binding)
	{
		return binding.cycleIndex == static_cast<size_t>(-1) ? 0 : binding.cycleIndex;
	}

	static void AppendRunAction(
		const Binding &binding, size_t index, bool includePost,
		std::vector<RuntimeAction> *actions)
	{
		if (!actions || binding.runCommandLists.empty())
			return;
		size_t runIndex = binding.runCommandLists.size() == 1 ? 0 : index;
		if (runIndex >= binding.runCommandLists.size() || binding.runCommandLists[runIndex].empty())
			return;
		RuntimeAction action;
		action.commandList = binding.runCommandLists[runIndex];
		action.includePost = includePost;
		actions->push_back(action);
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
			binding.condition.c_str(), conditionActive ? 1 : 0,
			variablesState.c_str());
	}
};

} // namespace Bunny
