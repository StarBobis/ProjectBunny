// Test-only chained provider. This is synthetic failure of the device-creation
// exports, not a complete D3D runtime or an implementation of resource OOM.
#include <Windows.h>
#include <d3d11.h>

namespace {
volatile LONG device_calls = 0;
volatile LONG swapchain_calls = 0;
}

extern "C" HRESULT WINAPI FaultCreateDevice(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device** device,
    D3D_FEATURE_LEVEL*, ID3D11DeviceContext** context)
{
    InterlockedIncrement(&device_calls);
    if (device) *device = nullptr;
    if (context) *context = nullptr;
    return E_OUTOFMEMORY;
}

extern "C" HRESULT WINAPI FaultCreateDeviceAndSwapChain(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain** chain,
    ID3D11Device** device, D3D_FEATURE_LEVEL*, ID3D11DeviceContext** context)
{
    InterlockedIncrement(&swapchain_calls);
    if (chain) *chain = nullptr;
    if (device) *device = nullptr;
    if (context) *context = nullptr;
    return E_OUTOFMEMORY;
}

extern "C" void WINAPI BunnyFaultGetCounts(UINT* devices, UINT* chains)
{
    if (devices) *devices = static_cast<UINT>(InterlockedCompareExchange(&device_calls, 0, 0));
    if (chains) *chains = static_cast<UINT>(InterlockedCompareExchange(&swapchain_calls, 0, 0));
}
