#pragma once

// Global DX12 runtime mode, module handle, and per-frame counter shards.
// Split out of Core/DX12Diagnostics.h; include that umbrella header from existing code.
#include "DX12CoreModels.h"

enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static std::atomic<LONG> presentCount{ 0 };
	inline static std::atomic<LONG> runtimeMode{ static_cast<LONG>(DX12RuntimeMode::Mod) };

	// Per-frame draw/dispatch counters feed diagnostics logs only. Sharded by
	// recording thread so the per-draw increment cannot ping-pong one cache
	// line across cores when the game records command lists from workers.
	struct alignas(64) FrameCounterShard
	{
		std::atomic<LONG> draws{ 0 };
		std::atomic<LONG> dispatches{ 0 };
	};
	static constexpr UINT FrameCounterShardCount = 64;
	inline static FrameCounterShard frameCounterShards[FrameCounterShardCount] = {};
	inline static std::atomic<UINT> frameCounterShardNext{ 0 };

	static void SetModule(HINSTANCE module)
	{
		DX12RuntimeState::module = module;
	}

	static HINSTANCE GetModule()
	{
		return module;
	}

	static LONG IncrementPresentCount()
	{
		return presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	static LONG GetPresentCount()
	{
		return presentCount.load(std::memory_order_relaxed);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = runtimeMode.load(std::memory_order_relaxed);
		return mode == static_cast<LONG>(DX12RuntimeMode::Hunting) ?
			DX12RuntimeMode::Hunting : DX12RuntimeMode::Mod;
	}

	static void SetRuntimeMode(DX12RuntimeMode mode);

	static bool IsModMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Mod;
	}

	static bool IsHuntingMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Hunting;
	}

	static FrameCounterShard *FrameCounterShardForThread()
	{
		static thread_local FrameCounterShard *shard = nullptr;
		if (!shard) {
			const UINT index =
				frameCounterShardNext.fetch_add(1, std::memory_order_relaxed) %
				FrameCounterShardCount;
			shard = &frameCounterShards[index];
		}
		return shard;
	}

	static void IncrementFrameDraws()
	{
		FrameCounterShardForThread()->draws.fetch_add(1, std::memory_order_relaxed);
	}

	static void IncrementFrameDispatches()
	{
		FrameCounterShardForThread()->dispatches.fetch_add(1, std::memory_order_relaxed);
	}

	static LONG TakeFrameDraws()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].draws.exchange(0, std::memory_order_relaxed);
		return total;
	}

	static LONG TakeFrameDispatches()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].dispatches.exchange(0, std::memory_order_relaxed);
		return total;
	}
};

class DX12InternalReplayState
{
public:
	static UINT &Depth()
	{
		static thread_local UINT depth = 0;
		return depth;
	}

	static void Enter()
	{
		++Depth();
	}

	static void Leave()
	{
		UINT &depth = Depth();
		if (depth)
			--depth;
	}

	static bool IsActive()
	{
		return Depth() != 0;
	}
};

class DX12InternalReplayScope
{
public:
	DX12InternalReplayScope() { DX12InternalReplayState::Enter(); }
	~DX12InternalReplayScope() { DX12InternalReplayState::Leave(); }
	DX12InternalReplayScope(const DX12InternalReplayScope&) = delete;
	DX12InternalReplayScope &operator=(const DX12InternalReplayScope&) = delete;
};
