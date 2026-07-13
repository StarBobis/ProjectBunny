#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHooks
{
public:
	static void Install();
	static void Shutdown();
};
