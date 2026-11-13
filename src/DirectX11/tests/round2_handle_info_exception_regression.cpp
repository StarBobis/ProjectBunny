#include "CommandList.h"
#include "DLLMainHook.h"
#include "globals.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
using Allocate = void* (__cdecl*)(size_t);

Allocate original_allocate = nullptr;
thread_local size_t fail_size = 0;
thread_local bool allocation_hit = false;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void* __cdecl allocate_with_one_failure(size_t bytes)
{
    if (fail_size && bytes == fail_size) {
        // Throwing itself must not recursively arm another allocation failure.
        fail_size = 0;
        allocation_hit = true;
        throw std::bad_alloc();
    }
    return original_allocate(bytes);
}

class AllocationHook {
    SIZE_T id = 0;
public:
    AllocationHook()
    {
        Allocate target = static_cast<Allocate>(&::operator new);
        const DWORD error = cHookMgr.Hook(&id, reinterpret_cast<void**>(&original_allocate),
            reinterpret_cast<void*>(target), reinterpret_cast<void*>(&allocate_with_one_failure));
        if (error != ERROR_SUCCESS) {
            std::fprintf(stderr, "FAIL handle-info exception: allocator hook install error %lu\n", error);
            // A partially installed hook cannot safely outlive its fixture.
            if (id) Restore();
            throw std::runtime_error("cannot install temporary allocation hook");
        }
    }
    AllocationHook(const AllocationHook&) = delete;
    AllocationHook& operator=(const AllocationHook&) = delete;
    ~AllocationHook() { Restore(); }

    void Restore() noexcept
    {
        fail_size = 0;
        if (!id) return;
        const DWORD error = cHookMgr.Unhook(id);
        if (error != ERROR_SUCCESS) {
            std::fprintf(stderr, "FAIL handle-info exception: cannot restore allocator hook: %lu\n", error);
            // Do not keep executing tests with a hook whose lifetime is unknown.
            std::_Exit(EXIT_FAILURE);
        }
        id = 0;
        original_allocate = nullptr;
    }
};

struct LockProbe {
    bool acquired = false;
};

DWORD WINAPI probe_global_lock(void* argument)
{
    auto* result = static_cast<LockProbe*>(argument);
    if (TryEnterCriticalSection(&G->mCriticalSection)) {
        result->acquired = true;
        LeaveCriticalSection(&G->mCriticalSection);
    }
    return 0;
}

bool lock_available()
{
    LockProbe result;
    HANDLE thread = CreateThread(nullptr, 0, probe_global_lock, &result, 0, nullptr);
    check(thread != nullptr, "handle-info exception: create lock probe thread");
    const DWORD wait = WaitForSingleObject(thread, 2000);
    if (wait != WAIT_OBJECT_0) {
        std::fputs("FAIL handle-info exception: nonblocking lock probe did not finish\n", stderr);
        std::_Exit(EXIT_FAILURE); // The worker still borrows stack memory.
    }
    CloseHandle(thread);
    return result.acquired;
}

class SnapshotFixture {
    HMODULE runtime = nullptr;
public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> source;

    SnapshotFixture()
    {
        wchar_t path[MAX_PATH] = {};
        check(GetSystemDirectoryW(path, MAX_PATH) != 0, "handle-info exception: system directory");
        wcscat_s(path, L"\\d3d11.dll");
        runtime = LoadLibraryW(path);
        check(runtime != nullptr, "handle-info exception: load native D3D11");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr && SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr)),
            "handle-info exception: create native WARP device");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 64;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(SUCCEEDED(device->CreateBuffer(&desc, nullptr, source.GetAddressOf())),
            "handle-info exception: create real source buffer");
        auto* bytes = static_cast<unsigned char*>(std::malloc(desc.ByteWidth));
        check(bytes != nullptr, "handle-info exception: allocate source snapshot");
        for (unsigned i = 0; i < desc.ByteWidth; ++i) bytes[i] = static_cast<unsigned char>(i);
        G->mResources[source.Get()].SetDataCache(bytes, desc.ByteWidth);
    }
    ~SnapshotFixture()
    {
        if (source) G->mResources.erase(source.Get());
        source.Reset();
        device.Reset();
        if (runtime) FreeLibrary(runtime);
    }
};

bool inject_and_probe(CustomResource& destination, ID3D11Buffer* source, size_t bytes)
{
    check(lock_available(), "handle-info exception: initial global lock must be unowned");
    allocation_hit = false;
    bool caught_bad_alloc = false, caught_other = false;
    {
        AllocationHook hook;
        fail_size = bytes;
        try {
            // No fixture construction or thread creation occurs while armed.
            destination.SetHandleInfo(source, 8, 16);
        } catch (const std::bad_alloc&) {
            caught_bad_alloc = true;
        } catch (...) {
            caught_other = true;
        }
        hook.Restore();
    }
    const bool released = lock_available();
    if (!released) {
        // Only this thread called SetHandleInfo after the unowned baseline;
        // that function acquires exactly one layer. Repair only that layer so
        // the red result cannot deadlock subsequent tests or fixture cleanup.
        LeaveCriticalSection(&G->mCriticalSection);
    }
    check(lock_available(), "handle-info exception: recovered exactly the leaked lock layer");
    check(allocation_hit && caught_bad_alloc && !caught_other && fail_size == 0,
        "handle-info exception: exact-size production allocation must throw once");
    return released;
}
} // namespace

void test_dx11_round2_handle_info_exception_regression()
{
    SnapshotFixture fixture;
    const auto snapshot = G->mResources.at(fixture.source.Get()).cached_data;
    CustomResource ordinary;
    ordinary.SetHandleInfo(fixture.source.Get(), 8, 16);
    check(ordinary.GetHandleInfo() && ordinary.GetHandleInfo()->cached_data == snapshot &&
        ordinary.GetHandleInfo()->cached_data_offset == 8 && ordinary.GetHandleInfo()->cached_data_size == 16 &&
        ordinary.GetHandleInfo()->GetCachedData()[0] == 8 && lock_available(),
        "handle-info exception: normal path shares actual snapshot and releases lock");
    ordinary.SetHandleInfo(fixture.source.Get(), 16, 8);
    check(ordinary.GetHandleInfo()->cached_data == snapshot &&
        ordinary.GetHandleInfo()->GetCachedData()[0] == 16 && ordinary.GetHandleInfo()->cached_data_size == 8 &&
        lock_available(), "handle-info exception: repeated capture updates view and releases lock");

    CustomResource first_allocation;
    const bool first_released = inject_and_probe(first_allocation, fixture.source.Get(), sizeof(ResourceHandleInfo));
    CustomResource nested_allocation;
    nested_allocation.handle_info = std::make_unique<ResourceHandleInfo>();
    const bool nested_released = inject_and_probe(nested_allocation, fixture.source.Get(), sizeof(RegionHashesCache));
    first_allocation.SetHandleInfo(fixture.source.Get(), 8, 16);
    nested_allocation.SetHandleInfo(fixture.source.Get(), 8, 16);
    check(first_allocation.GetHandleInfo() && nested_allocation.GetHandleInfo() &&
        first_allocation.GetHandleInfo()->cached_data == snapshot &&
        nested_allocation.GetHandleInfo()->cached_data == snapshot &&
        first_allocation.GetHandleInfo()->GetCachedData()[0] == 8 &&
        nested_allocation.GetHandleInfo()->GetCachedData()[15] == 23 && lock_available(),
        "handle-info exception: both failed captures can retry using the unchanged source snapshot");
    // Both failure sites have been exercised and repaired before a red assert.
    check(first_released, "handle-info exception: ResourceHandleInfo allocation failure must release global lock");
    check(nested_released, "handle-info exception: nested region-cache allocation failure must release global lock");
}
