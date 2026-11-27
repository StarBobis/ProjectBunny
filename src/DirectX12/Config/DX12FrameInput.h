#pragma once

#include "IniInputScope.h"
#include "IniXInput.h"

// The UI toggle takes effect for the remaining bindings of this same frame.
struct DX12FrameInput {
	migoto::XInputSnapshot controllers;
	UINT64 generation;
	migoto::InputDisableScope blocked = migoto::InputDisableScope::NONE;
};
