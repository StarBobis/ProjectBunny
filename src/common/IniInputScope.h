#pragma once

#include <windows.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>

namespace migoto {

enum class InputDisableScope : std::int8_t {
    INVALID = -1,
    NONE = 0,
    MODS = 1,
    ALL = 2,
};
inline bool InputBindingSuppressed(InputDisableScope blocked, InputDisableScope binding)
{
	return binding >= InputDisableScope::MODS && blocked >= binding;
}

inline std::wstring NormalizeInputSourcePath(std::wstring path)
{
	if (path.empty())
		return {};
	std::replace(path.begin(), path.end(), L'/', L'\\');
	const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (!required)
		return {};
	std::wstring absolute(required, L'\0');
	const DWORD written = GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr);
	if (!written || written >= required)
		return {};
	absolute.resize(written);
	while (!absolute.empty() && absolute.back() == L'\\')
		absolute.pop_back();
	return absolute;
}

inline bool IsInputPathWithinDirectory(const std::wstring& filename, const std::wstring& directory)
{
	const std::wstring source = NormalizeInputSourcePath(filename);
	const std::wstring root = NormalizeInputSourcePath(directory);
	if (root.empty() || root.size() > INT_MAX || source.size() <= root.size() || source[root.size()] != L'\\')
		return false;
	return CompareStringOrdinal(source.data(), static_cast<int>(root.size()), root.data(),
		static_cast<int>(root.size()), TRUE) == CSTR_EQUAL;
}

} // namespace migoto
