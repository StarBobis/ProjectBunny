#include "../lock.h"
#include "hook_lookup_audit_support.h"
#define LeaveCriticalSection dx11_hook_lookup_audit::leave_and_pause
#include "../HookedDevice.cpp"
#undef LeaveCriticalSection

void audit_dx11_device_lookup(ID3D11Device1* original, ID3D11Device1* wrapper,
    ID3D11Device1* other_original, ID3D11Device1* other_wrapper)
{
    using namespace dx11_hook_lookup_audit;
    require(!hooks_installed && lookup_hooked_device(original) == nullptr,
        "F181: pre-install lookup avoids the uninitialized registry lock");
    InitializeCriticalSectionPretty(&device_map_lock);
    hooks_installed = true;
    try {
        exercise_registry(device_map, &device_map_lock, original, wrapper,
            other_original, other_wrapper, lookup_hooked_device);
    } catch (...) {
        device_map.clear();
        hooks_installed = false;
        DeleteCriticalSection(&device_map_lock);
        throw;
    }
    hooks_installed = false;
    DeleteCriticalSection(&device_map_lock);
}
