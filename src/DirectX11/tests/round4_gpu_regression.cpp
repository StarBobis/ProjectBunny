#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include <d3dcompiler.h>

namespace {
using namespace dx11_slot_test;
class RenderStateScope {
    UINT hunting = G->hunting;
    bool analysis = G->analyse_frame;
public:
    RenderStateScope() { G->hunting = HUNTING_MODE_DISABLED; G->analyse_frame = false; }
    ~RenderStateScope() { G->hunting = hunting; G->analyse_frame = analysis; }
};
}

void test_dx11_round4_gpu_regression()
{
    using namespace dx11_slot_test;
    RenderStateScope globals;
    DataCacheTestScope policy;
    BatchDevice device;
    float initial_values[16]{};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(initial_values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = initial_values;
    ComPtr<ID3D11Buffer> output;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &output)),
        "round4 GPU: create real dual VB/render-target buffer");
    D3D11_RENDER_TARGET_VIEW_DESC view_desc{};
    view_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    view_desc.ViewDimension = D3D11_RTV_DIMENSION_BUFFER;
    view_desc.Buffer.NumElements = 4;
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(output.Get(), &view_desc, &view)),
        "round4 GPU: create native buffer RTV");
    const char vs_text[] = "float4 main(uint id:SV_VertexID):SV_POSITION{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
    const char ps_text[] = "float4 main():SV_TARGET{return float4(.2,.4,.6,1);}";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    require_batch(SUCCEEDED(D3DCompile(vs_text, sizeof(vs_text), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr)), "round4 GPU: compile native VS");
    require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs)), "round4 GPU: create native VS");
    code.Reset();
    require_batch(SUCCEEDED(D3DCompile(ps_text, sizeof(ps_text), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, nullptr)), "round4 GPU: compile native PS");
    require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps)), "round4 GPU: create native PS");
    device.context->VSSetShader(vs.Get(), nullptr, 0);
    device.context->PSSetShader(ps.Get(), nullptr, 0);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_VIEWPORT viewport{}; viewport.Width = 4; viewport.Height = 1; viewport.MaxDepth = 1;
    device.context->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* bound = view.Get();
    device.context->OMSetRenderTargets(1, &bound, nullptr);
    const UINT before = GetRegionHash(device.wrapper.get(), output.Get(), 0, sizeof(initial_values));
    require_batch(before != 0, "round4 GPU: warm real buffer snapshot and hash");
    device.wrapper->Draw(3, 0);
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> readback;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &readback)), "round4 GPU: native staging allocation");
    device.context->CopyResource(readback.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "round4 GPU: read actual GPU raster output");
    const UINT expected = crc32c_hw(0, mapped.pData, sizeof(initial_values));
    device.context->Unmap(readback.Get(), 0);
    const UINT actual = GetRegionHash(device.wrapper.get(), output.Get(), 0, sizeof(initial_values));
    std::fprintf(stderr, "round4 RTV buffer write: before=%08x actual=%08x GPU=%08x\n", before, actual, expected);
    require_batch(expected != before, "round4 GPU: actual native rasterization changed backing bytes");
    require_batch(actual == expected, "round4 GPU: raster write through buffer RTV invalidates old CPU hash");
    device.context->ClearState();
}
