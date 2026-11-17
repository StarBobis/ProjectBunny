#include "../lock.h"
#include "hook_lookup_audit_support.h"
#include "hook_install_audit_support.h"
#include "../DLLMainHook.h"

#define hook_context audit_install_hook_context
#define lookup_hooked_context audit_lookup_hooked_context
#define unregister_hooked_context audit_unregister_hooked_context
#undef InitializeCriticalSectionPretty
#define InitializeCriticalSectionPretty dx11_hook_install_audit::install_gate_enter
#define LeaveCriticalSection dx11_hook_lookup_audit::leave_and_pause
#define cHookMgr dx11_hook_install_audit::audit_hook_mgr
#include "../HookedContext.cpp"
#undef cHookMgr
#undef LeaveCriticalSection
#undef InitializeCriticalSectionPretty

void audit_dx11_context_lookup(ID3D11DeviceContext1* original, ID3D11DeviceContext1* wrapper,
    ID3D11DeviceContext1* other_original, ID3D11DeviceContext1* other_wrapper)
{
    using namespace dx11_hook_lookup_audit;
    require(hook_state.load(std::memory_order_acquire) == HookState::NotStarted &&
        lookup_hooked_context(original) == nullptr,
        "F181: pre-install lookup avoids the uninitialized registry lock");
    // Isolate lookup from native detour installation (F182). The map, lookup,
    // and diagnostic-aware lock entry are the unchanged production objects.
    _InitializeCriticalSectionPretty(&context_map_lock, "context_map_lock");
    hook_state.store(HookState::Ready, std::memory_order_release);
    try {
        exercise_registry(context_map, &context_map_lock, original, wrapper,
            other_original, other_wrapper, lookup_hooked_context);
    } catch (...) {
        context_map.clear();
        hook_state.store(HookState::NotStarted, std::memory_order_release);
        DeleteCriticalSection(&context_map_lock);
        throw;
    }
    hook_state.store(HookState::NotStarted, std::memory_order_release);
    DeleteCriticalSection(&context_map_lock);
}


namespace {

void fake_context_method() {}

HRESULT STDMETHODCALLTYPE fake_context_query_interface(ID3D11DeviceContext1*, REFIID, void**)
{
    return E_NOINTERFACE;
}

// Minimal COM-shaped fixture: QueryInterface answers E_NOINTERFACE so only the
// 115 ID3D11DeviceContext1 base hooks are created (no context1 hooks).
class FakeContext {
public:
    FakeContext()
    {
        slots.assign(256, reinterpret_cast<void*>(&fake_context_method));
        slots[0] = reinterpret_cast<void*>(&fake_context_query_interface);
        object.lpVtbl = reinterpret_cast<ID3D11DeviceContext1Vtbl*>(slots.data());
    }
    ID3D11DeviceContext1* get() { return &object; }

private:
    std::vector<void*> slots;
    ID3D11DeviceContext1 object{};
};

void clear_context_registry()
{
    ::EnterCriticalSection(&context_map_lock);
    context_map.clear();
    ::LeaveCriticalSection(&context_map_lock);
    DeleteCriticalSection(&context_map_lock);
    hook_state.store(HookState::NotStarted, std::memory_order_release);
}

} // namespace

void audit_dx11_context_hook_install()
{
    using namespace dx11_hook_install_audit;

    // T-1: two installers, one installation body.
    hook_state.store(HookState::NotStarted, std::memory_order_release);
    FakeContext original, other;
    run_double_install([&] {
        hook_context(original.get(), other.get());
    }, 115, "context");

    require(hook_state.load(std::memory_order_acquire) == HookState::Ready,
        "F182 T-1: context state is Ready after the single installation");
    require(context_map.size() == 1 && context_map.at(original.get()) == other.get(),
        "F182 T-1: the successful installer publishes exactly one map entry");
    require(lookup_hooked_context(original.get()) == other.get(),
        "F182 T-1: Ready lookup resolves the wrapper");
    clear_context_registry();

    // T-2: the third Hook fails; the first two are rolled back LIFO.
    audit_hook_mgr.reset();
    install_gate.reset(false);
    FakeContext failing, failing_hacker;
    audit_hook_mgr.fail_at = 3;
    ID3D11DeviceContext1* returned = hook_context(failing.get(), failing_hacker.get());
    require(returned == failing.get(),
        "F182 T-2: failed installation returns the original context pointer");
    require(hook_state.load(std::memory_order_acquire) == HookState::Failed,
        "F182 T-2: failure is published as Failed");
    require(context_map.empty(),
        "F182 T-2: failed installation does not insert a map entry");
    require(lookup_hooked_context(failing.get()) == nullptr,
        "F182 T-2: Failed state makes lookup return null");
    require(audit_hook_mgr.attempts == 3 && audit_hook_mgr.calls.size() == 2,
        "F182 T-2: installation stops at the failed third Hook");
    require(audit_hook_mgr.unhooked == std::vector<SIZE_T>({2, 1}),
        "F182 T-2: rollback unhooks the successful ids in LIFO order");
    require(audit_hook_mgr.enabled.empty(),
        "F182 T-2: no hook is enabled after a create failure");

    // Failed is terminal: no retry, no second map entry, no extra Hook call.
    ID3D11DeviceContext1* retried = hook_context(failing.get(), failing_hacker.get());
    require(retried == failing.get() && audit_hook_mgr.attempts == 3 &&
        context_map.empty(), "F182 T-2: Failed is terminal and never retried");

    DeleteCriticalSection(&context_map_lock);
    hook_state.store(HookState::NotStarted, std::memory_order_release);
}
