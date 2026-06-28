#include "DX12HookManager.h"

#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"

static void **DX12GetVTable(void *object)
{
	if (!object)
		return nullptr;
	return *reinterpret_cast<void***>(object);
}

DWORD DX12InstallVTableHook(void *object, const DX12VTableHook &hook)
{
	void **vtable = DX12GetVTable(object);
	if (!vtable) {
		DX12LogManager::JsonFunc(hook.name ? hook.name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"failed\",\"reason\":\"missing_vtable\"");
		return ERROR_INVALID_FUNCTION;
	}

	return DX12HookRegistry::HookFunction(hook.original, vtable[hook.slot], hook.hook, hook.name);
}

void DX12InstallVTableHooks(void *object, const DX12VTableHook *hooks, size_t hookCount)
{
	if (!object || !hooks)
		return;

	for (size_t i = 0; i < hookCount; ++i)
		DX12InstallVTableHook(object, hooks[i]);
}

DWORD DX12InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
{
	if (!module || !exportName)
		return ERROR_PROC_NOT_FOUND;

	return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
}


