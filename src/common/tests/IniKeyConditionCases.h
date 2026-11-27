#pragma once

#include <string>

namespace migoto::tests {

struct KeyConditionCase {
	const wchar_t *text;
	bool valid;
};

inline constexpr KeyConditionCase keyConditions[] = {
	{L"time + frame_number", true}, {L"res_width + res_height + effective_dpi", true},
	{L"hunting || sli || stereo_active || stereo_available", true},
	{L"x + y0 + w0007", true}, {L"$ini_expression_regression_value", true},
	{L"dxgi_format_unknown || random(d3d11_bind_vertex_buffer)", true}, {L"nan !== inf", true},
	{L"frame_analysis", false}, {L"random(2) + frame_time", false}, {L"fps", false},
	{L"window_width", false}, {L"rt_height", false}, {L"cursor_x", false}, {L"scissor_top", false},
	{L"1 || draw_type", false}, {L"thread_group_count_z", false}, {L"draw_number", false},
	{L"1 || $ks_undeclared", false}, {L"x2147483647", false}, {L"mystery", false},
	{L"0 && (1 +)", false},
};

// Both backend fixtures prepend [Constants].
inline std::string KeyConditionIni()
{
	const std::string longCondition = "0" + std::string(258, ' ') + " || 1";
	return "global $ks_hits=0\nglobal $ks_gate=1\nx=5\nw0007=11\n"
		"[CommandListScopeHit]\n$ks_hits=$ks_hits+1\n"
		"[KeyScopeInvalid]\nkey=F16\ncondition=0 && draw_type\ncondition=0\nrun=CommandListScopeHit\n"
		"[KeyScopeFalse]\nkey=F17\ncondition=0\ncondition=1\nrun=CommandListScopeHit\n"
		"[KeyScopeValid]\nkey=F18\ncondition=x00==5 && w7==11 && $ks_gate\nrun=CommandListScopeHit\n"
		"[KeyScopeCycle]\nkey=F19\ntype=cycle\nsmart=0\ncondition=0,0 && frame_analysis,1\n"
		"run=CommandListScopeHit,CommandListScopeHit,CommandListScopeHit\n"
		"[KeyScopeLong]\nkey=F20\ncondition=" + longCondition + "\nrun=CommandListScopeHit\n"
		"[KeyScopeLongCycle]\nkey=F21\ntype=cycle\ncondition=" + longCondition + "\nrun=CommandListScopeHit\n";
}

template<class Press, class Check>
void CheckKeyConditionPresses(Press press, Check check)
{
	check(press(0x7f) == 1, "invalid first Key condition leaves its action unconditional"); // F16
	check(press(0x80) == 1, "first valid false condition suppresses the key");
	check(press(0x81) == 2, "IniParam aliases and a declared global gate a key");
	check(press(0x82) == 2 && press(0x82) == 3 && press(0x82) == 4,
		"a malformed cycle field drops only that field's condition");
	check(press(0x83) == 4 && press(0x84) == 5, "ordinary Key condition is bounded and cycle field is complete");
}

} // namespace migoto::tests
