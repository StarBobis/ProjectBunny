#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <set>
#include <vector>
#include "../src/DirectX12/ModRuntime/DX12ImmutableUploadAllocator.h"

using Microsoft::WRL::ComPtr;

static void Check(bool value, const char *message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static void Hr(HRESULT hr, const char *message)
{
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAIL: %s hr=%08lx\n", message, static_cast<unsigned long>(hr));
        std::exit(1);
    }
}
struct UploadSlice
{
    ComPtr<ID3D12Resource> resource;
    D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};
static ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, UINT64 size, bool readback)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        readback ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&resource)), "create buffer");
    return resource;
}

int main(int argc, char **argv)
{
    const bool hardware = argc == 2 && std::strcmp(argv[1], "--hardware") == 0;
    ComPtr<IDXGIFactory4> factory;
    Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter> adapter;
    if (hardware) {
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const HRESULT result = factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            Hr(result, "enumerate hardware adapters");
            DXGI_ADAPTER_DESC1 desc{};
            Hr(candidate->GetDesc1(&desc), "adapter flags");
            // Some remote/virtualized sessions expose Microsoft's Basic
            // Render Driver without the SOFTWARE flag. Do not label it a GPU.
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && desc.VendorId != 0x1414 &&
                std::wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0) {
                Hr(candidate.As(&adapter), "adapter interface");
                break;
            }
        }
        if (!adapter) {
            std::puts("SKIP: no hardware adapter is visible in this process; use the WARP test.");
            return 77;
        }
    } else {
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "WARP");
    }
    DXGI_ADAPTER_DESC adapterDesc{};
    Hr(adapter->GetDesc(&adapterDesc), "adapter description");
    std::printf("Adapter: %ls\n", adapterDesc.Description);
    ComPtr<ID3D12Device> device;
    Hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device");
    constexpr UINT Count = 520;
    constexpr UINT Bytes = 256;
    DX12ImmutableUploadAllocator uploads;
    std::array<unsigned char, Bytes> bytes{};
    std::vector<UploadSlice> slices(Count);
    std::set<ID3D12Resource *> pages;
    const auto start = std::chrono::steady_clock::now();
    for (UINT i = 0; i < Count; ++i) {
        bytes.fill(static_cast<unsigned char>(i));
        Check(uploads.Allocate(device.Get(), bytes.data(), Bytes,
            slices[i].resource.GetAddressOf(), &slices[i].address), "allocate snapshot");
        Check(slices[i].address % 256 == 0, "CBV alignment");
        pages.insert(slices[i].resource.Get());
        if (i && slices[i].resource == slices[i - 1].resource)
            Check(slices[i].address == slices[i - 1].address + Bytes, "non-overlapping slices");
    }
    const auto pageUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    Check(pages.size() == 3, "520 snapshots require three pages, not 520 resources");
    uploads.Reset(); // Retained slices must remain valid past allocator/cache reset.
    uploads.Reset();
    UploadSlice fresh;
    bytes.fill(17);
    Check(uploads.Allocate(device.Get(), bytes.data(), Bytes, fresh.resource.GetAddressOf(),
        &fresh.address), "allocate after reset");
    Check(!pages.contains(fresh.resource.Get()), "reset never recycles retained pages");

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Hr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "queue");
    ComPtr<ID3D12CommandAllocator> allocator;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&list)), "command list");
    auto readback = Buffer(device.Get(), Count * Bytes, true);
    for (UINT i = 0; i < Count; ++i) {
        const UINT64 offset = slices[i].address - slices[i].resource->GetGPUVirtualAddress();
        list->CopyBufferRegion(readback.Get(), static_cast<UINT64>(i) * Bytes,
            slices[i].resource.Get(), offset, Bytes);
    }
    Hr(list->Close(), "close");
    ID3D12CommandList *lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(event != nullptr, "event");
    Hr(queue->Signal(fence.Get(), 1), "signal");
    Hr(fence->SetEventOnCompletion(1, event), "event on completion");
    Check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "GPU completed immutable snapshot reads");
    CloseHandle(event);
    void *mapped = nullptr;
    const D3D12_RANGE readRange{0, Count * Bytes};
    Hr(readback->Map(0, &readRange, &mapped), "readback map");
    const auto *data = static_cast<const unsigned char *>(mapped);
    for (UINT i = 0; i < Count; ++i)
        for (UINT n = 0; n < Bytes; ++n)
            Check(data[i * Bytes + n] == static_cast<unsigned char>(i), "GPU snapshot bytes unchanged");
    const D3D12_RANGE noWrites{0, 0};
    readback->Unmap(0, &noWrites);

    // Large views and non-aligned requests cannot overlap or wrap the page cursor.
    std::vector<unsigned char> large(131073, 0xAB);
    UploadSlice big, afterBig;
    Check(uploads.Allocate(device.Get(), large.data(), static_cast<UINT>(large.size()),
        big.resource.GetAddressOf(), &big.address), "large snapshot");
    Check(uploads.Allocate(device.Get(), bytes.data(), 1, afterBig.resource.GetAddressOf(),
        &afterBig.address), "small snapshot after full large page");
    Check(big.resource != afterBig.resource, "large page capacity respected");
    UploadSlice invalid;
    Check(!uploads.Allocate(device.Get(), bytes.data(), 0, invalid.resource.GetAddressOf(),
        &invalid.address), "reject empty allocation");
    Check(!invalid.resource && !invalid.address, "failure clears outputs");

    std::vector<ComPtr<ID3D12Resource>> dedicated;
    dedicated.reserve(Count);
    const auto oldStart = std::chrono::steady_clock::now();
    for (UINT i = 0; i < Count; ++i) {
        auto resource = Buffer(device.Get(), Bytes, false);
        const D3D12_RANGE noRead{0, 0};
        void *target = nullptr;
        Hr(resource->Map(0, &noRead, &target), "dedicated map");
        std::memcpy(target, bytes.data(), Bytes);
        resource->Unmap(0, nullptr);
        dedicated.push_back(std::move(resource));
    }
    const auto dedicatedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - oldStart).count();
    std::printf("PASS: alignment, non-overlap, rollover, reset lifetime, GPU contents, large/invalid requests\n");
    std::printf("BENCH (allocation CPU time, not game FPS): %u snapshots; dedicated=%lld us / %u resources; pages=%lld us / %zu resources\n",
        Count, dedicatedUs, Count, pageUs, pages.size());
    return 0;
}
