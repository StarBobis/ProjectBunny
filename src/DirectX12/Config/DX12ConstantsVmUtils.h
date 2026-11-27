#pragma once

#include "DX12Config.h"

#include "MigotoCommandVmUtils.hpp"

// [Constants] replay and the persist registry that backs d3dx_user.ini.
//
// Kept free of D3D12/runtime dependencies (it only touches the parsed command
// list model and the VM variable store) for two reasons:
//   - the offline persistence test in dev_tests can drive the exact code the
//     runtime drives instead of a copy of it, and
//   - the save path, the loader and the test all agree on what a "persist"
//     declaration is.
//
// DX11 reference: CommandList.cpp's CommandListVariable/VariableFlags::PERSIST
// plus ParseConstantsSection() and SavePersistentSettings() in IniHandler.cpp.

namespace Bunny {

class DX12ConstantsVmUtils
{
public:
	using Vm = MigotoCommandVmUtils;

	static Vm::SettingsLoadResult Initialize(const CommandListMap &commandLists, const std::wstring &userConfigPath = {},
		const IniDocument *reloadConfig = nullptr, const Vm::SettingsReloadSnapshot &reload = {},
		Vm::ShortcutPermission shortcuts = Vm::AllowShortcuts)
	{
		const auto policy = reloadConfig ? ReadCleanupPolicy(*reloadConfig) : Vm::UnknownSettingsPolicy::Observe;
		Vm::GlobalVariables globals;
		globals.shortcuts = shortcuts;
		globals.savePath = userConfigPath;
		globals.saveOwner = migoto::NormalizePersistentConfigPath(userConfigPath);
		if (const auto *constants = FindConstants(commandLists)) {
			Vm::State state;
			state.initializing = &globals;
			state.PushLocalScope();
			for (const auto &action : constants->actions) {
				if (action.kind == CommandListActionKind::VariableAssignment)
					RegisterDeclaration(state, action);
			}
			std::wstring_view previousNamespace;
			bool fromUserConfig = false;
			for (const auto &action : constants->actions) {
				if (action.iniNamespace != previousNamespace) {
					previousNamespace = action.iniNamespace;
					fromUserConfig = !globals.saveOwner.empty() && !previousNamespace.empty() &&
						migoto::NormalizePersistentConfigPath(action.iniNamespace) == globals.saveOwner;
				}
				ApplyAction(state, action, fromUserConfig);
			}
			state.FlushPostAssignments();
		}
		if (reloadConfig) {
			for (const auto &[name, value] : globals.savedUnknowns) {
				const auto bracket = name.find(L'[');
				// DX12 has no pool registry yet. A declared pool might own this
				// slot even when its variable has not been instantiated.
				if (bracket != std::wstring::npos && reloadConfig->FindSection(name.substr(1, bracket - 1).c_str()))
					continue;
				migoto::RecordUnknownSetting(globals.confirmedUnknowns, name, value);
			}
		}
		return Vm::PublishGlobals(std::move(globals), policy, reload);
	}

	static const CommandListConfig *FindConstants(const CommandListMap &commandLists)
	{
		for (const auto &item : commandLists) {
			if (IniDocumentText::ToLower(item.first) == L"constants")
				return &item.second;
		}
		return nullptr;
	}

private:
	static Vm::UnknownSettingsPolicy ReadCleanupPolicy(const IniDocument &ini)
	{
		std::wstring text;
		bool clear = true;
		if (ini.GetString(L"System", L"clear_unknown_settings", &text)) {
			text = IniDocumentText::ToLower(IniDocumentText::Trim(text));
			clear = Vm::ReadStaticIniBoolean(text).value_or(true);
		}
		return clear ? Vm::UnknownSettingsPolicy::ClearUnchanged : Vm::UnknownSettingsPolicy::Preserve;
	}

	static void RegisterDeclaration(Vm::State &state, const CommandListAction &action)
	{
		const auto declaration = migoto::VariableDeclaration::Parse(action.key);
		if (!declaration.IsDx12Declaration())
			return;

		const auto name = declaration.name;
		const bool qualified = name.substr(0, 2) == L"$\\";
		if (qualified) {
			const auto separator = name.rfind(L'\\');
			if (separator <= 2 || migoto::VariableTokenEnd(name, 1) != name.size() ||
				!migoto::ValidVariableName(L"$" + std::wstring(name.substr(separator + 1))))
				return;
		} else if (!migoto::ValidVariableName(name)) {
			return;
		}
		const auto key = qualified || action.iniNamespace.empty() ? std::wstring(name) :
			migoto::NamespacedVariable(name, action.iniNamespace);
		auto &globals = *state.initializing;
		if (globals.values.count(key))
			return;

		float value = 0;
		if (!action.value.empty()) {
			auto context = state.Context(action.iniNamespace);
			context.requireDeclaredVariables = true;
			Vm::ExpressionParser parser(action.value, context);
			value = parser.Parse();
			if (!parser.ok)
				return;
		}
		globals.values.emplace(key, value);
		if (declaration.flags & VariableFlags::LOCKED) {
			globals.locked.insert(key);
		} else if (declaration.flags & VariableFlags::PERSIST) {
			globals.persistentKeys.insert(key);
			globals.persistentNames.push_back(key);
		}
	}

	// Older DX12 saves wrote a single unqualified value for every mod sharing
	// that leaf. Seed each matching mutable declaration until the first new save.
	static bool RestoreLegacyValue(Vm::State &state, const CommandListAction &action)
	{
		const auto &key = action.key;
		if (key.size() < 2 || key[0] != L'$' || key[1] == L'\\')
			return false;
		auto &globals = *state.initializing;
		const std::wstring_view leaf = std::wstring_view(key).substr(1);
		std::optional<double> value;
		for (const auto &name : globals.persistentNames) {
			const auto separator = name.rfind(L'\\');
			if (separator == std::wstring::npos || std::wstring_view(name).substr(separator + 1) != leaf)
				continue;
			if (!value)
				value = state.Evaluate(action.value, action.iniNamespace);
			globals.values.at(name) = *value;
		}
		if (value)
			return true;
		for (const auto &name : globals.locked) {
			const auto separator = name.rfind(L'\\');
			if (separator != std::wstring::npos && std::wstring_view(name).substr(separator + 1) == leaf)
				return true;
		}
		return false;
	}

	// Preserve only literal settings owned by this user_config. Neither an absent
	// declaration nor an active non-persistent variable is a writable save target.
	static void RestoreSavedValue(Vm::State &state, const CommandListAction &action)
	{
		auto &globals = *state.initializing;
		const auto found = Vm::FindGlobal(globals, action.key, action.iniNamespace);
		if (found != globals.values.end() && globals.locked.count(found->first))
			return;
		if (found != globals.values.end() && globals.persistentKeys.count(found->first)) {
			state.Assign(action.key, action.value, action.iniNamespace);
			return;
		}
		if (found == globals.values.end() && RestoreLegacyValue(state, action))
			return;
		auto name = action.key;
		float value;
		if (migoto::NormalizeSavedVariableName(name) &&
			migoto::ReadSavedLiteral(IniDocumentText::ToLower(action.value), value))
			globals.savedUnknowns[name] = value;
	}

	static void ApplyAction(Vm::State &state, const CommandListAction &action, bool fromUserConfig)
	{
		const auto &ns = action.iniNamespace;
		if (action.kind == CommandListActionKind::ControlFlow) {
			state.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine, ns);
			return;
		}
		if (!state.Active())
			return;
		switch (action.kind) {
		case CommandListActionKind::VariableAssignment:
			if (migoto::VariableDeclaration::Parse(action.key).IsDx12Declaration())
				break;
			if (fromUserConfig)
				RestoreSavedValue(state, action);
			else
				state.Assign(action.key, action.value, ns);
			break;
		case CommandListActionKind::PostVariableAssignment:
			state.QueuePostAssign(action.key, action.value, ns);
			break;
		case CommandListActionKind::LocalVariableAssignment:
			state.DeclareLocal(action.key, action.value, ns);
			break;
		case CommandListActionKind::SetShaderConstant:
			state.AssignSymbol(action.key, action.value, ns);
			break;
		default:
			break;
		}
	}
};

} // namespace Bunny
