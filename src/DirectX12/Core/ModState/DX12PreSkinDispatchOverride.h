#pragma once

// Pre-skin dispatch override and probe cache entry state.
// Split out of Core/DX12ModState.h; include that umbrella header from existing code.
#include "../DX12CommandFlow.h"

class DX12PreSkinDispatchOverride
{
public:
	bool handlingSkip = false;
	bool hasDispatch = false;
	UINT groupsX = 0;
	UINT groupsY = 0;
	UINT groupsZ = 0;
};
class DX12PreSkinProbeCacheEntry
{
public:
	bool applied = false;
	DX12PreSkinDispatchOverride dispatchOverride;
};
