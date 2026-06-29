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
	HINSTANCE module = nullptr;
	volatile LONG presentCount = 0;
	volatile LONG runtimeMode = static_cast<LONG>(DX12RuntimeMode::Mod);
	volatile LONG perFrameTotalDraws = 0;
	volatile LONG perFrameTotalDispatches = 0;

	static DX12RuntimeState &Get()
	{
		static DX12RuntimeState state;
		return state;
	}

	static void SetModule(HINSTANCE module)
	{
		Get().module = module;
	}

	static HINSTANCE GetModule()
	{
		return Get().module;
	}

	static LONG IncrementPresentCount()
	{
		return InterlockedIncrement(&Get().presentCount);
	}

	static LONG GetPresentCount()
	{
		return InterlockedCompareExchange(&Get().presentCount, 0, 0);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = InterlockedCompareExchange(&Get().runtimeMode, 0, 0);
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
		InterlockedIncrement(&Get().perFrameTotalDraws);
	}

	static void IncrementFrameDispatches()
	{
		InterlockedIncrement(&Get().perFrameTotalDispatches);
	}

	static LONG TakeFrameDraws()
	{
		return InterlockedExchange(&Get().perFrameTotalDraws, 0);
	}

	static LONG TakeFrameDispatches()
	{
		return InterlockedExchange(&Get().perFrameTotalDispatches, 0);
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
