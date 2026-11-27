#pragma once

namespace migoto::tests {

struct FileConditionCase {
	const wchar_t *expression;
	bool loads;
	bool validConstant;
};

inline constexpr FileConditionCase fileConditions[] = {
	{L"0", false, true}, {L"-0.0", false, true}, {L"-4", true, true},
	{L"NAN", true, true}, {L"INF", true, true}, {L"0x20 >> 4 == 2", true, true},
	{L"ABS(-3) > 1 && !stereo_active", true, true},
	{L"sli || stereo_available", false, true},
	{L"time", false, false}, {L"frame_time", false, false}, {L"frame_number", false, false},
	{L"$ini_expression_regression_value", false, false}, {L"1 || random(1)", false, false},
	{L"0 && random(1)", false, false}, {L"random(random(1))", false, false},
	{L"!frame_analysis", false, false}, {L"draw_type", false, false}, {L"unknown_name", false, false},
	{L"1 +", false, false}, {L"1 || (2 && )", false, false}, {L"", false, false}
};

} // namespace migoto::tests
