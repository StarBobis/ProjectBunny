#include "../lock.h"
#include "hook_lookup_audit_support.h"
#include "hook_install_audit_support.h"
#include "../DLLMainHook.h"

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

// F-002: the registry entry must die with the wrapper, not with the hooked
// Release() call. This helper runs inside the production TU so it can observe
// the private map directly while driving the real HackerContext::Release()
// and the production HookedContext::Release() body.
void audit_dx11_context_release(ID3D11DeviceContext1* original,
    ID3D11DeviceContext1* wrapper,
    ID3D11DeviceContext1* guard_original,
    ID3D11DeviceContext1* guard_kept_wrapper,
    ID3D11DeviceContext1* guard_dying_wrapper,
    ID3D11DeviceContext1* locked_original,
    ID3D11DeviceContext1* locked_wrapper)
{
    using namespace dx11_hook_lookup_audit;
    require(hook_state.load(std::memory_order_acquire) == HookState::NotStarted &&
        lookup_hooked_context(original) == nullptr,
        "F002: pre-install lookup avoids the uninitialized registry lock");
    _InitializeCriticalSectionPretty(&context_map_lock, "context_map_lock");
    hook_state.store(HookState::Ready, std::memory_order_release);
    try {
        require(context_map.empty(), "F002: isolate the production context registry");

        // Same original address, two different wrappers: the wrapper that is
        // dying must not remove the other wrapper's registration. This is the
        // pointer-value guard against a reused original-context address.
        context_map[guard_original] = guard_kept_wrapper;
        require(lookup_hooked_context(guard_original) == guard_kept_wrapper,
            "F002: guard registration is visible");
        require(ID3D11DeviceContext1_Release(guard_dying_wrapper) == 0,
            "F002: guard wrapper owns the final native reference");
        require(context_map.size() == 1 &&
            context_map.find(guard_original) != context_map.end() &&
            context_map.find(guard_original)->second == guard_kept_wrapper,
            "F002: a non-matching wrapper must not erase the registration");
        require(lookup_hooked_context(guard_original) == guard_kept_wrapper,
            "F002: kept registration still resolves after the other wrapper dies");
        context_map.clear();

        // Main F-002 contract: first Release goes through the hooked vtable
        // path and must keep the entry while the native context is still
        // referenced. The direct wrapper Release then owns the final native
        // reference and must remove the entry before deleting the wrapper.
        context_map[original] = wrapper;
        require(ID3D11DeviceContext1_AddRef(wrapper) == 2,
            "F002: native context has Create plus one simulated QI reference");
        require(Release(original) == 1,
            "F002: hooked Release leaves one native reference");
        require(context_map.size() == 1 &&
            context_map.find(original) != context_map.end() &&
            context_map.find(original)->second == wrapper,
            "F002: hooked non-final Release keeps the registry entry");
        require(lookup_hooked_context(original) == wrapper,
            "F002: lookup still resolves while the native context is alive");
        require(ID3D11DeviceContext1_Release(wrapper) == 0,
            "F002: direct wrapper Release owns the final native reference");
        require(context_map.find(original) == context_map.end(),
            "F002: context_map no longer holds the dead original address");
        require(context_map.empty(),
            "F002: context_map entry is removed when the wrapper dies");
        require(lookup_hooked_context(original) == nullptr,
            "F002: removed entry no longer resolves to a dead wrapper");

        // Final Release through the hooked entry point: the outer
        // HookedContext::Release still holds context_map_lock while the inner
        // HackerContext::Release erases the node, so this exercises the
        // recursive-lock and invalidated-iterator path.
        context_map[locked_original] = locked_wrapper;
        require(ID3D11DeviceContext1_AddRef(locked_wrapper) == 2,
            "F002: locked context has Create plus one simulated QI reference");
        require(Release(locked_original) == 1,
            "F002: first hooked Release keeps one native reference");
        require(context_map.size() == 1 &&
            context_map.find(locked_original) != context_map.end() &&
            context_map.find(locked_original)->second == locked_wrapper,
            "F002: first hooked Release keeps the locked registry entry");
        require(Release(locked_original) == 0,
            "F002: second hooked Release removes the final native reference");
        require(context_map.empty(),
            "F002: recursive unregister removes the entry under the outer lock");
        require(lookup_hooked_context(locked_original) == nullptr,
            "F002: recursive unregister leaves no resolvable entry");
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
