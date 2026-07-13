#pragma once

#include <stdint.h>

#include "DX12BindingTrackerTypes.hpp"
#include "MigotoTextureOverride.hpp"

class DX12TextureOverrideMatch
{
public:
	Bunny::TextureOverrideConfig config;
	bool hasThisSrvTarget = false;
	bool compute = false;
	DX12CurrentShaderResourceBinding thisSrvBinding;
	uint32_t matchedHash = 0;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};
