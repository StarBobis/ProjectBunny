#include "IniDocument.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace Bunny {

std::wstring ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	return value;
}

std::wstring Trim(const std::wstring &value)
{
	const wchar_t *spaces = L" \t\r\n";
	size_t first = value.find_first_not_of(spaces);
	if (first == std::wstring::npos)
		return L"";
	size_t last = value.find_last_not_of(spaces);
	return value.substr(first, last - first + 1);
}

static bool IsCommentOrEmpty(const std::wstring &line)
{
	std::wstring trimmed = Trim(line);
	return trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#';
}

struct SectionPrefixEntry {
	const wchar_t *prefix;
	bool isPrefix;
};

static const SectionPrefixEntry kMigotoCommandListSections[] = {
	{L"TextureOverride", true},
	{L"CommandList", true},
	{L"Constants", false},
	{L"Present", false},
	{L"ShaderOverride", true},
	{L"CustomShader", true},
	{L"ShaderRegex", true},
	{L"BuiltInCommandList", true},
	{L"BuiltInCustomShader", true},
	{L"ClearRenderTargetView", false},
	{L"ClearDepthStencilView", false},
	{L"ClearUnorderedAccessViewUint", false},
	{L"ClearUnorderedAccessViewFloat", false},
};

static const SectionPrefixEntry kMigotoRegularSections[] = {
	{L"Pool", true},
	{L"Resource", true},
	{L"Key", true},
	{L"Include", true},
	{L"Preset", true},
	{L"Hunting", false},
	{L"Logging", false},
	{L"System", false},
	{L"Device", false},
	{L"Rendering", false},
	{L"Loader", false},
	{L"Profile", false},
	{L"Stereo", false},
	{L"ConvergenceMap", false},
};

static const wchar_t *SectionPrefixFromList(
	const std::wstring &section, const SectionPrefixEntry *entries, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		const SectionPrefixEntry &entry = entries[i];
		std::wstring prefix = entry.prefix;
		if (entry.isPrefix) {
			if (ToLower(section).rfind(ToLower(prefix), 0) == 0)
				return entry.prefix;
		} else if (ToLower(section) == ToLower(prefix)) {
			return entry.prefix;
		}
	}
	return nullptr;
}

const wchar_t *MigotoSectionPrefix(const std::wstring &section)
{
	const wchar_t *prefix = SectionPrefixFromList(
		section, kMigotoCommandListSections,
		sizeof(kMigotoCommandListSections) / sizeof(kMigotoCommandListSections[0]));
	if (prefix)
		return prefix;
	return SectionPrefixFromList(
		section, kMigotoRegularSections,
		sizeof(kMigotoRegularSections) / sizeof(kMigotoRegularSections[0]));
}

std::wstring MakeNamespacedSectionName(
	const std::wstring &section, const std::wstring &iniNamespace)
{
	if (iniNamespace.empty())
		return section;

	const wchar_t *prefix = MigotoSectionPrefix(section);
	if (!prefix)
		return section;

	size_t prefixLength = wcslen(prefix);
	if (section.size() < prefixLength)
		return section;
	return std::wstring(prefix) + L"\\" + iniNamespace + L"\\" + section.substr(prefixLength);
}

std::wstring ResolveNamespacedSectionReference(
	const std::wstring &reference,
	const std::wstring &prefix,
	const std::wstring &iniNamespace)
{
	std::wstring ref = Trim(reference);
	if (ref.empty())
		return L"";

	if (ToLower(ref).rfind(ToLower(prefix) + L"\\", 0) == 0)
		return ref;
	if (ToLower(ref).rfind(ToLower(prefix), 0) == 0)
		return MakeNamespacedSectionName(ref, iniNamespace);
	return MakeNamespacedSectionName(prefix + ref, iniNamespace);
}

bool IniDocument::LoadFromFile(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();

	std::ifstream file(mPath, std::ios::binary);
	if (!file.is_open()) {
		mError = L"Unable to open ini file";
		return false;
	}

	std::string bytes(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
	}

	UINT codePage = CP_UTF8;
	int length = MultiByteToWideChar(
		codePage, MB_ERR_INVALID_CHARS, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) {
		codePage = CP_ACP;
		length = MultiByteToWideChar(
			codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
			nullptr, 0);
	}
	if (length < 0) {
		mError = L"Unable to decode ini file";
		return false;
	}

	std::wstring text(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &text[0], length)) {
			mError = L"Unable to decode ini file";
			return false;
		}
	}
	return Parse(text.c_str());
}

bool IniDocument::Parse(const wchar_t *text)
{
	mSections.clear();
	mSectionIndex.clear();
	mError.clear();
	return AppendParse(text, mPath, L"", L"");
}

bool IniDocument::AppendParse(
	const wchar_t *text,
	const std::wstring &sourcePath,
	const std::wstring &sourceDir,
	const std::wstring &iniNamespace)
{
	if (!text)
		return true;

	IniSection *current = nullptr;
	std::wstring currentNamespace = iniNamespace;
	std::wistringstream stream(text);
	std::wstring line;
	int lineNumber = 0;

	while (std::getline(stream, line)) {
		lineNumber++;
		if (lineNumber == 1 && !line.empty() && line.front() == L'\xfeff')
			line.erase(line.begin());
		if (lineNumber == 1 && line.size() >= 3 &&
		    line[0] == L'\xef' && line[1] == L'\xbb' && line[2] == L'\xbf')
			line.erase(0, 3);
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();

		std::wstring trimmed = Trim(line);
		if (IsCommentOrEmpty(trimmed))
			continue;

		if (trimmed.size() >= 2 && trimmed.front() == L'[' && trimmed.back() == L']') {
			std::wstring originalName = Trim(trimmed.substr(1, trimmed.size() - 2));
			std::wstring sectionName = MakeNamespacedSectionName(originalName, currentNamespace);
			std::wstring sectionKey = ToLower(sectionName);
			auto found = mSectionIndex.find(sectionKey);
			if (found == mSectionIndex.end()) {
				IniSection section;
				section.name = sectionName;
				section.originalName = originalName;
				section.sourcePath = sourcePath;
				section.sourceDir = sourceDir;
				section.iniNamespace = sectionName == originalName ? L"" : currentNamespace;
				mSections.push_back(section);
				mSectionIndex[sectionKey] = mSections.size() - 1;
				current = &mSections.back();
			} else {
				current = &mSections[found->second];
			}
			continue;
		}

		if (!current) {
			size_t equals = line.find(L'=');
			if (equals != std::wstring::npos) {
				std::wstring key = ToLower(Trim(line.substr(0, equals)));
				std::wstring value = Trim(line.substr(equals + 1));
				if (key == L"namespace") {
					currentNamespace = value;
					continue;
				}
				if (key == L"condition")
					continue;
			}
			mError = L"Ini entry found before first section";
			return false;
		}

		IniEntry entry;
		entry.rawLine = line;
		entry.sourcePath = sourcePath;
		entry.sourceDir = sourceDir;
		entry.iniNamespace = currentNamespace;
		entry.lineNumber = lineNumber;

		size_t equals = line.find(L'=');
		if (equals == std::wstring::npos) {
			entry.key = Trim(line);
			entry.hasEquals = false;
		} else {
			entry.key = Trim(line.substr(0, equals));
			entry.value = Trim(line.substr(equals + 1));
			entry.hasEquals = true;
		}
		current->entries.push_back(entry);
	}

	return true;
}

const IniSection *IniDocument::FindSection(const wchar_t *section) const
{
	if (!section)
		return nullptr;
	auto found = mSectionIndex.find(ToLower(section));
	if (found == mSectionIndex.end())
		return nullptr;
	return &mSections[found->second];
}

bool IniDocument::GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const
{
	const IniSection *iniSection = FindSection(section);
	if (!iniSection || !key)
		return false;

	std::wstring keyLower = ToLower(key);
	for (auto it = iniSection->entries.rbegin(); it != iniSection->entries.rend(); ++it) {
		if (!it->hasEquals)
			continue;
		if (ToLower(it->key) == keyLower) {
			if (value)
				*value = it->value;
			return true;
		}
	}
	return false;
}

bool IniDocument::GetBool(const wchar_t *section, const wchar_t *key, bool *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	text = ToLower(Trim(text));
	bool parsed = text == L"1" || text == L"true" || text == L"yes" || text == L"on";
	if (!parsed && !(text == L"0" || text == L"false" || text == L"no" || text == L"off"))
		return false;
	if (value)
		*value = parsed;
	return true;
}

bool IniDocument::GetInt(const wchar_t *section, const wchar_t *key, int *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	wchar_t *end = nullptr;
	long parsed = std::wcstol(text.c_str(), &end, 0);
	if (!end || *Trim(end).c_str())
		return false;
	if (value)
		*value = static_cast<int>(parsed);
	return true;
}

} // namespace Bunny
