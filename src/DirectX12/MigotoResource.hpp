#pragma once

#include <stdint.h>

#include <string>
#include <unordered_map>

#include "IniDocument.hpp"

namespace Bunny {

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

} // namespace Bunny

namespace Bunny {

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

} // namespace Bunny
