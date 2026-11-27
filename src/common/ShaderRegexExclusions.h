#pragma once

#include <cstdint>
#include <set>
#include <string_view>

namespace migoto {

inline bool AddShaderRegexExcludedHash(std::set<std::uint64_t>& hashes, std::wstring_view value)
{
    if (value.size() >= 2 && value[0] == L'0' && (value[1] == L'x' || value[1] == L'X'))
        value.remove_prefix(2);
    if (value.empty() || value.size() > 16)
        return false;

    std::uint64_t hash = 0;
    for (wchar_t c : value) {
        unsigned digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else return false;
        hash = (hash << 4) | digit;
    }
    hashes.insert(hash);
    return true;
}

} // namespace migoto
