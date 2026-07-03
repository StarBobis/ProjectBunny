#pragma once

class DX12CommandListTrackingState
{
public:
	bool trackBindings = false;
	bool trackIaBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};
