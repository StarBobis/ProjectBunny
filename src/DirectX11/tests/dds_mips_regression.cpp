#include "slot_range_test_support.h"
#include "../../../3rdparty/DirectXTK/Inc/ScreenGrab.h"
#include "../../../3rdparty/DirectXTK/Src/dds.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using namespace dx11_slot_test;

class DdsTemporaryFile {
public:
    std::filesystem::path path;
    DdsTemporaryFile()
    {
        wchar_t executable[MAX_PATH]{}, filename[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0,
            "dds mips: locate actual regression executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(in_build && GetTempFileNameW(directory.c_str(), L"dds", 0, filename) != 0,
            "dds mips: reserve temporary file under build");
        path = filename;
    }
    ~DdsTemporaryFile() { DeleteFileW(path.c_str()); }
};

void check_dds(BatchDevice& device, DXGI_FORMAT format, UINT bytes_per_unit,
    bool compressed, UINT mip_count, bool dx10)
{
    const UINT width = compressed ? 8 : 7, height = compressed ? 8 : 5;
    // A distinct second array image detects accidental array/mip interleaving.
    std::vector<std::vector<uint8_t>> images(mip_count * 2);
    std::vector<D3D11_SUBRESOURCE_DATA> initial(images.size());
    for (UINT item = 0; item < 2; ++item) {
        for (UINT mip = 0; mip < mip_count; ++mip) {
            const UINT w = (std::max)(1u, width >> mip);
            const UINT h = (std::max)(1u, height >> mip);
            const UINT columns = compressed ? (w + 3) / 4 : w;
            const UINT rows = compressed ? (h + 3) / 4 : h;
            const UINT index = D3D11CalcSubresource(mip, item, mip_count);
            auto& image = images[index];
            image.resize(columns * bytes_per_unit * rows);
            for (size_t i = 0; i < image.size(); ++i)
                image[i] = static_cast<uint8_t>(i * 13 + mip * 47 + item * 101);
            initial[index].pSysMem = image.data();
            initial[index].SysMemPitch = columns * bytes_per_unit;
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mip_count;
    desc.ArraySize = 2;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, initial.data(), &texture)),
        "dds mips: create populated native texture");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)),
        "dds mips: create readable staging texture");
    device.context->CopyResource(staging.Get(), texture.Get());

    for (UINT mode = 0; mode < 4; ++mode) {
        DdsTemporaryFile file;
        ID3D11Resource* source = mode == 3 ? staging.Get() : texture.Get();
        const bool all_mips = mode >= 2;
        const HRESULT saved = mode == 0
            ? DirectX::SaveDDSTextureToFile(device.context.Get(), source, file.path.c_str())
            : DirectX::SaveDDSTextureToFile(device.context.Get(), source, file.path.c_str(), all_mips);
        require_batch(SUCCEEDED(saved), "dds mips: save actual DDS");
        std::ifstream stream(file.path, std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        const size_t header_size = 4 + sizeof(DirectX::DDS_HEADER) +
            (dx10 ? sizeof(DirectX::DDS_HEADER_DXT10) : 0);
        require_batch(bytes.size() >= header_size, "dds mips: complete DDS header");
        uint32_t magic = 0;
        DirectX::DDS_HEADER header{};
        memcpy(&magic, bytes.data(), sizeof(magic));
        memcpy(&header, bytes.data() + 4, sizeof(header));
        const UINT expected_mips = all_mips ? mip_count : 1;
        require_batch(magic == DirectX::DDS_MAGIC && header.size == sizeof(header) &&
            header.width == width && header.height == height && header.mipMapCount == expected_mips,
            "dds mips: header dimensions and mip count");
        require_batch(bool(header.flags & DDS_HEADER_FLAGS_MIPMAP) == (expected_mips > 1) &&
            (header.caps & DDS_SURFACE_FLAGS_MIPMAP) == (expected_mips > 1 ? DDS_SURFACE_FLAGS_MIPMAP : 0),
            "dds mips: mip flags reflect actual output");
        require_batch(header.pitchOrLinearSize == (compressed ? images[0].size() : width * bytes_per_unit),
            "dds mips: top level pitch or compressed linear size");
        if (dx10) {
            DirectX::DDS_HEADER_DXT10 extension{};
            memcpy(&extension, bytes.data() + 4 + sizeof(header), sizeof(extension));
            require_batch(extension.dxgiFormat == format && extension.arraySize == 1 &&
                extension.resourceDimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D,
                "dds mips: DX10 extension describes first array image");
        }
        size_t offset = header_size;
        for (UINT mip = 0; mip < expected_mips; ++mip) {
            const auto& expected = images[mip];
            require_batch(offset <= bytes.size() && expected.size() <= bytes.size() - offset &&
                memcmp(bytes.data() + offset, expected.data(), expected.size()) == 0,
                "dds mips: tightly packed mip bytes preserve row data and ordering");
            offset += expected.size();
        }
        require_batch(bytes.size() == offset, "dds mips: exact file length excludes padding and other array images");
    }
}
}

void test_dx11_dds_mips_regression()
{
    BatchDevice device;
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 3, false);
    check_dds(device, DXGI_FORMAT_R16G16B16A16_UINT, 8, false, 3, true);
    check_dds(device, DXGI_FORMAT_BC1_UNORM, 8, true, 3, false);
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 1, false);
    require_batch(DirectX::SaveDDSTextureToFile(device.context.Get(), nullptr, L"unused", true) == E_INVALIDARG &&
        DirectX::SaveDDSTextureToFile(device.context.Get(), nullptr, nullptr, false) == E_INVALIDARG,
        "dds mips: invalid source or filename rejected");
}
