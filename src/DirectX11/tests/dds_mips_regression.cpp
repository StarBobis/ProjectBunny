#include "slot_range_test_support.h"
#include "../../../3rdparty/DirectXTK/Inc/ScreenGrab.h"
#include "../../../3rdparty/DirectXTK/Src/dds.h"
#include "../../../3rdparty/DirectXTK/Src/ScreenGrabRows.h"

#include "../FrameAnalysisDDS.h"
#include <DirectXTex.h>
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
    bool compressed, UINT mip_count, bool dx10, bool tight = false, bool cube = false)
{
    const UINT width = tight ? 64 : (compressed ? 8 : 7), height = cube ? width : (compressed ? 8 : 5);
    const UINT slices = cube ? 6 : 2;
    // A distinct second array image detects accidental array/mip interleaving.
    std::vector<std::vector<uint8_t>> images(mip_count * slices);
    std::vector<D3D11_SUBRESOURCE_DATA> initial(images.size());
    for (UINT item = 0; item < slices; ++item) {
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
    desc.ArraySize = slices;
    desc.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
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
    D3D11_MAPPED_SUBRESOURCE probe{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &probe)), "dds mips: inspect real row layout");
    const bool tightly_packed = probe.RowPitch == initial[0].SysMemPitch;
    device.context->Unmap(staging.Get(), 0);
    require_batch(!tight || tightly_packed, "dds mips: native tight fixture exercises direct mapped write");

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
    DdsTemporaryFile base;
    const auto prefix = base.path.wstring();
    auto full_path = prefix;
    require_batch(SUCCEEDED(SaveFrameAnalysisDDS(device.context.Get(), staging.Get(), true, true, full_path)) &&
        full_path != prefix, "frame DDS: complete capture has its own deduplication key");
    DirectX::ScratchImage captured;
    require_batch(SUCCEEDED(DirectX::LoadFromDDSFile(full_path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, captured)) &&
        captured.GetMetadata().IsCubemap() == cube && captured.GetMetadata().arraySize == slices && captured.GetMetadata().mipLevels == mip_count,
        "frame DDS: all array slices and mip levels survive serialization");
    for (UINT item = 0; item < slices; ++item) {
        for (UINT mip = 0; mip < mip_count; ++mip) {
            const auto* image = captured.GetImage(mip, item, 0);
            const auto& expected = images[D3D11CalcSubresource(mip, item, mip_count)];
            require_batch(image && image->slicePitch == expected.size() &&
                !memcmp(image->pixels, expected.data(), expected.size()), "frame DDS: exact subresource pixels");
        }
    }
    auto top = prefix;
    require_batch(SUCCEEDED(SaveFrameAnalysisDDS(device.context.Get(), staging.Get(), false, true, top)) &&
        top != prefix && top != full_path && SUCCEEDED(DirectX::LoadFromDDSFile(top.c_str(),
            DirectX::DDS_FLAGS_NONE, nullptr, captured)) &&
        captured.GetMetadata().mipLevels == 1 && captured.GetMetadata().arraySize == 1 &&
        !memcmp(captured.GetPixels(), images.front().data(), images.front().size()),
        "frame DDS: top-only mode has independent storage and contains exactly the first surface");
    DeleteFileW(top.c_str());
    auto directory = base.path.parent_path().wstring();
    require_batch(FAILED(SaveFrameAnalysisDDS(device.context.Get(), staging.Get(), false, false, directory)),
        "frame DDS: an unwritable destination directory is reported as an error");
    auto same = prefix;
    require_batch(SUCCEEDED(SaveFrameAnalysisDDS(device.context.Get(), staging.Get(), true, true, same)) &&
        same == full_path, "frame DDS: immutable contents reuse the same file");
    images.back()[0] ^= 0xff;
    device.context->UpdateSubresource(texture.Get(), UINT(images.size() - 1), nullptr, images.back().data(),
        initial.back().SysMemPitch, 0);
    auto changed = prefix;
    require_batch(SUCCEEDED(SaveFrameAnalysisDDS(device.context.Get(), texture.Get(), true, true, changed)) &&
        changed != full_path, "frame DDS: an update outside the first surface changes the deduplication key");
    DeleteFileW(full_path.c_str());
    DeleteFileW(changed.c_str());

}
}

void test_dx11_dds_mips_regression()
{
    // WARP on this host exposes tight rows even for odd texture widths. Test
    // the production packing helper with explicit padding, without mocking a
    // D3D context or claiming that a padded GPU mapping was exercised.
    for (size_t rows : {size_t(1), size_t(7)}) {
        for (size_t padding : {size_t(0), size_t(1), size_t(64)}) {
            const size_t pitch = 13, source_pitch = pitch + padding;
            std::vector<uint8_t> source(source_pitch * rows, 0xcd), packed(pitch * rows + 2, 0xa5), expected;
            for (size_t y = 0; y < rows; ++y) {
                for (size_t x = 0; x < pitch; ++x) {
                    const auto value = static_cast<uint8_t>(y * 19 + x);
                    source[y * source_pitch + x] = value; expected.push_back(value);
                }
            }
            const auto original = source;
            const auto* output = DirectX::Internal::PackDDSRows(source.data(), source_pitch, packed.data() + 1, pitch, rows);
            require_batch(memcmp(output, expected.data(), expected.size()) == 0 && source == original &&
                packed.front() == 0xa5 && packed.back() == 0xa5, "dds rows: exact bytes, immutable source and guarded packing bounds");
            require_batch(padding ? output == packed.data() + 1 : output == source.data(),
                "dds rows: tight layout aliases source, padded layout uses owned buffer");
        }
    }
    BatchDevice device;
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 3, false);
    check_dds(device, DXGI_FORMAT_R16G16B16A16_UINT, 8, false, 3, true);
    check_dds(device, DXGI_FORMAT_BC1_UNORM, 8, true, 3, false);
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 1, false);
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 3, false, true);
    check_dds(device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 3, false, false, true);
    require_batch(DirectX::SaveDDSTextureToFile(device.context.Get(), nullptr, L"unused", true) == E_INVALIDARG &&
        DirectX::SaveDDSTextureToFile(device.context.Get(), nullptr, nullptr, false) == E_INVALIDARG,
        "dds mips: invalid source or filename rejected");
}
