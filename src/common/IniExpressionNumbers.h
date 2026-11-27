#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace migoto {

inline bool ReadBinaryDigits(std::wstring_view input, size_t start, std::uint64_t& out, size_t& length)
{
    std::uint64_t value = 0;
    length = 0;
    for (; start < input.size(); ++start, ++length) {
        const wchar_t c = input[start];
        if (c != L'0' && c != L'1')
            break;
        if (length >= 64)
            return false;
        value = (value << 1) | (c - L'0');
    }
    if (!length)
        return false;
    out = value;
    return true;
}

inline bool EqualFloatBits(float lhs, float rhs) noexcept
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

} // namespace migoto
