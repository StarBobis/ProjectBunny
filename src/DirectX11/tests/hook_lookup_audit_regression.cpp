#include "slot_range_test_support.h"

void audit_dx11_context_lookup(ID3D11DeviceContext1*, ID3D11DeviceContext1*,
    ID3D11DeviceContext1*, ID3D11DeviceContext1*);
void audit_dx11_context_release(ID3D11DeviceContext1*, ID3D11DeviceContext1*,
    ID3D11DeviceContext1*, ID3D11DeviceContext1*, ID3D11DeviceContext1*,
    ID3D11DeviceContext1*, ID3D11DeviceContext1*);
void audit_dx11_device_lookup(ID3D11Device1*, ID3D11Device1*, ID3D11Device1*, ID3D11Device1*);

namespace {
ULONG native_references(IUnknown* object)
{
    const ULONG value = object->AddRef();
    object->Release();
    return value;
}
}

void test_dx11_hook_lookup_regression()
{
    using namespace dx11_slot_test;
    // Each fixture retains actual WARP COM references and its Hacker wrappers
    // until both lookup threads have joined. No final Release or detour is
    // involved: those separate F002/F182 contracts remain unchanged.
    BatchDevice target, other;
    const ULONG context_before = native_references(target.context.Get());
    const ULONG device_before = native_references(target.native.Get());
    audit_dx11_context_lookup(target.context.Get(), target.wrapper.get(),
        other.context.Get(), other.wrapper.get());
    audit_dx11_device_lookup(target.native.Get(), target.device, other.native.Get(), other.device);
    require_batch(native_references(target.context.Get()) == context_before &&
        native_references(target.native.Get()) == device_before,
        "F181: borrowed lookup introduces no COM reference or release");
    require_batch(target.wrapper->GetContextFlags() == target.context->GetContextFlags(),
        "F181: retained real context and wrapper remain usable after the schedule");
    require_batch(target.device->GetFeatureLevel() == target.native->GetFeatureLevel(),
        "F181: retained real device and wrapper remain usable after the schedule");
}

void test_dx11_hook_release_regression()
{
    using namespace dx11_slot_test;
    // Deferred contexts keep the reference counts under this test's control;
    // an immediate context is also owned by the device.
    BatchDevice device;
    ComPtr<ID3D11DeviceContext1> context, guard_context, locked_context;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &context)),
        "F002: create actual WARP deferred context");
    require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &guard_context)),
        "F002: create actual WARP guard deferred context");
    require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &locked_context)),
        "F002: create actual WARP hooked-release deferred context");

    auto* wrapper = new HackerContext(device.native.Get(), context.Get());
    auto* guard_kept = new HackerContext(device.native.Get(), guard_context.Get());
    // Two wrappers over the same original context: the kept one must survive
    // the other wrapper's unregister call, which is the pointer-match guard.
    auto* guard_dying = new HackerContext(device.native.Get(), guard_context.Get());
    auto* locked_wrapper = new HackerContext(device.native.Get(), locked_context.Get());

    // Hooks disabled: HackerContext::Release still calls unregister, which
    // must return without touching the uninitialized registry lock.
    {
        ComPtr<ID3D11DeviceContext1> plain;
        require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &plain)),
            "F002: create unhooked deferred context");
        auto* plain_wrapper = new HackerContext(device.native.Get(), plain.Get());
        plain.Detach();
        require_batch(plain_wrapper->Release() == 0,
            "F002: release without installed hooks skips the registry lock");
    }

    ID3D11DeviceContext1* original = context.Get();
    ID3D11DeviceContext1* guard_original = guard_context.Get();
    ID3D11DeviceContext1* locked_original = locked_context.Get();
    // The audit owns and consumes these native references exactly as the
    // production Release paths do.
    context.Detach();
    guard_context.Detach();
    locked_context.Detach();
    audit_dx11_context_release(original, wrapper, guard_original, guard_kept, guard_dying,
        locked_original, locked_wrapper);

    // guard_kept outlived its native context: HackerContext's destructor does
    // not touch mOrigContext1, so direct deletion is the same teardown the
    // production Release path performs after the native count reaches zero.
    delete guard_kept;
}
