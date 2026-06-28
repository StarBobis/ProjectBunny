#pragma once

#include <string>
#include <vector>

#include "IniDocument.h"

namespace Bunny {

struct MigotoIniLoadResult
{
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

bool LoadMigotoIniWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result);

} // namespace Bunny
