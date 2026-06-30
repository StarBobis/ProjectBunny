#pragma once

#include <Windows.h>

#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12OriginalFunctionRegistry
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

	static void *GetVTableSlotTarget(void *object, UINT slot)
	{
		if (!object)
			return nullptr;
		void **vtable = *reinterpret_cast<void***>(object);
		return vtable ? vtable[slot] : nullptr;
	}

	template <typename T>
	static T Resolve(void *object, UINT slot, T fallback, const char *name)
	{
		void *target = GetVTableSlotTarget(object, slot);
		if (target) {
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

		DX12LogManager::JsonFunc(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}

	template <typename T, typename SlotCache, typename VtableCache>
	static T ResolveCached(
		void *object, UINT slot, T fallback, const char *name,
		SlotCache *slotCaches, size_t slotCacheCount, VtableCache *vtableCache)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *target = vtable[slot];

				if (slotCaches && slot < slotCacheCount &&
					slotCaches[slot].target == target &&
					slotCaches[slot].original)
					return reinterpret_cast<T>(slotCaches[slot].original);

				if (vtableCache && vtableCache->vtable == vtable && slot < vtableCache->originals.size() &&
					vtableCache->originals[slot]) {
					void *original = vtableCache->originals[slot];
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
					}
					return reinterpret_cast<T>(original);
				}

				void *original = DX12HookRegistry::GetOriginalFunction(target);
				if (original) {
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
					}
					if (vtableCache && slot < vtableCache->originals.size()) {
						if (vtableCache->vtable != vtable) {
							vtableCache->vtable = vtable;
							vtableCache->originals = {};
						}
						vtableCache->originals[slot] = original;
					}
					return reinterpret_cast<T>(original);
				}
			}
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

		DX12LogManager::JsonFunc(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};
