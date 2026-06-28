#pragma once

#include <windows.h>

#include "IniDocument.h"

namespace Bunny {

struct LoaderConfig {
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

struct RuntimeConfig {
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

private:
	void ReadLoaderConfig();
	void ReadRuntimeConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

std::wstring FindDefaultConfigPath(HINSTANCE module);

}
