#pragma once

#include <map>
#include <string>
#include <vector>

namespace Bunny {

class IniEntry {
public:
	std::wstring key;
	std::wstring value;
	std::wstring rawLine;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	int lineNumber = 0;
	bool hasEquals = false;
};

class IniSection {
public:
	std::wstring name;
	std::wstring originalName;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<IniEntry> entries;
};

class IniDocument {
public:
	bool LoadFromFile(const wchar_t *path);
	bool Parse(const wchar_t *text);
	bool AppendParse(
		const wchar_t *text,
		const std::wstring &sourcePath,
		const std::wstring &sourceDir,
		const std::wstring &iniNamespace);

	const IniSection *FindSection(const wchar_t *section) const;
	const std::vector<IniSection> &Sections() const { return mSections; }

	bool GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const;
	bool GetBool(const wchar_t *section, const wchar_t *key, bool *value) const;
	bool GetInt(const wchar_t *section, const wchar_t *key, int *value) const;

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	std::vector<IniSection> mSections;
	std::map<std::wstring, size_t> mSectionIndex;
	std::wstring mPath;
	std::wstring mError;
};

class IniDocumentUtils
{
public:
	static std::wstring ToLower(std::wstring value);
	static std::wstring Trim(const std::wstring &value);
	static const wchar_t *MigotoSectionPrefix(const std::wstring &section);
	static std::wstring MakeNamespacedSectionName(
		const std::wstring &section, const std::wstring &iniNamespace);
	static std::wstring ResolveNamespacedSectionReference(
		const std::wstring &reference,
		const std::wstring &prefix,
		const std::wstring &iniNamespace);
};

} // namespace Bunny

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace Bunny {

inline std::wstring IniDocumentUtils::ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	return value;
}

inline std::wstring IniDocumentUtils::Trim(const std::wstring &value)
{
	const wchar_t *spaces = L" \t\r\n";
	size_t first = value.find_first_not_of(spaces);
	if (first == std::wstring::npos)
		return L"";
	size_t last = value.find_last_not_of(spaces);
	return value.substr(first, last - first + 1);
}

class IniDocumentParseUtils
{
public:
static bool IsCommentOrEmpty(const std::wstring &line)
{
	std::wstring trimmed = IniDocumentUtils::Trim(line);
	return trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#';
}

class SectionPrefixEntry {
public:
	const wchar_t *prefix;
	bool isPrefix;
};

inline static const SectionPrefixEntry kMigotoCommandListSections[] = {
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

inline static const SectionPrefixEntry kMigotoRegularSections[] = {
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
			if (IniDocumentUtils::ToLower(section).rfind(IniDocumentUtils::ToLower(prefix), 0) == 0)
				return entry.prefix;
		} else if (IniDocumentUtils::ToLower(section) == IniDocumentUtils::ToLower(prefix)) {
			return entry.prefix;
		}
	}
	return nullptr;
}
};

inline const wchar_t *IniDocumentUtils::MigotoSectionPrefix(const std::wstring &section)
{
	const wchar_t *prefix = IniDocumentParseUtils::SectionPrefixFromList(
		section, IniDocumentParseUtils::kMigotoCommandListSections,
		sizeof(IniDocumentParseUtils::kMigotoCommandListSections) / sizeof(IniDocumentParseUtils::kMigotoCommandListSections[0]));
	if (prefix)
		return prefix;
	return IniDocumentParseUtils::SectionPrefixFromList(
		section, IniDocumentParseUtils::kMigotoRegularSections,
		sizeof(IniDocumentParseUtils::kMigotoRegularSections) / sizeof(IniDocumentParseUtils::kMigotoRegularSections[0]));
}

inline std::wstring IniDocumentUtils::MakeNamespacedSectionName(
	const std::wstring &section, const std::wstring &iniNamespace)
{
	if (iniNamespace.empty())
		return section;

	const wchar_t *prefix = IniDocumentUtils::MigotoSectionPrefix(section);
	if (!prefix)
		return section;

	size_t prefixLength = wcslen(prefix);
	if (section.size() < prefixLength)
		return section;
	return std::wstring(prefix) + L"\\" + iniNamespace + L"\\" + section.substr(prefixLength);
}

inline std::wstring IniDocumentUtils::ResolveNamespacedSectionReference(
	const std::wstring &reference,
	const std::wstring &prefix,
	const std::wstring &iniNamespace)
{
	std::wstring ref = IniDocumentUtils::Trim(reference);
	if (ref.empty())
		return L"";

	if (IniDocumentUtils::ToLower(ref).rfind(IniDocumentUtils::ToLower(prefix) + L"\\", 0) == 0)
		return ref;
	if (IniDocumentUtils::ToLower(ref).rfind(IniDocumentUtils::ToLower(prefix), 0) == 0)
		return IniDocumentUtils::MakeNamespacedSectionName(ref, iniNamespace);
	return IniDocumentUtils::MakeNamespacedSectionName(prefix + ref, iniNamespace);
}

inline bool IniDocument::LoadFromFile(const wchar_t *path)
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

inline bool IniDocument::Parse(const wchar_t *text)
{
	mSections.clear();
	mSectionIndex.clear();
	mError.clear();
	return AppendParse(text, mPath, L"", L"");
}

inline bool IniDocument::AppendParse(
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

		std::wstring trimmed = IniDocumentUtils::Trim(line);
		if (IniDocumentParseUtils::IsCommentOrEmpty(trimmed))
			continue;

		if (trimmed.size() >= 2 && trimmed.front() == L'[' && trimmed.back() == L']') {
			std::wstring originalName = IniDocumentUtils::Trim(trimmed.substr(1, trimmed.size() - 2));
			std::wstring sectionName = IniDocumentUtils::MakeNamespacedSectionName(originalName, currentNamespace);
			std::wstring sectionKey = IniDocumentUtils::ToLower(sectionName);
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
				std::wstring key = IniDocumentUtils::ToLower(IniDocumentUtils::Trim(line.substr(0, equals)));
				std::wstring value = IniDocumentUtils::Trim(line.substr(equals + 1));
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
			entry.key = IniDocumentUtils::Trim(line);
			entry.hasEquals = false;
		} else {
			entry.key = IniDocumentUtils::Trim(line.substr(0, equals));
			entry.value = IniDocumentUtils::Trim(line.substr(equals + 1));
			entry.hasEquals = true;
		}
		current->entries.push_back(entry);
	}

	return true;
}

inline const IniSection *IniDocument::FindSection(const wchar_t *section) const
{
	if (!section)
		return nullptr;
	auto found = mSectionIndex.find(IniDocumentUtils::ToLower(section));
	if (found == mSectionIndex.end())
		return nullptr;
	return &mSections[found->second];
}

inline bool IniDocument::GetString(const wchar_t *section, const wchar_t *key, std::wstring *value) const
{
	const IniSection *iniSection = FindSection(section);
	if (!iniSection || !key)
		return false;

	std::wstring keyLower = IniDocumentUtils::ToLower(key);
	for (auto it = iniSection->entries.rbegin(); it != iniSection->entries.rend(); ++it) {
		if (!it->hasEquals)
			continue;
		if (IniDocumentUtils::ToLower(it->key) == keyLower) {
			if (value)
				*value = it->value;
			return true;
		}
	}
	return false;
}

inline bool IniDocument::GetBool(const wchar_t *section, const wchar_t *key, bool *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	text = IniDocumentUtils::ToLower(IniDocumentUtils::Trim(text));
	bool parsed = text == L"1" || text == L"true" || text == L"yes" || text == L"on";
	if (!parsed && !(text == L"0" || text == L"false" || text == L"no" || text == L"off"))
		return false;
	if (value)
		*value = parsed;
	return true;
}

inline bool IniDocument::GetInt(const wchar_t *section, const wchar_t *key, int *value) const
{
	std::wstring text;
	if (!GetString(section, key, &text))
		return false;
	wchar_t *end = nullptr;
	long parsed = std::wcstol(text.c_str(), &end, 0);
	if (!end || *IniDocumentUtils::Trim(end).c_str())
		return false;
	if (value)
		*value = static_cast<int>(parsed);
	return true;
}

} // namespace Bunny
