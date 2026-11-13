#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "ResourceHash.h"
#include <d3dcompiler.h>
#include <atomic>

namespace {
using namespace dx11_slot_test;

class StreamStateScope {
    UINT hunting = G->hunting;
    bool analysis = G->analyse_frame;
public:
    StreamStateScope() { G->hunting = HUNTING_MODE_DISABLED; G->analyse_frame = false; }
    ~StreamStateScope() { G->hunting = hunting; G->analyse_frame = analysis; }
};

class StreamLifetime final : public IUnknown {
    std::atomic<ULONG> refs{1};
    std::shared_ptr<std::atomic<unsigned>> destroyed;
public:
    explicit StreamLifetime(std::shared_ptr<std::atomic<unsigned>> destroyed) : destroyed(std::move(destroyed)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = --refs;
        if (!count) { ++*destroyed; delete this; }
        return count;
    }
};

const GUID stream_lifetime_guid = {0xa0154c3e,0x9e70,0x4662,{0xbc,0x30,0x8e,0x91,0x99,0x84,0x10,0x73}};

ComPtr<ID3D11Buffer> stream_buffer(BatchDevice& device)
{
    unsigned char initial[128];
    std::memset(initial, 0xcd, sizeof(initial));
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(initial); desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = {}; data.pSysMem = initial;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &result)), "SO: create actual tracked native buffer");
    return result;
}

std::vector<unsigned char> stream_bytes(BatchDevice& device, ID3D11Buffer* source)
{
    D3D11_BUFFER_DESC desc = {}; source->GetDesc(&desc);
    std::vector<unsigned char> bytes(desc.ByteWidth);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "SO: staging allocation");
    device.context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "SO: actual GPU readback");
    std::memcpy(bytes.data(), mapped.pData, bytes.size());
    device.context->Unmap(staging.Get(), 0);
    return bytes;
}

void stream_shaders(BatchDevice& device)
{
    const char vs_source[] = "struct V{float4 p:SV_POSITION;float4 a:VALUE;float4 b:OTHER;};V main(uint id:SV_VertexID){V v;v.p=float4(0,0,0,1);v.a=float4(11,22,33,44);v.b=float4(55,66,77,88);return v;}";
    const char gs_source[] = "struct V{float4 p:SV_POSITION;float4 a:VALUE;float4 b:OTHER;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> output){output.Append(input[0]);}";
    ComPtr<ID3DBlob> code;
    require_batch(SUCCEEDED(D3DCompile(vs_source, sizeof(vs_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr)), "SO: actual VS compile");
    ComPtr<ID3D11VertexShader> vs;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs)), "SO: actual VS creation");
    code.Reset();
    require_batch(SUCCEEDED(D3DCompile(gs_source, sizeof(gs_source), nullptr, nullptr, nullptr, "main", "gs_5_0", 0, 0, &code, nullptr)), "SO: actual GS compile");
    const D3D11_SO_DECLARATION_ENTRY declaration[] = {{0,"VALUE",0,0,4,0},{0,"OTHER",0,0,4,1}};
    const UINT strides[] = {16,16};
    ComPtr<ID3D11GeometryShader> gs;
    require_batch(SUCCEEDED(device.native->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(), code->GetBufferSize(), declaration, 2,
        strides, 2, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs)), "SO: actual GS/SO declaration");
    device.context->VSSetShader(vs.Get(), nullptr, 0);
    device.context->GSSetShader(gs.Get(), nullptr, 0);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
}

void require_stream_targets(BatchDevice& device, ID3D11Buffer* a, ID3D11Buffer* b)
{
    ID3D11Buffer* actual[2] = {};
    device.context->SOGetTargets(2, actual);
    const bool equal = actual[0] == a && actual[1] == b;
    for (auto* target : actual) if (target) target->Release();
    require_batch(equal, "SO: selected and untouched target identities retain actual native ownership");
}
} // namespace

void test_dx11_stream_output_regression()
{
    StreamStateScope state_scope;
    DataCacheTestScope cache_scope;
    BatchDevice device;
    Registry registry;
    auto a = stream_buffer(device);
    auto b = stream_buffer(device);
    auto replacement = stream_buffer(device);
    registry.resource(L"resourcestream_a", device, a.Get(), DXGI_FORMAT_UNKNOWN, 16, 128);
    auto& replacement_resource = registry.resource(L"resourcestream_replacement", device, replacement.Get(), DXGI_FORMAT_UNKNOWN, 16, 128);
    ID3D11Buffer* initial[] = {a.Get(), b.Get()};
    const UINT initial_offsets[] = {32,48};
    device.context->SOSetTargets(2, initial, initial_offsets);
    stream_shaders(device);

    scalar(device, L"so0", L"ref resourcestream_a->region(16,4)");
    require_stream_targets(device, a.Get(), b.Get());
    // Force a current tracked CPU snapshot before a real SO shader write.
    require_batch(GetRegionHash(device.wrapper.get(), a.Get(), 0, 16) != 0, "SO: warm actual tracked region snapshot");
    unsigned char cached[4] = {};
    require_batch(TryReadResourceDataCache(a.Get(), 0, cached, sizeof(cached)), "SO: actual selected cache is present before Draw");
    device.wrapper->Draw(1,0);
    require_batch(!TryReadResourceDataCache(a.Get(), 0, cached, sizeof(cached)), "SO: real Draw invalidates selected tracked SO CPU snapshot");
    device.context->SOSetTargets(0, nullptr, nullptr);
    auto first = stream_bytes(device, a.Get());
    auto second = stream_bytes(device, b.Get());
    const float expected_a[] = {11,22,33,44};
    const float expected_b[] = {55,66,77,88};
    require_batch(std::memcmp(first.data()+16, expected_a, sizeof(expected_a)) == 0,
        "SO: actual GS writes at requested selected-slot offset; Region size does not limit SO writes");
    require_batch(std::memcmp(second.data()+48, expected_b, sizeof(expected_b)) == 0,
        "SO: unmentioned slot appends at its prior write position");
    for (UINT i = 0; i < 16; ++i) require_batch(first[i] == 0xcd, "SO: selected-slot prefix stays unchanged");
    for (UINT i = 32; i < first.size(); ++i) require_batch(first[i] == 0xcd, "SO: selected-slot suffix stays unchanged");
    for (UINT i = 0; i < second.size(); ++i)
        if (i < 48 || i >= 64) require_batch(second[i] == 0xcd, "SO: untouched slot preserves bytes outside appended output");

    device.context->SOSetTargets(2, initial, initial_offsets);
    scalar(device, L"so0", L"ref resourcestream_a->region(16,4)");
    ResourceCopyTarget source;
    CommandListScope scope; scope.emplace_front(); const std::wstring ns;
    require_batch(source.ParseTarget(L"so0", true, &ns, &scope), "SO: parse actual source target");
    UINT source_offset = 99;
    ComPtr<ID3D11Resource> source_resource;
    source_resource.Attach(source.GetResource(&device.state, nullptr, nullptr, &source_offset, nullptr, nullptr));
    require_batch(source_resource.Get() == a.Get() && source_offset == 0,
        "SO: native source reports buffer beginning because D3D11 cannot query SO write offset");
    source_resource.Reset();

    auto destroyed = std::make_shared<std::atomic<unsigned>>(0);
    auto* observer = new StreamLifetime(destroyed);
    require_batch(SUCCEEDED(replacement->SetPrivateDataInterface(stream_lifetime_guid, observer)), "SO: attach actual COM lifetime observer");
    observer->Release();
    CommandList repeat;
    append(repeat, L"so0", L"ref resourcestream_replacement");
    execute(repeat, device);
    const ULONG before = replacement->AddRef(); replacement->Release();
    for (unsigned i = 0; i < 8; ++i) { execute(repeat, device); require_stream_targets(device, replacement.Get(), b.Get()); }
    const ULONG after = replacement->AddRef(); replacement->Release();
    require_batch(before == after && destroyed->load() == 0, "SO: repeated replacement neither leaks native references nor consumes caller ownership");
    scalar(device, L"so0", L"null");
    require_stream_targets(device, nullptr, b.Get());
    require_batch(destroyed->load() == 0, "SO: null binding retains independent external owner");
    repeat.clear();
    replacement_resource.ResetRuntimeState();
    replacement.Reset();
    // Native D3D11 defers destruction of formerly bound device children.
    // ClearState/Flush submits that deferred destruction; no timing assumption.
    device.context->ClearState();
    device.context->Flush();
    require_batch(destroyed->load() == 1, "SO: final independent release destroys replacement exactly once after unbinding");
    std::fprintf(stderr, "[PASS] Stream output immediate native GS offsets/readback/COM lifecycle\n");
}
