// Published resource records and snapshot utilities.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12DescriptorSlotArray.h"

struct ResourceRecord
{
	ID3D12Resource *resource = nullptr;
	UINT64 identityGeneration = 0;
	DX12ResourcePublishedState *publishedState = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	bool hasHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	UINT64 gpuVirtualAddressRealEnd = 0;
	UINT64 gpuVirtualAddressOobEnd = 0;
	UINT64 placedHeapOffset = 0;
	UINT64 placedHeapSize = 0;
	bool hasPlacedHeapRange = false;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
};

class DX12ResourcePublishedState
{
public:
	static constexpr UINT32 ActiveFlag = 1u << 0;
	static constexpr UINT32 HasCurrentStateFlag = 1u << 1;
	static constexpr UINT32 StateTrackedByBarrierFlag = 1u << 2;

	std::atomic<UINT64> identityGeneration{0};
	std::atomic<UINT> currentState{static_cast<UINT>(D3D12_RESOURCE_STATE_COMMON)};
	std::atomic<UINT32> flags{0};

	void Publish(const ResourceRecord &record)
	{
		identityGeneration.store(0, std::memory_order_release);
		currentState.store(static_cast<UINT>(record.currentState), std::memory_order_relaxed);
		UINT32 publishedFlags = ActiveFlag;
		if (record.hasCurrentState)
			publishedFlags |= HasCurrentStateFlag;
		if (record.stateTrackedByBarrier)
			publishedFlags |= StateTrackedByBarrierFlag;
		flags.store(publishedFlags, std::memory_order_relaxed);
		identityGeneration.store(record.identityGeneration, std::memory_order_release);
	}

	void PublishState(const ResourceRecord &record)
	{
		currentState.store(static_cast<UINT>(record.currentState), std::memory_order_relaxed);
		UINT32 publishedFlags = ActiveFlag;
		if (record.hasCurrentState)
			publishedFlags |= HasCurrentStateFlag;
		if (record.stateTrackedByBarrier)
			publishedFlags |= StateTrackedByBarrierFlag;
		flags.store(publishedFlags, std::memory_order_release);
	}

	void Invalidate()
	{
		identityGeneration.store(0, std::memory_order_release);
		flags.store(0, std::memory_order_release);
	}

	bool TryRead(
		UINT64 expectedGeneration,
		UINT *state,
		bool *hasState,
		bool *trackedByBarrier) const
	{
		if (!expectedGeneration)
			return false;
		const UINT64 generationBefore =
			identityGeneration.load(std::memory_order_acquire);
		if (generationBefore != expectedGeneration)
			return false;
		const UINT32 publishedFlags = flags.load(std::memory_order_acquire);
		const UINT publishedState = currentState.load(std::memory_order_relaxed);
		const UINT64 generationAfter =
			identityGeneration.load(std::memory_order_acquire);
		if (generationAfter != generationBefore || !(publishedFlags & ActiveFlag))
			return false;
		if (state)
			*state = publishedState;
		if (hasState)
			*hasState = (publishedFlags & HasCurrentStateFlag) != 0;
		if (trackedByBarrier)
			*trackedByBarrier =
				(publishedFlags & StateTrackedByBarrierFlag) != 0;
		return true;
	}
};

class DX12ResourceSnapshotUtils
{
public:
static bool TryAddRef(ID3D12Resource *resource)
{
	if (!resource)
		return false;
	__try {
		resource->AddRef();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void Release(ID3D12Resource *resource)
{
	if (!resource)
		return;
	__try {
		resource->Release();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}
};

