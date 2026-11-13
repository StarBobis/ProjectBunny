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

Bytes dds_image()
{
    // Legacy RGBA8 DDS: one uncompressed texel, no sRGB metadata.
    Bytes bytes = {'D', 'D', 'S', ' '};
    const uint32_t header[] = {
        124, 0x100f, 1, 1, 4, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        32, 0x41, 0, 32, 0xff, 0xff00, 0xff0000, 0xff000000,
        0x1000, 0, 0, 0, 0
    };
    static_assert(sizeof(header) == 124, "DDS header size");
    for (uint32_t word : header)
        for (unsigned shift : {0, 8, 16, 24}) bytes.push_back(static_cast<unsigned char>(word >> shift));
    bytes.insert(bytes.end(), {0x12, 0x34, 0x56, 0xff});
    return bytes;
}

class PngFixture {
public:
    std::filesystem::path directory, config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    bool owns_config = false;

    PngFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate regression executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "PNG fixtures must be inside build/");
        directory = base / (L"png_color_regression_" + std::to_wstring(GetCurrentProcessId()));
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
    }

    ~PngFixture()
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

    void load_config()
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\ncalls=0\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[ResourcePNGCase]\ncolor_space=sRgB\n"
            "[ResourcePNGLinear]\ncolor_space=LINEAR\n"
            "[ResourcePNGInvalid]\ncolor_space=invalid_color\n"
            "[ResourcePNGDefault]\n"
            "[PoolPNGSource]\npool_size=2\ncolor_space=linear\n"
            "[PoolPNGCopy]\npool_size=1\ncolor_space=srgb\n";
        out.close();
        check(out.good(), "write actual color-space configuration");
        LoadConfigFile();
    }
};

class PngDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    PngDevice()
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

    ~PngDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

void parser_tests(PngFixture& fixture)
{
    const auto plain = fixture.write(L"plain.png", png_image());
    const auto tagged = fixture.write(L"tagged.png", png_image(true));
    const auto dds = fixture.write(L"plain.dds", dds_image());
    check(!PngHasSrgbChunk(dds.wstring()), "DDS does not enter PNG chunk detection");
    check(!PngHasSrgbChunk(plain.wstring()) && PngHasSrgbChunk(tagged.wstring()), "sRGB tag before IDAT only");
    check(!PngHasSrgbChunk(fixture.write(L"late.png", png_image(false, true)).wstring()), "stop at first IDAT");
    check(!PngHasSrgbChunk(fixture.write(L"profile.png", png_image(false, false, true)).wstring()), "do not infer sRGB from iCCP");
    check(PngHasSrgbChunk(fixture.write(L"profile-tag.png", png_image(true, false, true)).wstring()), "skip complete unrelated chunk before sRGB");
    check(!PngHasSrgbChunk((fixture.directory / L"missing.png").wstring()), "missing PNG is a normal false result");
    check(!PngHasSrgbChunk(fixture.write(L"empty.png", {}).wstring()), "empty file");
    const Bytes signature = {137, 80, 78, 71, 13, 10, 26, 10};
    for (size_t length = 1; length < signature.size(); ++length)
        check(!PngHasSrgbChunk(fixture.write((L"signature" + std::to_wstring(length) + L".png").c_str(),
            Bytes(signature.begin(), signature.begin() + length)).wstring()), "short signature");
    check(!PngHasSrgbChunk(fixture.write(L"not-png.png", Bytes(32, 0)).wstring()), "non-PNG file");
    Bytes complete;
    chunk(complete, "sRGB", {0});
    for (size_t length = 1; length < complete.size(); ++length) {
        Bytes bytes = signature;
        bytes.insert(bytes.end(), complete.begin(), complete.begin() + length);
        check(!PngHasSrgbChunk(fixture.write((L"chunk" + std::to_wstring(length) + L".png").c_str(), bytes).wstring()),
            "partial chunk header/data/CRC never matches");
    }
    Bytes huge = signature;
    huge.insert(huge.end(), {0xff, 0xff, 0xff, 0xff, 'j', 'u', 'n', 'k', 0, 0, 0, 0});
    chunk(huge, "sRGB", {0});
    check(!PngHasSrgbChunk(fixture.write(L"overflow.png", huge).wstring()), "reject huge skip before arithmetic/seek");
    Bytes truncated = signature;
    append_u32(truncated, 64);
    truncated.insert(truncated.end(), {'j', 'u', 'n', 'k', 0, 0, 0, 0});
    check(!PngHasSrgbChunk(fixture.write(L"truncated.png", truncated).wstring()), "reject chunk beyond file length");
    Bytes invalid_intent = signature;
    chunk(invalid_intent, "sRGB", {4});
    check(!PngHasSrgbChunk(fixture.write(L"invalid-intent.png", invalid_intent).wstring()), "invalid rendering intent");
    for (int i = 0; i < 128; ++i) {
        check(PngHasSrgbChunk(tagged.wstring()), "repeat tag scan");
        check(!PngHasSrgbChunk(plain.wstring()), "repeat untagged scan");
    }
    HANDLE exclusive = CreateFileW(tagged.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(exclusive != INVALID_HANDLE_VALUE, "parser closes FILE on positive match");
    CloseHandle(exclusive);
}

void verify_texture(PngDevice& device, const std::filesystem::path& path, CustomColorSpace color, bool srgb)
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

void metadata_tests(PngDevice& device)
{
    check(customResources.at(L"resourcepngcase").override_color_space == CustomColorSpace::SRGB &&
        customResources.at(L"resourcepnglinear").override_color_space == CustomColorSpace::LINEAR, "actual config case normalization");
    check(customResources.at(L"resourcepnginvalid").override_color_space == CustomColorSpace::DEFAULT &&
        customResources.at(L"resourcepngdefault").override_color_space == CustomColorSpace::DEFAULT, "unknown/default config falls back to automatic color");
    CustomResource copied;
    copied.CopyMetadataFrom(customResources.at(L"resourcepngcase"));
    check(copied.override_color_space == CustomColorSpace::SRGB, "resource metadata copy retains color override");
    auto& source = customResourcePools.at(L"poolpngsource");
    check(source.GetResource(0, false, false, false)->override_color_space == CustomColorSpace::LINEAR, "lazy pool element inherits template color");
    CommandList commands;
    CommandListScope scope;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring rhs = L"copy_desc poolpngsource";
    check(ParseCommandListResourceCopyTargetDirective(L"CommandListPNGRegression", L"poolpngcopy", &rhs,
        &commands, &ini_namespace), "parse actual pool copy_desc");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
    auto& destination = customResourcePools.at(L"poolpngcopy");
    check(destination.resource_template->override_color_space == CustomColorSpace::LINEAR &&
        destination.GetResource(1, false, false, false)->override_color_space == CustomColorSpace::LINEAR, "copy_desc and newly initialized pool element retain color");
}

void load_tests(PngFixture& fixture, PngDevice& device)
{
    for (int model : {-1, static_cast<int>(COINIT_APARTMENTTHREADED), static_cast<int>(COINIT_MULTITHREADED)}) {
        std::exception_ptr failure;
        std::thread loader([&]() {
            try {
                ULONG_PTR token = 0;
                check(CoGetContextToken(&token) == CO_E_NOTINITIALIZED, "fresh PNG thread has no COM apartment");
                if (model >= 0) check(SUCCEEDED(CoInitializeEx(nullptr, model)), "initialize host COM apartment");
                for (int repeat = 0; repeat < 2; ++repeat) {
                    verify_texture(device, fixture.directory / L"plain.png", CustomColorSpace::DEFAULT, false);
                    verify_texture(device, fixture.directory / L"plain.png", CustomColorSpace::SRGB, true);
                    verify_texture(device, fixture.directory / L"tagged.png", CustomColorSpace::DEFAULT, true);
                    verify_texture(device, fixture.directory / L"tagged.png", CustomColorSpace::LINEAR, false);
                    verify_texture(device, fixture.directory / L"plain.dds", CustomColorSpace::DEFAULT, false);
                    verify_texture(device, fixture.directory / L"plain.dds", CustomColorSpace::SRGB, true);
                    verify_texture(device, fixture.directory / L"plain.dds", CustomColorSpace::LINEAR, false);
                    for (const wchar_t* file : {L"missing.png", L"truncated.png", L"empty.png"}) {
                        CustomResource failed;
                        failed.filename = (fixture.directory / file).wstring();
                        failed.Substantiate(device.native.Get(), D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
                        check(!failed.resource && failed.is_null, "missing/malformed production PNG load fails normally");
                    }
                    check(model < 0 ? CoGetContextToken(&token) == CO_E_NOTINITIALIZED : SUCCEEDED(CoGetContextToken(&token)),
                        "successful/failed WIC loads balance owned COM and preserve host apartment");
                }
                if (model >= 0) CoUninitialize();
                check(CoGetContextToken(&token) == CO_E_NOTINITIALIZED, "no leaked S_FALSE COM initialization");
            } catch (...) { failure = std::current_exception(); }
        });
        loader.join();
        if (failure) std::rethrow_exception(failure);
    }
}
} // namespace

void test_dx11_png_color_space_regression()
{
    try {
        PngFixture fixture;
        parser_tests(fixture);
        PngDevice device;
        fixture.load_config();
        metadata_tests(device);
        load_tests(fixture, device);
        std::fputs("PASS: bounded PNG scan, real color config/pool metadata, WARP formats/pixels and COM lifetime\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: PNG color-space regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
