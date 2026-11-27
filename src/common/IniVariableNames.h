#pragma once

#include "EnumOptionPrefix.h"
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>

enum class VariableFlags {
	NONE            = 0,
	GLOBAL          = 0x00000001,
	PERSIST         = 0x00000002,
	LOCKED          = 0x00000004,
	INVALID         = (signed)0xffffffff,
};
SENSIBLE_ENUM(VariableFlags);
inline EnumName_t<const wchar_t *, VariableFlags> VariableFlagNames[] = {
	{L"global",  VariableFlags::GLOBAL},
	{L"persist", VariableFlags::PERSIST},
	{L"locked",  VariableFlags::LOCKED},

	{NULL,       VariableFlags::INVALID} // End of list marker
};

namespace migoto {

inline bool ValidVariableName(std::wstring_view name)
{
	if (name.length() < 2)
		return false;

	// Variable names begin with a $
	if (name[0] != L'$')
		return false;

	// First character must be a letter or underscore ($1, $2, etc reserved for future arguments):
	if ((name[1] < L'a' || name[1] > L'z') && name[1] != L'_')
		return false;

	// Subsequent characters must be in this list:
	return (name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz_0123456789", 2) == std::wstring_view::npos);
}

class VariableDeclaration
{
public:
	VariableFlags flags;
	std::wstring_view name;

	static VariableDeclaration Parse(const std::wstring &key)
	{
		const wchar_t *name = nullptr;
		const auto flags = parse_enum_option_string_prefix(VariableFlagNames, key.c_str(), &name);
		return {flags, name ? name : L""};
	}

	bool IsGlobal() const { return (flags & VariableFlags::GLOBAL) != 0; }
	// Older DX12 configs also allow a bare "persist $name".
	bool IsDx12Declaration() const { return IsGlobal() || flags == VariableFlags::PERSIST; }
};


inline std::wstring NamespacedVariable(std::wstring_view name, std::wstring_view iniNamespace)
{
	if (name.empty())
		return {};
	std::wstring result = L"$\\";
	result.reserve(2 + iniNamespace.size() + name.size());
	for (wchar_t c : iniNamespace)
		result += static_cast<wchar_t>(std::towlower(c));
	result += L'\\';
	for (wchar_t c : name.substr(1)) {
		if (c <= 127)
			result += (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + L'a' - L'A') : c;
		else
			result += static_cast<wchar_t>(std::towlower(c));
	}
	return result;
}

class NamespaceScanner
{
public:
	bool Consume(wchar_t c, std::wstring_view str, size_t pos)
	{
		if (!mStarted) {
			if (c != L'\\')
				return false;
			mStarted = true;
			// The first namespace segment is arbitrary, as in the original INI
			// syntax. Further path segments may include Unicode, dots and hyphens.
			mEnd = str.find(L'\\', pos + 1);
			if (mEnd == std::wstring::npos)
				return false;
			for (size_t i = mEnd + 1; i < str.size(); ++i) {
				const wchar_t next = str[i];
				if (wcschr(L" \t[]()+*/%=<>!&|^~$@#", next))
					break;
				if (next == L'\\')
					mEnd = i;
			}
		}
		return mEnd != std::wstring::npos && pos <= mEnd;
	}

private:
	bool mStarted = false;
	size_t mEnd = std::wstring::npos;
};

inline size_t VariableTokenEnd(std::wstring_view str, size_t start, bool allowUppercase = false)
{
	// Scans a variable token and returns the end position after performing minimal syntax validation:
	// 1. Ensures identifier characters match `[a-z_0-9]+`.
	// 2. Ensures an optional namespace is properly closed while allowing arbitrary characters in the parent namespace segment.
	NamespaceScanner namespace_scanner;

	for (size_t i = start; i < str.size(); ++i)
	{
		wchar_t c = str[i];

		// Consume an optional leading namespace (e.g. "\namespace\" or "\path\like\namespace\").
		if (namespace_scanner.Consume(c, str, i))
			continue;

		// The remaining characters must be valid identifier characters.
		if (!((c >= L'a' && c <= L'z') || (allowUppercase && c >= L'A' && c <= L'Z') ||
			(c >= L'0' && c <= L'9') || c == L'_'))
			return i;
	}

	return str.size();
}

} // namespace migoto
