#pragma once

#include <Windows.h>

class DX12CommandListTrackingCache
{
public:
	UINT64 generation = 0;
	bool trackBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};
