#include "FrameAnalysisDDS.h"
#include <DirectXTex.h>
#include <ScreenGrab.h>
#include <crc32c.h>
#include <wrl/client.h>

static bool existing_file(const std::wstring& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

HRESULT SaveFrameAnalysisDDS(ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    bool all_mips, bool deduplicate, std::wstring& path)
{
    if (!context || !texture || path.empty())
        return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const bool multiple_surfaces = desc.MipLevels > 1 || desc.ArraySize > 1;
    if (!all_mips || !multiple_surfaces) {
        if (deduplicate && multiple_surfaces) {
            // Earlier releases cached mip chains under the bare name.
            const auto dot = path.find_last_of(L'.');
            path.insert(dot == std::wstring::npos ? path.size() : dot, L"-top");
        }
        if (existing_file(path))
            return S_OK;
        return DirectX::SaveDDSTextureToFile(context, texture, path.c_str());
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    DirectX::ScratchImage capture;
    HRESULT hr = DirectX::CaptureTexture(device.Get(), context, texture, capture);
    if (FAILED(hr))
        return hr;

    if (deduplicate) {
        // The traditional key describes the first surface. Include every packed
        // subresource to distinguish lower mip and array changes between draws.
        uint32_t hash = 0;
        for (size_t i = 0; i < capture.GetImageCount(); ++i) {
            const auto& image = capture.GetImages()[i];
            hash = crc32c_append(hash, image.pixels, image.slicePitch);
        }
        wchar_t suffix[20];
        swprintf_s(suffix, L"-full-%08x", hash);
        const auto dot = path.find_last_of(L'.');
        path.insert(dot == std::wstring::npos ? path.size() : dot, suffix);
    }
    if (existing_file(path))
        return S_OK;
    return DirectX::SaveToDDSFile(capture.GetImages(), capture.GetImageCount(),
        capture.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.c_str());
}
