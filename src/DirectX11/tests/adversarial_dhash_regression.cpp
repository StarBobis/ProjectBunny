#include "slot_range_test_support.h"
#include <d3dcompiler.h>
#include <array>

namespace {
using namespace dx11_slot_test;
void sample_bc_mip(BatchDevice& device, ID3D11Texture2D* texture, UINT mip)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_BC1_UNORM;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MostDetailedMip = mip; desc.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(texture, &desc, &view)),
        "adversarial BC: original small mip supports direct SRV");
    const char shader[] = "Texture2D<float4> t:register(t0);RWStructuredBuffer<float4> o:register(u0);[numthreads(1,1,1)]void main(){o[0]=t.Load(int3(0,0,0));}";
    ComPtr<ID3DBlob> code;
    require_batch(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, nullptr)),
        "adversarial BC: real direct-sampling shader compiles");
    ComPtr<ID3D11ComputeShader> cs;
    require_batch(SUCCEEDED(device.native->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs)),
        "adversarial BC: direct-sampling shader creates");
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
    ComPtr<ID3D11Buffer> output;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&bd, nullptr, &output)), "adversarial BC: output buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 1;
    ComPtr<ID3D11UnorderedAccessView> uav;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(output.Get(), &ud, &uav)), "adversarial BC: output UAV");
    auto* raw_view = view.Get(); auto* raw_uav = uav.Get();
    device.context->CSSetShader(cs.Get(), nullptr, 0);
    device.context->CSSetShaderResources(0, 1, &raw_view);
    device.context->CSSetUnorderedAccessViews(0, 1, &raw_uav, nullptr);
    device.context->Dispatch(1,1,1);
    device.context->ClearState();
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = bd.MiscFlags = bd.StructureByteStride = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&bd, nullptr, &staging)), "adversarial BC: staging buffer");
    device.context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "adversarial BC: direct GPU result");
    std::array<float,4> rgba{}; std::memcpy(rgba.data(), mapped.pData, sizeof(rgba));
    device.context->Unmap(staging.Get(), 0);
    require_batch(rgba == std::array<float,4>{0,1,0,1}, "adversarial BC: native small mip samples exact opaque green");
}
}

void test_dx11_adversarial_dhash_regression()
{
    BatchDevice device;
    // One opaque green BC1 block per mip: valid 4x4, 2x2 and 1x1 levels.
    const std::array<unsigned char,8> block{0xe0,0x07,0,0,0,0,0,0};
    D3D11_SUBRESOURCE_DATA data[3]{};
    for (auto& item : data) { item.pSysMem = block.data(); item.SysMemPitch = 8; }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 4; desc.MipLevels = 3; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_BC1_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, data, &texture)), "adversarial BC: native valid mip chain");
    for (UINT mip = 0; mip < 3; ++mip) {
        sample_bc_mip(device, texture.Get(), mip);
        uint64_t hash = 0;
        const HRESULT hr = device.wrapper->ComputeDHash(texture.Get(), mip, DXGI_FORMAT_BC1_UNORM, hash);
        if (FAILED(hr)) std::fprintf(stderr, "adversarial BC mip %u ComputeDHash hr=%08x\n", mip, static_cast<unsigned>(hr));
        require_batch(SUCCEEDED(hr) && hash == UINT64_C(0x0000ffff0000ffff),
            "adversarial BC: legal compressed sub-4 mip must retain original opaque-green dHash");
    }
    device.context->ClearState();
}
