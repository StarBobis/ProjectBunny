#include "slot_range_test_support.h"
#include <new>

void test_dx11_overlay_lifecycle_regression();

namespace {
using namespace dx11_slot_test;
class InitializationRuntime {
public:
    HMODULE module = nullptr;
    InitializationRuntime()
    {
        module = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_batch(module != nullptr, "F005: load actual native runtime");
    }
    ~InitializationRuntime() { FreeLibrary(module); }
};
class PoisonedDeviceFixture {
    InitializationRuntime runtime;
    ComPtr<ID3D11DeviceContext1> context;
public:
    HackerDevice* device = nullptr;
    PoisonedDeviceFixture()
    {
        const auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime.module, "D3D11CreateDevice"));
        require_batch(create != nullptr, "F005: resolve real device creation");
        ComPtr<ID3D11Device> base; ComPtr<ID3D11DeviceContext> immediate;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        require_batch(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
            &base, nullptr, &immediate)), "F005: actual WARP constructor dependencies");
        ComPtr<ID3D11Device1> native;
        require_batch(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "F005: native versioned interfaces");
        base.Reset(); immediate.Reset();
        static_assert(alignof(HackerDevice) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "update poisoned storage alignment");
        std::unique_ptr<void, void(*)(void*)> storage(::operator new(sizeof(HackerDevice)), [](void* memory) { ::operator delete(memory); });
        std::memset(storage.get(), 0xa5, sizeof(HackerDevice));
        device = new (storage.get()) HackerDevice(native.Get(), context.Get());
        storage.release(); native.Detach(); // Transfer the normal native device reference to its wrapper.
    }
    ~PoisonedDeviceFixture() { context.Reset(); if (device) device->Release(); }
};
}

void test_dx11_device_initialization_regression()
{
    {
        PoisonedDeviceFixture fixture;
        require_batch(fixture.device->GetHackerContext() == nullptr && fixture.device->GetHackerSwapChain() == nullptr,
            "F005: real constructor initializes both pointers even when heap storage is nonzero");
    }
    test_dx11_overlay_lifecycle_regression();
}
