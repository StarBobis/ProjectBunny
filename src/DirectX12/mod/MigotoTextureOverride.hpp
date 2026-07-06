#pragma once

#include <stdint.h>

#include <map>
#include <vector>
#include <string>
#include <unordered_map>

#include "IniDocument.hpp"
#include "MigotoCommandList.hpp"

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

class NumericMatch
{
public:
	NumericMatchOp op = NumericMatchOp::Equal;
	uint32_t value = 0;
	bool enabled = false;
};

class TextureOverrideConfig
{
public:
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
	// Draw-call texture slot replacement (ps-tN/vs-tN/this=).
	std::map<uint32_t, std::wstring> drawPsSrvResources;
	std::map<uint32_t, std::wstring> drawVsSrvResources;
	std::wstring drawReplaceThisResource;
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

class MigotoTextureOverrideParser
{
public:
	static bool ParseHash(const std::wstring &text, uint32_t *value);
	static void ParseSections(
		const IniDocument &ini, TextureOverrideMap *textureOverrides);
};

} // namespace Bunny

#include "MigotoShaderOverride.hpp"

#include <algorithm>

namespace Bunny {

class MigotoTextureOverrideParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseSrvIndexedKey(
	const std::wstring &key, const wchar_t *prefix, const wchar_t *suffix,
	uint32_t *index)
{
	if (!index || !StartsWithI(key, prefix))
		return false;

	const size_t prefixLength = wcslen(prefix);
	const size_t suffixLength = suffix ? wcslen(suffix) : 0;
	if (key.size() <= prefixLength + suffixLength)
		return false;
	if (suffixLength) {
		std::wstring keySuffix = key.substr(key.size() - suffixLength);
		if (IniDocumentText::ToLower(keySuffix) != IniDocumentText::ToLower(suffix))
			return false;
	}

	const std::wstring digits = key.substr(
		prefixLength, key.size() - prefixLength - suffixLength);
	if (digits.empty())
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : digits) {
		if (ch < L'0' || ch > L'9')
			return false;
		const uint32_t digit = static_cast<uint32_t>(ch - L'0');
		if (parsed > (UINT32_MAX - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}

	*index = parsed;
	return true;
}

static bool ParseInt(const std::wstring &text, int *value);
static bool ParseUInt(const std::wstring &text, uint32_t *value);
static bool ParseNumericMatch(const std::wstring &text, NumericMatch *match);
static bool ParseUInt64(const std::wstring &text, uint64_t *value);
static bool ParseVertexBufferKey(const std::wstring &key, uint32_t *slot);
static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace);

};

inline bool MigotoTextureOverrideParser::ParseHash(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty() || trimmed.size() > 8)
		return false;

	uint32_t parsed = 0;
	for (wchar_t ch : trimmed) {
		unsigned digit = 0;
		if (ch >= L'0' && ch <= L'9')
			digit = ch - L'0';
		else if (ch >= L'a' && ch <= L'f')
			digit = ch - L'a' + 10;
		else if (ch >= L'A' && ch <= L'F')
			digit = ch - L'A' + 10;
		else
			return false;
		parsed = (parsed << 4) | digit;
	}

	*value = parsed;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseInt(const std::wstring &text, int *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	long parsed = std::wcstol(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<int>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseNumericMatch(const std::wstring &text, NumericMatch *match)
{
	if (!match)
		return false;

	std::wstring value = IniDocumentText::Trim(text);
	if (value.empty())
		return false;

	NumericMatchOp op = NumericMatchOp::Equal;
	if (value.rfind(L"<=", 0) == 0) {
		op = NumericMatchOp::LessEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L">=", 0) == 0) {
		op = NumericMatchOp::GreaterEqual;
		value = IniDocumentText::Trim(value.substr(2));
	} else if (value.rfind(L"!", 0) == 0) {
		op = NumericMatchOp::NotEqual;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"=", 0) == 0) {
		op = NumericMatchOp::Equal;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L"<", 0) == 0) {
		op = NumericMatchOp::Less;
		value = IniDocumentText::Trim(value.substr(1));
	} else if (value.rfind(L">", 0) == 0) {
		op = NumericMatchOp::Greater;
		value = IniDocumentText::Trim(value.substr(1));
	}

	uint32_t parsed = 0;
	if (!MigotoTextureOverrideParserText::ParseUInt(value, &parsed))
		return false;

	match->op = op;
	match->value = parsed;
	match->enabled = true;
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseUInt64(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long long parsed = std::wcstoull(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint64_t>(parsed);
	return true;
}

inline bool MigotoTextureOverrideParserText::ParseVertexBufferKey(const std::wstring &key, uint32_t *slot)
{
	if (!slot || key.size() < 3 || key[0] != L'v' || key[1] != L'b')
		return false;

	uint32_t parsed = 0;
	for (size_t i = 2; i < key.size(); ++i) {
		if (key[i] < L'0' || key[i] > L'9')
			return false;
		parsed = parsed * 10 + static_cast<uint32_t>(key[i] - L'0');
	}

	*slot = parsed;
	return true;
}

inline std::wstring MigotoTextureOverrideParserText::ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return IniDocumentText::ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

inline void MigotoTextureOverrideParser::ParseSections(
	const IniDocument &ini, TextureOverrideMap *textureOverrides)
{
	if (!textureOverrides)
		return;

	textureOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoTextureOverrideParserText::StartsWithI(section.name, L"TextureOverride"))
			continue;

		TextureOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

		for (const IniEntry &entry : section.entries) {
			std::wstring key = IniDocumentText::ToLower(entry.key);
			if (!entry.hasEquals)
				continue;

			if (key == L"hash") {
				uint32_t hash = 0;
				if (!MigotoTextureOverrideParser::ParseHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = IniDocumentText::ToLower(IniDocumentText::Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			if (key == L"ib") {
				config.indexBufferResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			uint32_t vbSlot = 0;
			if (MigotoTextureOverrideParserText::ParseVertexBufferKey(key, &vbSlot)) {
				config.vertexBufferResources[vbSlot] =
					MigotoTextureOverrideParserText::ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
					MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			if (key == L"match_vertex_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchVertexCount))
					config.hasMatchVertexCount = true;
				continue;
			}

			if (key == L"match_index_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchIndexCount))
					config.hasMatchIndexCount = true;
				continue;
			}

			if (key == L"match_instance_count") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchInstanceCount))
					config.hasMatchInstanceCount = true;
				continue;
			}

			if (key == L"match_first_vertex") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstVertex))
					config.hasMatchFirstVertex = true;
				continue;
			}

			if (key == L"match_first_index") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstIndex))
					config.hasMatchFirstIndex = true;
				continue;
			}

			if (key == L"match_first_instance") {
				if (MigotoTextureOverrideParserText::ParseNumericMatch(entry.value, &config.matchFirstInstance))
					config.hasMatchFirstInstance = true;
				continue;
			}

			if (key == L"match_priority") {
				int value = 0;
				if (MigotoTextureOverrideParserText::ParseInt(entry.value, &value)) {
					config.matchPriority = value;
					config.hasMatchPriority = true;
				}
				continue;
			}

			if (key == L"cs_hash") {
				uint64_t value = 0;
				if (MigotoShaderOverrideParser::ParseHash(entry.value, &value)) {
					config.csHash = value;
					config.hasCsHash = true;
				}
				continue;
			}

			if (key == L"match_uav_bytes") {
				uint64_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt64(entry.value, &value)) {
					config.matchUavBytes = value;
					config.hasMatchUavBytes = true;
				}
				continue;
			}

			uint32_t srvIndex = 0;
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_cs_t", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"match_srv", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (MigotoTextureOverrideParser::ParseHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"cs-t", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"srv", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			// Draw-call texture slot replacement: ps-tN / vs-tN / this=
			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"ps-t", L"", &srvIndex)) {
				config.drawPsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (MigotoTextureOverrideParserText::ParseSrvIndexedKey(key, L"vs-t", L"", &srvIndex)) {
				config.drawVsSrvResources[srvIndex] =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"this") {
				config.drawReplaceThisResource =
					MigotoTextureOverrideParserText::ResolveResourceReference(IniDocumentText::Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"override_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"override_vertex_count") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.overrideVertexCount = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"uav_byte_stride") {
				uint32_t value = 0;
				if (MigotoTextureOverrideParserText::ParseUInt(entry.value, &value)) {
					config.uavByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, IniDocumentText::Trim(entry.value), &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
		if (config.overrideByteStride && config.overrideVertexCount)
			config.overrideByteWidth =
				static_cast<uint64_t>(config.overrideByteStride) * config.overrideVertexCount;
		if (config.uavByteStride && config.overrideByteWidth)
			config.overrideNumElements = config.overrideByteWidth / config.uavByteStride;
		const bool hasDrawContextMatch =
			config.hasMatchVertexCount || config.hasMatchIndexCount ||
			config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
			config.hasMatchFirstIndex || config.hasMatchFirstInstance;
		if (hasHash || config.hasVertexLimitRaise || config.hasMatchCs || config.hasCsHash || hasDrawContextMatch)
			(*textureOverrides)[config.hash].push_back(config);
	}

	for (auto &bucket : *textureOverrides) {
		std::sort(bucket.second.begin(), bucket.second.end(),
			[](const TextureOverrideConfig &lhs, const TextureOverrideConfig &rhs) {
				if (lhs.matchPriority != rhs.matchPriority)
					return lhs.matchPriority > rhs.matchPriority;
				return IniDocumentText::ToLower(lhs.section) < IniDocumentText::ToLower(rhs.section);
			});
	}
}

} // namespace Bunny
