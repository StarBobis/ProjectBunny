#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <vector>

#include "DX12IaHashState.hpp"

class DX12IaHashStateCacheKey
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 presentGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaEpoch = 0;
	UINT64 iaContentEpoch = 0;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
};

class DX12IaHashStateCacheEntry
{
public:
	bool valid = false;
	UINT64 age = 0;
	DX12IaHashStateCacheKey key;
	bool hasIaHashState = false;
	DX12IaHashState state;
};

class DX12IaHashStateCacheUtils
{
public:
	static constexpr size_t CacheCapacity = 256;

	static bool KeysEqual(
		const DX12IaHashStateCacheKey &left,
		const DX12IaHashStateCacheKey &right)
	{
		return left.pipelineState == right.pipelineState &&
			left.reloadGeneration == right.reloadGeneration &&
			left.presentGeneration == right.presentGeneration &&
			left.contentGeneration == right.contentGeneration &&
			left.resolveGeneration == right.resolveGeneration &&
			left.targetPlanFingerprint == right.targetPlanFingerprint &&
			left.iaEpoch == right.iaEpoch &&
			left.iaContentEpoch == right.iaContentEpoch &&
			left.indexed == right.indexed &&
			left.vertexCount == right.vertexCount &&
			left.indexCount == right.indexCount &&
			left.firstVertex == right.firstVertex &&
			left.firstIndex == right.firstIndex &&
			left.baseVertex == right.baseVertex;
	}

	static bool TryLoad(
		std::vector<DX12IaHashStateCacheEntry> *entries,
		UINT64 *clock,
		const DX12IaHashStateCacheKey &key,
		DX12IaHashState *state,
		bool *hasIaHashState)
	{
		if (!entries || !clock || !state || !hasIaHashState)
			return false;

		for (size_t index = 0; index < entries->size(); ++index) {
			DX12IaHashStateCacheEntry &entry = (*entries)[index];
			if (!entry.valid ||
				!DX12IaHashStateCacheUtils::KeysEqual(entry.key, key))
				continue;

			entry.age = ++(*clock);
			*hasIaHashState = entry.hasIaHashState;
			*state = entry.state;
			return true;
		}
		return false;
	}

	static void Store(
		std::vector<DX12IaHashStateCacheEntry> *entries,
		UINT64 *clock,
		const DX12IaHashStateCacheKey &key,
		bool hasIaHashState,
		const DX12IaHashState &state)
	{
		if (!entries || !clock)
			return;

		DX12IaHashStateCacheEntry *target = nullptr;
		if (entries->size() < DX12IaHashStateCacheUtils::CacheCapacity) {
			DX12IaHashStateCacheEntry entry;
			entries->push_back(entry);
			target = &(*entries)[entries->size() - 1];
		} else {
			size_t targetIndex = 0;
			UINT64 oldestAge = (*entries)[0].age;
			for (size_t index = 1; index < entries->size(); ++index) {
				if ((*entries)[index].age < oldestAge) {
					oldestAge = (*entries)[index].age;
					targetIndex = index;
				}
			}
			target = &(*entries)[targetIndex];
		}

		target->valid = true;
		target->age = ++(*clock);
		target->key = key;
		target->hasIaHashState = hasIaHashState;
		target->state = state;
	}

	static void Clear(
		std::vector<DX12IaHashStateCacheEntry> *entries,
		UINT64 *clock)
	{
		if (entries)
			entries->clear();
		if (clock)
			*clock = 0;
	}
};
