#pragma once

#include <Windows.h>

#include <array>
#include <unordered_map>
#include <unordered_set>

#include "DX12LogManager.hpp"
#include "Nektra/NktHookLib.h"

class DX12HookRegistry
{
public:
	SRWLOCK lock;
	CNktHookLib hookManager;
	std::unordered_set<void*> hookedFunctions;
	std::unordered_map<void*, void*> originalFunctions;
	std::unordered_map<void*, std::array<void*, 256>> vtableOriginals;
	volatile LONG hooksFinalized = 0;

	DX12HookRegistry()
	{
		InitializeSRWLock(&lock);
	}

	static DX12HookRegistry &Get()
	{
		static DX12HookRegistry state;
		return state;
	}

	static bool RememberHook(void *target)
	{
		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		bool inserted = state.hookedFunctions.insert(target).second;
		ReleaseSRWLockExclusive(&state.lock);
		return inserted;
	}

	static void RememberOriginal(void *target, void *original)
	{
		if (!target || !original)
			return;

		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		state.originalFunctions[target] = original;
		ReleaseSRWLockExclusive(&state.lock);
	}

	static DWORD HookFunction(void **original, void *target, void *hook, const char *name)
	{
		if (!target || !hook)
			return ERROR_PROC_NOT_FOUND;
		if (!RememberHook(target))
			return ERROR_SUCCESS;

		SIZE_T hookId = 0;
		DWORD result = Get().hookManager.Hook(&hookId, original, target, hook);
		if (result == ERROR_SUCCESS && original)
			RememberOriginal(target, *original);
		DX12LogManager::JsonFunc(name ? name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
			result == ERROR_SUCCESS ? "installed" : "failed",
			target, original ? *original : nullptr, result);
		return result;
	}

	static void FinalizeHooks()
	{
		InterlockedExchange(&Get().hooksFinalized, 1);
	}

	static void *GetOriginalFunction(void *target)
	{
		if (!target)
			return nullptr;

		DX12HookRegistry &state = Get();
		if (state.hooksFinalized) {
			auto it = state.originalFunctions.find(target);
			return it != state.originalFunctions.end() ? it->second : nullptr;
		}

		AcquireSRWLockShared(&state.lock);
		auto it = state.originalFunctions.find(target);
		void *original = it != state.originalFunctions.end() ? it->second : nullptr;
		ReleaseSRWLockShared(&state.lock);
		return original;
	}

	static void UnhookAll()
	{
		Get().hookManager.UnhookAll();
	}

	static void CacheVtableOriginals(void *vtable, size_t maxSlot)
	{
		if (!vtable || maxSlot == 0 || maxSlot > 256)
			return;
		DX12HookRegistry &state = Get();
		void **slots = static_cast<void**>(vtable);
		std::array<void*, 256> originals = {};
		for (size_t i = 0; i < maxSlot; ++i) {
			void *target = slots[i];
			if (!target)
				continue;
			auto it = state.originalFunctions.find(target);
			if (it != state.originalFunctions.end())
				originals[i] = it->second;
		}
		state.vtableOriginals[vtable] = originals;
	}
};
