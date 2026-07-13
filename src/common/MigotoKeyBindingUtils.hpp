#pragma once

#include <Windows.h>

#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

#include "DirectX12/Presentation/DX12UiRuntime.hpp"
#include "MigotoCommandVmUtils.hpp"

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

	class KeyCombination
	{
	public:
		int primaryVk = 0;
		bool requireCtrl = false;
		bool requireShift = false;
		bool requireAlt = false;
		bool forbidCtrl = false;
		bool forbidShift = false;
		bool forbidAlt = false;
	};

	class VariableValues
	{
	public:
		std::wstring name;
		std::vector<std::wstring> values;
		double savedValue = 0.0;
		bool hasSavedValue = false;
	};

	class Binding
	{
	public:
		std::wstring section;
		std::wstring originalSection;
		std::wstring condition;
		std::wstring keyText;
		BindingType type = BindingType::Activate;
		KeyCombination key;
		std::vector<VariableValues> variables;
		std::vector<std::wstring> runCommandLists;
		bool wasDown = false;
		bool active = false;
		size_t cycleIndex = static_cast<size_t>(-1);
	};

	class RuntimeAction
	{
	public:
		std::wstring commandList;
		bool includePost = false;
	};

	static void ParseSections(const IniDocument &ini, std::vector<Binding> *bindings)
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

			for (const IniEntry &entry : section.entries) {
				if (!entry.hasEquals)
					continue;
				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
				std::wstring value = IniDocumentText::Trim(entry.value);
				if (key == L"condition") {
					binding.condition = IniDocumentText::ToLower(value);
					continue;
				}
				if (key == L"key") {
					binding.keyText = value;
					ParseKeyCombination(value, &binding.key);
					continue;
				}
				if (key == L"type") {
					binding.type = ParseBindingType(value);
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
					variable.values = SplitCommaValues(value);
					if (!variable.name.empty())
						binding.variables.push_back(variable);
					continue;
				}
			}

			if (binding.key.primaryVk)
				bindings->push_back(binding);
		}
	}

	static void ProcessBindings(std::vector<Binding> *bindings, std::vector<RuntimeAction> *actions)
	{
		if (!bindings || !actions)
			return;
		actions->clear();
		for (Binding &binding : *bindings) {
			const bool down = IsCombinationDown(binding.key);
			const bool pressed = down && !binding.wasDown;
			const bool released = !down && binding.wasDown;
			binding.wasDown = down;

			switch (binding.type) {
			case BindingType::Cycle:
				if (pressed && ConditionActive(binding))
					ActivateCycle(&binding, actions);
				break;
			case BindingType::Hold:
				if (pressed && ConditionActive(binding))
					ActivateBinding(&binding, actions, true, false);
				if (released && binding.active)
					DeactivateBinding(&binding, actions, true);
				break;
			case BindingType::Toggle:
				if (pressed && ConditionActive(binding)) {
					if (binding.active)
						DeactivateBinding(&binding, actions, true);
					else
						ActivateBinding(&binding, actions, true, false);
				}
				break;
			case BindingType::Activate:
			default:
				if (pressed && ConditionActive(binding))
					ActivateBinding(&binding, actions, false, true);
				break;
			}
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

	static void ParseKeyCombination(const std::wstring &value, KeyCombination *combo)
	{
		if (!combo)
			return;
		*combo = KeyCombination();
		size_t pos = 0;
		while (pos < value.size()) {
			while (pos < value.size() && IsSpace(value[pos]))
				++pos;
			size_t start = pos;
			while (pos < value.size() && !IsSpace(value[pos]))
				++pos;
			if (start == pos)
				continue;
			ApplyKeyToken(value.substr(start, pos - start), combo);
		}
	}

	static void ApplyKeyToken(const std::wstring &token, KeyCombination *combo)
	{
		std::wstring lower = IniDocumentText::ToLower(IniDocumentText::Trim(token));
		if (lower.empty() || !combo)
			return;
		if (lower == L"ctrl" || lower == L"control") {
			combo->requireCtrl = true;
			combo->forbidCtrl = false;
			return;
		}
		if (lower == L"shift") {
			combo->requireShift = true;
			combo->forbidShift = false;
			return;
		}
		if (lower == L"alt") {
			combo->requireAlt = true;
			combo->forbidAlt = false;
			return;
		}
		if (lower == L"no_ctrl" || lower == L"no_control") {
			combo->forbidCtrl = true;
			combo->requireCtrl = false;
			return;
		}
		if (lower == L"no_shift") {
			combo->forbidShift = true;
			combo->requireShift = false;
			return;
		}
		if (lower == L"no_alt") {
			combo->forbidAlt = true;
			combo->requireAlt = false;
			return;
		}
		if (lower == L"no_modifiers") {
			combo->forbidCtrl = true;
			combo->forbidShift = true;
			combo->forbidAlt = true;
			combo->requireCtrl = false;
			combo->requireShift = false;
			combo->requireAlt = false;
			return;
		}
		int vk = ParseVirtualKey(lower);
		if (vk)
			combo->primaryVk = vk;
	}

	static int ParseVirtualKey(const std::wstring &token)
	{
		std::wstring key = token;
		if (key.rfind(L"vk_", 0) == 0)
			key = key.substr(3);
		if (key.size() == 1) {
			wchar_t ch = key[0];
			if (ch >= L'a' && ch <= L'z')
				return static_cast<int>(std::towupper(ch));
			if (ch >= L'0' && ch <= L'9')
				return static_cast<int>(ch);
			if (ch == L'/')
				return VK_OEM_2;
			if (ch == L'\\')
				return VK_OEM_5;
			if (ch == L'.')
				return VK_OEM_PERIOD;
			if (ch == L',')
				return VK_OEM_COMMA;
			if (ch == L';')
				return VK_OEM_1;
			if (ch == L'\'')
				return VK_OEM_7;
			if (ch == L'[')
				return VK_OEM_4;
			if (ch == L']')
				return VK_OEM_6;
			if (ch == L'-')
				return VK_OEM_MINUS;
			if (ch == L'=')
				return VK_OEM_PLUS;
			if (ch == L'`')
				return VK_OEM_3;
		}

		if (key.size() >= 2 && key[0] == L'f') {
			int number = ParsePositiveInt(key.substr(1));
			if (number >= 1 && number <= 24)
				return VK_F1 + number - 1;
		}

		if (key == L"lbutton")
			return VK_LBUTTON;
		if (key == L"rbutton")
			return VK_RBUTTON;
		if (key == L"mbutton")
			return VK_MBUTTON;
		if (key == L"up")
			return VK_UP;
		if (key == L"down")
			return VK_DOWN;
		if (key == L"left")
			return VK_LEFT;
		if (key == L"right")
			return VK_RIGHT;
		if (key == L"space")
			return VK_SPACE;
		if (key == L"return" || key == L"enter")
			return VK_RETURN;
		if (key == L"escape" || key == L"esc")
			return VK_ESCAPE;
		if (key == L"tab")
			return VK_TAB;
		if (key == L"back")
			return VK_BACK;
		if (key == L"home")
			return VK_HOME;
		if (key == L"end")
			return VK_END;
		if (key == L"prior" || key == L"pageup")
			return VK_PRIOR;
		if (key == L"next" || key == L"pagedown")
			return VK_NEXT;
		if (key == L"insert")
			return VK_INSERT;
		if (key == L"delete")
			return VK_DELETE;
		return 0;
	}

	static bool IsCombinationDown(const KeyCombination &combo)
	{
		if (!combo.primaryVk)
			return false;
		const bool ctrl = KeyDown(VK_CONTROL) || KeyDown(VK_LCONTROL) || KeyDown(VK_RCONTROL);
		const bool shift = KeyDown(VK_SHIFT) || KeyDown(VK_LSHIFT) || KeyDown(VK_RSHIFT);
		const bool alt = KeyDown(VK_MENU) || KeyDown(VK_LMENU) || KeyDown(VK_RMENU);
		if (combo.requireCtrl && !ctrl)
			return false;
		if (combo.requireShift && !shift)
			return false;
		if (combo.requireAlt && !alt)
			return false;
		if (combo.forbidCtrl && ctrl)
			return false;
		if (combo.forbidShift && shift)
			return false;
		if (combo.forbidAlt && alt)
			return false;
		return KeyDown(combo.primaryVk);
	}

	static bool KeyDown(int vk)
	{
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	static bool ConditionActive(const Binding &binding)
	{
		if (binding.condition.empty())
			return true;
		MigotoCommandVmUtils::ExpressionContext context;
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

	static void ActivateCycle(Binding *binding, std::vector<RuntimeAction> *actions)
	{
		if (!binding || !actions)
			return;
		const size_t count = CycleValueCount(*binding);
		if (!count)
			return;
		const size_t matched = FindCurrentCycleIndex(*binding, count);
		const size_t next = matched == static_cast<size_t>(-1)
			? ((binding->cycleIndex == static_cast<size_t>(-1)) ? 0 : ((binding->cycleIndex + 1) % count))
			: ((matched + 1) % count);
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
			MigotoCommandVmUtils::SetGlobal(variable.name, variable.savedValue);
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
			double value = MigotoCommandVmUtils::EvaluateExpression(context, variable.values[index]);
			MigotoCommandVmUtils::SetGlobal(variable.name, value);
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

	static bool IsSpace(wchar_t ch)
	{
		return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
	}

	static int ParsePositiveInt(const std::wstring &text)
	{
		if (text.empty())
			return 0;
		int value = 0;
		for (wchar_t ch : text) {
			if (ch < L'0' || ch > L'9')
				return 0;
			value = value * 10 + static_cast<int>(ch - L'0');
		}
		return value;
	}
};

} // namespace Bunny
