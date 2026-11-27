#pragma once

namespace migoto::test {

struct UnaryFunctionCase
{
    const wchar_t* expression;
    float expected;
};

inline constexpr UnaryFunctionCase UnaryMathCases[] = {
    {L"countbits(7)", 3.0f},
    {L"sin(0)", 0.0f},
    {L"cos(0)", 1.0f},
    {L"tan(0)", 0.0f},
    {L"asin(1)", 1.5707963268f},
    {L"acos(1)", 0.0f},
    {L"atan(1)", 0.7853981634f},
    {L"abs(-3.25)", 3.25f},
    {L"sign(-3)", -1.0f},
    {L"sign(0)", 0.0f},
    {L"sign(3)", 1.0f},
    {L"ceil(-1.25)", -1.0f},
    {L"floor(-1.25)", -2.0f},
    {L"trunc(-1.75)", -1.0f},
    {L"round(0.5)", 1.0f},
    {L"round(-0.5)", -1.0f},
    {L"frac(-1.25)", 0.75f},
    {L"sqrt(9)", 3.0f},
    {L"rsqrt(4)", 0.5f},
    {L"exp(0)", 1.0f},
    {L"exp2(3)", 8.0f},
    {L"log(1)", 0.0f},
    {L"log2(8)", 3.0f},
    {L"saturate(-2)", 0.0f},
    {L"saturate(0.25)", 0.25f},
    {L"saturate(2)", 1.0f},
    {L"abs(-sqrt(9)) + exp2(log2(8))", 11.0f},
    {L"2 * sin(0) + 3 * cos(0)", 3.0f},
    {L"countbits(0)", 0.0f},
    {L"countbits(1)", 1.0f},
    {L"countbits(7.9)", 3.0f},
    {L"countbits(16777215)", 24.0f},
    {L"countbits(4294967040)", 24.0f},
    {L"countbits(4294967296)", 0.0f},
    {L"countbits(-1)", 32.0f},
    {L"countbits(-2.75)", 31.0f},
    {L"countbits(-4294967296)", 0.0f},
    {L"countbits(inf)", 0.0f},
    {L"countbits(-inf)", 0.0f},
    {L"countbits(nan)", 0.0f},
};

} // namespace migoto::test
