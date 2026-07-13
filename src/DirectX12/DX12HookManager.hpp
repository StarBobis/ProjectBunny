#pragma once

#include <Windows.h>
#include <stddef.h>

#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12HookTypes.hpp"

class DX12HookManager
{
public:
	static void **GetVTable(void *object)
	{
		if (!object)
			return nullptr;
		return *reinterpret_cast<void***>(object);
	}

	static DWORD InstallVTableHook(void *object, const DX12VTableHook &hook)
	{
		void **vtable = GetVTable(object);
		if (!vtable) {
			DX12LogManager::JsonFunc(hook.name ? hook.name : "HookInstall",
				"\"event\":\"HookInstall\",\"status\":\"failed\",\"reason\":\"missing_vtable\"");
			return ERROR_INVALID_FUNCTION;
		}

		return DX12HookRegistry::HookFunction(hook.original, vtable[hook.slot], hook.hook, hook.name);
	}

	static void InstallVTableHooks(void *object, const DX12VTableHook *hooks, size_t hookCount)
	{
		if (!object || !hooks)
			return;

		for (size_t i = 0; i < hookCount; ++i)
			InstallVTableHook(object, hooks[i]);
	}

	template <size_t N>
	static void InstallVTableHooks(void *object, const DX12VTableHook (&hooks)[N])
	{
		InstallVTableHooks(object, hooks, N);
	}

	static DWORD InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
	{
		if (!module || !exportName)
			return ERROR_PROC_NOT_FOUND;

		return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
	}
};
