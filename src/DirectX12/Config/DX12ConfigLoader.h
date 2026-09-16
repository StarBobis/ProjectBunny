#pragma once

#include "DX12Config.h"
#include "MigotoCommandVmUtils.hpp"

#include <set>

// INI include traversal and process-level loader configuration.

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

// Loads the persist file (d3dx_user.ini) as the final ini document. A missing
// file is the normal first-run case and stays silent; a file that exists but
// cannot be read or parsed is reported, because silently ignoring it would
// make the runtime fall back to the ini defaults while the next save
// overwrites the user's settings.
static void LoadUserConfigFile(const std::wstring &path, LoaderState *state)
{
	if (!state || path.empty())
		return;
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	if (state->seenFiles.count(MigotoIniLoaderPath::CanonicalPath(path))) {
		state->warnings.push_back(L"user_config already loaded by an include: " + path);
		return;
	}
	MigotoIniLoaderPath::LoadFileRecursive(path, path, state);
}

};

// Path of the persist file (d3dx_user.ini): the [Include] user_config entry
// resolved against the root ini's directory, or the DX11 default name beside
// it. The resolved path is published through
// MigotoCommandVmUtils::userConfigPath rather than a type of its own, because
// the runtime's save path only ever needs the string and must not depend on
// this header (Resources/ pulls it in early, and a second include in the same
// translation unit is then skipped by #pragma once - see the include order
// note in DX12ModRuntimeInternal.h).
namespace MigotoUserConfigPath
{
	inline static const wchar_t *kDefaultFileName = L"d3dx_user.ini";

	// Returns the user_config value of the first [Include] section that
	// declares one, or an empty string. Within a section the last duplicate
	// key wins, matching IniDocument::GetString.
	static std::wstring FindConfiguredName(const IniDocument &ini)
	{
		for (const IniSection &section : ini.Sections()) {
			if (!MigotoIniLoaderPath::StartsWithI(section.name, L"Include"))
				continue;
			std::wstring configured;
			for (const IniEntry &entry : section.entries) {
				if (!entry.hasEquals)
					continue;
				if (IniDocumentText::ToLower(IniDocumentText::Trim(entry.key)) == L"user_config")
					configured = IniDocumentText::Trim(entry.value);
			}
			if (!configured.empty())
				return configured;
		}
		return L"";
	}

	// Root-relative unless the configured value is already absolute
	// (MigotoIniLoaderPath::IsAbsolutePath covers "C:\...", "\\server\...",
	// "\..." and "/...").
	static std::wstring Resolve(const std::wstring &rootDir, const std::wstring &configured)
	{
		const std::wstring name = IniDocumentText::Trim(configured);
		if (name.empty())
			return MigotoIniLoaderPath::JoinPath(rootDir, kDefaultFileName);
		return MigotoIniLoaderPath::JoinPath(rootDir, name);
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

	// The user config is loaded very last so the persisted values override the
	// defaults declared by every other ini file (DX11: ParseIncludedIniFiles()
	// loads G->user_config after the include loop). It is resolved from the
	// root file even when the file itself is missing - the save path needs the
	// path to create it, and a missing file must not be an error.
	MigotoCommandVmUtils::userConfigPath = MigotoUserConfigPath::Resolve(
		state.rootDir, MigotoUserConfigPath::FindConfiguredName(state.document));
	MigotoIniLoaderPath::LoadUserConfigFile(
		MigotoCommandVmUtils::userConfigPath, &state);

	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return true;
}


// Process loader/runtime options are kept beside the INI include loader.


class LoaderConfig
{
public:
	std::wstring target;
	std::wstring module;
	std::wstring entryPoint = L"CBTProc";
	std::wstring launch;
	std::wstring workingDirectory;
	bool checkVersion = true;
	bool requireAdmin = false;
	bool waitForTarget = true;
	int hookProc = 5;
	int delay = 0;
};

class RuntimeConfig
{
public:
	std::wstring configPath;
};

class MigotoConfig {
public:
	bool Load(const wchar_t *path);

	const IniDocument &Ini() const { return mIni; }
	const LoaderConfig &Loader() const { return mLoader; }
	const RuntimeConfig &Runtime() const { return mRuntime; }

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	void ReadLoaderConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

class MigotoConfigLocator
{
public:
	static std::wstring FindDefaultConfigPath(HINSTANCE module);
};




class MigotoConfigReader
{
public:
static void ReadString(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, std::wstring *value)
{
	std::wstring text;
	if (ini.GetString(section, key, &text) && value)
		*value = text;
}

static void ReadBool(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, bool *value)
{
	bool parsed = false;
	if (ini.GetBool(section, key, &parsed) && value)
		*value = parsed;
}

static void ReadInt(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, int *value)
{
	int parsed = 0;
	if (ini.GetInt(section, key, &parsed) && value)
		*value = parsed;
}

};

inline bool MigotoConfig::Load(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();
	mLoader = LoaderConfig();
	mRuntime = RuntimeConfig();
	mRuntime.configPath = mPath;

	if (!mIni.LoadFromFile(mPath.c_str())) {
		mError = mIni.Error();
		return false;
	}

	ReadLoaderConfig();
	return true;
}

inline void MigotoConfig::ReadLoaderConfig()
{
	MigotoConfigReader::ReadString(mIni, L"Loader", L"target", &mLoader.target);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"module", &mLoader.module);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

inline std::wstring MigotoConfigLocator::FindDefaultConfigPath(HINSTANCE module)
{
	wchar_t path[MAX_PATH] = {};
	wchar_t cwdPath[MAX_PATH] = {};

	if (GetCurrentDirectoryW(MAX_PATH, cwdPath)) {
		PathAppendW(cwdPath, L"d3dx.ini");
		if (PathFileExistsW(cwdPath))
			return cwdPath;
	}

	if (module && GetModuleFileNameW(module, path, MAX_PATH)) {
		PathRemoveFileSpecW(path);
		PathAppendW(path, L"d3dx.ini");
		if (PathFileExistsW(path))
			return path;
	}

	return L"d3dx.ini";
}


} // namespace Bunny
