#pragma once

#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "IniDocument.hpp"
#include "MigotoIniCommandListUtils.hpp"

namespace Bunny {

enum class CommandListTargetKind
{
	Unknown,
	IndexBuffer,
	VertexBuffer,
	ConstantBuffer,
	ShaderResource,
	UnorderedAccessView
};

enum class CommandListShaderStage
{
	Unknown,
	Vertex,
	Pixel,
	Compute
};

class CommandListTarget
{
public:
	CommandListTargetKind kind = CommandListTargetKind::Unknown;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	uint32_t slot = 0;
};

enum class CommandListActionKind
{
	Run,
	CheckTextureOverride,
	HandlingSkip,
	SetIndexBuffer,
	SetVertexBuffer,
	Draw,
	DrawIndexed,
	DrawFromCaller,
	DrawIndexedFromCaller,
	Dispatch,
	ControlFlow,
	VariableAssignment,
	PostVariableAssignment,
	LocalVariableAssignment,
	ResourceAssignment,
	SetConstantBuffer,
	SetShaderResource,
	SetUnorderedAccessView,
	SetShader,
	SetShaderConstant,
	SetRenderState
};

class CommandListAction
{
public:
	CommandListActionKind kind = CommandListActionKind::Run;
	std::wstring commandList;
	CommandListTarget target;
	std::wstring resource;
	std::wstring key;
	std::wstring value;
	std::wstring rawLine;
	std::wstring iniNamespace;
	uint32_t args[5] = {};
	uint32_t argCount = 0;
};

class CommandListConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<CommandListAction> actions;
};

class CommandListLinks
{
public:
	std::vector<std::wstring> pre;
	std::vector<std::wstring> main;
	std::vector<std::wstring> post;
};

using CommandListMap = std::unordered_map<std::wstring, CommandListConfig>;

class MigotoCommandListParser
{
public:
	static bool ParseTarget(const std::wstring &text, CommandListTarget *target);
	static bool ParseActionFromRawLine(const std::wstring &rawLine, CommandListAction *action);
	static bool ParseActionFromEntry(
		const std::wstring &key, const std::wstring &value, CommandListAction *action);
	static void ParseLinksFromEntry(
		const std::wstring &key, const std::wstring &value, CommandListLinks *links);
	static void ResolveLinks(
		CommandListLinks *links, const std::wstring &iniNamespace);
	static void ResolveActionReferences(
		CommandListAction *action, const std::wstring &iniNamespace);
	static void ParseSections(const IniDocument &ini, CommandListMap *commandLists);
};

}

namespace Bunny {

class MigotoCommandListParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	return MigotoIniCommandListUtils::StartsWithI(value, prefix);
}

static bool EqualsI(const std::wstring &value, const wchar_t *text)
{
	return MigotoIniCommandListUtils::EqualsI(value, text);
}

	static bool IsExplicitCommandListSection(const std::wstring &section)
	{
		return MigotoIniCommandListUtils::IsExplicitCommandListSection(section);
	}

static bool ParseVertexBufferTarget(const std::wstring &text, uint32_t *slot)
{
	return MigotoIniCommandListUtils::ParseVertexBufferTarget(text, slot);
}

static bool ParseTargetSlot(const std::wstring &text, size_t start, uint32_t *slot)
{
	return MigotoIniCommandListUtils::ParseUnsignedSuffix(text, start, slot);
}

static bool ParseShaderStagePrefix(
	const std::wstring &text, size_t *prefixLength, CommandListShaderStage *stage)
{
	wchar_t parsedStage = 0;
	if (!MigotoIniCommandListUtils::ParseShaderStagePrefix(text, prefixLength, &parsedStage))
		return false;

	if (parsedStage == L'v') {
		*stage = CommandListShaderStage::Vertex;
		return true;
	}
	if (parsedStage == L'p') {
		*stage = CommandListShaderStage::Pixel;
		return true;
	}
	if (parsedStage == L'c') {
		*stage = CommandListShaderStage::Compute;
		return true;
	}
	return false;
}

static bool StartsWithTokenI(const std::wstring &value, const wchar_t *prefix)
{
	return MigotoIniCommandListUtils::StartsWithTokenI(value, prefix);
}

	static bool IsVariableAssignmentKey(const std::wstring &value)
	{
		return MigotoIniCommandListUtils::IsVariableAssignmentKey(value);
	}

	static bool IsVariableDeclarationKey(const std::wstring &value)
	{
		return MigotoIniCommandListUtils::IsVariableDeclarationKey(value);
	}

static bool IsPostVariableAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsPostVariableAssignmentKey(value);
}

static bool IsLocalVariableAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsLocalVariableAssignmentKey(value);
}

static bool IsResourceAssignmentKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsResourceAssignmentKey(value);
}

static bool IsShaderConstantKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsShaderConstantKey(value);
}

static bool IsRenderStateKey(const std::wstring &value)
{
	return MigotoIniCommandListUtils::IsRenderStateKey(value);
}

static void AppendRunList(const std::wstring &value, std::vector<std::wstring> *lists);
static std::wstring ResolveCommandListReference(
	const std::wstring &value, const std::wstring &iniNamespace);
static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace);
static bool ParseUInt(const std::wstring &text, uint32_t *value);
static bool ParseDrawArgs(
	const std::wstring &value, uint32_t expectedArgs, CommandListAction *action);
static void ResolveCommandListVector(
	std::vector<std::wstring> *lists, const std::wstring &iniNamespace);

};

inline bool MigotoCommandListParser::ParseTarget(const std::wstring &text, CommandListTarget *target)
{
	if (!target)
		return false;
	*target = CommandListTarget();

	std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(text));
	if (value == L"ib") {
		target->kind = CommandListTargetKind::IndexBuffer;
		return true;
	}

	uint32_t slot = 0;
	if (MigotoCommandListParserText::ParseVertexBufferTarget(value, &slot)) {
		target->kind = CommandListTargetKind::VertexBuffer;
		target->slot = slot;
		return true;
	}

	size_t prefixLength = 0;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	if (MigotoCommandListParserText::ParseShaderStagePrefix(value, &prefixLength, &stage)) {
		if (value.rfind(L"cb", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 2, &slot)) {
			target->kind = CommandListTargetKind::ConstantBuffer;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"t", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 1, &slot)) {
			target->kind = CommandListTargetKind::ShaderResource;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"u", prefixLength) == prefixLength &&
		    MigotoCommandListParserText::ParseTargetSlot(value, prefixLength + 1, &slot) &&
		    (stage == CommandListShaderStage::Pixel ||
		     stage == CommandListShaderStage::Compute)) {
			target->kind = CommandListTargetKind::UnorderedAccessView;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
	}
	return false;
}

inline void MigotoCommandListParserText::AppendRunList(const std::wstring &value, std::vector<std::wstring> *lists)
{
	if (!lists)
		return;
	std::wstring list = IniDocumentText::Trim(value);
	if (!list.empty())
		lists->push_back(list);
}

inline std::wstring MigotoCommandListParserText::ResolveCommandListReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	std::wstring ref = IniDocumentText::Trim(value);
	if (ref.empty())
		return L"";

	if (MigotoCommandListParserText::IsExplicitCommandListSection(ref))
		return IniDocumentText::MakeNamespacedSectionName(ref, iniNamespace);
	return IniDocumentText::ResolveNamespacedSectionReference(ref, L"CommandList", iniNamespace);
}

inline std::wstring MigotoCommandListParserText::ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return IniDocumentText::ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

inline bool MigotoCommandListParserText::ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

inline bool MigotoCommandListParserText::ParseDrawArgs(
	const std::wstring &value, uint32_t expectedArgs, CommandListAction *action)
{
	if (!action || expectedArgs > 5)
		return false;

	std::wstring text = value;
	size_t start = 0;
	uint32_t count = 0;
	while (start <= text.size()) {
		size_t comma = text.find(L',', start);
		std::wstring token = comma == std::wstring::npos ?
			text.substr(start) : text.substr(start, comma - start);
		if (count >= expectedArgs)
			return false;
		uint32_t parsed = 0;
		if (!MigotoCommandListParserText::ParseUInt(token, &parsed))
			return false;
		action->args[count++] = parsed;
		if (comma == std::wstring::npos)
			break;
		start = comma + 1;
	}

	if (count != expectedArgs)
		return false;
	action->argCount = count;
	return true;
}

inline bool MigotoCommandListParser::ParseActionFromRawLine(
	const std::wstring &rawLine, CommandListAction *action)
{
	if (!action)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(rawLine);
	std::wstring lower = IniDocumentText::ToLower(trimmed);
	if (lower.empty())
		return false;

	if (MigotoIniCommandListUtils::IsControlFlowRawLine(lower)) {
		action->kind = CommandListActionKind::ControlFlow;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	if (MigotoCommandListParserText::IsLocalVariableAssignmentKey(lower)) {
		action->kind = CommandListActionKind::LocalVariableAssignment;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	if (MigotoCommandListParserText::IsVariableDeclarationKey(lower)) {
		action->kind = CommandListActionKind::VariableAssignment;
		action->key = lower;
		action->rawLine = trimmed;
		return true;
	}

	return false;
}

inline bool MigotoCommandListParser::ParseActionFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListAction *action)
{
	if (!action)
		return false;

	action->key = key;
	action->value = value;

	if (key == L"run") {
		action->kind = CommandListActionKind::Run;
		action->commandList = value;
		return !action->commandList.empty();
	}

	if (key == L"checktextureoverride") {
		action->kind = CommandListActionKind::CheckTextureOverride;
		return MigotoCommandListParser::ParseTarget(value, &action->target);
	}

	if (key == L"handling" && IniDocumentText::ToLower(value) == L"skip") {
		action->kind = CommandListActionKind::HandlingSkip;
		return true;
	}

	if (key == L"draw") {
		if (IniDocumentText::ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::Draw;
		return MigotoCommandListParserText::ParseDrawArgs(value, 2, action);
	}

	if (key == L"drawindexed") {
		if (IniDocumentText::ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawIndexedFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::DrawIndexed;
		return MigotoCommandListParserText::ParseDrawArgs(value, 3, action);
	}

	if (key == L"dispatch") {
		action->kind = CommandListActionKind::Dispatch;
		return MigotoCommandListParserText::ParseDrawArgs(value, 3, action);
	}

	if (key == L"vs" || key == L"ps" || key == L"cs") {
		action->kind = CommandListActionKind::SetShader;
		action->resource = value;
		return !action->resource.empty();
	}

	if (MigotoCommandListParserText::IsPostVariableAssignmentKey(key)) {
		action->kind = CommandListActionKind::PostVariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsLocalVariableAssignmentKey(key)) {
		action->kind = CommandListActionKind::LocalVariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsVariableAssignmentKey(key) ||
		MigotoCommandListParserText::IsVariableDeclarationKey(key)) {
		action->kind = CommandListActionKind::VariableAssignment;
		return true;
	}

	if (MigotoCommandListParserText::IsResourceAssignmentKey(key)) {
		action->kind = CommandListActionKind::ResourceAssignment;
		action->resource = key;
		return !value.empty();
	}

	if (MigotoCommandListParserText::IsShaderConstantKey(key)) {
		action->kind = CommandListActionKind::SetShaderConstant;
		return true;
	}

	if (MigotoCommandListParserText::IsRenderStateKey(key)) {
		action->kind = CommandListActionKind::SetRenderState;
		return true;
	}

	CommandListTarget target;
	if (MigotoCommandListParser::ParseTarget(key, &target)) {
		switch (target.kind) {
		case CommandListTargetKind::IndexBuffer:
			action->kind = CommandListActionKind::SetIndexBuffer;
			break;
		case CommandListTargetKind::VertexBuffer:
			action->kind = CommandListActionKind::SetVertexBuffer;
			break;
		case CommandListTargetKind::ConstantBuffer:
			action->kind = CommandListActionKind::SetConstantBuffer;
			break;
		case CommandListTargetKind::ShaderResource:
			action->kind = CommandListActionKind::SetShaderResource;
			break;
		case CommandListTargetKind::UnorderedAccessView:
			action->kind = CommandListActionKind::SetUnorderedAccessView;
			break;
		default:
			return false;
		}
		action->target = target;
		action->resource = value;
		return !action->resource.empty();
	}
	return false;
}

inline void MigotoCommandListParser::ParseLinksFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListLinks *links)
{
	if (!links)
		return;

	std::wstring lower = IniDocumentText::ToLower(IniDocumentText::Trim(key));
	if (lower == L"run") {
		MigotoCommandListParserText::AppendRunList(value, &links->main);
		return;
	}
	if (lower == L"pre" || lower == L"pre run" || lower == L"prerun") {
		MigotoCommandListParserText::AppendRunList(value, &links->pre);
		return;
	}
	if (lower == L"post" || lower == L"post run" || lower == L"postrun") {
		MigotoCommandListParserText::AppendRunList(value, &links->post);
		return;
	}
}

inline void MigotoCommandListParserText::ResolveCommandListVector(
	std::vector<std::wstring> *lists, const std::wstring &iniNamespace)
{
	if (!lists)
		return;
	for (std::wstring &list : *lists)
		list = MigotoCommandListParserText::ResolveCommandListReference(list, iniNamespace);
}

inline void MigotoCommandListParser::ResolveLinks(
	CommandListLinks *links, const std::wstring &iniNamespace)
{
	if (!links)
		return;
	MigotoCommandListParserText::ResolveCommandListVector(&links->pre, iniNamespace);
	MigotoCommandListParserText::ResolveCommandListVector(&links->main, iniNamespace);
	MigotoCommandListParserText::ResolveCommandListVector(&links->post, iniNamespace);
}

inline void MigotoCommandListParser::ResolveActionReferences(
	CommandListAction *action, const std::wstring &iniNamespace)
{
	if (!action)
		return;
	action->iniNamespace = iniNamespace;
	if (action->kind == CommandListActionKind::Run)
		action->commandList = MigotoCommandListParserText::ResolveCommandListReference(action->commandList, iniNamespace);
	else if (action->kind == CommandListActionKind::SetIndexBuffer ||
	         action->kind == CommandListActionKind::SetVertexBuffer)
		action->resource = MigotoCommandListParserText::ResolveResourceReference(action->resource, iniNamespace);
}

inline void MigotoCommandListParser::ParseSections(const IniDocument &ini, CommandListMap *commandLists)
{
	if (!commandLists)
		return;

	commandLists->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoCommandListParserText::IsExplicitCommandListSection(section.name))
			continue;

		CommandListConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals)
					continue;

				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
				std::wstring value = IniDocumentText::Trim(entry.value);
			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}
		}

		(*commandLists)[config.section] = config;
		if (section.iniNamespace.empty())
			(*commandLists)[config.originalSection] = config;
	}
}

}
