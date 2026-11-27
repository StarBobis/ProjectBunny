#pragma once

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <vector>

#include "IniVariableNames.h"
#include "IniExpressionNumbers.h"

namespace migoto {

// Compare saved float bits so signed zero and NaNs survive reload confirmation.
using UnknownSettingsSnapshot = std::map<std::wstring, uint32_t>;

inline void RecordUnknownSetting(UnknownSettingsSnapshot &snapshot, const std::wstring &name, float value)
{
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	snapshot.emplace(name, bits);
}

enum class UnknownSettingsAction { Keep, Rewrite, Remove };

inline UnknownSettingsAction CompareUnknownSettings(bool clearUnknown, const UnknownSettingsSnapshot &current,
	const UnknownSettingsSnapshot *previous)
{
	if (current.empty())
		return UnknownSettingsAction::Keep;
	const bool unchanged = previous && current == *previous;
	if (clearUnknown && unchanged)
		return UnknownSettingsAction::Remove;
	if (!clearUnknown && !unchanged)
		return UnknownSettingsAction::Rewrite;
	return UnknownSettingsAction::Keep;
}

inline std::wstring NormalizePersistentConfigPath(std::wstring path)
{
	if (path.empty())
		return path;
	std::replace(path.begin(), path.end(), L'/', L'\\');
	const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (required) {
		std::vector<wchar_t> absolute(required);
		if (GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr))
			path.assign(absolute.data());
	}
	std::transform(path.begin(), path.end(), path.begin(), ::towlower);
	return path;
}

// Both backends pass a lowercase key. Pool slots use their decimal index.
inline bool NormalizeSavedVariableName(std::wstring &normalized)
{
	if (normalized.size() < 2 || normalized[0] != L'$' || normalized.find_first_of(L"\r\n") != std::wstring::npos)
		return false;
	std::wstring base_name = normalized;
	const size_t bracket = normalized.find(L'[');
	if (bracket != std::wstring::npos) {
		if (bracket <= 5 || normalized.compare(0, 5, L"$pool") != 0)
			return false;
		if (normalized.back() != L']' || bracket + 2 >= normalized.size())
			return false;
		const std::wstring slot = normalized.substr(bracket + 1, normalized.size() - bracket - 2);
		if (slot.find_first_not_of(L"0123456789") != std::wstring::npos)
			return false;
		errno = 0;
		wchar_t* slot_end = nullptr;
		const unsigned long long index = wcstoull(slot.c_str(), &slot_end, 10);
		if (errno == ERANGE || !slot_end || *slot_end || index > SIZE_MAX)
			return false;
		base_name.resize(bracket);
		normalized = base_name + L"[" + std::to_wstring(index) + L"]";
	}
	if (base_name.size() < 2)
		return false;
	if (base_name[1] == L'\\' || (bracket != std::wstring::npos && base_name.find(L'\\') != std::wstring::npos)) {
		const size_t leaf = base_name.find_last_of(L'\\');
		if (leaf <= 2 || !ValidVariableName(L"$" + base_name.substr(leaf + 1)))
			return false;
	} else if (!ValidVariableName(base_name)) {
		return false;
	}
	return true;
}

// The caller folds case before parsing, as for other INI literals.
inline bool ReadSavedLiteral(std::wstring literal, float &parsed)
{
	const size_t first = literal.find_first_not_of(L" \t\r\n");
	if (first == std::wstring::npos)
		return false;
	literal = literal.substr(first, literal.find_last_not_of(L" \t\r\n") - first + 1);
	size_t prefix = literal[0] == L'-' || literal[0] == L'+' ? 1 : 0;
	if (literal.size() > prefix + 2 && literal[prefix] == L'0' && literal[prefix + 1] == L'b') {
		uint64_t binary;
		size_t length;
		if (!ReadBinaryDigits(literal, prefix + 2, binary, length) || length + prefix + 2 != literal.size())
			return false;
		parsed = static_cast<float>(binary);
		if (literal[0] == L'-')
			parsed = -parsed;
	} else {
		wchar_t* end = nullptr;
		parsed = wcstof(literal.c_str(), &end);
		if (end == literal.c_str() || *end)
			return false;
	}
	return true;
}

// Each backend supplies its own exclusive file opener and access attributes.
// The destination is replaced only after its caller has closed a complete file.
class StagedSettingsFile
{
public:
	StagedSettingsFile() = default;
	StagedSettingsFile(const StagedSettingsFile &) = delete;
	StagedSettingsFile &operator=(const StagedSettingsFile &) = delete;

	template<class OpenExclusive>
	int Open(FILE **file, const std::wstring &destination, OpenExclusive openExclusive)
	{
		int error = EEXIST;
		for (unsigned attempt = 0; attempt < 32 && error == EEXIST; ++attempt) {
			auto candidate = destination + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
				std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
			error = openExclusive(file, candidate.c_str(), L"w");
			if (error == 0) {
				path.swap(candidate);
				return 0;
			}
		}
		return error;
	}

	DWORD Replace(const std::wstring &destination)
	{
		if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return GetLastError();
		path.clear();
		return ERROR_SUCCESS;
	}

	~StagedSettingsFile()
	{
		if (!path.empty())
			DeleteFileW(path.c_str());
	}

private:
	inline static std::atomic<unsigned long long> sequence{0};
	std::wstring path;
};

} // namespace migoto
