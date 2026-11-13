#include "DHash64Gpu.h"
#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
    constexpr char kDHashShader[] = R"(
        Texture2D<float4> Source : register(t0);
        SamplerState LinearClamp : register(s0);
        RWStructuredBuffer<uint> Bits : register(u0);

        // 9 samples per row, 8 rows.
        groupshared float SampleValue[72];

        // One bit per comparison.
        groupshared uint HashBits[64];

        // Equal contribution from all four channels.
        static const float4 ChannelWeights = float4(0.25f, 0.25f, 0.25f, 0.25f);

        // Encode a value from 0..16 into a 16-bit thermometer code.
        uint Thermometer16(uint value)
        {
            return (1u << value) - 1u;
        }

        [numthreads(64, 1, 1)]
        void main(uint3 GroupThreadID : SV_GroupThreadID)
        {
            uint i = GroupThreadID.x;

            uint x = i & 7u;
            uint y = i >> 3u;

            // Sample the left position for each comparison.
            float2 uv = float2(((float)x + 0.5f) / 9.0f, ((float)y + 0.5f) / 8.0f);

            float4 color = Source.SampleLevel(LinearClamp, uv, 0);
            SampleValue[i + y] = dot(color, ChannelWeights);

            // Sample the rightmost column. The other right-hand samples
            // are already covered by the next thread's left-hand sample.
            if (i < 8u)
            {
                float2 uv_last = float2(8.5f / 9.0f, ((float)i + 0.5f) / 8.0f);

                float4 last = Source.SampleLevel(LinearClamp, uv_last, 0);
                SampleValue[i * 9u + 8u] = dot(last, ChannelWeights);
            }

            // All 72 samples must be available before comparison.
            GroupMemoryBarrierWithGroupSync();

            // Calculate one dHash bit per thread.
            uint sample_index = i + y;

            HashBits[i] = SampleValue[sample_index] > SampleValue[sample_index + 1u] ? 1u : 0u;

            // All 64 comparison bits must be available before packing.
            GroupMemoryBarrierWithGroupSync();

            // Pack the hash into two 32-bit words.
            if (i == 0u)
            {
                uint hash_low = 0u;
                uint hash_high = 0u;

                [unroll]
                for (uint b = 0u; b < 32u; ++b)
                {
                    hash_low |= HashBits[b] << b;
                    hash_high |= HashBits[b + 32u] << b;
                }

                // Zero dHash fallback: encode RGBA using thermometer codes.
                if ((hash_low | hash_high) == 0u)
                {
                    float4 color = Source.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0);

                    // Quantize each channel to 17 levels (0..16).
                    // Explicit floor(+0.5) matches Python's half-up rounding.
                    uint4 q = (uint4)floor(saturate(color) * 16.0f + 0.5f);

                    // Generate 16-bit thermometer codes.
                    uint r = Thermometer16(q.r);
                    uint g = Thermometer16(q.g);
                    uint b = Thermometer16(q.b);
                    uint a = Thermometer16(q.a);

                    // Pack four 16-bit channels into 64 bits.
                    // Bits[0] = lower 32 bits (B, A)
                    // Bits[1] = upper 32 bits (R, G)
                    hash_low = (b << 16u) | a;
                    hash_high = (r << 16u) | g;
                }

                // Output layout:
                // Bits[0] = hash bits  0..31
                // Bits[1] = hash bits 32..63
                Bits[0] = hash_low;
                Bits[1] = hash_high;
            }
        }
    )";

bool SameDHashDevice(ID3D11Device* a, ID3D11Device* b)
{
    if (!a || !b) return false;
    ComPtr<IUnknown> left, right;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&left))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&right))) && left.Get() == right.Get();
}

class ScopedDHashCSState final {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ID3D11ClassInstance* instances[D3D11_SHADER_MAX_INTERFACES]{};
    UINT count = D3D11_SHADER_MAX_INTERFACES;
public:
    explicit ScopedDHashCSState(ID3D11DeviceContext* context) : context(context)
    {
        context->CSGetShader(&shader, instances, &count);
        context->CSGetShaderResources(0, 1, &srv);
        context->CSGetSamplers(0, 1, &sampler);
        context->CSGetUnorderedAccessViews(0, 1, &uav);
    }
    ~ScopedDHashCSState()
    {
        ID3D11ShaderResourceView* no_srv = nullptr;
        ID3D11UnorderedAccessView* no_uav = nullptr;
        context->CSSetShaderResources(0, 1, &no_srv);
        context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
        context->CSSetShader(shader.Get(), instances, count);
        ID3D11SamplerState* old_sampler = sampler.Get();
        ID3D11UnorderedAccessView* old_uav = uav.Get();
        ID3D11ShaderResourceView* old_srv = srv.Get();
        context->CSSetSamplers(0, 1, &old_sampler);
        // nullptr preserves an append/consume UAV's existing hidden counter.
        context->CSSetUnorderedAccessViews(0, 1, &old_uav, nullptr);
        context->CSSetShaderResources(0, 1, &old_srv);
        for (auto* instance : instances) if (instance) instance->Release();
    }
    ScopedDHashCSState(const ScopedDHashCSState&) = delete;
    ScopedDHashCSState& operator=(const ScopedDHashCSState&) = delete;
};
} // namespace

void DHash64Gpu::Reset() noexcept
{
    initialized = false;
    mStagingBuffer.Reset(); mOutputUAV.Reset(); mOutputBuffer.Reset();
    mSampler.Reset(); mComputeShader.Reset(); mOrigDevice1.Reset();
}

HRESULT DHash64Gpu::Initialize(ID3D11Device* device)
{
    if (!device) return E_INVALIDARG;
    if (initialized) return SameDHashDevice(device, mOrigDevice1.Get()) ? S_FALSE : E_INVALIDARG;
    Reset();
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) return E_NOTIMPL;
    mOrigDevice1 = device;
    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(kDHashShader, sizeof(kDHashShader) - 1, "DHash64CS.hlsl",
        nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (SUCCEEDED(hr)) hr = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &mComputeShader);
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.MaxAnisotropy = 1;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler.MaxLOD = 0;
    if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sampler, &mSampler);
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = 2 * sizeof(uint32_t); buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    buffer.StructureByteStride = sizeof(uint32_t);
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&buffer, nullptr, &mOutputBuffer);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 2;
    if (SUCCEEDED(hr)) hr = device->CreateUnorderedAccessView(mOutputBuffer.Get(), &uav, &mOutputUAV);
    buffer.Usage = D3D11_USAGE_STAGING; buffer.BindFlags = 0;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    buffer.MiscFlags = buffer.StructureByteStride = 0;
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&buffer, nullptr, &mStagingBuffer);
    if (FAILED(hr)) { Reset(); return hr; }
    initialized = true;
    return S_OK;
}

HRESULT DHash64Gpu::ComputeHash(ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    UINT mipLevel, DXGI_FORMAT format, uint64_t& outHash)
{
    outHash = 0;
    if (!initialized) return E_UNEXPECTED;
    if (!context || !texture || format == DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
    if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return E_INVALIDARG;
    ComPtr<ID3D11Device> context_device, texture_device;
    context->GetDevice(&context_device); texture->GetDevice(&texture_device);
    if (!SameDHashDevice(mOrigDevice1.Get(), context_device.Get()) ||
        !SameDHashDevice(mOrigDevice1.Get(), texture_device.Get())) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1 || !desc.MipLevels ||
        mipLevel >= desc.MipLevels || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return E_INVALIDARG;
    UINT support = 0;
    HRESULT hr = mOrigDevice1->CheckFormatSupport(format, &support);
    if (FAILED(hr)) return hr;
    if (!(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) || !(support & D3D11_FORMAT_SUPPORT_TEXTURE2D)) return E_INVALIDARG;

    // Sampling the application's texture directly could silently unbind an
    // alias from any CS UAV or OM output slot. Copy just the selected mip into
    // private storage, so no other pipeline stage is changed by binding it.
    D3D11_TEXTURE2D_DESC sample_desc = desc;
    sample_desc.Width = (std::max)(1u, desc.Width >> mipLevel);
    sample_desc.Height = (std::max)(1u, desc.Height >> mipLevel);
    UINT sample_mip = 0;
    const bool compressed = (desc.Format >= DXGI_FORMAT_BC1_TYPELESS && desc.Format <= DXGI_FORMAT_BC5_SNORM) ||
        (desc.Format >= DXGI_FORMAT_BC6H_TYPELESS && desc.Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
    // BC top levels must have block-aligned dimensions, although their lower
    // mips may be smaller than a block. Preserve the selected mip dimensions
    // by placing it below an aligned private top level, rather than padding
    // the sampled image and changing its normalized sampling coordinates.
    if (compressed) {
        while (sample_desc.Width % 4 || sample_desc.Height % 4) {
            sample_desc.Width *= 2;
            sample_desc.Height *= 2;
            ++sample_mip;
        }
    }
    sample_desc.MipLevels = sample_mip + 1;
    sample_desc.ArraySize = 1;
    sample_desc.Usage = D3D11_USAGE_DEFAULT;
    sample_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sample_desc.CPUAccessFlags = sample_desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> snapshot;
    hr = mOrigDevice1->CreateTexture2D(&sample_desc, nullptr, &snapshot);
    if (FAILED(hr)) return hr;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = format; view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MostDetailedMip = sample_mip;
    view.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> input;
    hr = mOrigDevice1->CreateShaderResourceView(snapshot.Get(), &view, &input);
    if (FAILED(hr)) return hr;
    context->CopySubresourceRegion(snapshot.Get(), sample_mip, 0, 0, 0, texture,
        D3D11CalcSubresource(mipLevel, 0, desc.MipLevels), nullptr);
    {
        ScopedDHashCSState saved(context);
        ID3D11ShaderResourceView* input_view = input.Get();
        ID3D11SamplerState* sampler = mSampler.Get();
        ID3D11UnorderedAccessView* output = mOutputUAV.Get();
        context->CSSetShader(mComputeShader.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 1, &input_view);
        context->CSSetSamplers(0, 1, &sampler);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->Dispatch(1, 1, 1);
    }
    context->CopyResource(mStagingBuffer.Get(), mOutputBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(mStagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    uint32_t words[2]{};
    std::memcpy(words, mapped.pData, sizeof(words));
    context->Unmap(mStagingBuffer.Get(), 0);
    outHash = uint64_t(words[0]) | (uint64_t(words[1]) << 32);
    return S_OK;
}
