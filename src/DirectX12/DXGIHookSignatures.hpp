#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookSignatures
{
public:
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};
