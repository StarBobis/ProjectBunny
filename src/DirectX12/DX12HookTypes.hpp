#pragma once

#include <Windows.h>

class DX12VTableHook
{
public:
	UINT slot;
	void **original;
	void *hook;
	const char *name;
};
