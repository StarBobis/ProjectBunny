#pragma once

#include <Windows.h>
#include <stddef.h>

struct DX12VTableHook
{
	UINT slot;
	void **original;
	void *hook;
	const char *name;
};

DWORD DX12InstallVTableHook(void *object, const DX12VTableHook &hook);
void DX12InstallVTableHooks(void *object, const DX12VTableHook *hooks, size_t hookCount);
DWORD DX12InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook);

template <size_t N>
void DX12InstallVTableHooks(void *object, const DX12VTableHook (&hooks)[N])
{
	DX12InstallVTableHooks(object, hooks, N);
}
