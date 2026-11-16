#pragma once

#include <d3d11_1.h>

#include "DLLMainHook.h"

ID3D11DeviceContext1* hook_context(ID3D11DeviceContext1 *orig_context, ID3D11DeviceContext1 *hacker_context);

ID3D11DeviceContext1* lookup_hooked_context(ID3D11DeviceContext1 *orig_context);

// Internal lifecycle hook: remove the registry entry when the wrapper that
// owns it is destroyed. Erases only when the stored value still names the
// dying wrapper, so a reused original-context address never loses a newer
// registration.
void unregister_hooked_context(ID3D11DeviceContext1 *orig_context,
		ID3D11DeviceContext1 *hacker_context);
