#pragma once

#include <string>
#include <vector>

#include "IniDocument.hpp"

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

#include <Windows.h>
#include <Shlwapi.h>

#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>

namespace Bunny {

class MigotoIniLoaderUtils
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentUtils::ToLower(value);
	std::wstring lowerPrefix = IniDocumentUtils::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool EndsWithI(const std::wstring &value, const wchar_t *suffix)
{
	std::wstring lower = IniDocumentUtils::ToLower(value);
	std::wstring lowerSuffix = IniDocumentUtils::ToLower(suffix);
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
		return IniDocumentUtils::ToLower(path);
	return IniDocumentUtils::ToLower(full);
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
		std::wstring lowerName = IniDocumentUtils::ToLower(name);
		std::wstring lowerPattern = IniDocumentUtils::ToLower(IniDocumentUtils::Trim(pattern));
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
		if (!MigotoIniLoaderUtils::StartsWithI(section.name, L"Include"))
			continue;
		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;
			std::wstring key = IniDocumentUtils::ToLower(IniDocumentUtils::Trim(entry.key));
			if (key == L"include")
				includes->push_back(IniDocumentUtils::Trim(entry.value));
			else if (key == L"include_recursive")
				recursiveIncludes->push_back(IniDocumentUtils::Trim(entry.value));
			else if (key == L"exclude_recursive")
				excludeRecursive->push_back(IniDocumentUtils::Trim(entry.value));
		}
	}
}

static void LoadDirectoryRecursive(
	const std::wstring &dir, const std::vector<std::wstring> &excludeRecursive,
	LoaderState *state)
{
	if (!state)
		return;

	std::wstring search = MigotoIniLoaderUtils::JoinPath(dir, L"*");
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
		if (MigotoIniLoaderUtils::ShouldExcludeRecursive(name, excludeRecursive))
			continue;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.push_back(name);
		else if (MigotoIniLoaderUtils::EndsWithI(name, L".ini"))
			iniFiles.push_back(name);
	} while (FindNextFileW(find, &findData));
	FindClose(find);

	std::sort(iniFiles.begin(), iniFiles.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentUtils::ToLower(a) < IniDocumentUtils::ToLower(b); });
	std::sort(directories.begin(), directories.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentUtils::ToLower(a) < IniDocumentUtils::ToLower(b); });

	for (const std::wstring &file : iniFiles)
	{
		std::wstring filePath = MigotoIniLoaderUtils::JoinPath(dir, file);
		MigotoIniLoaderUtils::LoadFileRecursive(filePath, MigotoIniLoaderUtils::MakeRelativeNamespace(state->rootDir, filePath), state);
	}
	for (const std::wstring &child : directories)
		MigotoIniLoaderUtils::LoadDirectoryRecursive(MigotoIniLoaderUtils::JoinPath(dir, child), excludeRecursive, state);
}

static void LoadFileRecursive(
	const std::wstring &path, const std::wstring &iniNamespace, LoaderState *state)
{
	if (!state)
		return;

	std::wstring canonical = MigotoIniLoaderUtils::CanonicalPath(path);
	if (!state->seenFiles.insert(canonical).second) {
		state->warnings.push_back(L"ini included multiple times: " + path);
		return;
	}

	std::wstring text;
	if (!MigotoIniLoaderUtils::ReadFileText(path, &text)) {
		state->errors.push_back(L"unable to load ini: " + path);
		return;
	}

	state->loadedFiles.push_back(path);
	std::wstring sourceDir = MigotoIniLoaderUtils::DirectoryOf(path);
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
	MigotoIniLoaderUtils::CollectIncludeEntries(localIni, &includes, &recursiveIncludes, &excludeRecursive);

	std::wstring includeBase = MigotoIniLoaderUtils::DirectoryOf(path);
	for (const std::wstring &include : includes)
	{
		std::wstring includePath = MigotoIniLoaderUtils::JoinPath(includeBase, include);
		MigotoIniLoaderUtils::LoadFileRecursive(includePath, MigotoIniLoaderUtils::MakeRelativeNamespace(state->rootDir, includePath), state);
	}
	for (const std::wstring &recursive : recursiveIncludes)
		MigotoIniLoaderUtils::LoadDirectoryRecursive(MigotoIniLoaderUtils::JoinPath(includeBase, recursive), excludeRecursive, state);
}

};

inline bool MigotoIniLoader::LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result)
{
	if (!rootPath || !result)
		return false;

	*result = MigotoIniLoadResult();
	MigotoIniLoaderUtils::LoaderState state;
	state.rootDir = MigotoIniLoaderUtils::DirectoryOf(rootPath);
	MigotoIniLoaderUtils::LoadFileRecursive(rootPath, L"", &state);

	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return true;
}

} // namespace Bunny
