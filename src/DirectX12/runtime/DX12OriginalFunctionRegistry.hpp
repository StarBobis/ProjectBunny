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

	// 批量预解析：一次性解析所有热点 vtable slot，填充二级 vtable 缓存
	// 后续所有 ResolveCached 调用直接命中 vtable 缓存，跳过全局 map 查找
	template <typename SlotCache, typename VtableCache>
	static void BulkResolveVtableSlots(
		void *object,
		const UINT *slots,
		size_t slotCount,
		SlotCache *slotCaches,
		size_t slotCacheCount,
		VtableCache *vtableCache)
	{
		if (!object || !slots || !slotCount || !vtableCache)
			return;

		void **vtable = *reinterpret_cast<void***>(object);
		if (!vtable)
			return;

		// 同一 vtable 已批量解析过，跳过
		if (vtableCache->vtable == vtable)
			return;

		// 清空并设置新的 vtable
		vtableCache->vtable = vtable;
		vtableCache->originals = {};
		vtableCache->slotResolved = {};

		for (size_t i = 0; i < slotCount; ++i) {
			UINT slot = slots[i];
			if (slot >= vtableCache->originals.size())
				continue;

			void *target = vtable[slot];
			if (!target) {
				vtableCache->slotResolved[slot] = true;
				continue;
			}

			void *original = DX12HookRegistry::GetOriginalFunction(target);
			vtableCache->originals[slot] = original;
			vtableCache->slotResolved[slot] = true;

			// 同步更新一级 slot 缓存
			if (slotCaches && slot < slotCacheCount) {
				slotCaches[slot].target = target;
				slotCaches[slot].original = original;
				slotCaches[slot].resolved = true;
			}
		}
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

				// 一级缓存：per-slot TLS 缓存（命中率最高）
				if (slotCaches && slot < slotCacheCount &&
					slotCaches[slot].resolved &&
					slotCaches[slot].target == target) {
					if (slotCaches[slot].original)
						return reinterpret_cast<T>(slotCaches[slot].original);
					// 负缓存命中：已解析过，无 hook，直接用 fallback
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// 二级缓存：per-vtable 批量缓存
				if (vtableCache && vtableCache->vtable == vtable &&
					slot < vtableCache->slotResolved.size() &&
					vtableCache->slotResolved[slot]) {
					void *original = vtableCache->originals[slot];
					// 更新一级缓存
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
						slotCaches[slot].resolved = true;
					}
					if (original)
						return reinterpret_cast<T>(original);
					// 负缓存命中
					if (fallback)
						return fallback;
					goto log_missing;
				}

				// 三级：全局 hook registry 查找（最慢，仅首次访问）
				void *original = DX12HookRegistry::GetOriginalFunction(target);

				// 更新一级缓存（包括负结果）
				if (slotCaches && slot < slotCacheCount) {
					slotCaches[slot].target = target;
					slotCaches[slot].original = original;
					slotCaches[slot].resolved = true;
				}

				// 更新二级 vtable 缓存（包括负结果）
				if (vtableCache && slot < vtableCache->slotResolved.size()) {
					if (vtableCache->vtable != vtable) {
						vtableCache->vtable = vtable;
						vtableCache->originals = {};
						vtableCache->slotResolved = {};
					}
					vtableCache->originals[slot] = original;
					vtableCache->slotResolved[slot] = true;
				}

				if (original)
					return reinterpret_cast<T>(original);

				// 负结果：slot 未被 hook，走 fallback
				if (fallback)
					return fallback;
				goto log_missing;
			}
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

	log_missing:
		DX12LogManager::JsonFunc(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};
