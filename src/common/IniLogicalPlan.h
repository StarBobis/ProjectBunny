#pragma once

#include "IniLogicalExpression.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace migoto::logical {

struct Chain {
	size_t key;
	std::vector<size_t> rightOperands;
	size_t safePrefix = 0;
	size_t end = 0;

	static size_t Key(size_t position, bool isAnd) { return position * 2 + isAnd; }
};

struct Plan {
	std::vector<Chain> chains;
	bool valid = false;

	const Chain *Find(size_t start, bool isAnd) const
	{
		const auto key = Chain::Key(start, isAnd);
		const auto it = std::lower_bound(chains.begin(), chains.end(), key,
			[](const Chain &chain, size_t offset) { return chain.key < offset; });
		return it != chains.end() && it->key == key ? &*it : nullptr;
	}
};

// Fed by the normal expression grammar during syntax validation. Parentheses
// preserve the shape of an operand, while a unary sign creates another node.
class PlanBuilder {
public:
	Plan &plan;
	size_t nodes = 0;
	size_t unsafeNodes = 0;
	bool lastDrawType = false;
	bool lastFiniteNonzeroLiteral = false;

	explicit PlanBuilder(Plan &plan) : plan(plan) {}

	void Operator(std::wstring_view op)
	{
		if (op == L"(" || op == L")")
			return;
		++nodes;
		if (op != L"%" && !IsBooleanOperator(op))
			++unsafeNodes;
	}

	void Literal(float value)
	{
		++nodes;
		lastDrawType = false;
		lastFiniteNonzeroLiteral = SafeDrawTypeDivisor(value);
	}

	void Identifier(std::wstring_view name)
	{
		++nodes;
		lastDrawType = name == L"draw_type";
		lastFiniteNonzeroLiteral = false;
		if (name.front() == L'$' || lastDrawType || name == L"nan" || name == L"inf" || name == L"infinity" ||
			name == L"vertex_count" || name == L"index_count" || name == L"instance_count" ||
			name == L"first_vertex" || name == L"first_index" || name == L"first_instance" ||
			name == L"thread_group_count_x" || name == L"thread_group_count_y" || name == L"thread_group_count_z")
			return;
		++unsafeNodes;
	}
};

} // namespace migoto::logical
