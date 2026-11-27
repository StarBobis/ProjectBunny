#pragma once

namespace migoto {

enum class IncludeDecision { Load, Skip, InvalidExpression, NotStatic };

template<class Parse, class Evaluate>
IncludeDecision EvaluateIncludeCondition(Parse parse, Evaluate evaluate)
{
	if (!parse())
		return IncludeDecision::InvalidExpression;
	float value;
	if (!evaluate(value))
		return IncludeDecision::NotStatic;
	// The file preamble uses float truth (including NaN), unlike INI bool options.
	return value != 0.0f ? IncludeDecision::Load : IncludeDecision::Skip;
}

} // namespace migoto
