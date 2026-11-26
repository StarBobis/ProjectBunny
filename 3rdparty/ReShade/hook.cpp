/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "hook.hpp"
#include <cassert>
#include <MinHook.h>

// Verify status codes match the ones from MinHook
static_assert(static_cast<int>(reshade::hook::status::unknown) == MH_UNKNOWN);
static_assert(static_cast<int>(reshade::hook::status::success) == MH_OK);
static_assert(static_cast<int>(reshade::hook::status::not_executable) == MH_ERROR_NOT_EXECUTABLE);
static_assert(static_cast<int>(reshade::hook::status::unsupported_function) == MH_ERROR_UNSUPPORTED_FUNCTION);
static_assert(static_cast<int>(reshade::hook::status::allocation_failure) == MH_ERROR_MEMORY_ALLOC);
static_assert(static_cast<int>(reshade::hook::status::memory_protection_failure) == MH_ERROR_MEMORY_PROTECT);

// Lifetime and group activation are serialized by the DX11 user32 hook owner.

bool reshade::hook::enable() const
{
	return MH_QueueEnableHook(target) == MH_OK;
}
bool reshade::hook::disable() const
{
	return MH_QueueDisableHook(target) == MH_OK;
}

reshade::hook::status reshade::hook::install()
{
	if (!valid())
		return hook::status::unsupported_function;

	// Create a disabled trampoline. Publish every original before enabling the group.
	return static_cast<hook::status>(MH_CreateHook(target, replacement, &trampoline));
}
reshade::hook::status reshade::hook::uninstall()
{
	if (!valid())
		return hook::status::unsupported_function;

	const MH_STATUS status_code = MH_RemoveHook(target);
	if (status_code == MH_ERROR_NOT_CREATED)
		return hook::status::success; // If the hook was never created, then uninstallation is implicitly successful
	else if (status_code != MH_OK)
		return static_cast<hook::status>(status_code);

	trampoline = nullptr;

	return hook::status::success;
}

reshade::hook::address reshade::hook::call() const
{
	assert(installed());

	return trampoline;
}

bool reshade::hook::apply_queued_actions()
{
	return MH_ApplyQueued() == MH_OK;
}
