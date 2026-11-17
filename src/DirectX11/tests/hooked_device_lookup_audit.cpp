#include "../lock.h"
#include "hook_lookup_audit_support.h"
#include "hook_install_audit_support.h"
#include "../DLLMainHook.h"

// This copy uses a fake hook manager. Never expose it as the real entry points
// used by HackerDevice and native integration tests in the same executable.
#define hook_device audit_install_hook_device
#define lookup_hooked_device audit_lookup_hooked_device
#undef InitializeCriticalSectionPretty
#define InitializeCriticalSectionPretty dx11_hook_install_audit::install_gate_enter
#define LeaveCriticalSection dx11_hook_lookup_audit::leave_and_pause
#define cHookMgr dx11_hook_install_audit::audit_hook_mgr
#include "../HookedDevice.cpp"
#undef cHookMgr
#undef LeaveCriticalSection
#undef InitializeCriticalSectionPretty

void audit_dx11_device_lookup(ID3D11Device1* original, ID3D11Device1* wrapper,
    ID3D11Device1* other_original, ID3D11Device1* other_wrapper)
{
    using namespace dx11_hook_lookup_audit;
    require(hook_state.load(std::memory_order_acquire) == HookState::NotStarted &&
        lookup_hooked_device(original) == nullptr,
        "F181: pre-install lookup avoids the uninitialized registry lock");
    _InitializeCriticalSectionPretty(&device_map_lock, "device_map_lock");
    hook_state.store(HookState::Ready, std::memory_order_release);
    try {
        exercise_registry(device_map, &device_map_lock, original, wrapper,
            other_original, other_wrapper, lookup_hooked_device);
    } catch (...) {
        device_map.clear();
        hook_state.store(HookState::NotStarted, std::memory_order_release);
        DeleteCriticalSection(&device_map_lock);
        throw;
    }
    hook_state.store(HookState::NotStarted, std::memory_order_release);
    DeleteCriticalSection(&device_map_lock);
}

namespace {

void fake_device_method() {}

HRESULT STDMETHODCALLTYPE fake_device_query_interface(ID3D11Device1*, REFIID, void**)
{
    return E_NOINTERFACE;
}

// Minimal COM-shaped fixture: a fake vtable with 128 slots, every base method
// present, and QueryInterface answering E_NOINTERFACE so only the 43
// ID3D11Device1 base hooks are created (no ID3D11Device hooks).
class FakeDevice {
public:
    FakeDevice()
    {
        slots.assign(128, reinterpret_cast<void*>(&fake_device_method));
        slots[0] = reinterpret_cast<void*>(&fake_device_query_interface);
        object.lpVtbl = reinterpret_cast<ID3D11Device1Vtbl*>(slots.data());
    }
    ID3D11Device1* get() { return &object; }

private:
    std::vector<void*> slots;
    ID3D11Device1 object{};
};

void clear_device_registry()
{
    ::EnterCriticalSection(&device_map_lock);
    device_map.clear();
    ::LeaveCriticalSection(&device_map_lock);
    DeleteCriticalSection(&device_map_lock);
    hook_state.store(HookState::NotStarted, std::memory_order_release);
}

} // namespace

void audit_dx11_device_hook_install()
{
    using namespace dx11_hook_install_audit;

    // T-1: two installers, one installation body.
    hook_state.store(HookState::NotStarted, std::memory_order_release);
    FakeDevice original, other;
    run_double_install([&] {
        hook_device(original.get(), other.get());
    }, 43, "device");

    require(hook_state.load(std::memory_order_acquire) == HookState::Ready,
        "F182 T-1: device state is Ready after the single installation");
    require(device_map.size() == 1 && device_map.at(original.get()) == other.get(),
        "F182 T-1: the successful installer publishes exactly one map entry");
    require(lookup_hooked_device(original.get()) == other.get(),
        "F182 T-1: Ready lookup resolves the wrapper");
    clear_device_registry();

    // T-2: the third Hook fails; the first two are rolled back LIFO.
    audit_hook_mgr.reset();
    install_gate.reset(false);
    FakeDevice failing, failing_hacker;
    audit_hook_mgr.fail_at = 3;
    ID3D11Device1* returned = hook_device(failing.get(), failing_hacker.get());
    require(returned == failing.get(),
        "F182 T-2: failed installation returns the original device pointer");
    require(hook_state.load(std::memory_order_acquire) == HookState::Failed,
        "F182 T-2: failure is published as Failed");
    require(device_map.empty(),
        "F182 T-2: failed installation does not insert a map entry");
    require(lookup_hooked_device(failing.get()) == nullptr,
        "F182 T-2: Failed state makes lookup return null");
    require(audit_hook_mgr.attempts == 3 && audit_hook_mgr.calls.size() == 2,
        "F182 T-2: installation stops at the failed third Hook");
    require(audit_hook_mgr.unhooked == std::vector<SIZE_T>({2, 1}),
        "F182 T-2: rollback unhooks the successful ids in LIFO order");
    require(audit_hook_mgr.enabled.empty(),
        "F182 T-2: no hook is enabled after a create failure");

    // Failed is terminal: no retry, no second map entry, no extra Hook call.
    ID3D11Device1* retried = hook_device(failing.get(), failing_hacker.get());
    require(retried == failing.get() && audit_hook_mgr.attempts == 3 &&
        device_map.empty(), "F182 T-2: Failed is terminal and never retried");

    DeleteCriticalSection(&device_map_lock);
    hook_state.store(HookState::NotStarted, std::memory_order_release);
}
