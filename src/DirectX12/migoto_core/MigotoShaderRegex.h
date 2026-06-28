#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "IniDocument.h"
#include "MigotoCommandList.h"

namespace Bunny {

struct ShaderRegexConfig
{
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

void ParseShaderRegexSections(
	const IniDocument &ini, ShaderRegexMap *shaderRegexes);

}
