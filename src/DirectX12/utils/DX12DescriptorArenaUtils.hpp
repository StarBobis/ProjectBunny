#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

class DX12DescriptorArenaUtils
{
public:
	template <class ComObject>
	static bool AppendUniqueOwned(
		std::vector<ComObject*> *objects,
		ComObject *object)
	{
		if (!objects || !object)
			return false;
		if (std::find(objects->begin(), objects->end(), object) != objects->end()) {
			object->Release();
			return false;
		}
		objects->push_back(object);
		return true;
	}

	template <class ComObject>
	static void MoveUniqueOwned(
		std::vector<ComObject*> *source,
		std::vector<ComObject*> *target)
	{
		if (!source || !target)
			return;
		for (ComObject *object : *source)
			AppendUniqueOwned(target, object);
		source->clear();
	}

	static uint32_t NextCapacity(
		uint32_t currentCapacity,
		uint32_t currentOffset,
		uint32_t requiredDescriptors,
		uint32_t initialCapacity,
		uint32_t maximumCapacity)
	{
		if (!requiredDescriptors || requiredDescriptors > maximumCapacity)
			return 0;
		const uint64_t highWater =
			static_cast<uint64_t>(currentOffset) + requiredDescriptors;
		const uint64_t doubled = currentCapacity ?
			static_cast<uint64_t>(currentCapacity) * 2 : initialCapacity;
		uint64_t capacity = (std::max)(
			static_cast<uint64_t>(initialCapacity),
			(std::max)(highWater, doubled));
		capacity = (std::max)(capacity,
			static_cast<uint64_t>(requiredDescriptors));
		capacity = (std::min)(capacity,
			static_cast<uint64_t>(maximumCapacity));
		return static_cast<uint32_t>(capacity);
	}
};
