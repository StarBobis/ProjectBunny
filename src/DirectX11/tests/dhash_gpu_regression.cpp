#include "slot_range_test_support.h"
#include "DHash64Gpu.h"
#include <d3dcompiler.h>
#include <cmath>
#include <array>

namespace {
using namespace dx11_slot_test;
using Pixel = std::array<unsigned char, 4>;

std::vector<Pixel> dhash_pixels(UINT width, UINT height)
{
    std::vector<Pixel> result(width * height);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
            result[y * width + x] = {static_cast<unsigned char>((x * 71 + y * 13 + 17) % 256),
                static_cast<unsigned char>((x * 19 + y * 83 + 41) % 256),
                static_cast<unsigned char>((x * 113 + y * 37 + 97) % 256),
                static_cast<unsigned char>((x * 47 + y * 109 + 23) % 256)};
    return result;
}

double decode_channel(unsigned char channel, bool srgb)
{
    const double value = channel / 255.0;
    return srgb ? (value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4)) : value;
}

std::array<double, 4> cpu_sample(const std::vector<Pixel>& pixels, UINT width, UINT height, double u, double v, bool srgb)
{
    const double px = u * width - 0.5, py = v * height - 0.5;
    const int left = static_cast<int>(std::floor(px)), top = static_cast<int>(std::floor(py));
    const double dx = px - left, dy = py - top;
    std::array<double, 4> result{};
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const int sx = (std::max)(0, (std::min)(static_cast<int>(width) - 1, left + x));
        const int sy = (std::max)(0, (std::min)(static_cast<int>(height) - 1, top + y));
        const double weight = (x ? dx : 1 - dx) * (y ? dy : 1 - dy);
        for (unsigned channel = 0; channel < 4; ++channel)
            result[channel] += decode_channel(pixels[sy * width + sx][channel], srgb && channel != 3) * weight;
    }
    return result;
}

uint64_t cpu_dhash(const std::vector<Pixel>& pixels, UINT width, UINT height, bool srgb = false)
{
    double samples[8][9]{};
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 9; ++x) {
        const auto rgba = cpu_sample(pixels, width, height, (x + 0.5) / 9, (y + 0.5) / 8, srgb);
        for (double channel : rgba) samples[y][x] += channel * 0.25;
    }
    uint64_t result = 0;
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x)
        if (samples[y][x] > samples[y][x + 1]) result |= uint64_t{1} << (y * 8 + x);
    if (result) return result;
    const auto center = cpu_sample(pixels, width, height, 0.5, 0.5, srgb);
    for (double value : center) {
        const unsigned level = static_cast<unsigned>(std::floor((std::max)(0.0, (std::min)(1.0, value)) * 16 + 0.5));
        result = (result << 16) | ((uint64_t{1} << level) - 1);
    }
    return result;
}

ComPtr<ID3D11Texture2D> dhash_texture(BatchDevice& device, UINT width, UINT height,
    const std::vector<Pixel>& pixels, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
    UINT flags = D3D11_BIND_SHADER_RESOURCE, UINT array = 1)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = array;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = flags;
    std::vector<D3D11_SUBRESOURCE_DATA> initial(array);
    for (auto& data : initial) { data.pSysMem = pixels.data(); data.SysMemPitch = width * sizeof(Pixel); }
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, initial.data(), &result)), "DHash: actual populated texture");
    return result;
}

void require_hash(BatchDevice& device, ID3D11Texture2D* texture, UINT mip, DXGI_FORMAT format, uint64_t expected)
{
    uint64_t hash = ~uint64_t{0};
    const HRESULT hr = device.wrapper->ComputeDHash(texture, mip, format, hash);
    if (FAILED(hr) || hash != expected)
        std::fprintf(stderr, "DHash actual hr=%08lx hash=%016llx expected=%016llx\n", static_cast<unsigned long>(hr),
            static_cast<unsigned long long>(hash), static_cast<unsigned long long>(expected));
    require_batch(SUCCEEDED(hr) && hash == expected, "DHash: actual GPU result equals independent CPU bilinear RGBA oracle");
}

class DHashPipelineSnapshot {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11ClassInstance> instances[D3D11_SHADER_MAX_INTERFACES];
    UINT instance_count = D3D11_SHADER_MAX_INTERFACES;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11UnorderedAccessView> cs_uav[2], om_uav[8];
    ComPtr<ID3D11RenderTargetView> rtvs[8];
    ComPtr<ID3D11DepthStencilView> dsv;
public:
    explicit DHashPipelineSnapshot(ID3D11DeviceContext* context) : context(context)
    {
        ID3D11ClassInstance* raw[D3D11_SHADER_MAX_INTERFACES]{};
        context->CSGetShader(&shader, raw, &instance_count);
        for (UINT i = 0; i < instance_count; ++i) instances[i].Attach(raw[i]);
        context->CSGetShaderResources(0, 1, &srv); context->CSGetSamplers(0, 1, &sampler);
        for (UINT i = 0; i < 2; ++i) context->CSGetUnorderedAccessViews(i, 1, &cs_uav[i]);
        for (UINT i = 0; i < 8; ++i) {
            context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, i, 1, &om_uav[i]);
        }
        ID3D11RenderTargetView* raw_rtvs[8]{};
        context->OMGetRenderTargets(8, raw_rtvs, &dsv);
        for (UINT i = 0; i < 8; ++i) rtvs[i].Attach(raw_rtvs[i]);
    }
    void verify()
    {
        DHashPipelineSnapshot after(context);
        bool same = shader.Get() == after.shader.Get() && instance_count == after.instance_count &&
            srv.Get() == after.srv.Get() && sampler.Get() == after.sampler.Get() && dsv.Get() == after.dsv.Get();
        for (UINT i = 0; i < instance_count; ++i) same &= instances[i].Get() == after.instances[i].Get();
        for (UINT i = 0; i < 2; ++i) same &= cs_uav[i].Get() == after.cs_uav[i].Get();
        for (UINT i = 0; i < 8; ++i) same &= rtvs[i].Get() == after.rtvs[i].Get() && om_uav[i].Get() == after.om_uav[i].Get();
        require_batch(same, "DHash: shader/class instances/SRV/sampler/CS UAV0+1/OM output bindings are exactly restored");
    }
};

void test_dhash_state(BatchDevice& device)
{
    const char source[] = "interface IValue{float Value();};class Impl:IValue{float value;float Value(){return value;}};Impl original_impl;IValue selected;RWStructuredBuffer<float> output:register(u0);[numthreads(1,1,1)]void main(){output[0]=selected.Value()+original_impl.Value();}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)),
        "DHash: compile actual dynamically linked CS");
    ComPtr<ID3D11ClassLinkage> linkage;
    ComPtr<ID3D11ClassInstance> instance;
    ComPtr<ID3D11ComputeShader> shader;
    require_batch(SUCCEEDED(device.native->CreateClassLinkage(&linkage)) &&
        SUCCEEDED(device.native->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), linkage.Get(), &shader)) &&
        SUCCEEDED(linkage->CreateClassInstance("Impl", 0, 0, 0, 0, &instance)), "DHash: actual CS class instance");
    ID3D11ClassInstance* raw_instance = instance.Get();
    device.context->CSSetShader(shader.Get(), &raw_instance, 1);
    const auto pattern = dhash_pixels(19, 13);
    auto input = dhash_texture(device, 19, 13, pattern, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET);
    auto unrelated = dhash_texture(device, 19, 13, pattern);
    ComPtr<ID3D11ShaderResourceView> old_srv;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(unrelated.Get(), nullptr, &old_srv)), "DHash: original unrelated CS SRV0");
    ID3D11ShaderResourceView* raw_srv = old_srv.Get(); device.context->CSSetShaderResources(0, 1, &raw_srv);
    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.MaxAnisotropy = 1; sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ComPtr<ID3D11SamplerState> sampler;
    require_batch(SUCCEEDED(device.native->CreateSamplerState(&sampler_desc, &sampler)), "DHash: original sampler");
    ID3D11SamplerState* raw_sampler = sampler.Get(); device.context->CSSetSamplers(0, 1, &raw_sampler);
    auto output = buffer(device, 64, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    D3D11_UNORDERED_ACCESS_VIEW_DESC output_desc{};
    output_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; output_desc.Buffer.NumElements = 16;
    output_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    ComPtr<ID3D11UnorderedAccessView> output_uav, alias_uav;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(output.Get(), &output_desc, &output_uav)) &&
        SUCCEEDED(device.native->CreateUnorderedAccessView(input.Get(), nullptr, &alias_uav)), "DHash: original output and source-alias UAVs");
    ID3D11UnorderedAccessView* raw_output = output_uav.Get(); const UINT count = 7;
    device.context->CSSetUnorderedAccessViews(0, 1, &raw_output, &count);
    ID3D11UnorderedAccessView* raw_alias = alias_uav.Get(); device.context->CSSetUnorderedAccessViews(1, 1, &raw_alias, nullptr);
    {
        DHashPipelineSnapshot saved(device.context.Get());
        require_hash(device, input.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(pattern, 19, 13)); saved.verify();
    }
    ID3D11UnorderedAccessView* no_uav = nullptr; device.context->CSSetUnorderedAccessViews(1, 1, &no_uav, nullptr);
    ComPtr<ID3D11RenderTargetView> alias_rtv;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(input.Get(), nullptr, &alias_rtv)), "DHash: original source-alias RTV");
    ID3D11RenderTargetView* raw_rtv = alias_rtv.Get(); device.context->OMSetRenderTargets(1, &raw_rtv, nullptr);
    {
        DHashPipelineSnapshot saved(device.context.Get());
        require_hash(device, input.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(pattern, 19, 13)); saved.verify();
    }
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    device.context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 2, 1, &raw_alias, nullptr);
    {
        DHashPipelineSnapshot saved(device.context.Get());
        require_hash(device, input.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(pattern, 19, 13)); saved.verify();
    }
    D3D11_BUFFER_DESC counter_desc{};
    counter_desc.ByteWidth = 4; counter_desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> counter;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&counter_desc, nullptr, &counter)), "DHash: actual append counter copy target");
    device.context->CopyStructureCount(counter.Get(), 0, output_uav.Get());
    counter_desc.Usage = D3D11_USAGE_STAGING; counter_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> counter_staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&counter_desc, nullptr, &counter_staging)), "DHash: append counter staging");
    device.context->CopyResource(counter_staging.Get(), counter.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(counter_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "DHash: append counter readback");
    UINT actual_count = 0; std::memcpy(&actual_count, mapped.pData, sizeof(actual_count));
    device.context->Unmap(counter_staging.Get(), 0);
    require_batch(actual_count == count, "DHash: restoring UAV0 preserves its hidden append counter");
    device.context->ClearState();
}

void test_dhash_failures(BatchDevice& device, ID3D11Texture2D* valid, uint64_t expected)
{
    DHash64Gpu engine;
    uint64_t hash = 123;
    require_batch(FAILED(engine.ComputeHash(device.context.Get(), valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: uninitialized failure clears output");
    require_batch(FAILED(engine.Initialize(nullptr)) && !engine.IsInitialized() && SUCCEEDED(engine.Initialize(device.native.Get())),
        "DHash: initialization retries after invalid initialization without partial service");
    require_batch(engine.Initialize(device.native.Get()) == S_FALSE, "DHash: repeated same-device initialization is stable");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(nullptr, valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: null context rejected without touching native state");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(device.context.Get(), nullptr, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: null texture rejected without stale output");
    ComPtr<ID3D11DeviceContext> deferred;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &deferred)), "DHash: actual deferred context");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(deferred.Get(), valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: deferred readback rejected without recorded work");
    ComPtr<ID3D11DeviceContext1> deferred1; require_batch(SUCCEEDED(deferred.As(&deferred1)), "DHash: deferred 11.1 interface");
    HackerContext deferred_wrapper(device.native.Get(), deferred1.Get());
    hash = 123;
    require_batch(FAILED(deferred_wrapper.ComputeDHash(valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: actual deferred wrapper returns HRESULT failure and clears output");
    const auto pixels = dhash_pixels(9, 8);
    auto array = dhash_texture(device, 9, 8, pixels, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 2);
    auto integer = dhash_texture(device, 9, 8, pixels, DXGI_FORMAT_R8G8B8A8_UINT);
    for (auto* bad : {array.Get(), integer.Get()}) {
        hash = 123;
        const DXGI_FORMAT format = bad == integer.Get() ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
        require_batch(FAILED(engine.ComputeHash(device.context.Get(), bad, 0, format, hash)) && hash == 0,
            "DHash: array and non-sampleable integer formats fail without stale hash");
    }
    for (const auto format : {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D24_UNORM_S8_UINT, static_cast<DXGI_FORMAT>(0x7ffffffe)}) {
        hash = 123;
        require_batch(FAILED(engine.ComputeHash(device.context.Get(), valid, 0, format, hash)) && hash == 0,
            "DHash: unsupported/incompatible view format clears hash");
    }
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(device.context.Get(), valid, 99, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: invalid mip rejected");
    BatchDevice other;
    auto other_texture = dhash_texture(other, 9, 8, pixels);
    require_batch(FAILED(engine.Initialize(other.native.Get())), "DHash: initialized engine cannot silently switch devices");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(other.context.Get(), valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: foreign context rejected");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(device.context.Get(), other_texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: foreign texture rejected");
    UINT quality = 0;
    require_batch(SUCCEEDED(device.native->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality)) && quality,
        "DHash: WARP supports actual MSAA boundary fixture");
    D3D11_TEXTURE2D_DESC desc{}; valid->GetDesc(&desc); desc.SampleDesc.Count = 4;
    ComPtr<ID3D11Texture2D> msaa;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &msaa)), "DHash: actual MSAA texture creation");
    hash = 123;
    require_batch(FAILED(engine.ComputeHash(device.context.Get(), msaa.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == 0,
        "DHash: MSAA explicitly unsupported");
    const ULONG before = valid->AddRef(); valid->Release();
    for (unsigned i = 0; i < 4; ++i) {
        require_batch(SUCCEEDED(engine.ComputeHash(device.context.Get(), valid, 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) && hash == expected,
            "DHash: all failures leave valid same-device retry usable");
    }
    const ULONG after = valid->AddRef(); valid->Release();
    require_batch(before == after, "DHash: repeated GPU snapshot calls do not retain source COM references");
}
} // namespace

void test_dx11_dhash_gpu_regression()
{
    BatchDevice device;
    const auto pixels = dhash_pixels(19, 13);
    auto texture = dhash_texture(device, 19, 13, pixels);
    require_hash(device, texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(pixels, 19, 13));
    std::vector<Pixel> alpha_gradient(72);
    for (UINT y = 0; y < 8; ++y) for (UINT x = 0; x < 9; ++x)
        alpha_gradient[y * 9 + x] = Pixel{static_cast<unsigned char>(x * 10), 0, 0, static_cast<unsigned char>(255 - x * 20)};
    auto alpha_texture = dhash_texture(device, 9, 8, alpha_gradient);
    require_batch(cpu_dhash(alpha_gradient, 9, 8) == ~uint64_t{0}, "DHash: alpha-inclusive average reverses RGB-only gradient");
    require_hash(device, alpha_texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, ~uint64_t{0});
    const auto exact = dhash_pixels(9, 8);
    auto exact_texture = dhash_texture(device, 9, 8, exact);
    require_hash(device, exact_texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(exact, 9, 8));
    std::vector<Pixel> black(72, Pixel{0,0,0,0});
    auto zero = dhash_texture(device, 9, 8, black);
    require_batch(cpu_dhash(black, 9, 8) == 0, "DHash: independent oracle has a valid zero case");
    require_hash(device, zero.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    std::vector<Pixel> constant(72, Pixel{64,128,192,255});
    auto flat = dhash_texture(device, 9, 8, constant);
    require_hash(device, flat.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(constant, 9, 8));
    auto typeless = dhash_texture(device, 19, 13, pixels, DXGI_FORMAT_R8G8B8A8_TYPELESS);
    require_hash(device, typeless.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(pixels, 19, 13));
    require_hash(device, typeless.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, cpu_dhash(pixels, 19, 13, true));
    // Distinct populated mips verify the engine samples selected mip as LOD zero.
    const auto large = dhash_pixels(18, 16);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 18; desc.Height = 16; desc.MipLevels = 2; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial[2]{};
    initial[0].pSysMem = large.data(); initial[0].SysMemPitch = 18 * sizeof(Pixel);
    initial[1].pSysMem = exact.data(); initial[1].SysMemPitch = 9 * sizeof(Pixel);
    ComPtr<ID3D11Texture2D> mips;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, initial, &mips)), "DHash: actual two populated mip texture");
    require_hash(device, mips.Get(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, cpu_dhash(exact, 9, 8));
    test_dhash_state(device);
    test_dhash_failures(device, texture.Get(), cpu_dhash(pixels, 19, 13));
    device.context->ClearState(); device.context->Flush();
    std::fprintf(stderr, "[PASS] DHash actual WARP/CPU oracle/state aliases/HRESULT failures\n");
}
