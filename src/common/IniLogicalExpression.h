#pragma once

#include <cmath>
#include <string_view>

namespace migoto::logical {

// The DX11 optimizer only skips trees made entirely of opted-in operands
// and operators. Arithmetic, resource discovery and random samples stay eager.
inline bool IsBooleanOperator(std::wstring_view op)
{
	return op == L"&&" || op == L"||" || op == L"!" ||
		op == L"==" || op == L"!=" || op == L"===" || op == L"!==" ||
		op == L"<" || op == L"<=" || op == L">" || op == L">=";
}

inline bool SafeDrawTypeDivisor(float value)
{
	return std::isfinite(value) && value != 0.0f;
}

template<bool IsAnd>
inline bool LeftDecides(float left)
{
	return (left != 0.0f) != IsAnd;
}

} // namespace migoto::logical
