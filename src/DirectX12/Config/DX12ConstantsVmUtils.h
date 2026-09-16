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
	// Runs the [Constants] command list of a freshly loaded config and rebuilds
	// the persist registry from it. The user config (d3dx_user.ini) was loaded
	// last by MigotoIniLoader, so its assignments are part of `commandLists` and
	// land here after the ini defaults - that is how persisted values win.
	static void Initialize(const CommandListMap &commandLists)
	{
		std::vector<std::wstring> persistNames;
		CollectPersistDeclarations(commandLists, &persistNames);

		MigotoCommandVmUtils::ClearGlobals();
		if (const CommandListConfig *constants = FindConstants(commandLists)) {
			MigotoCommandVmUtils::State state;
			state.PushLocalScope();
			for (const CommandListAction &action : constants->actions)
				ApplyAction(&state, action);
			state.PopLocalScope();
		}

		MigotoCommandVmUtils::RegisterPersistedVariables(persistNames);
		// The registry now describes the live config and every value matches the
		// file that was just read, so nothing is pending until the next write
		// (DX11 clears user_config_dirty the same way after loading).
		MigotoCommandVmUtils::ClearPersistDirty();
	}

	static const CommandListConfig *FindConstants(const CommandListMap &commandLists)
	{
		for (const auto &item : commandLists) {
			if (IniDocumentText::ToLower(item.first) == L"constants")
				return &item.second;
		}
		return nullptr;
	}

	// Every variable declared as persistent, in declaration order, across all
	// parsed sections ([Constants] of the mod inis plus the same section of
	// d3dx_user.ini). Duplicates are left in the list; registration keeps the
	// first, matching DX11's "redeclaration keeps the original flags".
	static void CollectPersistDeclarations(
		const CommandListMap &commandLists, std::vector<std::wstring> *out)
	{
		if (!out)
			return;
		out->clear();
		for (const auto &item : commandLists) {
			for (const CommandListAction &action : item.second.actions) {
				if (action.kind != CommandListActionKind::VariableAssignment &&
				    action.kind != CommandListActionKind::PostVariableAssignment)
					continue;
				const std::wstring name = PersistentDeclarationName(
					action.key.empty() ? action.rawLine : action.key);
				if (name.empty())
					continue;
				if (!IsPersistDeclarationLine(
					    action.rawLine.empty() ? action.key : action.rawLine))
					continue;
				out->push_back(name);
			}
		}
	}

	// The name a persist declaration is written back under. The command-list
	// parser lower-cases every action key, so the declared spelling has to be
	// recovered from ExtractVariableName and collapsed onto the flat global the
	// VM actually uses: "$\LiuYin\swapkey0" and "$swapkey0" are one variable in
	// DX12 (NormalizeVariableName), and the name written to d3dx_user.ini must
	// resolve back to that same slot when the file is read again. Writing the
	// raw namespaced spelling would not: "$\TestMod\panel_x = 0.5" re-reads as
	// "$\testmod\panel_x", which normalizes to "$\testmod\panel_x" instead of
	// the "$panel_x" slot the mod and the [Key] bindings write.
	static std::wstring PersistentDeclarationName(const std::wstring &key)
	{
		const std::wstring extracted = MigotoCommandVmUtils::ExtractVariableName(key);
		const size_t lastSeparator = extracted.rfind(L'\\');
		if (lastSeparator == std::wstring::npos || lastSeparator + 1 >= extracted.size())
			return extracted;
		return L"$" + extracted.substr(lastSeparator + 1);
	}

	// Mirrors the "global persist $x" / "persist $x" prefixes DX11 accepts
	// (VariableFlagNames). "global " alone and "local " are not persistent.
	static bool IsPersistDeclarationLine(const std::wstring &line)
	{
		const std::wstring lower =
			IniDocumentText::ToLower(IniDocumentText::Trim(line));
		return lower.rfind(L"global persist $", 0) == 0 ||
			lower.rfind(L"persist $", 0) == 0;
	}

	// The subset of the command-list VM that [Constants] can contain: control
	// flow, variable assignment (deferred targets included), local declarations
	// and shader constants. Everything else in that section is ignored on
	// purpose - [Constants] is a declaration list, not a command list body.
	static void ApplyAction(
		MigotoCommandVmUtils::State *state, const CommandListAction &action)
	{
		if (!state)
			return;
		if (action.kind == CommandListActionKind::ControlFlow) {
			state->ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return;
		}
		if (!state->Active())
			return;

		switch (action.kind) {
		case CommandListActionKind::VariableAssignment:
		case CommandListActionKind::PostVariableAssignment:
			state->Assign(action.key, action.value);
			break;
		case CommandListActionKind::LocalVariableAssignment:
			state->DeclareLocal(action.key, action.value);
			break;
		case CommandListActionKind::SetShaderConstant:
			state->AssignSymbol(action.key, action.value);
			break;
		default:
			break;
		}
	}
};

} // namespace Bunny
