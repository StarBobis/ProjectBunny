#pragma once

namespace migoto::tests {

struct HuntingModeCase {
	const char *setting;
	unsigned loaded;
	bool keys;
	unsigned toggled;
};

inline constexpr HuntingModeCase huntingModes[] = {
	{"0", 2, false, 2}, {"1", 1, true, 2}, {"1+1", 2, true, 1},
	{"bad_setting", 2, false, 2}, {"4", 4, true, 1},
};

} // namespace migoto::tests
