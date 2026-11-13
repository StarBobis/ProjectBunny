"""Execute production getters against WARP resources with a reduced target adapter.

The adapter replaces pipeline slot lookup, not D3D11 resources or COM ownership.
INI parsing and command-list integration are outside this test's scope.
Run in an x64 MSVC developer shell.
"""
import pathlib
import subprocess
import runpy

ROOT = pathlib.Path(__file__).resolve().parents[1]
extract = runpy.run_path(str(ROOT / 'scripts/test-dx11-pool-flags.py'))['method']


def main():
    source = (ROOT / 'src/DirectX11/CommandList.cpp').read_bytes().decode('latin1')
    hashing = (ROOT / 'src/DirectX11/ResourceHash.cpp').read_bytes().decode('latin1')
    util = (ROOT / 'src/common/util.h').read_bytes().decode('latin1')
    methods = '\n'.join(extract(hashing if signature.startswith(('float BitCast', 'float Encode', 'uint64_t Hash', 'uint32_t Hash')) else source, signature) for signature in (
        'float BitCastToFloat(', 'float EncodeFloat30(',
        'uint64_t HashPointer(', 'uint32_t HashUnsigned32(',
        'float ResourceCopyTarget::GetResourceId(', 'float ResourceCopyTarget::GetPoolId(',
        'const CustomResourcePool* CustomResourcePool::ResolvePool(',
        'float ResourceCopyTarget::GetResourceStride(', 'float ResourceCopyTarget::GetResourceSize('
    ))
    harness = r'''
#include <d3d11.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>
struct CommandListState {};
class CustomResourcePool {
public:
    CustomResourcePool* source_pool = nullptr;
    const CustomResourcePool* ResolvePool() const;
};
struct CustomResource {
    int override_stride = -1;
    DXGI_FORMAT override_format = static_cast<DXGI_FORMAT>(-1);
    UINT buf_size = 0;
};
enum class ResourceCopyTargetType { CUSTOM_RESOURCE, VERTEX_BUFFER, CONSTANT_BUFFER };
namespace ResourcePropertyResult {
constexpr float UNKNOWN = -1, RESOURCE_NOT_FOUND = -2, NOT_A_BUFFER = -3;
}
class ResourceCopyTarget {
public:
    ResourceCopyTargetType type = ResourceCopyTargetType::VERTEX_BUFFER;
    CustomResource* custom = nullptr;
    ID3D11Resource* resource = nullptr;
    ID3D11View* bound_view = nullptr;
    UINT bound_stride = 0, bound_size = 0;
    CustomResourcePool* custom_resource_pool = nullptr;
    CustomResource* GetCustomResource(CommandListState*) { return custom; }
    ID3D11Resource* GetResource(CommandListState*, ID3D11View** view, UINT* stride = nullptr,
        UINT* = nullptr, DXGI_FORMAT* = nullptr, UINT* size = nullptr) {
        *view = bound_view;
        if (*view) (*view)->AddRef();
        if (stride) *stride = bound_stride;
        if (size) *size = bound_size;
        if (resource) resource->AddRef();
        return resource;
    }
    float GetResourceId(CommandListState*);
    float GetPoolId();
    float GetResourceStride(CommandListState*);
    float GetResourceSize(CommandListState*);
};
'''
    tests = r'''
static ULONG refs(IUnknown* p) { p->AddRef(); return p->Release(); }
int main() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 256; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = 16;
    ID3D11Buffer* buffer = nullptr;
    assert(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)));
    ID3D11ShaderResourceView* view = nullptr;
    assert(SUCCEEDED(device->CreateShaderResourceView(buffer, nullptr, &view)));
    ResourceCopyTarget target;
    target.resource = buffer; target.bound_view = view;
    auto before = refs(buffer), view_before = refs(view);
    for (int i = 0; i < 1000; ++i) {
        assert(target.GetResourceStride(nullptr) == 16);
        assert(target.GetResourceSize(nullptr) == 256);
        assert(target.GetResourceId(nullptr) > 0);
    }
    assert(refs(buffer) == before && refs(view) == view_before);
    target.bound_stride = 32; target.bound_size = 64;
    assert(target.GetResourceStride(nullptr) == 32 && target.GetResourceSize(nullptr) == 64);
    CustomResource custom;
    target.type = ResourceCopyTargetType::CUSTOM_RESOURCE; target.custom = &custom;
    custom.override_stride = 48; custom.buf_size = 128;
    assert(target.GetResourceStride(nullptr) == 48 && target.GetResourceSize(nullptr) == 128);
    custom.override_stride = -1; custom.override_format = DXGI_FORMAT_R32G32_FLOAT;
    assert(target.GetResourceStride(nullptr) == 8);
    target.custom = nullptr; target.resource = nullptr; target.bound_view = nullptr;
    assert(target.GetResourceSize(nullptr) == -2 && target.GetResourceStride(nullptr) == -2);
    assert(target.GetResourceId(nullptr) == 0 && target.GetPoolId() == 0);
    CustomResourcePool pool;
    target.custom_resource_pool = &pool;
    assert(target.GetPoolId() > 0 && target.GetPoolId() == target.GetPoolId());
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT;
    ID3D11Texture2D* texture = nullptr;
    assert(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &texture)));
    // A real custom texture has a custom-resource owner. A null owner now
    // correctly means RESOURCE_NOT_FOUND before any backing-resource lookup.
    target.custom = &custom;
    custom.override_stride = -1; custom.override_format = DXGI_FORMAT_UNKNOWN; custom.buf_size = 0;
    target.resource = texture; target.bound_size = target.bound_stride = 0;
    assert(target.GetResourceSize(nullptr) == -3 && target.GetResourceStride(nullptr) == -3);
    texture->Release(); view->Release(); buffer->Release();
    for (uint32_t payload : {0u, 1u, 0x7fffffu, 0x800000u, 0x3fffffffu}) {
        float encoded = EncodeFloat30(payload);
        assert(std::isnormal(encoded) && encoded > 0);
        assert(encoded == EncodeFloat30(payload | 0xc0000000u));
        uint32_t bits; memcpy(&bits, &encoded, sizeof(bits));
        assert((((bits >> 23) - 1) << 23 | (bits & 0x7fffff)) == payload);
    }
    context->Release(); device->Release();
}
'''
    output = ROOT / 'build/dx11-property-regression'
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'properties.cpp'
    cpp.write_text(harness + extract(util, 'static UINT dxgi_format_size(') + methods + tests, encoding='utf-8')
    subprocess.run(['cl', '/nologo', '/std:c++17', '/EHsc', '/W4', str(cpp),
                    '/Fe:properties.exe', '/link', 'd3d11.lib'], cwd=output, check=True)
    subprocess.run([str(output / 'properties.exe')], check=True)
    print('PASS: WARP getters, overrides, missing/non-buffer errors, COM reference balance, float30 boundaries')


if __name__ == '__main__':
    main()
