#pragma once

#include "Presentation/DX12PresentationRuntime.hpp"

#include <set>

namespace Bunny {

class MigotoIniLoadResult
{
public:
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

class MigotoIniLoader
{
public:
	static bool LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result);
};

} // namespace Bunny



namespace Bunny {

class MigotoIniLoaderPath
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool EndsWithI(const std::wstring &value, const wchar_t *suffix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerSuffix = IniDocumentText::ToLower(suffix);
	if (lower.size() < lowerSuffix.size())
		return false;
	return lower.compare(lower.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0;
}

static bool IsAbsolutePath(const std::wstring &path)
{
	return path.size() >= 2 && (path[1] == L':' || path[0] == L'\\' || path[0] == L'/');
}

static std::wstring DirectoryOf(const std::wstring &path)
{
	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
	PathRemoveFileSpecW(buffer);
	return buffer;
}

static std::wstring JoinPath(const std::wstring &base, const std::wstring &path)
{
	if (path.empty())
		return base;
	if (IsAbsolutePath(path))
		return path;

	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, base.c_str(), _TRUNCATE);
	PathAppendW(buffer, path.c_str());
	return buffer;
}

static std::wstring CanonicalPath(const std::wstring &path)
{
	wchar_t full[MAX_PATH];
	if (!GetFullPathNameW(path.c_str(), ARRAYSIZE(full), full, nullptr))
		return IniDocumentText::ToLower(path);
	return IniDocumentText::ToLower(full);
}

static bool IsDisabledName(const std::wstring &name)
{
	return StartsWithI(name, L"disabled") || StartsWithI(name, L"DISABLED");
}

static bool ShouldExcludeRecursive(const std::wstring &name, const std::vector<std::wstring> &patterns)
{
	if (IsDisabledName(name))
		return true;

	for (const std::wstring &pattern : patterns) {
		std::wstring lowerName = IniDocumentText::ToLower(name);
		std::wstring lowerPattern = IniDocumentText::ToLower(IniDocumentText::Trim(pattern));
		if (lowerPattern.empty())
			continue;
		if (lowerPattern == lowerName)
			return true;
		if (lowerPattern.back() == L'*') {
			lowerPattern.pop_back();
			if (lowerName.rfind(lowerPattern, 0) == 0)
				return true;
		}
	}
	return false;
}

static bool ReadFileText(const std::wstring &path, std::wstring *text)
{
	if (!text)
		return false;
	text->clear();

	FILE *file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		fclose(file);
		return true;
	}

	std::string bytes;
	bytes.resize(static_cast<size_t>(size));
	size_t read = fread(&bytes[0], 1, bytes.size(), file);
	fclose(file);
	if (read != bytes.size())
		return false;

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
	if (length < 0)
		return false;

	text->assign(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &(*text)[0], length))
			return false;
	}
	return true;
}

static std::wstring MakeRelativeNamespace(
	const std::wstring &rootDir, const std::wstring &path)
{
	wchar_t relative[MAX_PATH];
	if (PathRelativePathToW(
		relative, rootDir.c_str(), FILE_ATTRIBUTE_DIRECTORY,
		path.c_str(), FILE_ATTRIBUTE_NORMAL)) {
		std::wstring value = relative;
		if (value.rfind(L".\\", 0) == 0 || value.rfind(L"./", 0) == 0)
			value = value.substr(2);
		return value;
	}
	return path;
}

class LoaderState
{
public:
	std::wstring rootDir;
	std::set<std::wstring> seenFiles;
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

static void CollectIncludeEntries(
	const IniDocument &ini, std::vector<std::wstring> *includes,
	std::vector<std::wstring> *recursiveIncludes,
	std::vector<std::wstring> *excludeRecursive)
{
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoIniLoaderPath::StartsWithI(section.name, L"Include"))
			continue;
		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;
			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			if (key == L"include")
				includes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"include_recursive")
				recursiveIncludes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"exclude_recursive")
				excludeRecursive->push_back(IniDocumentText::Trim(entry.value));
		}
	}
}

static void LoadDirectoryRecursive(
	const std::wstring &dir, const std::vector<std::wstring> &excludeRecursive,
	LoaderState *state)
{
	if (!state)
		return;

	std::wstring search = MigotoIniLoaderPath::JoinPath(dir, L"*");
	WIN32_FIND_DATAW findData = {};
	HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		state->warnings.push_back(L"include_recursive path not found: " + dir);
		return;
	}

	std::vector<std::wstring> iniFiles;
	std::vector<std::wstring> directories;
	do {
		std::wstring name = findData.cFileName;
		if (name == L"." || name == L"..")
			continue;
		if (MigotoIniLoaderPath::ShouldExcludeRecursive(name, excludeRecursive))
			continue;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.push_back(name);
		else if (MigotoIniLoaderPath::EndsWithI(name, L".ini"))
			iniFiles.push_back(name);
	} while (FindNextFileW(find, &findData));
	FindClose(find);

	std::sort(iniFiles.begin(), iniFiles.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });
	std::sort(directories.begin(), directories.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });

	for (const std::wstring &file : iniFiles)
	{
		std::wstring filePath = MigotoIniLoaderPath::JoinPath(dir, file);
		MigotoIniLoaderPath::LoadFileRecursive(filePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, filePath), state);
	}
	for (const std::wstring &child : directories)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(dir, child), excludeRecursive, state);
}

static void LoadFileRecursive(
	const std::wstring &path, const std::wstring &iniNamespace, LoaderState *state)
{
	if (!state)
		return;

	std::wstring canonical = MigotoIniLoaderPath::CanonicalPath(path);
	if (!state->seenFiles.insert(canonical).second) {
		state->warnings.push_back(L"ini included multiple times: " + path);
		return;
	}

	std::wstring text;
	if (!MigotoIniLoaderPath::ReadFileText(path, &text)) {
		state->errors.push_back(L"unable to load ini: " + path);
		return;
	}

	state->loadedFiles.push_back(path);
	std::wstring sourceDir = MigotoIniLoaderPath::DirectoryOf(path);
	if (!state->document.AppendParse(text.c_str(), path, sourceDir, iniNamespace)) {
		state->errors.push_back(
			L"unable to parse ini: " + path + L" (" + state->document.Error() + L")");
		return;
	}

	std::vector<std::wstring> includes;
	std::vector<std::wstring> recursiveIncludes;
	std::vector<std::wstring> excludeRecursive;
	IniDocument localIni;
	localIni.Parse(text.c_str());
	MigotoIniLoaderPath::CollectIncludeEntries(localIni, &includes, &recursiveIncludes, &excludeRecursive);

	std::wstring includeBase = MigotoIniLoaderPath::DirectoryOf(path);
	for (const std::wstring &include : includes)
	{
		std::wstring includePath = MigotoIniLoaderPath::JoinPath(includeBase, include);
		MigotoIniLoaderPath::LoadFileRecursive(includePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, includePath), state);
	}
	for (const std::wstring &recursive : recursiveIncludes)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(includeBase, recursive), excludeRecursive, state);
}

};

inline bool MigotoIniLoader::LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result)
{
	if (!rootPath || !result)
		return false;

	*result = MigotoIniLoadResult();
	MigotoIniLoaderPath::LoaderState state;
	state.rootDir = MigotoIniLoaderPath::DirectoryOf(rootPath);
	MigotoIniLoaderPath::LoadFileRecursive(rootPath, L"", &state);

	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return true;
}

} // namespace Bunny




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

}
