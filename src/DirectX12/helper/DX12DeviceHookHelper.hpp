#pragma once

#include <Windows.h>

#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12DeviceHookHelper
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12FallbackPath",
			"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
			"\"slot\":%u,\"fallback\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
	}

	template <typename T>
	static T GetOriginal(void *object, UINT slot, T fallback, const char *name)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *original = DX12HookRegistry::GetOriginalFunction(vtable[slot]);
				if (original)
					return reinterpret_cast<T>(original);
			}
		}
		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}
		DX12LogManager::JsonFunc(name ? name : "ID3D12Device::Unknown",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};
