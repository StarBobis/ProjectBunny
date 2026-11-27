#pragma once

#include <cmath>
#include <limits>

namespace migoto::test {

struct ArithmeticCase
{
    const wchar_t *expression;
    float expected;

    bool Matches(double actual) const
    {
        if (std::isnan(expected))
            return std::isnan(actual);
        return actual == expected && (expected != 0.0f || std::signbit(actual) == std::signbit(expected));
    }
};

inline constexpr ArithmeticCase ArithmeticCases[] = {
    {L"16777217", 16777216.0f},
    {L"0b1000000000000000000000001", 16777216.0f},
    {L"1.000000059604644775390625", 1.0f},
    {L"(16777216 + 1) - 16777216", 0.0f},
    {L"16777218 - 1 - 16777216", 0.0f},
    {L"(4097 * 4097) - 16785408", 0.0f},
    {L"16777216 - (4096 ** 2 + 1)", 0.0f},
    {L"2 ** 3 ** 2", 512.0f},
    {L"2 ** -150", 0.0f},
    {L"1 / 0.00000025", 4000000.0f},
    {L"-3 // 2", -2.0f},
    {L"3 // -2", -2.0f},
    {L"0.00000075 // 0.00000025", 3.0f},
    {L"-7 % 4", -3.0f},
    {L"3 % 0.25", 0.0f},
    {L"-4 % 2", -0.0f},
    {L"1 / -0", -std::numeric_limits<float>::infinity()},
    {L"-1 // -0", std::numeric_limits<float>::infinity()},
    {L"0 / 0", std::numeric_limits<float>::quiet_NaN()},
    {L"1 % 0", std::numeric_limits<float>::quiet_NaN()},
    {L"1e38 * 10", std::numeric_limits<float>::infinity()},
    {L"-1e-99", -0.0f},
    {L"-0 == 0", 1.0f},
    {L"-0 !== 0", 1.0f},
    {L"1 == 1.0000001", 0.0f},
    {L"1 != 1.0000001", 1.0f},
    {L"16777217 <= 16777216", 1.0f},
    {L"16777217 > 16777216", 0.0f},
    {L"(1 / 0) == (1 / 0)", 1.0f},
    {L"(0 / 0) != (0 / 0)", 1.0f},
    {L"(0 / 0) >= 0", 0.0f},
    {L"!0.0000001", 0.0f},
    {L"0.0000001 && -1e-20", 1.0f},
    {L"-0 || 0", 0.0f},
    {L"!(0 / 0)", 0.0f},
    {L"0 || (0 / 0)", 1.0f},
};

} // namespace migoto::test
