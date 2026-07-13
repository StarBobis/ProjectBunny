#pragma once

#include <Windows.h>

struct PlacedResourceInfo
{
	bool hasPlacedHeap = false;
	UINT64 heapOffset = 0;
	UINT64 heapSize = 0;
};
