#pragma once

#include <Windows.h>

class DXGIHooks
{
public:
	static void Install();
	static void ActivateHotPathHooks();
};
