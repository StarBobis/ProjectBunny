#pragma once

#include <Windows.h>

class D3D12ProxyTargetHelper
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module);
};
