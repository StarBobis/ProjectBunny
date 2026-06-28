#include "MigotoTextureOverride.h"

#include "MigotoShaderOverride.h"

#include <algorithm>

namespace Bunny {

static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = ToLower(value);
	std::wstring lowerPrefix = ToLower(prefix);
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
		if (ToLower(keySuffix) != ToLower(suffix))
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

bool ParseTextureOverrideHash(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
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

static bool ParseInt(const std::wstring &text, int *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	long parsed = std::wcstol(trimmed.c_str(), &end, 0);
	if (!end || *Trim(end).c_str())
		return false;

	*value = static_cast<int>(parsed);
	return true;
}

static bool ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

static bool ParseNumericMatch(const std::wstring &text, NumericMatch *match)
{
	if (!match)
		return false;

	std::wstring value = Trim(text);
	if (value.empty())
		return false;

	NumericMatchOp op = NumericMatchOp::Equal;
	if (value.rfind(L"<=", 0) == 0) {
		op = NumericMatchOp::LessEqual;
		value = Trim(value.substr(2));
	} else if (value.rfind(L">=", 0) == 0) {
		op = NumericMatchOp::GreaterEqual;
		value = Trim(value.substr(2));
	} else if (value.rfind(L"!", 0) == 0) {
		op = NumericMatchOp::NotEqual;
		value = Trim(value.substr(1));
	} else if (value.rfind(L"=", 0) == 0) {
		op = NumericMatchOp::Equal;
		value = Trim(value.substr(1));
	} else if (value.rfind(L"<", 0) == 0) {
		op = NumericMatchOp::Less;
		value = Trim(value.substr(1));
	} else if (value.rfind(L">", 0) == 0) {
		op = NumericMatchOp::Greater;
		value = Trim(value.substr(1));
	}

	uint32_t parsed = 0;
	if (!ParseUInt(value, &parsed))
		return false;

	match->op = op;
	match->value = parsed;
	match->enabled = true;
	return true;
}

static bool ParseUInt64(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long long parsed = std::wcstoull(trimmed.c_str(), &end, 0);
	if (!end || *Trim(end).c_str())
		return false;

	*value = static_cast<uint64_t>(parsed);
	return true;
}

static bool ParseVertexBufferKey(const std::wstring &key, uint32_t *slot)
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

static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

void ParseTextureOverrideSections(
	const IniDocument &ini, TextureOverrideMap *textureOverrides)
{
	if (!textureOverrides)
		return;

	textureOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!StartsWithI(section.name, L"TextureOverride"))
			continue;

		TextureOverrideConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;
		bool hasHash = false;

		for (const IniEntry &entry : section.entries) {
			std::wstring key = ToLower(entry.key);
			if (!entry.hasEquals)
				continue;

			if (key == L"hash") {
				uint32_t hash = 0;
				if (!ParseTextureOverrideHash(entry.value, &hash))
					continue;
				config.hash = hash;
				hasHash = true;
				continue;
			}

			if (key == L"handling") {
				std::wstring value = ToLower(Trim(entry.value));
				if (value == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (ParseCommandListActionFromEntry(key, value, &action)) {
						ResolveCommandListActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			if (key == L"ib") {
				config.indexBufferResource =
					ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (ParseCommandListActionFromEntry(key, Trim(entry.value), &action)) {
					ResolveCommandListActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			uint32_t vbSlot = 0;
			if (ParseVertexBufferKey(key, &vbSlot)) {
				config.vertexBufferResources[vbSlot] =
					ResolveResourceReference(entry.value, entry.iniNamespace);
				CommandListAction action;
				if (ParseCommandListActionFromEntry(key, Trim(entry.value), &action)) {
					ResolveCommandListActionReferences(&action, entry.iniNamespace);
					config.actions.push_back(action);
				}
				continue;
			}

			if (key == L"match_vertex_count") {
				if (ParseNumericMatch(entry.value, &config.matchVertexCount))
					config.hasMatchVertexCount = true;
				continue;
			}

			if (key == L"match_index_count") {
				if (ParseNumericMatch(entry.value, &config.matchIndexCount))
					config.hasMatchIndexCount = true;
				continue;
			}

			if (key == L"match_instance_count") {
				if (ParseNumericMatch(entry.value, &config.matchInstanceCount))
					config.hasMatchInstanceCount = true;
				continue;
			}

			if (key == L"match_first_vertex") {
				if (ParseNumericMatch(entry.value, &config.matchFirstVertex))
					config.hasMatchFirstVertex = true;
				continue;
			}

			if (key == L"match_first_index") {
				if (ParseNumericMatch(entry.value, &config.matchFirstIndex))
					config.hasMatchFirstIndex = true;
				continue;
			}

			if (key == L"match_first_instance") {
				if (ParseNumericMatch(entry.value, &config.matchFirstInstance))
					config.hasMatchFirstInstance = true;
				continue;
			}

			if (key == L"match_priority") {
				int value = 0;
				if (ParseInt(entry.value, &value)) {
					config.matchPriority = value;
					config.hasMatchPriority = true;
				}
				continue;
			}

			if (key == L"cs_hash") {
				uint64_t value = 0;
				if (ParseShaderOverrideHash(entry.value, &value)) {
					config.csHash = value;
					config.hasCsHash = true;
				}
				continue;
			}

			if (key == L"match_uav_bytes") {
				uint64_t value = 0;
				if (ParseUInt64(entry.value, &value)) {
					config.matchUavBytes = value;
					config.hasMatchUavBytes = true;
				}
				continue;
			}

			uint32_t srvIndex = 0;
			if (ParseSrvIndexedKey(key, L"match_cs_t", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (ParseTextureOverrideHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (ParseSrvIndexedKey(key, L"match_srv", L"_hash", &srvIndex)) {
				uint32_t value = 0;
				if (ParseTextureOverrideHash(entry.value, &value))
					config.preSkinMatchCsSrvHashes[srvIndex] = value;
				continue;
			}

			if (ParseSrvIndexedKey(key, L"cs-t", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					ResolveResourceReference(Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (ParseSrvIndexedKey(key, L"srv", L"", &srvIndex)) {
				config.preSkinCsSrvResources[srvIndex] =
					ResolveResourceReference(Trim(entry.value), entry.iniNamespace);
				continue;
			}

			if (key == L"override_byte_stride") {
				uint32_t value = 0;
				if (ParseUInt(entry.value, &value)) {
					config.overrideByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"override_vertex_count") {
				uint32_t value = 0;
				if (ParseUInt(entry.value, &value)) {
					config.overrideVertexCount = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			if (key == L"uav_byte_stride") {
				uint32_t value = 0;
				if (ParseUInt(entry.value, &value)) {
					config.uavByteStride = value;
					config.hasVertexLimitRaise = true;
				}
				continue;
			}

			CommandListAction action;
			if (ParseCommandListActionFromEntry(key, Trim(entry.value), &action)) {
				ResolveCommandListActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			ParseCommandListLinksFromEntry(key, entry.value, &config.commandLists);
		}

		ResolveCommandListLinks(&config.commandLists, section.iniNamespace);
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
				return ToLower(lhs.section) < ToLower(rhs.section);
			});
	}
}

} // namespace Bunny
