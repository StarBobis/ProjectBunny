#include "slot_range_test_support.h"

void audit_dx11_context_lookup(ID3D11DeviceContext1*, ID3D11DeviceContext1*,
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
