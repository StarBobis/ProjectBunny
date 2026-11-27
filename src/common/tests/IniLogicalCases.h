#pragma once

#include "ExpressionFunctions.h"
#include <string>

namespace migoto::tests {

inline constexpr char logicalPolicyKey[] = "expression_short_circuit=$logical_policy_flag\n";
inline constexpr char logicalPolicyDeclaration[] = "global $logical_policy_flag=0\n";

struct LogicalCase {
	const wchar_t *operand;
	bool safe;
};

inline constexpr LogicalCase logicalCases[] = {
	{L"1", true}, {L"$ini_expression_regression_value", true},
	{L"draw_type % 2 == 0", true}, {L"(draw_type) % (2.0)", true},
	{L"!vertex_count || first_vertex < instance_count", true},
	{L"index_count !== first_index && first_instance === thread_group_count_z", true},
	{L"thread_group_count_x <= thread_group_count_y", true},
	{L"$ini_expression_regression_value % 2", false}, {L"draw_type % 0", false},
	{L"draw_type % -2", false}, {L"draw_type % 2 % 3", false},
	{L"draw_type % (2 + 2)", false}, {L"!draw_type % 2", false},
	{L"-vertex_count", false}, {L"index_count + 2", false},
	{L"draw_type & 1", false}, {L"abs(1)", false},
	{L"frame_number", false}, {L"random(10)", false},
};

inline std::wstring GuardedOperand(const LogicalCase &c)
{
	return std::wstring(L"0 && ((") + c.operand + L") == $ini_expression_regression_value)";
}

template<class Evaluate, class Check>
void CheckLogicalRandomOrder(Evaluate evaluate, Check check)
{
	auto seed = [] { SeedExpressionRandom(73u - 0x9e3779b9u * GetExpressionRandomSampleCount(), 41); };
	seed();
	const float first = RandomExpressionValue(1);
	const float second = RandomExpressionValue(1);
	const float third = RandomExpressionValue(1);
	auto checkOrder = [&](float last, const wchar_t *tail, unsigned count) {
		const auto threshold = std::to_wstring((first + last) / 2);
		const auto expression = std::wstring(L"random(1) > ") + threshold + tail;
		const float expected = last > std::stof(threshold) ? 1.0f : 0.0f;
		check((first > std::stof(threshold)) != (expected != 0), "seeds distinguish operand evaluation order");
		seed();
		const auto before = GetExpressionRandomSampleCount();
		check(evaluate(expression) == expected && GetExpressionRandomSampleCount() == before + count,
			"eager logical chain samples right operands first exactly once");
	};
	checkOrder(second, L" && random(1) >= 0", 2);
	checkOrder(second, L" || random(1) < 0", 2);
	checkOrder(third, L" && random(1) >= 0 && random(1) >= 0", 3);
	checkOrder(third, L" || (random(1) < 0 && random(1) >= 0)", 3);
}

} // namespace migoto::tests
