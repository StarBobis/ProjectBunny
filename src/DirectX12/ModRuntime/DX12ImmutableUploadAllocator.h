#pragma once

#include <d3d12.h>
#include <algorithm>
#include <cstring>

// Append-only UPLOAD pages for immutable constant-buffer snapshots. The caller
// serializes Allocate/Reset. Each returned resource owns a reference and must be
// retained until its command lists retire, just like a dedicated allocation.
// No offset is reused, even when a cache entry is evicted or a frame advances.
class DX12ImmutableUploadAllocator
{
public:
    static constexpr UINT64 PageBytes = 64 * 1024;
    static constexpr UINT64 Alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

    DX12ImmutableUploadAllocator() = default;
    DX12ImmutableUploadAllocator(const DX12ImmutableUploadAllocator &) = delete;
    DX12ImmutableUploadAllocator &operator=(const DX12ImmutableUploadAllocator &) = delete;

    ~DX12ImmutableUploadAllocator()
    {
        Reset();
    }

    void Reset()
    {
        if (mResource) {
            // Unmapping is not retirement: external cache/command-list owners
            // keep the resource alive, and published bytes remain unchanged.
            mResource->Unmap(0, nullptr);
            mResource->Release();
        }
        mResource = nullptr;
        mDevice = nullptr;
        mMapped = nullptr;
        mGpuStart = 0;
        mUsed = 0;
        mCapacity = 0;
    }

    bool Allocate(ID3D12Device *device, const void *bytes, UINT byteCount,
        ID3D12Resource **resource, D3D12_GPU_VIRTUAL_ADDRESS *gpuAddress)
    {
        if (!resource || !gpuAddress)
            return false;
        *resource = nullptr;
        *gpuAddress = 0;
        if (!device || !bytes || !byteCount)
            return false;
        const UINT64 allocationBytes =
            (static_cast<UINT64>(byteCount) + Alignment - 1) & ~(Alignment - 1);
        if (device != mDevice || allocationBytes > mCapacity - mUsed) {
            if (!CreatePage(device, (std::max)(PageBytes, allocationBytes)))
                return false;
        }

        std::memcpy(mMapped + mUsed, bytes, byteCount);
        *gpuAddress = mGpuStart + mUsed;
        mResource->AddRef();
        *resource = mResource;
        mUsed += allocationBytes;
        return true;
    }

private:
    bool CreatePage(ID3D12Device *device, UINT64 capacity)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = capacity;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource *page = nullptr;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&page))) || !page)
            return false;
        void *mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(page->Map(0, &noRead, &mapped)) || !mapped) {
            page->Release();
            return false;
        }
        const auto gpuStart = page->GetGPUVirtualAddress();
        if (!gpuStart) {
            page->Unmap(0, nullptr);
            page->Release();
            return false;
        }

        Reset();
        mResource = page;
        mDevice = device;
        mMapped = static_cast<unsigned char *>(mapped);
        mGpuStart = gpuStart;
        mCapacity = capacity;
        return true;
    }

    ID3D12Resource *mResource = nullptr;
    ID3D12Device *mDevice = nullptr; // owned transitively by mResource
    unsigned char *mMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS mGpuStart = 0;
    UINT64 mUsed = 0;
    UINT64 mCapacity = 0;
};
