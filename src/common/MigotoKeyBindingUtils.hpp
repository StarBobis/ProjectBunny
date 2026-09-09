#pragma once

#include <Windows.h>

#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#include "DirectX12/DX12Runtime.h"
#include "MigotoCommandVmUtils.hpp"
#include "vkeys.h"

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
		// Optional reverse ("back =") key combination. Mirrors the DX11
		// semantics: it is only registered/used for type=cycle bindings,
		// where a press steps the cycle backwards.
		KeyCombination back;
		bool hasBack = false;
		std::vector<VariableValues> variables;
		std::vector<std::wstring> runCommandLists;
		bool wasDown = false;
		bool wasBackDown = false;
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
				// "back = <combination>" supplies the reverse cycle key.
				// It is stored unconditionally; ProcessBindings only makes
				// use of it for type=cycle (DX11 behaviour), and a binding
				// is kept if either combination resolved to a real key.
				if (key == L"back") {
					ParseKeyCombination(value, &binding.back);
					binding.hasBack = true;
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

			if (binding.key.primaryVk ||
			    (binding.hasBack && binding.back.primaryVk))
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

			const bool backDown = binding.hasBack && IsCombinationDown(binding.back);
			const bool backPressed = backDown && !binding.wasBackDown;
			binding.wasBackDown = backDown;

			switch (binding.type) {
			case BindingType::Cycle:
				// The forward ("key =") and reverse ("back =")
				// combinations both step the same cycle; reverse is only
				// honoured for cycle bindings (matches DX11, where "back"
				// keys are registered solely for KeyOverrideCycleBack).
				if (pressed && ConditionActive(binding))
					ActivateCycle(&binding, actions, 1);
				if (binding.hasBack && backPressed && ConditionActive(binding))
					ActivateCycle(&binding, actions, -1);
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

			// Diagnostic edge trace (TIMING builds only, sink may be null):
			// report forward/back press edges together with the condition
			// result and the post-action state of the binding's variables.
			// ConditionActive is a pure expression evaluation, so tracing it
			// here never changes the semantics of the cases above.
			if (g_keyPressSink && (pressed || backPressed)) {
				const bool conditionActive = ConditionActive(binding);
				EmitKeyPressDiag(binding,
					pressed ? L"forward" : L"back", conditionActive);
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
		// Legacy short alias not present in the shared VKMappings table:
		if (key == L"esc")
			return VK_ESCAPE;
		// Shared DX11/DX12 virtual key table (common/vkeys.h): full
		// VK_* names (OEM keys included), single characters, 0x-hex codes,
		// F1-F24, mouse/navigation aliases, etc.
		const int vk = ParseVKey(key.c_str());
		return vk < 0 ? 0 : vk;
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

	static void EmitKeyPressDiag(const Binding &binding, const wchar_t *direction, bool conditionActive)
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
			variables << variable.name << L"="
					  << MigotoCommandVmUtils::LookupVariable(context, variable.name);
			if (++written >= 32)
				break;
		}
		const std::wstring variablesState = variables.str();
		g_keyPressSink(
			binding.section.c_str(), binding.keyText.c_str(),
			static_cast<int>(binding.type), direction ? direction : L"",
			binding.condition.c_str(), conditionActive ? 1 : 0,
			variablesState.c_str());
	}
};

} // namespace Bunny
