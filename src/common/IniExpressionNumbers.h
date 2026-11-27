#pragma once

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cwchar>
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

// The view must end at a NUL terminator because wcstof reads a prefix.
// The DX12 grammar also accepts the uppercase binary prefix.
inline bool ReadExpressionNumber(std::wstring_view input, float& out, size_t& length, bool uppercaseBinary = false)
{
    if (input.size() >= 3 && input[0] == L'0' &&
        (input[1] == L'b' || (uppercaseBinary && input[1] == L'B'))) {
        std::uint64_t value;
        if (!ReadBinaryDigits(input, 2, value, length))
            return false;
        out = static_cast<float>(value);
        length += 2;
        return true;
    }

    const wchar_t* begin = input.empty() ? L"" : input.data();
    wchar_t* end = nullptr;
    errno = 0;
    out = std::wcstof(begin, &end);
    if (end == begin)
        return false;
    length = static_cast<size_t>(end - begin);
    return true;
}

namespace arithmetic {

inline float Add(float lhs, float rhs) noexcept { return lhs + rhs; }
inline float Subtract(float lhs, float rhs) noexcept { return lhs - rhs; }
inline float Multiply(float lhs, float rhs) noexcept { return lhs * rhs; }
inline float Divide(float lhs, float rhs) noexcept { return lhs / rhs; }
inline float FloorDivide(float lhs, float rhs) noexcept { return std::floor(lhs / rhs); }
inline float Remainder(float lhs, float rhs) noexcept { return std::fmod(lhs, rhs); }
inline float Power(float lhs, float rhs) noexcept { return std::pow(lhs, rhs); }

} // namespace arithmetic

inline bool EqualFloatBits(float lhs, float rhs) noexcept
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

} // namespace migoto
