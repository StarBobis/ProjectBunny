// Tests the production SurfaceCache class extracted by the companion script.
// Real WARP/DXGI objects; no game, GPU submissions or DLSS installation needed.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "surface_cache_under_test.h"

using Microsoft::WRL::ComPtr;
using Cache = Bunny::DX12CustomShaderSurfaceCache;

static void Check(bool ok, const char *text)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", text);
        std::exit(1);
    }
}

static void CheckHr(HRESULT hr, const char *text)
{
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAIL: %s hr=%08lx\n", text, static_cast<unsigned long>(hr));
        std::exit(1);
    }
}

// Implementation-level ownership checks on WARP, complemented by a real
// same-HWND recreation test (COM generally does not promise exact refcounts).
static ULONG Refs(IUnknown *object)
{
    object->AddRef();
    return object->Release();
}

static HWND MakeWindow()
{
    HWND window = CreateWindowExW(0, L"STATIC", L"SurfaceCacheTest", WS_OVERLAPPEDWINDOW,
        0, 0, 160, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(window != nullptr, "CreateWindowEx");
    return window;
}

static ComPtr<IDXGISwapChain3> MakeChain(
    IDXGIFactory4 *factory, ID3D12CommandQueue *queue, HWND window)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 128;
    desc.Height = 96;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> base;
    CheckHr(factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &base),
        "CreateSwapChainForHwnd");
    ComPtr<IDXGISwapChain3> result;
    CheckHr(base.As(&result), "QI SwapChain3");
    return result;
}

static void Ensure(Cache *cache, IDXGISwapChain3 *chain, bool withOutput)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    DXGI_FORMAT format{};
    UINT width = 0, height = 0;
    ID3D12Resource *resource = nullptr;
    Check(cache->EnsureBackBufferView(chain, &rtv, &format, &width, &height,
        withOutput ? &resource : nullptr), "EnsureBackBufferView");
    Check(rtv.ptr && width == 128 && height == 96, "valid view metrics");
    if (withOutput) {
        Check(resource != nullptr, "output resource");
        resource->Release();
    }
}

int main(int argc, char **argv)
{
    Check(argc == 2, "usage: test invalidate|no-output|switch|recreate|resize");
    ComPtr<IDXGIFactory4> factory;
    CheckHr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter> warp;
    CheckHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP");
    ComPtr<ID3D12Device> device;
    CheckHr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device");
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    CheckHr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "queue");
    HWND window = MakeWindow(), window2 = nullptr;
    auto chain = MakeChain(factory.Get(), queue.Get(), window);
    Cache *cache = Cache::ForDevice(device.Get());
    Check(cache != nullptr, "cache");
    const ULONG chainBaseline = Refs(chain.Get());

    if (!std::strcmp(argv[1], "invalidate")) {
        Ensure(cache, chain.Get(), true);
        // DXGI back buffers can themselves retain their parent swap chain.
        // Only the post-invalidation baseline is independent of that detail.
        Check(Refs(chain.Get()) > chainBaseline, "cache retains chain references");
        Cache::InvalidateChain(chain.Get());
        Check(Refs(chain.Get()) == chainBaseline, "Invalidate releases cached chain QI");
        Cache::InvalidateChain(chain.Get());
        Check(Refs(chain.Get()) == chainBaseline, "Invalidate is idempotent");
    } else if (!std::strcmp(argv[1], "no-output")) {
        ComPtr<ID3D12Resource> buffer;
        CheckHr(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "buffer");
        const ULONG baseline = Refs(buffer.Get());
        Ensure(cache, chain.Get(), false);
        Check(Refs(buffer.Get()) == baseline + 1, "no-output path retains one cache resource ref");
        Ensure(cache, chain.Get(), true);
        Check(Refs(buffer.Get()) == baseline + 1, "output borrow preserves cache ref");
        Cache::InvalidateChain(chain.Get());
        Check(Refs(buffer.Get()) == baseline, "Invalidate releases exactly one buffer ref");
    } else if (!std::strcmp(argv[1], "switch")) {
        ComPtr<ID3D12Resource> oldBuffer;
        CheckHr(chain->GetBuffer(0, IID_PPV_ARGS(&oldBuffer)), "old buffer");
        const ULONG baseline = Refs(oldBuffer.Get());
        const ULONG oldChainBaseline = Refs(chain.Get());
        Ensure(cache, chain.Get(), true);
        Check(Refs(oldBuffer.Get()) == baseline + 1, "old buffer cached");
        window2 = MakeWindow();
        auto next = MakeChain(factory.Get(), queue.Get(), window2);
        Ensure(cache, next.Get(), true);
        Check(Refs(chain.Get()) == oldChainBaseline, "switch releases old chain QI");
        Check(Refs(oldBuffer.Get()) == baseline, "switch releases old buffer before overwrite");
        Cache::InvalidateChain(next.Get());
    } else if (!std::strcmp(argv[1], "recreate")) {
        for (unsigned iteration = 0; iteration < 16; ++iteration) {
            Ensure(cache, chain.Get(), true);
            Cache::InvalidateChain(chain.Get());
            chain.Reset();
            chain = MakeChain(factory.Get(), queue.Get(), window);
        }
    } else if (!std::strcmp(argv[1], "resize")) {
        Ensure(cache, chain.Get(), false);
        Cache::InvalidateChain(chain.Get());
        CheckHr(chain->ResizeBuffers(2, 128, 96, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "ResizeBuffers");
        Ensure(cache, chain.Get(), true);
        Cache::InvalidateChain(chain.Get());
    } else {
        Check(false, "unknown test case");
    }
    Cache::ClearAll();
    chain.Reset();
    if (window2)
        DestroyWindow(window2);
    DestroyWindow(window);
    std::printf("PASS: %s\n", argv[1]);
    return 0;
}
