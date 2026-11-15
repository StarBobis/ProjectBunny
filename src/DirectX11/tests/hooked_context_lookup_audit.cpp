#include "../lock.h"
#include "hook_lookup_audit_support.h"
#define LeaveCriticalSection dx11_hook_lookup_audit::leave_and_pause
#include "../HookedContext.cpp"
#undef LeaveCriticalSection

void audit_dx11_context_lookup(ID3D11DeviceContext1* original, ID3D11DeviceContext1* wrapper,
    ID3D11DeviceContext1* other_original, ID3D11DeviceContext1* other_wrapper)
{
    using namespace dx11_hook_lookup_audit;
    require(!hooks_installed && lookup_hooked_context(original) == nullptr,
        "F181: pre-install lookup avoids the uninitialized registry lock");
    // Isolate lookup from native detour installation (F182). The map, lookup,
    // and diagnostic-aware lock entry are the unchanged production objects.
    InitializeCriticalSectionPretty(&context_map_lock);
    hooks_installed = true;
    try {
        exercise_registry(context_map, &context_map_lock, original, wrapper,
            other_original, other_wrapper, lookup_hooked_context);
    } catch (...) {
        context_map.clear();
        hooks_installed = false;
        DeleteCriticalSection(&context_map_lock);
        throw;
    }
    hooks_installed = false;
    DeleteCriticalSection(&context_map_lock);
}
