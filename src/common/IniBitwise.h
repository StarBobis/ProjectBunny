#pragma once

#include <cstdint>
#include <cstring>

namespace migoto::bitwise {

inline std::int32_t Integer(double value) noexcept
{
    // DX11 takes float32 operands. Its x64 integer conversion yields INT32_MIN
    // for NaN, infinity and overflow. Check first so the cast stays defined.
    const float operand = static_cast<float>(value);
    if (!(operand >= -2147483648.0f && operand < 2147483648.0f))
        return INT32_MIN;
    return static_cast<std::int32_t>(operand);
}

inline float Not(double value) noexcept
{
    return static_cast<float>(~Integer(value));
}

inline float And(double lhs, double rhs) noexcept
{
    return static_cast<float>(Integer(lhs) & Integer(rhs));
}

inline float Or(double lhs, double rhs) noexcept
{
    return static_cast<float>(Integer(lhs) | Integer(rhs));
}

inline float Xor(double lhs, double rhs) noexcept
{
    return static_cast<float>(Integer(lhs) ^ Integer(rhs));
}

inline float ShiftLeft(double lhs, double rhs) noexcept
{
    const auto count = static_cast<std::uint32_t>(Integer(rhs)) & 31u;
    const auto bits = static_cast<std::uint32_t>(Integer(lhs)) << count;
    std::int32_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return static_cast<float>(result);
}

inline float ShiftRight(double lhs, double rhs) noexcept
{
    const auto count = static_cast<std::uint32_t>(Integer(rhs)) & 31u;
    return static_cast<float>(Integer(lhs) >> count);
}

} // namespace migoto::bitwise
