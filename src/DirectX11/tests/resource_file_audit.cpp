#include "slot_range_test_support.h"
#include <ScreenGrab.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <array>

extern "C" size_t dx11_audit_device1_vtable_entries(void);
extern "C" size_t dx11_audit_create_texture2d_slot(void);

namespace {
using namespace dx11_slot_test;
class ResourceFiles {
    std::filesystem::path base, old_cwd = std::filesystem::current_path();
    bool old_warnings = G->gShowWarnings;
public:
    std::filesystem::path root;
    ResourceFiles()
    {
        wchar_t executable[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        require_batch(length && length < MAX_PATH, "F108: locate resource fixture host");
        base = std::filesystem::canonical(std::filesystem::path(executable).parent_path());
        bool build = false; for (const auto& part : base) build |= !_wcsicmp(part.c_str(), L"build");
        require_batch(build, "F108: fixtures remain under build");
        root = base / L"resource-file-audit";
        require_batch(std::filesystem::create_directory(root), "F108: exclusive resource fixture");
        std::filesystem::copy_file(base / L"wic-regression.png", root / L"image");
        std::filesystem::copy_file(base / L"wic-regression.png", root / L"image.png");
        std::filesystem::create_directory(root / L"folder.dds");
        std::filesystem::copy_file(base / L"wic-regression.png", root / L"folder.dds" / L"image");
        std::filesystem::current_path(root); G->gShowWarnings = false;
    }
    ~ResourceFiles()
    {
        G->gShowWarnings = old_warnings;
        std::error_code error; std::filesystem::current_path(old_cwd, error); if (error) std::abort();
        const auto resolved = std::filesystem::canonical(root, error);
        if (error || resolved != root || resolved.parent_path() != base) std::abort();
        std::filesystem::remove_all(root, error); if (error) std::abort();
    }
};
bool creation_lock_is_free()
{
    bool acquired = false;
    std::thread probe([&] {
        if (TryEnterCriticalSection(&resource_creation_mode_lock)) {
            acquired = true; LeaveCriticalSection(&resource_creation_mode_lock);
        }
    });
    probe.join(); return acquired;
}
void load_texture(CustomResource& resource, ID3D11Device* device, const wchar_t* name)
{
    resource.filename = name; resource.override_type = CustomResourceType::TEXTURE2D;
    resource.Substantiate(device, D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
}
std::vector<unsigned char> texture_bytes(BatchDevice& device, CustomResource& resource)
{
    require_batch(resource.resource && !resource.is_null, "F108: actual loaded texture exists");
    ComPtr<ID3D11Texture2D> texture, staging;
    require_batch(SUCCEEDED(resource.resource->QueryInterface(IID_PPV_ARGS(&texture))), "F108: native 2D texture");
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    const size_t row_bytes = static_cast<size_t>(desc.Width) * dxgi_format_size(desc.Format);
    require_batch(row_bytes != 0, "F108: fixture has a known uncompressed pixel format");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "F108: actual texture readback");
    device.context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)) && row_bytes <= mapped.RowPitch,
        "F108: readable native texture rows");
    std::vector<unsigned char> bytes(row_bytes * desc.Height);
    for (UINT row = 0; row < desc.Height; ++row)
        std::memcpy(bytes.data()+row*row_bytes, static_cast<unsigned char*>(mapped.pData)+row*mapped.RowPitch, row_bytes);
    device.context->Unmap(staging.Get(),0); return bytes;
}
struct TextureThrowState {
    ID3D11Device* owner;
    void** original;
    bool seen = false;
};
thread_local TextureThrowState* texture_throw = nullptr;
HRESULT STDMETHODCALLTYPE throw_texture_creation(ID3D11Device* self, const D3D11_TEXTURE2D_DESC*,
    const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D** output)
{
    if (!texture_throw || self != texture_throw->owner) return E_UNEXPECTED;
    *reinterpret_cast<void***>(self) = texture_throw->original;
    texture_throw->seen = true; if (output) *output = nullptr;
    throw std::runtime_error("F108 expected C++ texture creation exception");
}
class TextureCreationThrowScope {
    ID3D11Device* device;
    std::vector<void*> shadow;
    TextureThrowState state;
public:
    explicit TextureCreationThrowScope(ID3D11Device* native) : device(native), state{native,*reinterpret_cast<void***>(native)}
    {
        const auto entries = dx11_audit_device1_vtable_entries();
        const auto slot = dx11_audit_create_texture2d_slot();
        require_batch(slot < entries && !texture_throw, "F108: SDK-defined per-object fault slot");
        shadow.assign(state.original,state.original+entries); shadow[slot]=reinterpret_cast<void*>(&throw_texture_creation);
        texture_throw = &state; *reinterpret_cast<void***>(device)=shadow.data();
    }
    bool observed() const { return state.seen; }
    ~TextureCreationThrowScope()
    {
        *reinterpret_cast<void***>(device)=state.original; texture_throw=nullptr;
    }
};
}

void test_dx11_resource_filename_regression()
{
    ResourceFiles files; BatchDevice device;
    require_batch(creation_lock_is_free(), "F108: resource creation lock initially unowned");
    CustomResource reference; load_texture(reference,device.native.Get(),L"image.png");
    std::fputs("F108: reference PNG loaded\n",stderr); std::fflush(stderr);
    const auto expected = texture_bytes(device,reference);
    CustomResource extensionless;
    bool threw = false;
    try { load_texture(extensionless,device.native.Get(),L"image"); }
    catch (const std::exception& error) { threw = true; std::fprintf(stderr,"F108 extensionless load threw: %s\n",error.what()); }
    const bool unlocked = creation_lock_is_free();
    if (!unlocked) LeaveCriticalSection(&resource_creation_mode_lock); // Release the known old bug's acquisition before failing this process.
    require_batch(!threw && unlocked && texture_bytes(device,extensionless) == expected,
        "F108: extensionless PNG uses WIC and releases the creation lock");
    std::fputs("F108: extensionless PNG and lock checked\n",stderr); std::fflush(stderr);
    CustomResource parent_dot; load_texture(parent_dot,device.native.Get(),L"folder.dds/image");
    require_batch(texture_bytes(device,parent_dot) == expected, "F108: a parent directory suffix cannot choose the DDS loader");
    require_batch(SUCCEEDED(DirectX::SaveDDSTextureToFile(device.context.Get(),reference.resource,L"image.DDS")),
        "F108: generate real DDS fixture");
    CustomResource dds; load_texture(dds,device.native.Get(),L"image.DDS");
    require_batch(texture_bytes(device,dds) == expected, "F108: uppercase DDS keeps its original loader and pixel data");
    const std::vector<uint8_t> rgba{0x17, 0x57, 0x91, 0xff};
    D3D11_TEXTURE2D_DESC rgba_desc{};
    rgba_desc.Width = rgba_desc.Height = rgba_desc.MipLevels = rgba_desc.ArraySize = rgba_desc.SampleDesc.Count = 1;
    rgba_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rgba_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA rgba_data{rgba.data(), 4, 0};
    ComPtr<ID3D11Texture2D> rgba_texture;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&rgba_desc, &rgba_data, &rgba_texture)) &&
        SUCCEEDED(DirectX::SaveDDSTextureToFile(device.context.Get(), rgba_texture.Get(), L"rgba.dds")),
        "DDS: create a known linear RGBA fixture with an sRGB counterpart");
    CustomResource srgb; srgb.override_color_space = CustomColorSpace::SRGB;
    load_texture(srgb, device.native.Get(), L"rgba.dds");
    require_batch(srgb.resource && SUCCEEDED(DirectX::SaveDDSTextureToFile(device.context.Get(),
        srgb.resource, L"tagged.dds")), "DDS: write an explicitly sRGB fixture");
    CustomResource linear; linear.override_color_space = CustomColorSpace::LINEAR;
    load_texture(linear, device.native.Get(), L"tagged.dds");
    ComPtr<ID3D11Texture2D> gamma_texture, linear_texture;
    require_batch(SUCCEEDED(srgb.resource->QueryInterface(IID_PPV_ARGS(&gamma_texture))) &&
        linear.resource && SUCCEEDED(linear.resource->QueryInterface(IID_PPV_ARGS(&linear_texture))), "DDS: both color overrides created");
    D3D11_TEXTURE2D_DESC gamma_desc{}, linear_desc{};
    gamma_texture->GetDesc(&gamma_desc);
    linear_texture->GetDesc(&linear_desc);
    require_batch(gamma_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        linear_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && texture_bytes(device,linear) == rgba,
        "DDS: linear override removes sRGB metadata without changing pixels");
    std::fputs("F108: parent-dot and DDS checked\n",stderr); std::fflush(stderr);
    CustomResource missing; load_texture(missing,device.native.Get(),L"missing");
    require_batch(missing.substantiated && missing.is_null && !missing.resource && creation_lock_is_free(),
        "F108: missing extensionless texture fails safely with the existing one-shot state");
    missing.filename=L"image.png"; missing.Substantiate(device.native.Get(),D3D11_BIND_SHADER_RESOURCE,static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    require_batch(!missing.resource, "F108: failure does not introduce automatic per-frame retries");

    CustomResource exception;
    std::fputs("F108: inject through this executable's known proxy device vtable\n",stderr); std::fflush(stderr);
    bool observed = false, escaped = false;
    {
        TextureCreationThrowScope fault(device.device);
        try { load_texture(exception,device.device,L"image.png"); }
        catch (const std::runtime_error& error) { escaped = std::strstr(error.what(),"F108 expected") != nullptr; }
        observed = fault.observed();
    }
    const bool exception_unlocked = creation_lock_is_free();
    if (!exception_unlocked) LeaveCriticalSection(&resource_creation_mode_lock);
    require_batch(observed && escaped && exception_unlocked, "F108: unexpected C++ failure propagates while scoped creation ownership is released");
    std::fputs("F108: exception propagated and creation lock released\n",stderr); std::fflush(stderr);
    CustomResource recovered; load_texture(recovered,device.native.Get(),L"image.png");
    require_batch(texture_bytes(device,recovered) == expected, "F108: later independent resource creation still works");

    const std::array<unsigned,4> data{1,2,3,4};
    std::ofstream binary("rawdata",std::ios::binary); binary.write(reinterpret_cast<const char*>(data.data()),sizeof(data)); binary.close();
    require_batch(binary.good(), "F108: write extensionless buffer fixture");
    CustomResource buffer_resource; buffer_resource.filename=L"rawdata"; buffer_resource.override_type=CustomResourceType::BUFFER;
    buffer_resource.override_format=DXGI_FORMAT_R32_UINT;
    buffer_resource.Substantiate(device.native.Get(),D3D11_BIND_SHADER_RESOURCE,static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    ComPtr<ID3D11Buffer> buffer, staging;
    require_batch(buffer_resource.resource && SUCCEEDED(buffer_resource.resource->QueryInterface(IID_PPV_ARGS(&buffer))),
        "F108: extensionless buffer stays on its preexisting buffer loading path");
    D3D11_BUFFER_DESC desc{}; buffer->GetDesc(&desc);
    require_batch(desc.ByteWidth == sizeof(data), "F108: complete buffer extent retained");
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc,nullptr,&staging)), "F108: actual buffer readback");
    device.context->CopyResource(staging.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)), "F108: Map actual buffer bytes");
    const bool same = !std::memcmp(mapped.pData,data.data(),sizeof(data)); device.context->Unmap(staging.Get(),0);
    require_batch(same && creation_lock_is_free(), "F108: buffer content and creation lock remain correct");
}
