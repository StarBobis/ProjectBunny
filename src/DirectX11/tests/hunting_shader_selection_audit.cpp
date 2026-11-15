#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include <array>
#include <tuple>
#include <d3dcompiler.h>

void with_dx11_audit_hunting_configuration(void (*scenario)());

namespace {
using namespace dx11_slot_test;

template<class... Values>
class HuntingSelectionSnapshot {
    std::tuple<Values&...> globals;
    std::tuple<Values...> saved;
public:
    explicit HuntingSelectionSnapshot(Values&... values) : globals(values...), saved(values...) {}
    ~HuntingSelectionSnapshot() { globals = std::move(saved); }
};

ComPtr<ID3DBlob> compile_selection_shader(const char* source, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
        "main", profile, 0, 0, &code, &errors)), "F293: compile native hunting fixture shader");
    return code;
}

ComPtr<ID3D11Buffer> selection_buffer(BatchDevice& device, const void* contents, UINT bytes, UINT flags)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = flags;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = contents;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &result)),
        "F293: production buffer creation populates actual hash metadata");
    return result;
}

class SelectionShaders {
    BatchDevice& device;
public:
    std::array<ComPtr<ID3D11VertexShader>, 2> vertex;
    std::array<ComPtr<ID3D11PixelShader>, 2> pixel;
    ComPtr<ID3DBlob> input_code;
    explicit SelectionShaders(BatchDevice& fixture) : device(fixture)
    {
        const char* vertex_sources[] = {
            "float4 main(float4 p:POSITION):SV_Position{return p;}",
            "float4 main(float4 p:POSITION):SV_Position{return p*float4(0.5,0.5,1,1);}",
        };
        const char* pixel_sources[] = {
            "float4 main():SV_Target{return float4(1,0,0,1);}",
            "float4 main():SV_Target{return float4(0,1,0,1);}",
        };
        for (size_t i = 0; i < 2; ++i) {
            auto vs = compile_selection_shader(vertex_sources[i], "vs_5_0");
            auto ps = compile_selection_shader(pixel_sources[i], "ps_5_0");
            require_batch(SUCCEEDED(device.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex[i])) &&
                SUCCEEDED(device.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel[i])),
                "F293: production shader creation registers real WARP shaders");
            if (!i) input_code = vs;
        }
    }
    ~SelectionShaders()
    {
        device.wrapper->ClearState();
        for (const auto& shader : vertex) CleanupShaderMaps(shader.Get());
        for (const auto& shader : pixel) CleanupShaderMaps(shader.Get());
    }
};

void clear_selected_shader_buffers()
{
    G->mSelectedVertexShader_VertexBuffer.clear(); G->mSelectedPixelShader_VertexBuffer.clear();
    G->mSelectedVertexShader_IndexBuffer.clear(); G->mSelectedPixelShader_IndexBuffer.clear();
}

void check_selected_shader_buffers(const std::set<uint32_t>& vs, const std::set<uint32_t>& ps, uint32_t index)
{
    if (G->mSelectedVertexShader_VertexBuffer != vs || G->mSelectedPixelShader_VertexBuffer != ps) {
        std::fprintf(stderr, "F293 VB selection: actual VS/PS=%zu/%zu expected=%zu/%zu\n",
            G->mSelectedVertexShader_VertexBuffer.size(), G->mSelectedPixelShader_VertexBuffer.size(), vs.size(), ps.size());
    }
    require_batch(G->mSelectedVertexShader_VertexBuffer == vs && G->mSelectedPixelShader_VertexBuffer == ps,
        "F293: selected PS and VS keep independent actual vertex-buffer sets");
    const std::set<uint32_t> ib = index ? std::set<uint32_t>{index} : std::set<uint32_t>{};
    require_batch(G->mSelectedVertexShader_IndexBuffer == (vs.empty() ? std::set<uint32_t>{} : ib) &&
        G->mSelectedPixelShader_IndexBuffer == (ps.empty() ? std::set<uint32_t>{} : ib),
        "F293: index-buffer attribution and existing region-hash policy remain unchanged");
}

void exercise_shader_selection()
{
    const auto old_vs = G->mSelectedVertexShader, old_ps = G->mSelectedPixelShader;
    const auto old_vs_vb = G->mSelectedVertexShader_VertexBuffer, old_ps_vb = G->mSelectedPixelShader_VertexBuffer;
    const auto old_vs_ib = G->mSelectedVertexShader_IndexBuffer, old_ps_ib = G->mSelectedPixelShader_IndexBuffer;
    {
        HuntingSelectionSnapshot saved(G->hunting, G->marking_mode, G->DumpUsage, G->track_region_hashes,
            G->track_implicit_index_buffers, G->mSelectedVertexShader, G->mSelectedPixelShader,
            G->mSelectedGeometryShader, G->mSelectedDomainShader, G->mSelectedHullShader,
            G->mSelectedIndexBuffer, G->mSelectedVertexBuffer, G->mSelectedIndexBufferPos,
            G->mSelectedVertexBufferPos, G->gSelectedVertexBufferSlotId,
            G->mSelectedVertexShader_VertexBuffer, G->mSelectedPixelShader_VertexBuffer,
            G->mSelectedVertexShader_IndexBuffer, G->mSelectedPixelShader_IndexBuffer,
            G->mVisitedVertexShaders, G->mVisitedPixelShaders, G->mVisitedVertexBuffers, G->mVisitedIndexBuffers,
            G->mVisitedVertexBuffersLastSeenFrame, G->mVisitedIndexBuffersLastSeenFrame,
            G->mSelectedRenderTarget, G->mSelectedRenderTargetSnapshot, G->mSelectedRenderTargetSnapshotList);
        DataCacheTestScope cache;
        G->marking_mode = MarkingMode::ORIGINAL; G->DumpUsage = false; G->track_implicit_index_buffers = false;
        G->mSelectedGeometryShader = G->mSelectedDomainShader = G->mSelectedHullShader = UINT64_MAX;
        G->mSelectedIndexBuffer = G->mSelectedVertexBuffer = UINT32_MAX;
        G->gSelectedVertexBufferSlotId = -1;
        G->mSelectedRenderTarget = reinterpret_cast<ID3D11Resource*>(~uintptr_t(0));
        BatchDevice device; SelectionShaders shaders(device);
        const UINT64 vs0 = G->mShaders.at(shaders.vertex[0].Get()), vs1 = G->mShaders.at(shaders.vertex[1].Get());
        const UINT64 ps0 = G->mShaders.at(shaders.pixel[0].Get()), ps1 = G->mShaders.at(shaders.pixel[1].Get());
        require_batch(vs0 && ps0 && vs0 != vs1 && ps0 != ps1, "F293: fixture shaders have distinct real production hashes");
        ComPtr<ID3D11InputLayout> layout;
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        require_batch(SUCCEEDED(device.device->CreateInputLayout(&element, 1, shaders.input_code->GetBufferPointer(),
            shaders.input_code->GetBufferSize(), &layout)), "F293: actual input layout for bound vertex data");
        device.wrapper->IASetInputLayout(layout.Get());
        device.wrapper->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        std::array<float, 32> vertices{};
        for (size_t i = 0; i < vertices.size(); ++i) vertices[i] = static_cast<float>(i % 5) / 4;
        auto vb0 = selection_buffer(device, vertices.data(), sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
        const uint32_t region0 = crc32c_hw(0, vertices.data(), 3 * 16);
        vertices[0] = -0.75f;
        auto vb1 = selection_buffer(device, vertices.data(), sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
        const uint32_t region1 = crc32c_hw(0, vertices.data(), 3 * 16);
        const uint16_t indices[] = {0, 0, 1, 2, 0, 1, 2, 1};
        auto ib = selection_buffer(device, indices, sizeof(indices), D3D11_BIND_INDEX_BUFFER);
        const uint32_t whole0 = GetResourceHash(vb0.Get()), whole1 = GetResourceHash(vb1.Get());
        const uint32_t whole_ib = GetResourceHash(ib.Get());
        const uint32_t offset_ib = crc32c_hw(0, indices + 2, 3 * sizeof(uint16_t));
        const uint32_t zero_offset_region = crc32c_hw(0, indices + 1, 3 * sizeof(uint16_t));
        require_batch(whole0 && whole1 && whole0 != whole1 && region0 && region1 && region0 != region1 &&
            whole_ib && offset_ib && whole_ib != offset_ib && whole_ib != zero_offset_region,
            "F293: real whole/region hashes distinguish both VBs and zero/nonzero IB policy");
        for (const bool region : {false, true}) {
            for (const int index_mode : {-1, 0, 2}) {
                G->track_region_hashes = region;
                // Pending buffer browsing enables the existing region-hash path
                // without selecting a buffer that would also trigger snapshots.
                G->mSelectedIndexBufferPos = G->mSelectedVertexBufferPos = region ? INT_MAX : -1;
                const uint32_t a = region ? region0 : whole0, b = region ? region1 : whole1;
                const uint32_t expected_ib = index_mode < 0 ? 0 : region && index_mode ? offset_ib : whole_ib;
                auto draw = [&](size_t vs, size_t ps, size_t vb) {
                    device.wrapper->VSSetShader(shaders.vertex[vs].Get(), nullptr, 0);
                    device.wrapper->PSSetShader(shaders.pixel[ps].Get(), nullptr, 0);
                    ID3D11Buffer* bound = vb ? vb1.Get() : vb0.Get(); UINT stride = 16, offset = 0;
                    device.wrapper->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
                    device.wrapper->IASetIndexBuffer(index_mode < 0 ? nullptr : ib.Get(), DXGI_FORMAT_R16_UINT,
                        index_mode < 0 ? 0 : index_mode);
                    ComPtr<ID3D11VertexShader> actual_vs; ComPtr<ID3D11PixelShader> actual_ps;
                    device.context->VSGetShader(&actual_vs, nullptr, nullptr); device.context->PSGetShader(&actual_ps, nullptr, nullptr);
                    ComPtr<ID3D11Buffer> actual_vb, actual_ib; UINT actual_stride = 0, actual_offset = 0, ib_offset = 0;
                    DXGI_FORMAT format{};
                    device.context->IAGetVertexBuffers(0, 1, &actual_vb, &actual_stride, &actual_offset);
                    device.context->IAGetIndexBuffer(&actual_ib, &format, &ib_offset);
                    require_batch(actual_vs.Get() == shaders.vertex[vs].Get() && actual_ps.Get() == shaders.pixel[ps].Get() &&
                        actual_vb.Get() == bound && actual_stride == stride && !actual_offset &&
                        actual_ib.Get() == (index_mode < 0 ? nullptr : ib.Get()) && (!actual_ib || ib_offset == index_mode),
                        "F293: wrappers bind the actual native shader and buffer pipeline");
                    // No render target is needed: this fixture observes hunting
                    // metadata from real draws, rather than rendered pixels.
                    if (index_mode < 0) device.wrapper->Draw(3, 0);
                    else device.wrapper->DrawIndexed(3, 1, 0);
                };
                std::fprintf(stderr, "F293: real hunting region=%d IB-offset=%d\n", region, index_mode);
                clear_selected_shader_buffers(); G->mSelectedVertexShader = UINT64_MAX; G->mSelectedPixelShader = ps0;
                draw(0, 0, 0); check_selected_shader_buffers({}, {a}, expected_ib);
                draw(0, 1, 1); check_selected_shader_buffers({}, {a}, expected_ib);
                draw(1, 0, 1); check_selected_shader_buffers({}, {a, b}, expected_ib);
                draw(1, 0, 1); check_selected_shader_buffers({}, {a, b}, expected_ib);
                clear_selected_shader_buffers(); G->mSelectedVertexShader = vs0; G->mSelectedPixelShader = UINT64_MAX;
                draw(0, 0, 0); check_selected_shader_buffers({a}, {}, expected_ib);
                draw(1, 0, 1); check_selected_shader_buffers({a}, {}, expected_ib);
                draw(0, 1, 1); check_selected_shader_buffers({a, b}, {}, expected_ib);
                clear_selected_shader_buffers(); G->mSelectedVertexShader = vs0; G->mSelectedPixelShader = ps0;
                draw(0, 0, 0); check_selected_shader_buffers({a}, {a}, expected_ib);
                draw(1, 0, 1); check_selected_shader_buffers({a}, {a, b}, expected_ib);
                draw(0, 1, 1); check_selected_shader_buffers({a, b}, {a, b}, expected_ib);
                clear_selected_shader_buffers(); draw(1, 1, 0); check_selected_shader_buffers({}, {}, expected_ib);
                G->hunting = HUNTING_MODE_DISABLED;
                draw(0, 0, 0); check_selected_shader_buffers({}, {}, expected_ib);
                G->hunting = HUNTING_MODE_ENABLED;
            }
        }
    }
    require_batch(G->mSelectedVertexShader == old_vs && G->mSelectedPixelShader == old_ps &&
        G->mSelectedVertexShader_VertexBuffer == old_vs_vb && G->mSelectedPixelShader_VertexBuffer == old_ps_vb &&
        G->mSelectedVertexShader_IndexBuffer == old_vs_ib && G->mSelectedPixelShader_IndexBuffer == old_ps_ib,
        "F293: actual global shader selections and their four buffer sets are restored");
}
}

void test_dx11_hunting_shader_selection_regression()
{
    with_dx11_audit_hunting_configuration(exercise_shader_selection);
}
