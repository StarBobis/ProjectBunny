#include "slot_range_test_support.h"
#include "ShaderBindings.h"
#include <d3dcompiler.h>

namespace {
using namespace dx11_slot_test;

class AppendStateScope {
    UINT hunting = G->hunting;
    bool analysis = G->analyse_frame;
public:
    AppendStateScope() { G->hunting = HUNTING_MODE_DISABLED; G->analyse_frame = false; }
    ~AppendStateScope() { G->hunting = hunting; G->analyse_frame = analysis; }
};

ULONG observed_refs(IUnknown* value)
{
    const ULONG count = value->AddRef(); value->Release(); return count;
}

ResourceCopyTarget append_target(const wchar_t* text)
{
    ResourceCopyTarget result;
    CommandListScope scope; scope.emplace_front(); const std::wstring ns;
    require_batch(result.ParseTarget(text, true, &ns, &scope), "SO append: actual dynamic target parse");
    return result;
}

ComPtr<ID3D11Buffer> append_buffer(BatchDevice& device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 160; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
    unsigned char initial[160]; std::memset(initial, 0xcd, sizeof(initial));
    D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = initial;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, &data, &result)), "SO append: actual buffer creation");
    return result;
}

std::vector<unsigned char> append_readback(BatchDevice& device, ID3D11Buffer* source)
{
    D3D11_BUFFER_DESC desc = {}; source->GetDesc(&desc);
    std::vector<unsigned char> bytes(desc.ByteWidth);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "SO append: actual staging allocation");
    device.context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "SO append: actual GPU output Map");
    std::memcpy(bytes.data(), mapped.pData, bytes.size());
    device.context->Unmap(staging.Get(), 0);
    return bytes;
}

void install_append_shader(BatchDevice& device)
{
    const char vs_source[] = "struct V{float4 p:SV_POSITION;float4 a:VALUE;float4 b:OTHER;};V main(uint id:SV_VertexID){V v;v.p=float4(0,0,0,1);v.a=float4(91.125,22,33,44);v.b=float4(55,66,77,88);return v;}";
    const char gs_source[] = "struct V{float4 p:SV_POSITION;float4 a:VALUE;float4 b:OTHER;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> output){output.Append(input[0]);}";
    ComPtr<ID3DBlob> code;
    require_batch(SUCCEEDED(D3DCompile(vs_source, sizeof(vs_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr)), "SO append: actual VS compile");
    ComPtr<ID3D11VertexShader> vs;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs)), "SO append: actual VS create");
    code.Reset();
    require_batch(SUCCEEDED(D3DCompile(gs_source, sizeof(gs_source), nullptr, nullptr, nullptr, "main", "gs_5_0", 0, 0, &code, nullptr)), "SO append: actual GS compile");
    const UINT64 hash = fnv_64_buf(code->GetBufferPointer(), code->GetBufferSize());
    const D3D11_SO_DECLARATION_ENTRY declaration[] = {{0,"VALUE",0,0,4,0},{0,"OTHER",0,0,4,1}};
    const UINT strides[] = {16,16};
    ComPtr<ID3D11GeometryShader> gs;
    require_batch(SUCCEEDED(device.device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(), code->GetBufferSize(),
        declaration, 2, strides, 2, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs)), "SO append: real wrapper GS/SO creation");
    EnterCriticalSection(&G->mCriticalSection);
    const bool registered = G->mShaders.count(gs.Get()) != 0;
    const auto visited_before = G->mVisitedGeometryShaders;
    LeaveCriticalSection(&G->mCriticalSection);
    require_batch(!registered, "SO append: SO shader bypass has no original hash registration");
    // Exercise the real hunting registration path, which cannot discover a hash
    // for a shader created by CreateGeometryShaderWithStreamOutput.
    G->hunting = HUNTING_MODE_ENABLED;
    device.wrapper->GSSetShader(gs.Get(), nullptr, 0);
    G->hunting = HUNTING_MODE_DISABLED;
    EnterCriticalSection(&G->mCriticalSection);
    const bool visited_unchanged = G->mVisitedGeometryShaders == visited_before;
    LeaveCriticalSection(&G->mCriticalSection);
    require_batch(visited_unchanged, "SO append: actual GSSetShader does not add unregistered SO shader to hunting");
    ShaderBindings bindings;
    require_batch(!device.device->GetShaderBindings(hash, &bindings), "SO append: SO shader bypass does not cache declarations");
    device.context->VSSetShader(vs.Get(), nullptr, 0);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
}

void test_dynamic_so_ownership(BatchDevice& device, Registry& registry)
{
    device.context->ClearState();
    auto& index = registry.variable(L"$stream_append_index", 0);
    ComPtr<ID3D11Buffer> buffers[4];
    for (unsigned i = 0; i < 4; ++i) {
        buffers[i] = append_buffer(device);
        const auto name = L"resourcestream_append_slot" + std::to_wstring(i);
        registry.resource(name, device, buffers[i].Get(), DXGI_FORMAT_UNKNOWN, 16, 160);
        index.fval = static_cast<float>(i);
        scalar(device, L"so[$stream_append_index]", L"ref " + name);
    }
    ULONG before[4] = {};
    for (unsigned i = 0; i < 4; ++i) before[i] = observed_refs(buffers[i].Get());
    auto source = append_target(L"so[$stream_append_index]");
    for (unsigned round = 0; round < 8; ++round) {
        for (unsigned i = 0; i < 4; ++i) {
            index.fval = static_cast<float>(i);
            ComPtr<ID3D11Resource> actual;
            UINT offset = 99;
            actual.Attach(source.GetResource(&device.state, nullptr, nullptr, &offset, nullptr, nullptr));
            require_batch(actual.Get() == buffers[i].Get() && offset == 0, "SO append: all four dynamic source slots preserve identity and report offset zero");
            actual.Reset();
            scalar(device, L"so[$stream_append_index]", L"ref resourcestream_append_slot" + std::to_wstring(i));
        }
    }
    ID3D11Buffer* actual[4] = {};
    device.context->SOGetTargets(4, actual);
    bool identity = true;
    for (unsigned i = 0; i < 4; ++i) { identity &= actual[i] == buffers[i].Get(); if (actual[i]) actual[i]->Release(); }
    require_batch(identity, "SO append: dynamic writes preserve all four native target identities");
    for (unsigned i = 0; i < 4; ++i)
        require_batch(observed_refs(buffers[i].Get()) == before[i], "SO append: take/release helpers balance lower slots and caller ownership");
    index.fval = -1;
    scalar(device, L"so[$stream_append_index]", L"null");
    index.fval = 4;
    scalar(device, L"so[$stream_append_index]", L"null");
    device.context->SOGetTargets(4, actual);
    identity = true;
    for (unsigned i = 0; i < 4; ++i) { identity &= actual[i] == buffers[i].Get(); if (actual[i]) actual[i]->Release(); }
    require_batch(identity, "SO append: invalid dynamic slots leave native targets unchanged");
    for (unsigned i = 0; i < 4; ++i) { index.fval = static_cast<float>(i); scalar(device, L"so[$stream_append_index]", L"null"); }
    device.context->ClearState(); device.context->Flush();
}

ComPtr<ID3D11Texture2D> output_texture(BatchDevice& device, bool depth)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = 4; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &result)), "SO helpers: actual output texture");
    return result;
}

void test_output_merger_ownership(BatchDevice& device, Registry& registry)
{
    auto rt0 = output_texture(device, false), rt3 = output_texture(device, false), replacement = output_texture(device, false);
    auto depth = output_texture(device, true), replacement_depth = output_texture(device, true);
    ComPtr<ID3D11RenderTargetView> v0, v3, vr;
    ComPtr<ID3D11DepthStencilView> vd, vdr;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(rt0.Get(), nullptr, &v0)) &&
        SUCCEEDED(device.native->CreateRenderTargetView(rt3.Get(), nullptr, &v3)) &&
        SUCCEEDED(device.native->CreateRenderTargetView(replacement.Get(), nullptr, &vr)) &&
        SUCCEEDED(device.native->CreateDepthStencilView(depth.Get(), nullptr, &vd)) &&
        SUCCEEDED(device.native->CreateDepthStencilView(replacement_depth.Get(), nullptr, &vdr)), "SO helpers: actual RTV/DSV creation");
    auto& resource_rt = registry.resource(L"resourcestream_append_rt", device, replacement.Get());
    resource_rt.view = vr.Get(); vr->AddRef();
    auto& resource_depth = registry.resource(L"resourcestream_append_depth", device, replacement_depth.Get());
    resource_depth.view = vdr.Get(); vdr->AddRef();
    ID3D11RenderTargetView* initial[4] = {v0.Get(), nullptr, nullptr, v3.Get()};
    device.context->OMSetRenderTargets(4, initial, vd.Get());
    auto& index = command_list_globals.at(L"$stream_append_index"); index.fval = 3;
    auto rt_source = append_target(L"o[$stream_append_index]");
    for (unsigned i = 0; i < 8; ++i) {
        ComPtr<ID3D11View> view; ComPtr<ID3D11Resource> resource;
        resource.Attach(rt_source.GetResource(&device.state, view.GetAddressOf(), nullptr, nullptr, nullptr, nullptr));
        require_batch(resource.Get() == rt3.Get() && view.Get() == v3.Get(), "SO helpers: take_slot returns selected RTV resource/view");
    }
    scalar(device, L"o[$stream_append_index]", L"ref resourcestream_append_rt");
    scalar(device, L"od", L"ref resourcestream_append_depth");
    const ULONG before_v0 = observed_refs(v0.Get()), before_vr = observed_refs(vr.Get()), before_vdr = observed_refs(vdr.Get());
    for (unsigned i = 0; i < 12; ++i) {
        scalar(device, L"o[$stream_append_index]", L"ref resourcestream_append_rt");
        scalar(device, L"od", L"ref resourcestream_append_depth");
    }
    require_batch(observed_refs(v0.Get()) == before_v0 && observed_refs(vr.Get()) == before_vr && observed_refs(vdr.Get()) == before_vdr,
        "SO helpers: RTV replacement retains caller and DSV replacement releases every fetched RTV reference");
    ID3D11RenderTargetView* actual[4] = {}; ComPtr<ID3D11DepthStencilView> actual_depth;
    device.context->OMGetRenderTargets(4, actual, &actual_depth);
    const bool identity = actual[0] == v0.Get() && !actual[1] && !actual[2] && actual[3] == vr.Get() && actual_depth.Get() == vdr.Get();
    for (auto* view : actual) if (view) view->Release();
    require_batch(identity, "SO helpers: actual RTV/DSV identities and holes survive helper-driven replacements");
    scalar(device, L"od", L"null"); scalar(device, L"o[$stream_append_index]", L"null");
    device.context->ClearState(); device.context->Flush();
}
} // namespace

void test_dx11_stream_output_append_regression()
{
    AppendStateScope state_scope;
    BatchDevice device;
    Registry registry;
    auto a = append_buffer(device), b = append_buffer(device);
    registry.resource(L"resourcestream_append_a", device, a.Get(), DXGI_FORMAT_UNKNOWN, 16, 160);
    install_append_shader(device);
    ID3D11Buffer* targets[] = {a.Get(), b.Get()};
    const UINT offsets[] = {0,32};
    device.context->SOSetTargets(2, targets, offsets);
    device.wrapper->Draw(1,0);
    scalar(device, L"so0", L"ref resourcestream_append_a->region(64,4)");
    device.wrapper->Draw(1,0);
    device.context->SOSetTargets(0, nullptr, nullptr);
    auto first = append_readback(device, a.Get()), second = append_readback(device, b.Get());
    const float expected_a[] = {91.125f,22,33,44}, expected_b[] = {55,66,77,88};
    require_batch(std::memcmp(first.data(), expected_a, 16) == 0 && std::memcmp(first.data()+64, expected_a, 16) == 0,
        "SO append: successive real GS draws retain first record and obey selected nonzero Region offset");
    require_batch(std::memcmp(second.data()+32, expected_b, 16) == 0 && std::memcmp(second.data()+48, expected_b, 16) == 0,
        "SO append: untouched target appends second record immediately after its first record");
    for (UINT i = 0; i < 160; ++i) {
        if (!(i < 16 || (i >= 64 && i < 80))) require_batch(first[i] == 0xcd, "SO append: selected stream preserves every byte outside emitted records");
        if (!(i >= 32 && i < 64)) require_batch(second[i] == 0xcd, "SO append: untouched stream preserves prefix and suffix while appending");
    }
    test_dynamic_so_ownership(device, registry);
    test_output_merger_ownership(device, registry);
    device.context->ClearState(); device.context->Flush();
    std::fprintf(stderr, "[PASS] Stream output append/array helper native Draw and ownership\n");
}
