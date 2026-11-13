#pragma once
#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <stdexcept>
#include <vector>

namespace dx11_slot_test {
using Microsoft::WRL::ComPtr;

inline void require_batch(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class BatchDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    CommandListState state;

    BatchDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_batch(runtime != nullptr, "batch: load actual D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_batch(create != nullptr, "batch: resolve actual WARP device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        require_batch(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "batch: create actual WARP device");
        require_batch(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "batch: query D3D11.1");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
        state.mHackerDevice = device;
        state.mHackerContext = wrapper.get();
        state.mOrigDevice1 = native.Get();
        state.mOrigContext1 = context.Get();
    }

    ~BatchDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

inline ComPtr<ID3D11Texture2D> texture(BatchDevice& device, UINT colour, UINT width = 4)
{
    std::vector<UINT> pixels(width * 3, colour);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = 3;
    desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * sizeof(UINT);
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, &initial, &result)), "batch: create actual populated texture");
    return result;
}

inline ComPtr<ID3D11ShaderResourceView> srv(BatchDevice& device, ID3D11Resource* resource)
{
    ComPtr<ID3D11ShaderResourceView> result;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(resource, nullptr, &result)), "batch: create actual SRV");
    return result;
}

inline void set_slot(BatchDevice& device, wchar_t stage, UINT slot, ID3D11ShaderResourceView* view)
{
    switch (stage) {
    case L'v': device.context->VSSetShaderResources(slot, 1, &view); break;
    case L'h': device.context->HSSetShaderResources(slot, 1, &view); break;
    case L'd': device.context->DSSetShaderResources(slot, 1, &view); break;
    case L'g': device.context->GSSetShaderResources(slot, 1, &view); break;
    case L'p': device.context->PSSetShaderResources(slot, 1, &view); break;
    case L'c': device.context->CSSetShaderResources(slot, 1, &view); break;
    }
}

inline ComPtr<ID3D11ShaderResourceView> get_slot(BatchDevice& device, wchar_t stage, UINT slot)
{
    ComPtr<ID3D11ShaderResourceView> result;
    switch (stage) {
    case L'v': device.context->VSGetShaderResources(slot, 1, &result); break;
    case L'h': device.context->HSGetShaderResources(slot, 1, &result); break;
    case L'd': device.context->DSGetShaderResources(slot, 1, &result); break;
    case L'g': device.context->GSGetShaderResources(slot, 1, &result); break;
    case L'p': device.context->PSGetShaderResources(slot, 1, &result); break;
    case L'c': device.context->CSGetShaderResources(slot, 1, &result); break;
    }
    return result;
}

inline ComPtr<ID3D11Resource> bound_resource(BatchDevice& device, wchar_t stage, UINT slot)
{
    auto view = get_slot(device, stage, slot);
    ComPtr<ID3D11Resource> resource;
    if (view) view->GetResource(&resource);
    return resource;
}

inline bool parse(CommandList& list, std::wstring destination, std::wstring source)
{
    std::transform(destination.begin(), destination.end(), destination.begin(), ::towlower);
    std::transform(source.begin(), source.end(), source.begin(), ::towlower);
    const std::wstring ini_namespace;
    CommandListScope scope;
    scope.emplace_front();
    list.scope = &scope;
    const bool result = ParseCommandListResourceCopyTargetDirective(L"CommandListSlotBatchRegression",
        destination.c_str(), &source, &list, &ini_namespace);
    list.scope = nullptr;
    return result;
}

inline void append(CommandList& list, const std::wstring& destination, const std::wstring& source)
{
    require_batch(parse(list, destination, source), "batch: parse actual resource-copy directive");
}

inline ShaderResourceBatchStats execute(CommandList& list, BatchDevice& device)
{
    // These counters measure native calls issued by the batch helpers only.
    // They intentionally exclude test observations and standalone barriers.
    ShaderResourceBatchStats stats;
    device.state.slot_batch_stats = &stats;
    try {
        for (const auto& command : list.commands) command->run(&device.state);
    } catch (...) {
        device.state.slot_batch_stats = nullptr;
        throw;
    }
    device.state.slot_batch_stats = nullptr;
    return stats;
}

inline void require_pixels(BatchDevice& device, ID3D11Resource* resource, UINT colour)
{
    ComPtr<ID3D11Texture2D> original;
    require_batch(resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&original))), "batch: copied texture exists");
    D3D11_TEXTURE2D_DESC desc = {};
    original->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "batch: create real staging readback");
    device.context->CopyResource(staging.Get(), resource);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "batch: map actual copy");
    bool equal = true;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y);
        for (UINT x = 0; x < desc.Width; ++x) equal &= row[x] == colour;
    }
    device.context->Unmap(staging.Get(), 0);
    require_batch(equal, "batch: all texture rows retain actual copied bytes");
}


class Registry {
public:
    std::vector<std::wstring> resources, pools, variables;
    ~Registry()
    {
        ClearDeferredBindFlags();
        for (const auto& name : pools) {
            const auto found = customResourcePools.find(name);
            if (found == customResourcePools.end()) continue;
            const auto* owned = &found->second;
            for (auto it = customResources.begin(); it != customResources.end();) {
                if (it->second.pool == owned) it = customResources.erase(it);
                else ++it;
            }
            const auto prefix = L"$" + name + L"[";
            for (auto it = command_list_globals.begin(); it != command_list_globals.end();) {
                if (it->first.compare(0, prefix.size(), prefix) == 0) {
                    auto* value = &it->second;
                    persistent_variables.erase(std::remove(persistent_variables.begin(), persistent_variables.end(), value), persistent_variables.end());
                    it = command_list_globals.erase(it);
                } else ++it;
            }
            customResourcePools.erase(found);
        }
        for (const auto& name : resources) customResources.erase(name);
        for (const auto& name : variables) command_list_globals.erase(name);
    }
    CustomResource& resource(const std::wstring& name, BatchDevice& device, ID3D11Resource* native = nullptr,
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, UINT stride = 0, UINT size = 0)
    {
        require_batch(!customResources.count(name), "slot: unique resource fixture");
        resources.push_back(name);
        auto& value = customResources[name];
        value.name = name; value.resource = native;
        if (native) native->AddRef();
        value.device = device.native.Get();
        value.is_null = native == nullptr; value.substantiated = true;
        value.format = format; value.stride = stride; value.buf_size = size;
        return value;
    }
    CustomResourcePool& pool(const std::wstring& name, BatchDevice& device, PoolIndexType type, size_t count)
    {
        require_batch(!customResourcePools.count(name), "slot: unique pool fixture");
        pools.push_back(name);
        auto& value = customResourcePools[name];
        value.name = name; value.index_type = type;
        value.resource_template = &resource(name + L"_template", device);
        value.resource_template->pool = &value; value.resource_template->pool_index = -1;
        value.variable_template = std::make_unique<CommandListVariable>(name + L"_template", -3, VariableFlags::NONE);
        value.element_type_switch_reset = false;
        value.Initialize(count);
        return value;
    }
    CommandListVariable& variable(const std::wstring& name, float value)
    {
        require_batch(!command_list_globals.count(name), "slot: unique global fixture");
        variables.push_back(name);
        return command_list_globals.emplace(name, CommandListVariable(name, value, VariableFlags::GLOBAL)).first->second;
    }
};

inline void scalar(BatchDevice& device, const std::wstring& destination, const std::wstring& source)
{
    CommandList command;
    append(command, destination, source);
    execute(command, device);
}

inline ComPtr<ID3D11Buffer> buffer(BatchDevice& device, UINT size, UINT flags, UINT misc = 0, UINT stride = 0)
{
    std::vector<unsigned char> bytes(size);
    for (UINT i = 0; i < size; ++i) bytes[i] = static_cast<unsigned char>(i * 37 + 11);
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = size; desc.BindFlags = flags;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.MiscFlags = misc; desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = bytes.data();
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, &data, &result)), "slot: real buffer creation");
    return result;
}

inline void assign(BatchDevice& device, CustomResource* destination, ID3D11Resource* source,
    ID3D11View* view = nullptr, UINT offset = 0, UINT size = 0, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
{
    ResourceCopyTarget target;
    target.type = ResourceCopyTargetType::CUSTOM_RESOURCE; target.SetCustomResource(destination);
    target.SetResource(&device.state, source, view, 0, offset, format, size);
}

inline ComPtr<ID3D11Buffer> get_cb(BatchDevice& device, wchar_t stage, UINT slot, UINT& offset, UINT& count)
{
    ComPtr<ID3D11Buffer> result;
    switch (stage) {
    case L'v': device.context->VSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    case L'h': device.context->HSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    case L'd': device.context->DSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    case L'g': device.context->GSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    case L'p': device.context->PSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    case L'c': device.context->CSGetConstantBuffers1(slot, 1, &result, &offset, &count); break;
    }
    return result;
}
} // namespace dx11_slot_test
