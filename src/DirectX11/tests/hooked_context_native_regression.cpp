// Native hook registry integration: use the real lifecycle entry points.
#include "../HookedContext.cpp"
#include "hook_lookup_audit_support.h"

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
