#pragma once

#include "DX12Config.h"
#include "bounded_path_format.h"

#include <filesystem>
#include <map>
#include "IniSavedSettings.h"

// INI include traversal and process-level loader configuration.

namespace Bunny {



class MigotoIniLoadResult
{
public:
	IniDocument document;
	std::wstring userConfigPath;
	bool includesComplete = true;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

class MigotoIniLoader
{
public:
	static bool LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result,
		const std::wstring &wipePath = {}, unsigned huntingMode = 0);
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

	// F-200 #5: a truncated base or a failed append would point the caller at
	// a different file. Return an empty path and let the call site reject the
	// include/directory instead.
	wchar_t buffer[MAX_PATH];
	if (!CopyPathBounded(buffer, _countof(buffer), base.c_str()))
		return L"";
	if (!PathAppendBounded(buffer, _countof(buffer), path.c_str()))
		return L"";
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

	const long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
	if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}
	if (size == 0)
		return fclose(file) == 0;

	std::string bytes;
	bytes.resize(static_cast<size_t>(size));
	size_t read = fread(&bytes[0], 1, bytes.size(), file);
	if (fclose(file) != 0 || read != bytes.size())
		return false;

	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
		if (bytes.empty())
			return true;
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
	if (length <= 0)
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

struct PendingIncludeSection
{
	IniSection section;
	std::vector<std::wstring> excludes;
};

class LoaderState
{
public:
	unsigned huntingMode = 0;
	std::wstring rootDir;
	std::map<std::wstring, bool> parsedFiles; // Canonical path to successful parse, including a false guard.
	std::vector<PendingIncludeSection> pending;
	IniDocument document;
	bool includesComplete = true;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};


static void QueueIncludes(std::vector<IniSection> &sections, LoaderState &state)
{
	std::vector<std::wstring> excluded;
	for (const auto &section : sections) {
		for (const auto &entry : section.entries) {
			if (entry.hasEquals && !_wcsicmp(entry.key.c_str(), L"exclude_recursive"))
				excluded.push_back(entry.value);
		}
	}
	for (auto &section : sections)
		state.pending.push_back({std::move(section), excluded});
}

static void LoadDirectoryRecursive(
	const std::wstring &dir, const std::vector<std::wstring> &excludeRecursive,
	const std::wstring &recursiveRoot, LoaderState *state)
{
	if (!state)
		return;
	if (dir.empty()) {
		state->includesComplete = false;
		state->warnings.push_back(L"include_recursive path rejected: path too long");
		return;
	}

	std::wstring search = MigotoIniLoaderPath::JoinPath(dir, L"*");
	if (search.empty()) {
		state->includesComplete = false;
		state->warnings.push_back(L"include_recursive path rejected: path too long: " + dir);
		return;
	}
	WIN32_FIND_DATAW findData = {};
	HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		const DWORD error = GetLastError();
		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			state->includesComplete = false;
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
	if (GetLastError() != ERROR_NO_MORE_FILES) {
		state->includesComplete = false;
		state->warnings.push_back(L"unable to finish enumerating include_recursive directory: " + dir);
	}
	FindClose(find);

	std::sort(iniFiles.begin(), iniFiles.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });
	std::sort(directories.begin(), directories.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });

	for (const std::wstring &file : iniFiles)
	{
		std::wstring filePath = MigotoIniLoaderPath::JoinPath(dir, file);
		if (filePath.empty()) {
			state->includesComplete = false;
			state->warnings.push_back(L"include path rejected: path too long: " + file);
			continue;
		}
		const auto scope = migoto::IsInputPathWithinDirectory(filePath, recursiveRoot)
			? migoto::InputDisableScope::MODS : migoto::InputDisableScope::ALL;
		LoadFile(filePath, MakeRelativeNamespace(state->rootDir, filePath), state, scope);
	}
	for (const std::wstring &child : directories) {
		std::wstring childPath = MigotoIniLoaderPath::JoinPath(dir, child);
		if (childPath.empty()) {
			state->includesComplete = false;
			state->warnings.push_back(L"include_recursive path rejected: path too long: " + child);
			continue;
		}
		MigotoIniLoaderPath::LoadDirectoryRecursive(childPath, excludeRecursive, recursiveRoot, state);
	}
}

static void LoadFile(const std::wstring &path, const std::wstring &iniNamespace, LoaderState *state,
	migoto::InputDisableScope scope = migoto::InputDisableScope::ALL, bool userConfig = false)
{
	if (!state)
		return;
	if (path.empty()) {
		state->errors.push_back(L"include path rejected: path too long");
		return;
	}

	std::wstring canonical = MigotoIniLoaderPath::CanonicalPath(path);
	const auto [record, firstVisit] = state->parsedFiles.try_emplace(std::move(canonical), false);
	if (!firstVisit) {
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
	IniFileParseResult file;
	if (!state->document.AppendParse(text.c_str(), path, sourceDir, iniNamespace, scope, &file, state->huntingMode)) {
		state->errors.push_back(
			L"unable to parse ini: " + path + L" (" + state->document.Error() + L")");
		return;
	}

	if (!file.conditionError.empty()) {
		// A malformed guard can hide declarations. Preserve unknown settings
		// until a complete reload, and reject an unreadable persistence source.
		state->includesComplete = false;
		auto &diagnostics = userConfig ? state->errors : state->warnings;
		diagnostics.push_back(std::move(file.conditionError));
		return;
	}
	record->second = true;
	for (auto &warning : file.warnings)
		state->warnings.push_back(std::move(warning));
	QueueIncludes(file.includes, *state);
}

static void LoadQueuedIncludes(LoaderState &state)
{
	// DX11 completes the current generation before visiting directives found in
	// its children. This decides which scope a multiply included key receives.
	while (!state.pending.empty()) {
		std::vector<PendingIncludeSection> current;
		current.swap(state.pending);
		std::stable_sort(current.begin(), current.end(), [](const auto &a, const auto &b) {
			return _wcsicmp(a.section.name.c_str(), b.section.name.c_str()) < 0;
		});
		for (const auto &batch : current) {
			for (const auto &entry : batch.section.entries) {
				const bool recursive = !_wcsicmp(entry.key.c_str(), L"include_recursive");
				if (!entry.hasEquals || (!recursive && _wcsicmp(entry.key.c_str(), L"include")))
					continue;
				const auto path = JoinPath(entry.sourceDir, entry.value);
				if (path.empty() || entry.value.empty()) {
					state.includesComplete = false;
					state.warnings.push_back(entry.key + L" path rejected: " + entry.value);
				} else if (recursive) {
					LoadDirectoryRecursive(path, batch.excludes, path, &state);
				} else {
					LoadFile(path, MakeRelativeNamespace(state.rootDir, path), &state);
				}
			}
		}
	}
}

// Loads the persist file (d3dx_user.ini) as the final ini document. A missing
// file is the normal first-run case and stays silent; a file that exists but
// cannot be read or parsed is reported, because silently ignoring it would
// make the runtime fall back to the ini defaults while the next save
// overwrites the user's settings.
static bool LoadUserConfigFile(const std::wstring &path, LoaderState *state)
{
	if (path.empty())
		return true;
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		const auto error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
			return true;
		state->errors.push_back(L"unable to access user_config: " + path);
		return false;
	}
	const auto prior = state->parsedFiles.find(CanonicalPath(path));
	if (prior != state->parsedFiles.end()) {
		state->includesComplete = false;
		if (!prior->second) {
			state->errors.push_back(L"user_config failed to load as an include: " + path);
			return false;
		}
		state->warnings.push_back(L"user_config already loaded by an include: " + path);
		return true;
	}
	const auto errorsBefore = state->errors.size();
	LoadFile(path, path, state, migoto::InputDisableScope::ALL, true);
	LoadQueuedIncludes(*state);
	return state->errors.size() == errorsBefore;
}

};

// Resolve the active user settings path relative to the root configuration.
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

inline bool MigotoIniLoader::LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result,
	const std::wstring &wipePath, unsigned huntingMode)
{
	if (!rootPath || !result)
		return false;

	*result = MigotoIniLoadResult();
	if (wcslen(rootPath) >= static_cast<size_t>(MAX_PATH)) {
		// F-200 #5: DirectoryOf/JoinPath are MAX_PATH APIs; a longer root path
		// would be truncated into a different directory before the first
		// include is resolved. Reject it and report through the error list.
		result->errors.push_back(L"config path rejected: path too long");
		return wipePath.empty();
	}
	MigotoIniLoaderPath::LoaderState state;
	state.huntingMode = huntingMode;
	state.rootDir = MigotoIniLoaderPath::DirectoryOf(rootPath);
	MigotoIniLoaderPath::LoadFile(rootPath, L"", &state);
	MigotoIniLoaderPath::LoadQueuedIncludes(state);

	// The user config is loaded very last so the persisted values override the
	// defaults declared by every other ini file (DX11: ParseIncludedIniFiles()
	// loads G->user_config after the include loop). It is resolved from the
	// root file even when the file itself is missing - the save path needs the
	// path to create it, and a missing file must not be an error.
	const std::wstring configuredUserConfig = MigotoUserConfigPath::FindConfiguredName(state.document);
	auto userConfig = MigotoUserConfigPath::Resolve(state.rootDir, configuredUserConfig);
	if (userConfig.empty()) {
		// F-200 #5: the persist file path could not be built completely.
		// Reject it instead of letting the save path write to a truncated name.
		state.warnings.push_back(L"user_config path rejected: path too long: " +
			(configuredUserConfig.empty() ? std::wstring(MigotoUserConfigPath::kDefaultFileName) : configuredUserConfig));
	}
	const bool skipSavedValues = !wipePath.empty() && migoto::NormalizePersistentConfigPath(wipePath) ==
		migoto::NormalizePersistentConfigPath(userConfig);
	bool loadedUserConfig = skipSavedValues || MigotoIniLoaderPath::LoadUserConfigFile(userConfig, &state);
	if (!wipePath.empty()) {
		// A root, ordinary include or second name for the same file is still
		// part of the new configuration. Never unlink it during publication.
		std::error_code error;
		if (std::filesystem::exists(wipePath, error)) {
			for (const auto &path : state.loadedFiles) {
				if (std::filesystem::equivalent(wipePath, path, error)) {
					state.errors.push_back(L"cannot wipe settings used as a config or include: " + path);
					break;
				}
				if (error)
					break;
			}
		}
		if (error)
			state.errors.push_back(L"cannot verify the active user settings path: " + wipePath);
		if (userConfig.empty() || !state.includesComplete)
			state.errors.push_back(L"unable to wipe settings with an incomplete config: " + wipePath);
		loadedUserConfig = loadedUserConfig && state.errors.empty();
	}
	if (loadedUserConfig)
		result->userConfigPath = std::move(userConfig);

	result->includesComplete = state.includesComplete;
	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return loadedUserConfig;
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
