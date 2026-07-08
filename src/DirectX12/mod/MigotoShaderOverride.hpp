#pragma once

#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "IniDocument.hpp"
#include "MigotoCommandList.hpp"

namespace Bunny {

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

}

namespace Bunny {

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

}
