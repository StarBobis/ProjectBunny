#pragma once

enum HuntingMode {
	HUNTING_MODE_DISABLED = 0,
	HUNTING_MODE_ENABLED = 1,
	HUNTING_MODE_SOFT_DISABLED = 2,
};

namespace migoto {

// DX11 skips hunting key registration when the configured value is zero,
// then keeps the runtime soft-disabled so shader processing stays available.
constexpr unsigned LoadedHuntingMode(unsigned configured)
{
    return configured == HUNTING_MODE_DISABLED ? HUNTING_MODE_SOFT_DISABLED : configured;
}

constexpr unsigned ToggledHuntingMode(unsigned current)
{
    return current == HUNTING_MODE_ENABLED ? HUNTING_MODE_SOFT_DISABLED : HUNTING_MODE_ENABLED;
}

} // namespace migoto
