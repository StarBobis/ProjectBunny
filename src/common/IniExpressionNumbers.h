#pragma once

#include <cerrno>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <string_view>

namespace migoto {

inline bool ReadIniBooleanAlias(std::wstring_view text, bool &value)
{
	if (text == L"1" || text == L"true" || text == L"yes" || text == L"on") {
		value = true;
		return true;
	}
	if (text == L"0" || text == L"false" || text == L"no" || text == L"off") {
		value = false;
		return true;
	}
	return false;
}

inline bool IniBooleanValue(float value) noexcept
{
	return !std::isnan(value) && value != 0.0f;
}

inline bool ReadIniIntegerLiteral(const wchar_t *text, int &out)
{
	wchar_t *end = nullptr;
	errno = 0;
	const long long value = std::wcstoll(text, &end, 10);
	if (end == text || *end)
		return false;
	if (value <= INT_MIN)
		out = INT_MIN;
	else if (value >= INT_MAX)
		out = INT_MAX;
	else
		out = static_cast<int>(value);
	return true;
}

inline bool ConvertExpressionToInt(float value, int &out) noexcept
{
	if (std::isnan(value))
		out = 0;
	else if (value <= static_cast<float>(INT_MIN))
		out = INT_MIN;
	else if (value >= static_cast<float>(INT_MAX))
		out = INT_MAX;
	else
		out = static_cast<int>(value);
	return true;
}

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
