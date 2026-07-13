#pragma once

#include <Windows.h>

enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static volatile LONG presentCount = 0;
	inline static volatile LONG runtimeMode = static_cast<LONG>(DX12RuntimeMode::Mod);
	inline static volatile LONG perFrameTotalDraws = 0;
	inline static volatile LONG perFrameTotalDispatches = 0;

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
		return InterlockedIncrement(&presentCount);
	}

	static LONG GetPresentCount()
	{
		return InterlockedCompareExchange(&presentCount, 0, 0);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = InterlockedCompareExchange(&runtimeMode, 0, 0);
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

	static void IncrementFrameDraws()
	{
		InterlockedIncrement(&perFrameTotalDraws);
	}

	static void IncrementFrameDispatches()
	{
		InterlockedIncrement(&perFrameTotalDispatches);
	}

	static LONG TakeFrameDraws()
	{
		return InterlockedExchange(&perFrameTotalDraws, 0);
	}

	static LONG TakeFrameDispatches()
	{
		return InterlockedExchange(&perFrameTotalDispatches, 0);
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
