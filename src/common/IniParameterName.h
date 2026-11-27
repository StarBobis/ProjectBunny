#pragma once

#include <cerrno>
#include <climits>
#include <cwchar>
#include <cwctype>

namespace migoto {

struct IniParameterName {
	wchar_t component;
	int index;
};

inline bool ParseIniParameterName(const wchar_t *name, IniParameterName &result)
{
	if (!name || !*name)
		return false;
	const auto component = static_cast<wchar_t>(std::towlower(name[0]));
	if (component != L'x' && component != L'y' && component != L'z' && component != L'w')
		return false;
	int parsed_index = 0;
	if (name[1]) {
		wchar_t *end = nullptr;
		errno = 0;
		const long long value = wcstoll(name + 1, &end, 10);
		// Every consumer reserves index + 1 in a signed int. Keep the
		// existing nonnegative scanf forms, without wrapping or truncating.
		if (end == name + 1 || *end || errno == ERANGE || value < 0 || value >= INT_MAX)
			return false;
		parsed_index = static_cast<int>(value);
	}
	result = {component, parsed_index};
	return true;
}

} // namespace migoto
