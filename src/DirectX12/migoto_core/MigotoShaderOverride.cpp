#include "MigotoShaderOverride.h"

namespace Bunny {

static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = ToLower(value);
	std::wstring lowerPrefix = ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

bool ParseShaderOverrideHash(const std::wstring &text, uint64_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
	if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0)
		trimmed = trimmed.substr(2);
	if (trimmed.empty())
		return false;

	uint64_t parsed = 0;
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

void ParseShaderOverrideSections(
	const IniDocument &ini, ShaderOverrideMap *shaderOverrides)
{
	if (!shaderOverrides)
		return;

	shaderOverrides->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!StartsWithI(section.name, L"ShaderOverride"))
			continue;

		ShaderOverrideConfig config;
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
				uint64_t hash = 0;
				if (!ParseShaderOverrideHash(entry.value, &hash))
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

			CommandListAction action;
			if (ParseCommandListActionFromEntry(key, Trim(entry.value), &action)) {
				ResolveCommandListActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			ParseCommandListLinksFromEntry(key, entry.value, &config.commandLists);
		}

		ResolveCommandListLinks(&config.commandLists, section.iniNamespace);
		if (hasHash)
			(*shaderOverrides)[config.hash] = config;
	}
}

}
