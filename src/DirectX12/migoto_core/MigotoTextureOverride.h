#pragma once

#include <stdint.h>

#include <map>
#include <vector>
#include <string>
#include <unordered_map>

#include "IniDocument.h"
#include "MigotoCommandList.h"

namespace Bunny {

enum class NumericMatchOp
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual
};

struct NumericMatch
{
	NumericMatchOp op = NumericMatchOp::Equal;
	uint32_t value = 0;
	bool enabled = false;
};

struct TextureOverrideConfig
{
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	uint32_t hash = 0;
	bool handlingSkip = false;
	std::wstring indexBufferResource;
	std::map<uint32_t, std::wstring> vertexBufferResources;
	bool hasMatchVertexCount = false;
	bool hasMatchIndexCount = false;
	bool hasMatchInstanceCount = false;
	bool hasMatchFirstVertex = false;
	bool hasMatchFirstIndex = false;
	bool hasMatchFirstInstance = false;
	bool hasMatchCs = false;
	bool hasMatchUavBytes = false;
	bool hasCsHash = false;
	std::map<uint32_t, uint32_t> preSkinMatchCsSrvHashes;
	std::map<uint32_t, std::wstring> preSkinCsSrvResources;
	NumericMatch matchVertexCount;
	NumericMatch matchIndexCount;
	NumericMatch matchInstanceCount;
	NumericMatch matchFirstVertex;
	NumericMatch matchFirstIndex;
	NumericMatch matchFirstInstance;
	uint64_t matchCs = 0;
	uint64_t matchUavBytes = 0;
	uint64_t csHash = 0;
	bool hasMatchPriority = false;
	int matchPriority = 0;
	bool hasVertexLimitRaise = false;
	uint32_t overrideByteStride = 0;
	uint32_t overrideVertexCount = 0;
	uint64_t overrideByteWidth = 0;
	uint32_t uavByteStride = 0;
	uint64_t overrideNumElements = 0;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
};

using TextureOverrideList = std::vector<TextureOverrideConfig>;
using TextureOverrideMap = std::unordered_map<uint32_t, TextureOverrideList>;

bool ParseTextureOverrideHash(const std::wstring &text, uint32_t *value);
void ParseTextureOverrideSections(
	const IniDocument &ini, TextureOverrideMap *textureOverrides);

} // namespace Bunny
