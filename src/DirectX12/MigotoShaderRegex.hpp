#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "IniDocument.hpp"
#include "MigotoCommandList.hpp"

#include "DX12LogManager.hpp"

namespace Bunny {

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

}

namespace Bunny {

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
			DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
				"\"stage\":\"sub_section\",\"section\":\"%S\",\"group\":\"%S\"",
				section.name.c_str(), groupName.c_str());
			continue;
		}

		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
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
					DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
						"\"stage\":\"entry_no_equals\",\"section\":\"%S\",\"raw\":\"%S\"",
						section.name.c_str(), entry.rawLine.c_str());
					continue;
				}

			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			std::wstring value = IniDocumentText::Trim(entry.value);
			if (key == L"shader_model") {
				size_t before = config.shaderModels.size();
				MigotoShaderRegexParserText::AppendShaderModels(value, &config.shaderModels);
				DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
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
			DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
				"\"stage\":\"erase_empty_models\",\"section\":\"%S\"",
				it->second.section.c_str());
			it = shaderRegexes->erase(it);
			++erasedCount;
		} else {
			++it;
		}
	}
	DX12LogManager::DebugJsonFunc("MigotoShaderRegexParse",
		"\"stage\":\"final\",\"count\":%zu,\"erased\":%zu",
		shaderRegexes->size(), erasedCount);
}

}
