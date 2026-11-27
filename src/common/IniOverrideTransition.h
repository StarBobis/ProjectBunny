#pragma once

#include <cfloat>
#include <cmath>
#include <cstdint>

namespace migoto {

enum class TransitionType { INVALID = -1, LINEAR, COSINE };

struct OverrideTransitionValue
{
	float start = FLT_MAX;
	float target = FLT_MAX;
	std::uint64_t activation_time = 0;
	int time = -1;
	TransitionType transition_type = TransitionType::LINEAR;

	void Schedule(float current, float value, std::uint64_t now, int duration, TransitionType type)
	{
		start = current;
		target = value;
		activation_time = now;
		time = duration;
		transition_type = type;
	}

	float Update(std::uint64_t now)
	{
		if (time == -1)
			return FLT_MAX;
		const float elapsed = static_cast<float>(now - activation_time);
		float percent = time ? elapsed / time : 1.0f;
		if (percent >= 1.0f) {
			time = -1;
			return target;
		}
		if (transition_type == TransitionType::COSINE)
			percent = static_cast<float>((1.0 - std::cos(percent * 3.14159265358979323846)) / 2.0);
		return target * percent + start * (1.0f - percent);
	}
};

// When several overrides own a variable, the last release restores the
// value from before the first activation, independent of release order.
class OverrideSavedValue
{
public:
	float Reset()
	{
		const float result = original;
		original = FLT_MAX;
		owners = 0;
		return result;
	}

	void Save(float value)
	{
		if (!owners)
			original = value;
		++owners;
	}

	int Restore(float *value)
	{
		if (--owners == 0) {
			if (value)
				*value = original;
			original = FLT_MAX;
		}
		return owners;
	}
private:
	float original = FLT_MAX;
	int owners = 0;
};

} // namespace migoto
