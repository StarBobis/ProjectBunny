#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "PngColorSpace.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using Bytes = std::vector<unsigned char>;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void append_u32(Bytes& bytes, uint32_t value)
{
    for (int shift : {24, 16, 8, 0}) bytes.push_back(static_cast<unsigned char>(value >> shift));
}

void chunk(Bytes& png, const char* type, const Bytes& data)
{
    append_u32(png, static_cast<uint32_t>(data.size()));
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    uint32_t crc = UINT32_MAX;
    for (size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    append_u32(png, ~crc);
}

Bytes png_image(bool srgb = false, bool late_srgb = false, bool profile = false)
{
    Bytes png = {137, 80, 78, 71, 13, 10, 26, 10};
    chunk(png, "IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
    // iCCP-only is a parser fixture, not a valid ICC profile/decoder test.
    if (profile) chunk(png, "iCCP", {'p', 0, 0, 0});
    if (srgb) chunk(png, "sRGB", {0});
    const Bytes row = {0, 0x12, 0x34, 0x56, 0xff};
    Bytes compressed = {0x78, 0x01, 0x01, 5, 0, 0xfa, 0xff};
    compressed.insert(compressed.end(), row.begin(), row.end());
    uint32_t a = 1, b = 0;
    for (unsigned char sample_byte : row) { a = (a + sample_byte) % 65521; b = (b + a) % 65521; }
    append_u32(compressed, (b << 16) | a);
    chunk(png, "IDAT", compressed);
    if (late_srgb) chunk(png, "sRGB", {0});
    chunk(png, "IEND", {});
    return png;
}

class DetectionFixture {
public:
    std::filesystem::path directory, config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    bool owns_config = false;
    bool saved_force = G->gForceDetectColorSpace;
    bool saved_initialized = G->gConfigInitialized;
    bool saved_pending = G->gReloadConfigPending;
    bool saved_wipe = G->gWipeUserConfig;
    unsigned saved_frame = G->frame_no;

    DetectionFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate regression executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "PNG fixtures must be inside build/");
        directory = base / (L"color_detection_options_" + std::to_wstring(GetCurrentProcessId()));
        config = base / L"d3dx.ini";
        settings = base / (directory.filename().wstring() + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing settings fixture");
        check(CreateDirectoryW(directory.c_str(), nullptr) != 0, "create exclusively owned PNG fixture directory");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            RemoveDirectoryW(directory.c_str());
            throw std::runtime_error("refuse existing d3dx.ini");
        }
        CloseHandle(file);
        owns_config = true;
        migoto_handle = nullptr;
        G->gWipeUserConfig = false;
        G->gReloadConfigPending = false;
        G->gConfigInitialized = false;
        G->frame_no = 0;
        G->user_config_dirty = 0;
    }

    ~DetectionFixture()
    {
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->gForceDetectColorSpace = saved_force;
        G->gConfigInitialized = saved_initialized;
        G->gReloadConfigPending = saved_pending;
        G->gWipeUserConfig = saved_wipe;
        G->frame_no = saved_frame;
        if (owns_config) { DeleteFileW(config.c_str()); DeleteFileW(settings.c_str()); }
        // Only files in the directory created exclusively by this fixture.
        if (!directory.empty()) {
            std::error_code error;
            for (const auto& file : std::filesystem::directory_iterator(directory, error))
                DeleteFileW(file.path().c_str());
            RemoveDirectoryW(directory.c_str());
        }
    }

    std::filesystem::path write(const wchar_t* name, const Bytes& bytes)
    {
        const auto path = directory / name;
        check(!std::filesystem::exists(path), "PNG fixture name is unused");
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        check(out.good(), "write PNG fixture bytes");
        return path;
    }

    void configure(const char* force)
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\ncalls=0\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\nconfig_initialization_delay=0\n";
        if (force) out << "force_detect_color_space=" << force << "\n";
        out << "[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[ResourcePNGCase]\ncolor_space=sRgB\n"
            "[ResourcePNGLinear]\ncolor_space=LINEAR\n"
            "[ResourcePNGInvalid]\ncolor_space=invalid_color\n"
            "[ResourcePNGDefault]\n"
            "[PoolPNGSource]\npool_size=2\ncolor_space=linear\n"
            "[PoolPNGCopy]\npool_size=1\ncolor_space=srgb\n";
        out.close();
        check(out.good(), "write actual color-space configuration");
    }
};

class DetectionDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    DetectionDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(runtime != nullptr, "load native D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve native D3D11 device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real PNG WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query D3D11.1 interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~DetectionDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

void verify_texture(DetectionDevice& device, const std::filesystem::path& path, CustomColorSpace color, bool srgb)
{
    CustomResource image;
    image.filename = path.wstring();
    image.override_color_space = color;
    image.Substantiate(device.native.Get(), D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    check(image.resource != nullptr && !image.is_null, "production CustomResource PNG load");
    ComPtr<ID3D11Texture2D> texture;
    check(SUCCEEDED(image.resource->QueryInterface(IID_PPV_ARGS(&texture))), "loaded PNG is a texture");
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool is_srgb = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    check(desc.Width == 1 && desc.Height == 1 && (rgba || bgra) && is_srgb == srgb, "actual auto/linear/sRGB DXGI format");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "create real PNG staging texture");
    device.context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE map = {};
    check(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "map actual decoded PNG pixels");
    const auto* pixels = static_cast<const unsigned char*>(map.pData);
    const bool correct = pixels[0] == (rgba ? 0x12 : 0x56) && pixels[1] == 0x34 &&
        pixels[2] == (rgba ? 0x56 : 0x12) && pixels[3] == 0xff;
    device.context->Unmap(staging.Get(), 0);
    check(correct, "color flags preserve encoded RGBA bytes");
}

void assert_flags(DetectionFixture& fixture, bool enabled)
{
    check(G->gForceDetectColorSpace == enabled, "real System option is loaded on every load/reload");
    CustomResource automatic;
    check(automatic.override_color_space == CustomColorSpace::DEFAULT, "new custom resources initialize color enum to DEFAULT");
    const auto tagged = (fixture.directory / L"tagged.png").wstring();
    const auto plain = (fixture.directory / L"plain.png").wstring();
    check(automatic.GetWICFlags(tagged) == (enabled ? DirectX::WIC_LOADER_FORCE_SRGB : DirectX::WIC_LOADER_DEFAULT),
        "production flag selector gates additional PNG scanning while preserving WIC default handling");
    check(automatic.GetWICFlags(plain) == DirectX::WIC_LOADER_DEFAULT &&
        automatic.GetWICFlags((fixture.directory / L"missing.png").wstring()) == DirectX::WIC_LOADER_DEFAULT,
        "plain and missing PNG paths retain default WIC flags");
    const auto& srgb = customResources.at(L"resourcepngcase");
    const auto& linear = customResources.at(L"resourcepnglinear");
    check(srgb.override_color_space == CustomColorSpace::SRGB && linear.override_color_space == CustomColorSpace::LINEAR,
        "actual enum configuration is case insensitive");
    check(customResources.at(L"resourcepnginvalid").override_color_space == CustomColorSpace::DEFAULT &&
        customResources.at(L"resourcepngdefault").override_color_space == CustomColorSpace::DEFAULT,
        "unknown and absent color names fall back to DEFAULT");
    check(srgb.GetWICFlags(plain) == DirectX::WIC_LOADER_FORCE_SRGB &&
        srgb.GetWICFlags(tagged) == DirectX::WIC_LOADER_FORCE_SRGB &&
        linear.GetWICFlags(tagged) == DirectX::WIC_LOADER_IGNORE_SRGB &&
        linear.GetWICFlags(plain) == DirectX::WIC_LOADER_IGNORE_SRGB,
        "explicit enum overrides take priority independently of extra PNG detection");
    CustomResource copied;
    copied.CopyMetadataFrom(srgb);
    check(copied.override_color_space == CustomColorSpace::SRGB, "metadata copies preserve enum values");
}

void verify_apartments(DetectionFixture& fixture, DetectionDevice& device)
{
    for (int model : {-1, static_cast<int>(COINIT_APARTMENTTHREADED), static_cast<int>(COINIT_MULTITHREADED)}) {
        std::exception_ptr failure;
        std::thread loader([&]() {
            bool initialized = false;
            try {
                ULONG_PTR token = 0;
                check(CoGetContextToken(&token) == CO_E_NOTINITIALIZED, "color option loader starts without COM");
                if (model >= 0) {
                    check(SUCCEEDED(CoInitializeEx(nullptr, model)), "initialize actual host COM apartment");
                    initialized = true;
                }
                verify_texture(device, fixture.directory / L"plain.png", CustomColorSpace::DEFAULT, false);
                verify_texture(device, fixture.directory / L"plain.png", CustomColorSpace::SRGB, true);
                verify_texture(device, fixture.directory / L"tagged.png", CustomColorSpace::DEFAULT, true);
                verify_texture(device, fixture.directory / L"tagged.png", CustomColorSpace::LINEAR, false);
                check(model < 0 ? CoGetContextToken(&token) == CO_E_NOTINITIALIZED : SUCCEEDED(CoGetContextToken(&token)),
                    "real WIC loads balance own COM and retain host apartment");
            } catch (...) { failure = std::current_exception(); }
            if (initialized) CoUninitialize();
        });
        loader.join();
        if (failure) std::rethrow_exception(failure);
    }
}
} // namespace

void test_dx11_color_detection_options_regression()
{
    try {
        DetectionFixture fixture;
        fixture.write(L"plain.png", png_image());
        fixture.write(L"tagged.png", png_image(true));
        DetectionDevice device;
        fixture.configure(nullptr);
        LoadConfigFile();
        assert_flags(fixture, false);
        verify_apartments(fixture, device);
        for (const char* option : {"1", "0", "invalid", "1", static_cast<const char*>(nullptr)}) {
            fixture.configure(option);
            ReloadConfig(device.device);
            const bool enabled = option && option[0] == '1';
            assert_flags(fixture, enabled);
            verify_apartments(fixture, device);
        }
        std::puts("PASS: actual color enum config, reload, optional PNG flag selection and WIC COM loads");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: color detection options regression: %s\n", error.what());
        std::exit(1);
    }
}
