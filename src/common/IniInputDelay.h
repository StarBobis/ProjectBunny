#pragma once

#include <cstdint>

namespace migoto {

struct InputDelays {
	int press = 0;
	int release = 0;
};

// Each physical binding has its own timer, even when several keys share a listener.
class InputDelayState {
	std::uint64_t changedAt = 0;
	bool delivered = false;
public:
	template<class Clock>
	bool EventDue(bool down, bool &lastSample, const InputDelays &delays, Clock &&clock)
	{
		const bool changed = down != lastSample;
		lastSample = down;
		if (down == delivered)
			return false;

		const int delay = down ? delays.press : delays.release;
		if (delay != 0) {
			const std::uint64_t now = clock();
			if (changed)
				changedAt = now;
			// Preserve DX11's unsigned elapsed-time comparison, including negative settings.
			if (now - changedAt < static_cast<std::uint64_t>(delay))
				return false;
		}
		delivered = down;
		return true;
	}
};

} // namespace migoto
