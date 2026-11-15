#include "slot_range_test_support.h"
#include "ShaderBindings.h"
#include <d3dcompiler.h>
#include <array>
#include <filesystem>
#include <fstream>

extern "C" decltype(&D3DCreateBlob) __imp_D3DCreateBlob;

namespace {
using namespace dx11_slot_test;
ComPtr<ID3DBlob> compile_ownership(const char* text, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(text, std::strlen(text), "ownership-audit", nullptr, nullptr,
        "main", profile, 0, 0, &code, &errors)) && code, "F153: compile real fixture shader");
    return code;
}
class ShaderFilesScope {
    std::wstring old_path = G->SHADER_PATH, old_cache_path = G->SHADER_CACHE_PATH;
    ShaderHashType old_hash = G->shader_hash_type;
    int old_hunting = G->hunting;
    bool old_cache = G->CACHE_SHADERS, old_binary = G->EXPORT_BINARY, old_assembly = G->EXPORT_SHADERS;
    int old_hlsl = G->EXPORT_HLSL;
    int old_fix = G->decompiler_settings.fixSvPosition, old_recompile = G->decompiler_settings.recompileVs;
public:
    std::filesystem::path root, fixes;
    ShaderFilesScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "F153: locate host");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool build = false;
        for (const auto& part : base) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "F153: owned files remain under build");
        root = base / L"shader-ownership"; fixes = root / L"fixes";
        require_batch(!std::filesystem::exists(root) && std::filesystem::create_directories(fixes) &&
            std::filesystem::create_directory(root / L"cache"), "F153: exclusive replacement/cache paths");
        wcscpy_s(G->SHADER_PATH, MAX_PATH, fixes.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, (root / L"cache").c_str());
        G->shader_hash_type = ShaderHashType::FNV; G->hunting = 0;
        G->CACHE_SHADERS = G->EXPORT_BINARY = G->EXPORT_SHADERS = G->EXPORT_HLSL = false;
        G->decompiler_settings.fixSvPosition = G->decompiler_settings.recompileVs = false;
    }
    ~ShaderFilesScope()
    {
        wcscpy_s(G->SHADER_PATH, MAX_PATH, old_path.c_str()); wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, old_cache_path.c_str());
        G->shader_hash_type = old_hash; G->hunting = old_hunting;
        G->CACHE_SHADERS = old_cache; G->EXPORT_BINARY = old_binary; G->EXPORT_SHADERS = old_assembly; G->EXPORT_HLSL = old_hlsl;
        G->decompiler_settings.fixSvPosition = old_fix; G->decompiler_settings.recompileVs = old_recompile;
        std::error_code error; std::filesystem::remove_all(root, error);
    }
};
std::array<unsigned char,4> render_pixel(BatchDevice& device, ID3D11VertexShader* vertex, ID3D11PixelShader* pixel)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &target)), "F153: render target");
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(target.Get(), nullptr, &view)), "F153: target view");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "F153: staging");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> state;
    require_batch(SUCCEEDED(device.native->CreateRasterizerState(&raster, &state)), "F153: uncull fixture triangle");
    device.context->ClearState();
    D3D11_VIEWPORT viewport{}; viewport.Width = viewport.Height = viewport.MaxDepth = 1;
    device.context->RSSetViewports(1, &viewport); device.context->RSSetState(state.Get());
    ID3D11RenderTargetView* bound = view.Get(); device.context->OMSetRenderTargets(1, &bound, nullptr);
    const float blue[] = {0,0,1,1}; device.context->ClearRenderTargetView(view.Get(), blue);
    device.context->VSSetShader(vertex, nullptr, 0); device.context->PSSetShader(pixel, nullptr, 0);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); device.context->Draw(3,0);
    device.context->ClearState(); device.context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)), "F153: read actual rendered pixel");
    std::array<unsigned char,4> colour{}; std::memcpy(colour.data(), mapped.pData, colour.size());
    device.context->Unmap(staging.Get(),0); return colour;
}

unsigned injected_blob_calls = 0;
HRESULT WINAPI fail_diagnostic_blob(SIZE_T, ID3DBlob** output)
{
    ++injected_blob_calls;
    if (output) *output = nullptr;
    return E_OUTOFMEMORY;
}
class BlobFailureScope {
    decltype(&D3DCreateBlob) original = __imp_D3DCreateBlob;
    static void set(decltype(&D3DCreateBlob) value)
    {
        DWORD old_protection = 0;
        if (!VirtualProtect(&__imp_D3DCreateBlob, sizeof(__imp_D3DCreateBlob), PAGE_READWRITE, &old_protection))
            throw std::runtime_error("F172: change only this regression executable's import slot");
        __imp_D3DCreateBlob = value;
        DWORD ignored = 0;
        if (!VirtualProtect(&__imp_D3DCreateBlob, sizeof(__imp_D3DCreateBlob), old_protection, &ignored))
            std::abort();
    }
public:
    BlobFailureScope() { injected_blob_calls = 0; set(&fail_diagnostic_blob); }
    ~BlobFailureScope() noexcept
    {
        try { set(original); }
        catch (...) { std::abort(); }
    }
};
}
void test_dx11_shader_replacement_ownership_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device; ShaderFilesScope files;
    auto original = compile_ownership("float4 main():SV_Target{return float4(0,1,0,1);}", "ps_5_0");
    auto replacement = compile_ownership("float4 main():SV_Target{return float4(1,0,0,1);}", "ps_5_0");
    auto vertex_code = compile_ownership("float4 main(uint i:SV_VertexID):SV_Position{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[i],0,1);}", "vs_5_0");
    ComPtr<ID3D11VertexShader> vertex;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &vertex)), "F153: vertex shader");
    const auto* original_bytes = static_cast<const unsigned char*>(original->GetBufferPointer());
    const std::vector<unsigned char> snapshot(original_bytes, original_bytes + original->GetBufferSize());
    auto create_and_render = [&](const std::array<unsigned char,4>& expected) {
        ComPtr<ID3D11PixelShader> pixel;
        require_batch(SUCCEEDED(device.device->CreatePixelShader(original->GetBufferPointer(), original->GetBufferSize(), nullptr, &pixel)) && pixel,
            "F153: actual wrapped CreatePixelShader succeeds");
        require_batch(render_pixel(device, vertex.Get(), pixel.Get()) == expected, "F153: actual pixel identifies original versus replacement shader");
        require_batch(!std::memcmp(original->GetBufferPointer(), snapshot.data(), snapshot.size()), "F153: borrowed original bytecode is preserved");
        CleanupShaderMaps(pixel.Get());
    };
    create_and_render({0,255,0,255});
    wchar_t stem[64]{};
    swprintf_s(stem, L"%016llx-ps_replace.bin", fnv_64_buf(original->GetBufferPointer(), original->GetBufferSize()));
    const auto binary = files.fixes / stem;
    {
        std::ofstream output(binary, std::ios::binary);
        output.write(static_cast<const char*>(replacement->GetBufferPointer()), replacement->GetBufferSize());
        output.close(); require_batch(output.good(), "F153: actual replacement binary file");
    }
    std::fputs("F153: create shader from owned replacement array under allocator checks\n", stderr); std::fflush(stderr);
    create_and_render({255,0,0,255});
    std::filesystem::remove(binary);
    create_and_render({0,255,0,255});
}

void test_dx11_shader_diagnostic_failure_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device; ShaderFilesScope files;
    auto code = compile_ownership("float4 main():SV_Target{return float4(1,0,1,1);}", "ps_5_0");
    auto vertex_code = compile_ownership("float4 main(uint i:SV_VertexID):SV_Position{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[i],0,1);}", "vs_5_0");
    ComPtr<ID3D11VertexShader> vertex;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(vertex_code->GetBufferPointer(),vertex_code->GetBufferSize(),nullptr,&vertex)), "F172: vertex shader");
    G->hunting = 1;
    ComPtr<ID3D11PixelShader> pixel;
    HRESULT result;
    std::fputs("F172: fail only optional D3DCreateBlob after real native shader creation\n",stderr); std::fflush(stderr);
    { BlobFailureScope failure;
      result=device.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel); }
    require_batch(injected_blob_calls==1 && pixel, "F172: actual native shader exists and diagnostic boundary was reached once");
    ComPtr<ID3D11Device> owner; pixel->GetDevice(&owner);
    ComPtr<IUnknown> actual_owner, native_owner;
    require_batch(SUCCEEDED(owner.As(&actual_owner)) && SUCCEEDED(device.native.As(&native_owner)) && actual_owner.Get()==native_owner.Get(),
        "F172: returned shader belongs to the real WARP device");
    require_batch(result==S_OK, "F172: optional bytecode-retention failure must not replace native shader success");
    require_batch(G->mShaders.count(pixel.Get()) && G->mShaders.at(pixel.Get())==fnv_64_buf(code->GetBufferPointer(),code->GetBufferSize()) &&
        !G->mReloadedShaders.count(pixel.Get()) && !G->mOriginalShaders.count(pixel.Get()),
        "F172: publish normal shader tracking without fake partial reload metadata");
    require_batch(render_pixel(device,vertex.Get(),pixel.Get())==std::array<unsigned char,4>{255,0,255,255}, "F172: created shader remains usable after diagnostic failure");
    ComPtr<ID3D11PixelShader> recovered;
    require_batch(SUCCEEDED(device.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&recovered)) && recovered &&
        G->mReloadedShaders.count(recovered.Get()), "F172: normal diagnostic retention works after import-slot restoration");
    CleanupShaderMaps(recovered.Get()); CleanupShaderMaps(pixel.Get());
}
