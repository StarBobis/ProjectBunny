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
		migoto::KeyCycleCursor cycle;
		std::vector<std::wstring> cycleConditions;
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
				if (binding.type == BindingType::Cycle && !SelectCycleStep(binding, trigger.reverse))
					continue;
				const bool allowed = ConditionActive(binding);
				if (allowed)
					Press(binding, actions);
				if (g_keyPressSink)
					EmitKeyPressDiag(binding, trigger, allowed);
			}
		}
	}

	static void Press(Binding &binding, std::vector<RuntimeAction> *actions)
	{
		switch (binding.type) {
		case BindingType::Cycle:
			ApplyVariables(&binding, binding.cycle.index);
			AppendRunAction(binding, binding.cycle.index, true, actions);
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
		AppendRunAction(*binding, 0, includePost, actions);
		binding->active = false;
	}

	static bool SelectCycleStep(Binding &binding, bool back)
	{
		const auto count = CycleValueCount(binding);
		if (binding.cycle.smart)
			binding.cycle.Resync(count, [&](size_t index) { return CycleStepMatches(binding, index); });
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
			const auto &expression = AtStep(variable.values, index);
			if (expression.empty())
				continue;
			context.iniNamespace = variable.iniNamespace;
			if (MigotoCommandVmUtils::IsLockedGlobal(context, variable.name))
				continue;
			double value = MigotoCommandVmUtils::EvaluateExpression(context, expression);
			MigotoCommandVmUtils::SetScopedGlobal(context, variable.name, value);
		}
	}

	static size_t CycleValueCount(const Binding &binding)
	{
		size_t count = binding.cycleConditions.size();
		for (const VariableValues &variable : binding.variables) {
			if (variable.values.size() > count)
				count = variable.values.size();
		}
		if (binding.runCommandLists.size() > count)
			count = binding.runCommandLists.size();
		return count;
	}

	static bool CycleStepMatches(const Binding &binding, size_t index)
	{
		MigotoCommandVmUtils::ExpressionContext context;
		for (const auto &variable : binding.variables) {
			const auto &expression = AtStep(variable.values, index);
			if (expression.empty())
				continue;
			context.iniNamespace = variable.iniNamespace;
			const auto expected = MigotoCommandVmUtils::EvaluateExpression(context, expression);
			if (static_cast<float>(MigotoCommandVmUtils::LookupVariable(context, variable.name)) != static_cast<float>(expected))
				return false;
		}
		return true;
	}

	static void AppendRunAction(
		const Binding &binding, size_t index, bool includePost,
		std::vector<RuntimeAction> *actions)
	{
		const auto &command = AtStep(binding.runCommandLists, index);
		if (!actions || command.empty())
			return;
		RuntimeAction action;
		action.commandList = command;
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
			CurrentCondition(binding).c_str(), conditionActive ? 1 : 0,
			variablesState.c_str());
	}
};

} // namespace Bunny
