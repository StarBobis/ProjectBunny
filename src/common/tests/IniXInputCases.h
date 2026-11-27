#pragma once

#include "IniXInput.h"
#include <string>

namespace migoto::test {

template <class IsDown, class Require>
void CheckXInputButtons(IsDown down, Require require)
{
	XInputSnapshot pads;
	pads.slots[1].connected = true;
	const std::pair<const wchar_t *, WORD> names[] = {
		{L"A", XINPUT_GAMEPAD_A}, {L"B", XINPUT_GAMEPAD_B}, {L"X", XINPUT_GAMEPAD_X}, {L"Y", XINPUT_GAMEPAD_Y},
		{L"BACK", XINPUT_GAMEPAD_BACK}, {L"START", XINPUT_GAMEPAD_START}, {L"GUIDE", 0x400},
		{L"LEFT_THUMB", XINPUT_GAMEPAD_LEFT_THUMB}, {L"RIGHT_THUMB", XINPUT_GAMEPAD_RIGHT_THUMB},
		{L"LEFT_SHOULDER", XINPUT_GAMEPAD_LEFT_SHOULDER}, {L"RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER},
		{L"DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN}, {L"DPAD_UP", XINPUT_GAMEPAD_DPAD_UP},
		{L"DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {L"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
	};
	for (const auto &[name, bit] : names) {
		const auto key = std::wstring(L"XB2_") + name;
		const auto any = std::wstring(L"xb_") + name;
		const auto no = std::wstring(L"no_") + key;
		pads.slots[1].state.Gamepad.wButtons = 0;
		require(!down(key, pads) && down(no, pads), "released button on a connected port");
		pads.slots[1].state.Gamepad.wButtons = bit;
		require(down(key, pads) && down(any, pads) && !down(no, pads), "named, numbered and inverted button");
	}
	require(!down(L"NO_XB1_A", pads) && !down(L"XB1_A", pads), "disconnected port never satisfies a button");
	pads.slots[1].state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	require(!down(L"NO_XB_A", pads), "disconnected slots do not satisfy an inverted any-port key");
	pads.slots[3].connected = true;
	require(down(L"NO_XB_A", pads), "any connected port can satisfy an inverted any-port key");
	auto &pad = pads.slots[1].state.Gamepad;
	pad.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	require(!down(L"XB_LEFT_TRIGGER", pads), "default trigger threshold is strict");
	++pad.bLeftTrigger;
	require(down(L"XB2_LEFT_TRIGGER", pads), "first value above the default threshold");
	pad.bRightTrigger = 128;
	require(!down(L"xb2_right_trigger > 128", pads), "explicit threshold boundary");
	pad.bRightTrigger = 129;
	require(down(L"xb2_right_trigger>128", pads), "explicit threshold crossed");
	pad.bRightTrigger = 255;
	require(down(L"XB2_RIGHT_TRIGGER > 255", pads), "DX11 full-press saturation");
}

} // namespace migoto::test
