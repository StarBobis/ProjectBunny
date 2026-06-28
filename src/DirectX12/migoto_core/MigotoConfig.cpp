#include "MigotoConfig.h"

#include <Shlwapi.h>

namespace Bunny {

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

bool MigotoConfig::Load(const wchar_t *path)
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

void MigotoConfig::ReadLoaderConfig()
{
	ReadString(mIni, L"Loader", L"target", &mLoader.target);
	ReadString(mIni, L"Loader", L"module", &mLoader.module);
	ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

void MigotoConfig::ReadRuntimeConfig()
{
	ReadBool(mIni, L"DX12", L"proxy_mode", &mRuntime.dx12ProxyMode);
	ReadBool(mIni, L"DX12", L"safe_mode", &mRuntime.dx12SafeMode);
	ReadBool(mIni, L"DX12", L"overlay", &mRuntime.enableOverlay);
	ReadBool(mIni, L"System", L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	ReadBool(mIni, L"System", L"dx12_safe_mode", &mRuntime.dx12SafeMode);
	ReadBool(mIni, L"System", L"dx12_overlay", &mRuntime.enableOverlay);
}

std::wstring FindDefaultConfigPath(HINSTANCE module)
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
