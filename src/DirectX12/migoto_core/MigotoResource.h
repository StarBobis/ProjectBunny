#pragma once

#include <stdint.h>

#include <string>
#include <unordered_map>

#include "IniDocument.h"

namespace Bunny {

struct ResourceConfig
{
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

void ParseResourceSections(const IniDocument &ini, ResourceMap *resources);

} // namespace Bunny
