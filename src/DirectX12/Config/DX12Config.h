#pragma once

#include "Core/DX12Diagnostics.h"
#include "MigotoIniCommandListUtils.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Configuration data models and parsers shared by the DX12 runtime.
// This file is intentionally independent of the hook implementations.

namespace Bunny {




class IniEntry {
public:
	std::wstring key;
	std::wstring value;
	std::wstring rawLine;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	int lineNumber = 0;
	bool hasEquals = false;
};

class IniSection {
public:
	std::wstring name;
	std::wstring originalName;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<IniEntry> entries;
};

class IniDocument {
public:
	bool LoadFromFile(const wchar_t *path);
	bool Parse(const wchar_t *text);
	bool AppendParse(
		const wchar_t *text,
		const std::wstring &sourcePath,
		const std::wstring &sourceDir,
		const std::wstring &iniNamespace);

	const IniSection *FindSection(const wchar_t *section) const;
	const std::vector<IniSection> &Sections() const { return mSections; }

	bool GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const;
	bool GetBool(const wchar_t *section, const wchar_t *key, bool *value) const;
	bool GetInt(const wchar_t *section, const wchar_t *key, int *value) const;

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	std::vector<IniSection> mSections;
	std::map<std::wstring, size_t> mSectionIndex;
	std::wstring mPath;
	std::wstring mError;
};

class IniDocumentText
{
public:
	static std::wstring ToLower(std::wstring value);
	static std::wstring Trim(const std::wstring &value);
	static const wchar_t *MigotoSectionPrefix(const std::wstring &section);
	static std::wstring MakeNamespacedSectionName(
		const std::wstring &section, const std::wstring &iniNamespace);
	static std::wstring ResolveNamespacedSectionReference(
		const std::wstring &reference,
		const std::wstring &prefix,
		const std::wstring &iniNamespace);
};




inline std::wstring IniDocumentText::ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	return value;
}

inline std::wstring IniDocumentText::Trim(const std::wstring &value)
{
	const wchar_t *spaces = L" \t\r\n";
	size_t first = value.find_first_not_of(spaces);
	if (first == std::wstring::npos)
		return L"";
	size_t last = value.find_last_not_of(spaces);
	return value.substr(first, last - first + 1);
}

class IniDocumentParser
{
public:
static bool IsCommentOrEmpty(const std::wstring &line)
{
	std::wstring trimmed = IniDocumentText::Trim(line);
	return trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#';
}

class SectionPrefixEntry {
public:
	const wchar_t *prefix;
	bool isPrefix;
};

inline static const SectionPrefixEntry kMigotoCommandListSections[] = {
	{L"TextureOverride", true},
	{L"CommandList", true},
	{L"Constants", false},
	{L"Present", false},
	{L"ShaderOverride", true},
	{L"CustomShader", true},
	{L"ShaderRegex", true},
	{L"BuiltInCommandList", true},
	{L"BuiltInCustomShader", true},
	{L"ClearRenderTargetView", false},
	{L"ClearDepthStencilView", false},
	{L"ClearUnorderedAccessViewUint", false},
	{L"ClearUnorderedAccessViewFloat", false},
};

inline static const SectionPrefixEntry kMigotoRegularSections[] = {
	{L"Pool", true},
	{L"Resource", true},
	{L"Key", true},
	{L"Include", true},
	{L"Preset", true},
	{L"Hunting", false},
	{L"Logging", false},
	{L"System", false},
	{L"Device", false},
	{L"Rendering", false},
	{L"Loader", false},
	{L"Profile", false},
	{L"Stereo", false},
	{L"ConvergenceMap", false},
};

static const wchar_t *SectionPrefixFromList(
	const std::wstring &section, const SectionPrefixEntry *entries, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		const SectionPrefixEntry &entry = entries[i];
		std::wstring prefix = entry.prefix;
		if (entry.isPrefix) {
			if (IniDocumentText::ToLower(section).rfind(IniDocumentText::ToLower(prefix), 0) == 0)
				return entry.prefix;
		} else if (IniDocumentText::ToLower(section) == IniDocumentText::ToLower(prefix)) {
			return entry.prefix;
		}
	}
	return nullptr;
}
};

inline const wchar_t *IniDocumentText::MigotoSectionPrefix(const std::wstring &section)
{
	const wchar_t *prefix = IniDocumentParser::SectionPrefixFromList(
		section, IniDocumentParser::kMigotoCommandListSections,
		sizeof(IniDocumentParser::kMigotoCommandListSections) / sizeof(IniDocumentParser::kMigotoCommandListSections[0]));
	if (prefix)
		return prefix;
	return IniDocumentParser::SectionPrefixFromList(
		section, IniDocumentParser::kMigotoRegularSections,
		sizeof(IniDocumentParser::kMigotoRegularSections) / sizeof(IniDocumentParser::kMigotoRegularSections[0]));
}

inline std::wstring IniDocumentText::MakeNamespacedSectionName(
	const std::wstring &section, const std::wstring &iniNamespace)
{
	if (iniNamespace.empty())
		return section;

	const wchar_t *prefix = IniDocumentText::MigotoSectionPrefix(section);
	if (!prefix)
		return section;

	size_t prefixLength = wcslen(prefix);
	if (section.size() < prefixLength)
		return section;
	return std::wstring(prefix) + L"\\" + iniNamespace + L"\\" + section.substr(prefixLength);
}

inline std::wstring IniDocumentText::ResolveNamespacedSectionReference(
	const std::wstring &reference,
	const std::wstring &prefix,
	const std::wstring &iniNamespace)
{
	std::wstring ref = IniDocumentText::Trim(reference);
	if (ref.empty())
		return L"";

	if (IniDocumentText::ToLower(ref).rfind(IniDocumentText::ToLower(prefix) + L"\\", 0) == 0)
		return ref;
	if (IniDocumentText::ToLower(ref).rfind(IniDocumentText::ToLower(prefix), 0) == 0)
		return IniDocumentText::MakeNamespacedSectionName(ref, iniNamespace);
	return IniDocumentText::MakeNamespacedSectionName(prefix + ref, iniNamespace);
}

inline bool IniDocument::LoadFromFile(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();

	std::ifstream file(mPath, std::ios::binary);
	if (!file.is_open()) {
		mError = L"Unable to open ini file";
		return false;
	}

	std::string bytes(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
	}

	UINT codePage = CP_UTF8;
	int length = MultiByteToWideChar(
		codePage, MB_ERR_INVALID_CHARS, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) {
		codePage = CP_ACP;
		length = MultiByteToWideChar(
			codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
			nullptr, 0);
	}
	if (length < 0) {
		mError = L"Unable to decode ini file";
		return false;
	}

	std::wstring text(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &text[0], length)) {
			mError = L"Unable to decode ini file";
			return false;
		}
	}
	return Parse(text.c_str());
}

inline bool IniDocument::Parse(const wchar_t *text)
{
	mSections.clear();
	mSectionIndex.clear();
	mError.clear();
	return AppendParse(text, mPath, L"", L"");
}

inline bool IniDocument::AppendParse(
	const wchar_t *text,
	const std::wstring &sourcePath,
	const std::wstring &sourceDir,
	const std::wstring &iniNamespace)
{
	if (!text)
		return true;

	IniSection *current = nullptr;
	std::wstring currentNamespace = iniNamespace;
	std::wistringstream stream(text);
	std::wstring line;
	int lineNumber = 0;

	while (std::getline(stream, line)) {
		lineNumber++;
		if (lineNumber == 1 && !line.empty() && line.front() == L'\xfeff')
			line.erase(line.begin());
		if (lineNumber == 1 && line.size() >= 3 &&
		    line[0] == L'\xef' && line[1] == L'\xbb' && line[2] == L'\xbf')
			line.erase(0, 3);
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();

		std::wstring trimmed = IniDocumentText::Trim(line);
		if (IniDocumentParser::IsCommentOrEmpty(trimmed))
			continue;

		if (trimmed.size() >= 2 && trimmed.front() == L'[' && trimmed.back() == L']') {
			std::wstring originalName = IniDocumentText::Trim(trimmed.substr(1, trimmed.size() - 2));
			std::wstring sectionName = IniDocumentText::MakeNamespacedSectionName(originalName, currentNamespace);
			std::wstring sectionKey = IniDocumentText::ToLower(sectionName);
			auto found = mSectionIndex.find(sectionKey);
			if (found == mSectionIndex.end()) {
				IniSection section;
				section.name = sectionName;
				section.originalName = originalName;
				section.sourcePath = sourcePath;
				section.sourceDir = sourceDir;
				section.iniNamespace = sectionName == originalName ? L"" : currentNamespace;
				mSections.push_back(section);
				mSectionIndex[sectionKey] = mSections.size() - 1;
				current = &mSections.back();
			} else {
				current = &mSections[found->second];
			}
			continue;
		}

		if (!current) {
			size_t equals = line.find(L'=');
			if (equals != std::wstring::npos) {
				std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(line.substr(0, equals)));
				std::wstring value = IniDocumentText::Trim(line.substr(equals + 1));
				if (key == L"namespace") {
					currentNamespace = value;
					continue;
				}
				if (key == L"condition")
					continue;
			}
			mError = L"Ini entry found before first section";
			return false;
		}

		IniEntry entry;
		entry.rawLine = line;
		entry.sourcePath = sourcePath;
		entry.sourceDir = sourceDir;
		entry.iniNamespace = currentNamespace;
		entry.lineNumber = lineNumber;

		size_t equals = line.find(L'=');
		if (equals == std::wstring::npos) {
			entry.key = IniDocumentText::Trim(line);
			entry.hasEquals = false;
		} else {
			entry.key = IniDocumentText::Trim(line.substr(0, equals));
			entry.value = IniDocumentText::Trim(line.substr(equals + 1));
			entry.hasEquals = true;
		}
		current->entries.push_back(entry);
	}

	return true;
}

inline const IniSection *IniDocument::FindSection(const wchar_t *section) const
{
	if (!section)
		return nullptr;
	auto found = mSectionIndex.find(IniDocumentText::ToLower(section));
	if (found == mSectionIndex.end())
		return nullptr;
	return &mSections[found->second];
}

inline bool IniDocument::GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const
{
	const IniSection *iniSection = FindSection(section);
	if (!iniSection || !key)
		return false;

	std::wstring keyLower = IniDocumentText::ToLower(key);
	for (auto it = iniSection->entries.rbegin(); it != iniSection->entries.rend(); ++it) {
		if (!it->hasEquals)
			continue;
		if (IniDocumentText::ToLower(it->key) == keyLower) {
			if (value)
				*value = it->value;
			return true;
		}
	}
	return false;
}

inline bool IniDocument::GetBool(const wchar_t *section, const wchar_t *key, bool *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	text = IniDocumentText::ToLower(IniDocumentText::Trim(text));
	bool parsed = text == L"1" || text == L"true" || text == L"yes" || text == L"on";
	if (!parsed && !(text == L"0" || text == L"false" || text == L"no" || text == L"off"))
		return false;
	if (value)
		*value = parsed;
	return true;
}

inline bool IniDocument::GetInt(const wchar_t *section, const wchar_t *key, int *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	wchar_t *end = nullptr;
	long parsed = std::wcstol(text.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;
	if (value)
		*value = static_cast<int>(parsed);
	return true;
}




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
	DrawIndexedAuto,
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
		// `drawindexed = auto` recomputes the index count from the replaced
		// index buffer at draw time (mirrors DX11 get_index_count_from_current_ib).
		if (IniDocumentText::ToLower(value) == L"auto") {
			action->kind = CommandListActionKind::DrawIndexedAuto;
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
	         action->kind == CommandListActionKind::SetVertexBuffer ||
	         action->kind == CommandListActionKind::SetConstantBuffer ||
	         action->kind == CommandListActionKind::SetShaderResource ||
	         action->kind == CommandListActionKind::SetUnorderedAccessView)
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




class ShaderOverrideConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	uint64_t hash = 0;
	bool handlingSkip = false;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
};

using ShaderOverrideMap = std::unordered_map<uint64_t, ShaderOverrideConfig>;

class MigotoShaderOverrideParser
{
public:
	static bool ParseHash(const std::wstring &text, uint64_t *value);
	static void ParseSections(
		const IniDocument &ini, ShaderOverrideMap *shaderOverrides);
};



class MigotoShaderOverrideParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

};

inline bool MigotoShaderOverrideParser::ParseHash(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty())
		return false;

	uint64_t parsed = 0;
	for (wchar_t ch : trimmed) {
		unsigned digit = 0;
		if (ch >= L'0' && ch <= L'9')
			digit = ch - L'0';
		else if (ch >= L'a' && ch <= L'f')
			digit = ch - L'a' + 10;
		else if (ch >= L'A' && ch <= L'F')
			digit = ch - L'A' + 10;
		else
			return false;
		parsed = (parsed << 4) | digit;
	}

	*value = parsed;
	return true;
}

inline void MigotoShaderOverrideParser::ParseSections(
	const IniDocument &ini, ShaderOverrideMap *shaderOverrides)
{
	if (!shaderOverrides)
		return;

	shaderOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoShaderOverrideParserText::StartsWithI(section.name, L"ShaderOverride"))
			continue;

		ShaderOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

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

				std::wstring key = IniDocumentText::ToLower(entry.key);

			if (key == L"hash") {
				uint64_t hash = 0;
				if (!MigotoShaderOverrideParser::ParseHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
		if (hasHash)
			(*shaderOverrides)[config.hash] = config;
	}
}




enum class NumericMatchOp
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual
};

class NumericMatch
{
public:
	NumericMatchOp op = NumericMatchOp::Equal;
	uint32_t value = 0;
	bool enabled = false;
};

class TextureOverrideConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	uint32_t hash = 0;
	bool handlingSkip = false;
	std::wstring indexBufferResource;
	std::map<uint32_t, std::wstring> vertexBufferResources;
	bool hasMatchVertexCount = false;
	bool hasMatchIndexCount = false;
	bool hasMatchInstanceCount = false;
	bool hasMatchFirstVertex = false;
	bool hasMatchFirstIndex = false;
	bool hasMatchFirstInstance = false;
	bool hasMatchCs = false;
	bool hasMatchUavBytes = false;
	bool hasCsHash = false;
	std::map<uint32_t, uint32_t> preSkinMatchCsSrvHashes;
	std::map<uint32_t, std::wstring> preSkinCsSrvResources;
	// Draw-call texture slot replacement (ps-tN/vs-tN/this=).
	std::map<uint32_t, std::wstring> drawPsSrvResources;
	std::map<uint32_t, std::wstring> drawVsSrvResources;
	std::wstring drawReplaceThisResource;
	NumericMatch matchVertexCount;
	NumericMatch matchIndexCount;
	NumericMatch matchInstanceCount;
	NumericMatch matchFirstVertex;
	NumericMatch matchFirstIndex;
	NumericMatch matchFirstInstance;
	uint64_t matchCs = 0;
	uint64_t matchUavBytes = 0;
	uint64_t csHash = 0;
	bool hasMatchPriority = false;
	int matchPriority = 0;
	bool hasVertexLimitRaise = false;
	uint32_t overrideByteStride = 0;
	uint32_t overrideVertexCount = 0;
	uint64_t overrideByteWidth = 0;
	uint32_t uavByteStride = 0;
	uint64_t overrideNumElements = 0;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
};

using TextureOverrideList = std::vector<TextureOverrideConfig>;
using TextureOverrideMap = std::unordered_map<uint32_t, TextureOverrideList>;

class MigotoTextureOverrideParser
{
public:
	static bool ParseHash(const std::wstring &text, uint32_t *value);
	static void ParseSections(
		const IniDocument &ini, TextureOverrideMap *textureOverrides);
};




class MigotoTextureOverrideParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseSrvIndexedKey(
	const std::wstring &key, const wchar_t *prefix, const wchar_t *suffix,
	uint32_t *index)
{
	if (!index || !StartsWithI(key, prefix))
		return false;

	const size_t prefixLength = wcslen(prefix);
	const size_t suffixLength = suffix ? wcslen(suffix) : 0;
	if (key.size() <= prefixLength + suffixLength)
		return false;
	if (suffixLength) {
		std::wstring keySuffix = key.substr(key.size() - suffixLength);
		if (IniDocumentText::ToLower(keySuffix) != IniDocumentText::ToLower(suffix))
			return false;
	}

	const std::wstring digits = key.substr(
		prefixLength, key.size() - prefixLength - suffixLength);
	if (digits.empty())
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : digits) {
		if (ch < L'0' || ch > L'9')
			return false;
		const uint32_t digit = static_cast<uint32_t>(ch - L'0');
		if (parsed > (UINT32_MAX - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}

	*index = parsed;
	return true;
}

static bool ParseInt(const std::wstring &text, int *value);
static bool ParseUInt(const std::wstring &text, uint32_t *value);
static bool ParseNumericMatch(const std::wstring &text, NumericMatch *match);
static bool ParseUInt64(const std::wstring &text, uint64_t *value);
static bool ParseVertexBufferKey(const std::wstring &key, uint32_t *slot);
static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace);

};

inline bool MigotoTextureOverrideParser::ParseHash(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty() || trimmed.size() > 8)
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : trimmed) {
		unsigned digit = 0;
		if (ch >= L'0' && ch <= L'9')
			digit = ch - L'0';
		else if (ch >= L'a' && ch <= L'f')
			digit = ch - L'a' + 10;
		else if (ch >= L'A' && ch <= L'F')
			digit = ch - L'A' + 10;
		else
			return false;
		parsed = (parsed << 4) | digit;
	}

	*value = parsed;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseInt(const std::wstring &text, int *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	long parsed = std::wcstol(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<int>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt(const std::wstring &text, uint32_t *value)
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

inline bool MigotoTextureOverrideParserText::ParseNumericMatch(const std::wstring &text, NumericMatch *match)
{
	if (!match)
		return false;

	std::wstring value = IniDocumentText::Trim(text);
	if (value.empty())
		return false;

	NumericMatchOp op = NumericMatchOp::Equal;
	if (value.rfind(L"<=", 0) == 0) {
		op = NumericMatchOp::LessEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L">=", 0) == 0) {
		op = NumericMatchOp::GreaterEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L"!", 0) == 0) {
		op = NumericMatchOp::NotEqual;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"=", 0) == 0) {
		op = NumericMatchOp::Equal;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"<", 0) == 0) {
		op = NumericMatchOp::Less;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L">", 0) == 0) {
		op = NumericMatchOp::Greater;
		value = IniDocumentText::Trim(value.substr(1));
	}

	uint32_t parsed = 0;
	if (!MigotoTextureOverrideParserText::ParseUInt(value, &parsed))
		return false;

	match->op = op;
	match->value = parsed;
	match->enabled = true;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt64(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long long parsed = std::wcstoull(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint64_t>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseVertexBufferKey(const std::wstring &key, uint32_t *slot)
{
	if (!slot || key.size() < 3 || key[0] != L'v' || key[1] != L'b')
		return false;

	uint32_t parsed = 0;
	for (size_t i = 2; i < key.size(); ++i) {
		if (key[i] < L'0' || key[i] > L'9')
			return false;
		parsed = parsed * 10 + static_cast<uint32_t>(key[i] - L'0');
	}

	*slot = parsed;
	return true;
}

inline std::wstring MigotoTextureOverrideParserText::ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return IniDocumentText::ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

inline void MigotoTextureOverrideParser::ParseSections(
	const IniDocument &ini, TextureOverrideMap *textureOverrides)
{
	if (!textureOverrides)
		return;

	textureOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoTextureOverrideParserText::StartsWithI(section.name, L"TextureOverride"))
			continue;

		TextureOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

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

				std::wstring key = IniDocumentText::ToLower(entry.key);

			if (key == L"hash") {
				uint32_t hash = 0;
				if (!MigotoTextureOverrideParser::ParseHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			if (key == L"ib") {
				config.indexBufferResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			uint32_t vbSlot = 0;
			if (MigotoTextureOverrideParserText::ParseVertexBufferKey(key, &vbSlot)) {
				config.vertexBufferResources[vbSlot] =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			if (key == L"match_vertex_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchVertexCount))
					config.hasMatchVertexCount = true;
				continue;
			}

			if (key == L"match_index_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchIndexCount))
					config.hasMatchIndexCount = true;
				continue;
			}

			if (key == L"match_instance_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchInstanceCount))
					config.hasMatchInstanceCount = true;
				continue;
			}

			if (key == L"match_first_vertex") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstVertex))
					config.hasMatchFirstVertex = true;
				continue;
			}

			if (key == L"match_first_index") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstIndex))
					config.hasMatchFirstIndex = true;
				continue;
			}

			if (key == L"match_first_instance") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstInstance))
					config.hasMatchFirstInstance = true;
				continue;
			}

			if (key == L"match_priority") {
				int value = 0;
				if (MigotoTextureOverrideParserText::ParseInt(entry.value, &value)) {
					config.matchPriority = value;
					config.hasMatchPriority = true;
				}
				continue;
			}

			if (key == L"cs_hash") {
				uint64_t value = 0;
				if (MigotoShaderOverrideParser::ParseHash(entry.value, &value)) {
					config.csHash = value;
					config.hasCsHash = true;
				}
				continue;
			}

			if (key == L"match_uav_bytes") {
				uint64_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt64(entry.value, &value)) {
					config.matchUavBytes = value;
					config.hasMatchUavBytes = true;
				}
				continue;
			}

			uint32_t srvIndex = 0;
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_cs_t", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_srv", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"cs-t", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"srv", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			// Draw-call texture slot replacement: ps-tN / vs-tN / this=
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"ps-t", L"", &srvIndex)) {
				config.drawPsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"vs-t", L"", &srvIndex)) {
				config.drawVsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"this") {
				config.drawReplaceThisResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"override_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"override_vertex_count") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideVertexCount = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"uav_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.uavByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
		if (config.overrideByteStride && config.overrideVertexCount)
			config.overrideByteWidth =
				static_cast<uint64_t>(config.overrideByteStride) * config.overrideVertexCount;
		if (config.uavByteStride && config.overrideByteWidth)
			config.overrideNumElements = config.overrideByteWidth / config.uavByteStride;
		const bool hasDrawContextMatch =
			config.hasMatchVertexCount || config.hasMatchIndexCount ||
			config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
			config.hasMatchFirstIndex || config.hasMatchFirstInstance;
		if (hasHash || config.hasVertexLimitRaise || config.hasMatchCs || config.hasCsHash || hasDrawContextMatch)
			(*textureOverrides)[config.hash].push_back(config);
	}

	for (auto &bucket : *textureOverrides) {
		std::sort(bucket.second.begin(), bucket.second.end(),
			[](const TextureOverrideConfig &lhs, const TextureOverrideConfig &rhs) {
				if (lhs.matchPriority != rhs.matchPriority)
					return lhs.matchPriority > rhs.matchPriority;
				return IniDocumentText::ToLower(lhs.section) < IniDocumentText::ToLower(rhs.section);
			});
	}
}




// Resource and ShaderRegex sections use the same INI model.


class ResourceConfig
{
public:
	std::wstring section;
	std::wstring name;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring type;
	std::wstring filename;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::wstring format;
	std::wstring data;
	uint32_t stride = 0;
	uint32_t arraySize = 0;
	uint32_t byteWidth = 0;
	bool hasStride = false;
	bool hasArraySize = false;
	bool hasByteWidth = false;
};

using ResourceMap = std::unordered_map<std::wstring, ResourceConfig>;

class MigotoResourceParser
{
public:
	static void ParseSections(const IniDocument &ini, ResourceMap *resources);
};



class MigotoResourceParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseUInt(const std::wstring &text, uint32_t *value)
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

};

inline void MigotoResourceParser::ParseSections(const IniDocument &ini, ResourceMap *resources)
{
	if (!resources)
		return;

	resources->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoResourceParserText::StartsWithI(section.name, L"Resource"))
			continue;

		ResourceConfig config;
		config.section = section.name;
		config.name = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;

			std::wstring key = IniDocumentText::ToLower(entry.key);
			if (key == L"type") {
				config.type = IniDocumentText::Trim(entry.value);
			} else if (key == L"filename" || key == L"file") {
				config.filename = IniDocumentText::Trim(entry.value);
			} else if (key == L"format") {
				config.format = IniDocumentText::Trim(entry.value);
			} else if (key == L"data") {
				config.data = IniDocumentText::Trim(entry.value);
			} else if (key == L"stride") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.stride = value;
					config.hasStride = true;
				}
			} else if (key == L"array" || key == L"array_size") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.arraySize = value;
					config.hasArraySize = true;
				}
			} else if (key == L"byte_width" || key == L"width") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.byteWidth = value;
					config.hasByteWidth = true;
				}
			}
		}

		(*resources)[config.name] = config;
		if (section.iniNamespace.empty())
			(*resources)[config.originalSection] = config;
	}
}




class ShaderRegexConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<std::string> shaderModels;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
	bool handlingSkip = false;
	bool hasPattern = false;
};

using ShaderRegexMap = std::unordered_map<std::wstring, ShaderRegexConfig>;

class MigotoShaderRegexParser
{
public:
	static void ParseSections(
		const IniDocument &ini, ShaderRegexMap *shaderRegexes);
};



class MigotoShaderRegexParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static std::wstring ShaderRegexGroupName(const std::wstring &section)
{
	size_t namespaceEnd = 0;
	if (IniDocumentText::ToLower(section).rfind(L"shaderregex\\", 0) == 0) {
		size_t suffixStart = section.find_last_of(L'\\');
		if (suffixStart != std::wstring::npos)
			namespaceEnd = suffixStart + 1;
	}
	size_t dot = section.find(L'.', namespaceEnd);
	if (dot == std::wstring::npos)
		return section;
	return section.substr(0, dot);
}

static bool IsShaderRegexMainSection(const std::wstring &section)
{
	return ShaderRegexGroupName(section) == section;
}

static void AppendShaderModels(
	const std::wstring &value, std::vector<std::string> *shaderModels)
{
	if (!shaderModels)
		return;

	size_t start = 0;
	while (start <= value.size()) {
		size_t space = value.find_first_of(L" \t\r\n", start);
		std::wstring token = space == std::wstring::npos ?
			value.substr(start) : value.substr(start, space - start);
		token = IniDocumentText::ToLower(IniDocumentText::Trim(token));
		if (!token.empty()) {
			std::string model;
			model.reserve(token.size());
			bool ascii = true;
			for (wchar_t ch : token) {
				if (ch > 0x7f) {
					ascii = false;
					break;
				}
				model.push_back(static_cast<char>(ch));
			}
			if (ascii)
				shaderModels->push_back(model);
		}
		if (space == std::wstring::npos)
			break;
		start = space + 1;
	}
}

};

inline void MigotoShaderRegexParser::ParseSections(
	const IniDocument &ini, ShaderRegexMap *shaderRegexes)
{
	if (!shaderRegexes)
		return;

	shaderRegexes->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoShaderRegexParserText::StartsWithI(section.name, L"ShaderRegex"))
			continue;

		const std::wstring groupName = MigotoShaderRegexParserText::ShaderRegexGroupName(section.name);
		ShaderRegexConfig &config = (*shaderRegexes)[groupName];
		if (config.section.empty()) {
			config.section = groupName;
			config.originalSection = section.originalName.empty() ? groupName : MigotoShaderRegexParserText::ShaderRegexGroupName(section.originalName);
			config.sourcePath = section.sourcePath;
			config.sourceDir = section.sourceDir;
			config.iniNamespace = section.iniNamespace;
		}

		const bool isMain = MigotoShaderRegexParserText::IsShaderRegexMainSection(section.name);
		if (!isMain) {
			config.hasPattern = true;
			DX12LogManager::DebugFields("MigotoShaderRegexParse",
				"\"stage\":\"sub_section\",\"section\":\"%S\",\"group\":\"%S\"",
				section.name.c_str(), groupName.c_str());
			continue;
		}

		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		DX12LogManager::DebugFields("MigotoShaderRegexParse",
			"\"stage\":\"main_section_found\",\"section\":\"%S\",\"group\":\"%S\",\"entries\":%zu",
			section.name.c_str(), groupName.c_str(), section.entries.size());

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals) {
					DX12LogManager::DebugFields("MigotoShaderRegexParse",
						"\"stage\":\"entry_no_equals\",\"section\":\"%S\",\"raw\":\"%S\"",
						section.name.c_str(), entry.rawLine.c_str());
					continue;
				}

			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			std::wstring value = IniDocumentText::Trim(entry.value);
			if (key == L"shader_model") {
				size_t before = config.shaderModels.size();
				MigotoShaderRegexParserText::AppendShaderModels(value, &config.shaderModels);
				DX12LogManager::DebugFields("MigotoShaderRegexParse",
					"\"stage\":\"shader_model\",\"section\":\"%S\",\"value\":\"%S\",\"before\":%zu,\"after\":%zu",
					section.name.c_str(), value.c_str(), before, config.shaderModels.size());
				continue;
			}
			if (key == L"temps" || key == L"filter_index")
				continue;

			if (key == L"handling") {
				std::wstring handling = IniDocumentText::ToLower(IniDocumentText::Trim(value));
				if (handling == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, handling, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
	}

	size_t erasedCount = 0;
	for (auto it = shaderRegexes->begin(); it != shaderRegexes->end();) {
		if (it->second.shaderModels.empty()) {
			DX12LogManager::DebugFields("MigotoShaderRegexParse",
				"\"stage\":\"erase_empty_models\",\"section\":\"%S\"",
				it->second.section.c_str());
			it = shaderRegexes->erase(it);
			++erasedCount;
		} else {
			++it;
		}
	}
	DX12LogManager::DebugFields("MigotoShaderRegexParse",
		"\"stage\":\"final\",\"count\":%zu,\"erased\":%zu",
		shaderRegexes->size(), erasedCount);
}


} // namespace Bunny
