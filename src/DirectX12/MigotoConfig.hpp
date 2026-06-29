#pragma once

#include <windows.h>

#include "IniDocument.hpp"

namespace Bunny {

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
	bool dx12ProxyMode = true;
	bool dx12SafeMode = false;
	bool enableOverlay = false;
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
	void ReadRuntimeConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

class MigotoConfigUtils
{
public:
	static std::wstring FindDefaultConfigPath(HINSTANCE module);
};

}

#include <Shlwapi.h>

namespace Bunny {

class MigotoConfigReadUtils
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
	ReadRuntimeConfig();
	return true;
}

inline void MigotoConfig::ReadLoaderConfig()
{
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"target", &mLoader.target);
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"module", &mLoader.module);
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	MigotoConfigReadUtils::ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	MigotoConfigReadUtils::ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	MigotoConfigReadUtils::ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	MigotoConfigReadUtils::ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	MigotoConfigReadUtils::ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	MigotoConfigReadUtils::ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

inline void MigotoConfig::ReadRuntimeConfig()
{
	MigotoConfigReadUtils::ReadBool(mIni, L"DX12", L"proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReadUtils::ReadBool(mIni, L"DX12", L"safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReadUtils::ReadBool(mIni, L"DX12", L"overlay", &mRuntime.enableOverlay);
	MigotoConfigReadUtils::ReadBool(mIni, L"System", L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReadUtils::ReadBool(mIni, L"System", L"dx12_safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReadUtils::ReadBool(mIni, L"System", L"dx12_overlay", &mRuntime.enableOverlay);
}

inline std::wstring MigotoConfigUtils::FindDefaultConfigPath(HINSTANCE module)
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
