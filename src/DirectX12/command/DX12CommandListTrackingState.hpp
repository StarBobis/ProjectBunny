#pragma once

class DX12CommandListTrackingState
{
public:
	bool trackBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};
