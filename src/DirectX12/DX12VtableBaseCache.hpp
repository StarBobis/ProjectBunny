#pragma once

#include <array>

class DX12VtableBaseCache
{
public:
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
	std::array<bool, 256> slotResolved{};
};
