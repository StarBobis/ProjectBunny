#pragma once

#include <initializer_list>
#include <optional>
#include <string>

namespace migoto::tests {

struct KeyCyclePress
{
	bool back;
	std::optional<float> externalValue;
	float value;
	int score;
};

struct KeyCycleCase
{
	const char *name;
	const char *options;
	std::initializer_list<KeyCyclePress> presses;

	std::string Config() const
	{
		return std::string("global $phase=99\nglobal $score=0\n"
			"[CommandListOne]\n$score=$score+1\n"
			"[CommandListTen]\n$score=$score+10\n"
			"[CommandListHundred]\n$score=$score+100\n"
			"[KeyCycle]\nkey=XB2_A\nback=XB2_B\ntype=cycle\n") + options;
	}
};

inline const KeyCycleCase keyCycleCases[] = {
	{"manual index and bounded endpoints",
	 "smart=false\nwrap=off\n$phase=1,2,2\nrun=CommandListOne,CommandListTen,CommandListHundred\n",
	 {{true, {}, 2, 100}, {false, 8.0f, 8, 100}, {true, {}, 2, 110}, {true, {}, 1, 111},
	  {true, 7.0f, 7, 111}, {false, {}, 2, 121}}},
	{"smart resync prefers the current duplicate and compares float32 exactly",
	 "$phase=1,1,2\nrun=CommandListOne,CommandListTen,CommandListHundred\n",
	 {{false, {}, 1, 1}, {false, {}, 1, 11}, {false, {}, 2, 111}, {false, 1.0f, 1, 121},
	  {false, 0.9999995f, 2, 221}, {true, 1.0f, 2, 321}}},
	{"condition selects a slot before activation and shorter lists retain their final entry",
	 "smart=no\n$phase=1,,3,\ncondition=1,,0,1,1,1\nrun=CommandListOne,,CommandListTen,\n",
	 {{false, {}, 1, 1}, {false, 8.0f, 8, 1}, {false, {}, 8, 1}, {false, {}, 3, 11},
	  {false, 9.0f, 3, 21}, {false, {}, 3, 31}, {false, {}, 1, 32}}},
	{"an explicitly empty final command remains empty on later steps",
	 "smart=0\n$phase=1,2,3\nrun=CommandListOne,,\n",
	 {{false, {}, 1, 1}, {false, {}, 2, 1}, {false, {}, 3, 1}, {false, {}, 1, 2}}},
	{"command-only presets match and invalid flags use the DX11 defaults",
	 "smart=invalid\nwrap=invalid\nrun=CommandListOne,CommandListTen\n",
	 {{false, {}, 99, 10}, {false, {}, 99, 11}, {true, {}, 99, 21}}},
	{"bounded movement after a failed condition consumes its slot",
	 "smart=0\nwrap=0\n$phase=1,2\ncondition=0,1\nrun=CommandListOne\n",
	 {{false, {}, 99, 0}, {false, {}, 2, 1}, {false, 3.0f, 3, 1}, {true, {}, 3, 1}}},
	{"malformed cycle condition is unconditional in its own slot",
	 "smart=0\n$phase=1,2\ncondition=(+,0\nrun=CommandListOne\n",
	 {{false, {}, 1, 1}, {false, {}, 1, 1}}},
	{"static expression flags and a single bounded step",
	 "smart=1-1\nwrap=1==0\n$phase=1\nrun=CommandListOne\n",
	 {{false, 1.0f, 1, 1}, {false, 7.0f, 7, 1}, {true, 8.0f, 8, 1}}},
	{"an empty cycle ignores both directions", "", {{true, {}, 99, 0}, {false, {}, 99, 0}}},
};

} // namespace migoto::tests
