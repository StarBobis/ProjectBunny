#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace migoto::unary {

inline std::uint32_t CountSetBits(float value)
{
    if (!std::isfinite(value))
        return 0;
    double wrapped = std::fmod(std::trunc(static_cast<double>(value)), 4294967296.0);
    if (wrapped < 0)
        wrapped += 4294967296.0;
    auto bits = static_cast<std::uint32_t>(wrapped);
    std::uint32_t count = 0;
    while (bits) {
        bits &= bits - 1;
        ++count;
    }
    return count;
}

struct Function {
    std::wstring_view name;
    float (*evaluate)(float);
};

inline constexpr Function Sin{L"sin", [](float x) { return std::sin(x); }};
inline constexpr Function Cos{L"cos", [](float x) { return std::cos(x); }};
inline constexpr Function Tan{L"tan", [](float x) { return std::tan(x); }};
inline constexpr Function Asin{L"asin", [](float x) { return std::asin(x); }};
inline constexpr Function Acos{L"acos", [](float x) { return std::acos(x); }};
inline constexpr Function Atan{L"atan", [](float x) { return std::atan(x); }};
inline constexpr Function Abs{L"abs", [](float x) { return std::fabs(x); }};
inline constexpr Function Sign{L"sign", [](float x) { return static_cast<float>((x > 0) - (x < 0)); }};
inline constexpr Function Ceil{L"ceil", [](float x) { return std::ceil(x); }};
inline constexpr Function Floor{L"floor", [](float x) { return std::floor(x); }};
inline constexpr Function Trunc{L"trunc", [](float x) { return std::trunc(x); }};
inline constexpr Function Round{L"round", [](float x) { return std::round(x); }};
inline constexpr Function Frac{L"frac", [](float x) { return x - std::floor(x); }};
inline constexpr Function Sqrt{L"sqrt", [](float x) { return std::sqrt(x); }};
inline constexpr Function Rsqrt{L"rsqrt", [](float x) { return static_cast<float>(1.0 / std::sqrt(x)); }};
inline constexpr Function Exp{L"exp", [](float x) { return std::exp(x); }};
inline constexpr Function Exp2{L"exp2", [](float x) { return std::exp2(x); }};
inline constexpr Function Log{L"log", [](float x) { return std::log(x); }};
inline constexpr Function Log2{L"log2", [](float x) { return std::log2(x); }};
inline constexpr Function Saturate{L"saturate", [](float x) { return x < 0.0f ? 0.0f : (x < 1.0f ? x : 1.0f); }};
inline constexpr Function CountBits{L"countbits", [](float x) { return static_cast<float>(CountSetBits(x)); }};

inline const Function* Find(std::wstring_view name)
{
    static constexpr const Function* functions[] = {
        &Sin, &Cos, &Tan, &Asin, &Acos, &Atan, &Abs, &Sign, &Ceil, &Floor, &Trunc,
        &Round, &Frac, &Sqrt, &Rsqrt, &Exp, &Exp2, &Log, &Log2, &Saturate, &CountBits
    };
    for (const Function* function : functions) {
        if (function->name == name)
            return function;
    }
    return nullptr;
}

} // namespace migoto::unary
