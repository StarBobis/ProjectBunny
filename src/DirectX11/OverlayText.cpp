#include "OverlayText.h"
#include <algorithm>
#include <cmath>
#include <locale.h>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void CheckTextResult(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::runtime_error(operation);
}
class TextLocale {
public:
    _locale_t value = _create_locale(LC_CTYPE, ".UTF8");
    ~TextLocale() { if (value) _free_locale(value); }
};
_locale_t OverlayLocale()
{
    static const TextLocale locale;
    if (!locale.value) throw std::runtime_error("Overlay UTF-8 locale initialization failed");
    return locale.value;
}
class GdiTextSurface {
    HFONT font = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldFont = nullptr, oldBitmap = nullptr;
public:
    HDC dc = nullptr;
    void* bits = nullptr;
    GdiTextSurface(const wchar_t* family, int size, bool bold)
    {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("Overlay memory DC failed");
        font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH, family);
        if (!font) { DeleteDC(dc); throw std::runtime_error("Overlay font allocation failed"); }
        oldFont = SelectObject(dc, font);
        if (!oldFont || oldFont == HGDI_ERROR) {
            DeleteObject(font); DeleteDC(dc); throw std::runtime_error("Overlay font selection failed");
        }
        SetTextColor(dc, RGB(255,255,255)); SetBkMode(dc, TRANSPARENT);
    }
    void CreateBitmap(UINT width, UINT height)
    {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) throw std::runtime_error("Overlay DIB allocation failed");
        oldBitmap = SelectObject(dc, bitmap);
        if (!oldBitmap || oldBitmap == HGDI_ERROR) throw std::runtime_error("Overlay DIB selection failed");
        memset(bits, 0, static_cast<size_t>(width) * height * 4);
    }
    ~GdiTextSurface()
    {
        if (oldBitmap && oldBitmap != HGDI_ERROR) SelectObject(dc, oldBitmap);
        if (oldFont && oldFont != HGDI_ERROR) SelectObject(dc, oldFont);
        if (bitmap) DeleteObject(bitmap);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
    }
    GdiTextSurface(const GdiTextSurface&) = delete;
    GdiTextSurface& operator=(const GdiTextSurface&) = delete;
};
constexpr size_t CacheByteLimit = 16 * 1024 * 1024;
constexpr size_t CacheEntryLimit = 64;
}

std::wstring DecodeOverlayUtf8(const std::string& text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw std::runtime_error("Overlay UTF-8 decoding failed");
    std::wstring result(length, L'\0');
    CheckTextResult(MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        &result[0], length) ? S_OK : E_FAIL, "Overlay UTF-8 decoding failed");
    return result;
}

std::wstring FormatOverlayText(const char* format, va_list args)
{
    char buffer[1024] = {};
    _vsnprintf_s_l(buffer, _countof(buffer), _TRUNCATE, format, OverlayLocale(), args);
    return DecodeOverlayUtf8(buffer);
}

std::wstring FormatOverlayText(const wchar_t* format, va_list args)
{
    wchar_t buffer[1024] = {};
    _vsnwprintf_s_l(buffer, _countof(buffer), _TRUNCATE, format, OverlayLocale(), args);
    return buffer;
}

OverlayTextFont::OverlayTextFont(ID3D11Device* source, const wchar_t* fontFamily, float size, bool weight)
    : device(source), family(fontFamily ? fontFamily : L"Microsoft YaHei UI"),
      fontHeight(0), bold(weight)
{
    if (!source || !std::isfinite(size) || size < 1 || size > 128)
        throw std::invalid_argument("Invalid overlay text device or font size");
    fontHeight = static_cast<int>(size);
    // No font/COM/system factory initialization during swap-chain creation.
}

void OverlayTextFont::SetViewport(unsigned newWidth, unsigned newHeight)
{
    std::lock_guard<std::mutex> guard(mutex);
    // A single raster remains bounded, including pathological diagnostic text.
    newWidth = (std::max)(1u, (std::min)(newWidth, 4096u));
    newHeight = (std::max)(1u, (std::min)(newHeight, 1024u));
    if (width == newWidth && height == newHeight) return;
    entries.clear(); ages.clear(); cachedBytes = 0;
    width = newWidth; height = newHeight;
}

OverlayTextFont::Entry& OverlayTextFont::Get(const wchar_t* text, bool wrap)
{
    std::wstring key = text ? text : L"";
    const auto length = static_cast<int>(key.size());
    // Inputs are NUL-terminated. An extra code unit distinguishes cache layouts.
    if (!wrap) key.push_back(L'\0');
    auto found = entries.find(key);
    if (found != entries.end()) {
        ages.splice(ages.end(), ages, found->second.age);
        return found->second;
    }
    GdiTextSurface surface(family.c_str(), fontHeight, bold);
    const UINT flags = (wrap ? DT_WORDBREAK : 0) | DT_NOPREFIX | DT_EDITCONTROL | DT_EXPANDTABS;
    RECT measured = {0, 0, static_cast<LONG>(width > 4 ? width - 4 : 1), 0};
    if (length && !DrawTextW(surface.dc, key.c_str(), length, &measured, flags | DT_CALCRECT))
        throw std::runtime_error("Overlay GDI text measurement failed");
    const UINT pixelsWide = (std::max)(1u, (std::min)(width, static_cast<UINT>((std::max)(0L, measured.right)) + 4));
    const UINT pixelsHigh = (std::max)(1u, (std::min)(height, static_cast<UINT>((std::max)(0L, measured.bottom)) + 4));
    surface.CreateBitmap(pixelsWide, pixelsHigh);
    RECT bounds = {2, 2, static_cast<LONG>(pixelsWide > 2 ? pixelsWide - 2 : pixelsWide), static_cast<LONG>(pixelsHigh)};
    if (length && pixelsWide > 4 && pixelsHigh > 4 &&
        !DrawTextW(surface.dc, key.c_str(), length, &bounds, flags))
        throw std::runtime_error("Overlay GDI text drawing failed");
    if (!GdiFlush()) throw std::runtime_error("Overlay GDI flush failed");
    std::vector<uint8_t> pixels(static_cast<size_t>(pixelsWide) * pixelsHigh * 4);
    const auto* source = static_cast<const uint8_t*>(surface.bits);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const auto alpha = (std::max)(source[i], (std::max)(source[i+1], source[i+2]));
        pixels[i] = pixels[i+1] = pixels[i+2] = pixels[i+3] = alpha;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = pixelsWide; desc.Height = pixelsHigh; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = { pixels.data(), pixelsWide * 4, 0 };
    ComPtr<ID3D11Texture2D> texture;
    CheckTextResult(device->CreateTexture2D(&desc, &data, &texture), "Overlay text texture upload failed");
    Entry entry;
    CheckTextResult(device->CreateShaderResourceView(texture.Get(), nullptr, &entry.texture), "Overlay text view failed");
    entry.size = DirectX::XMFLOAT2(static_cast<float>(pixelsWide), static_cast<float>(pixelsHigh));
    entry.bytes = pixels.size();
    while (!ages.empty() && (entries.size() >= CacheEntryLimit || cachedBytes + entry.bytes > CacheByteLimit)) {
        cachedBytes -= entries.at(ages.front()).bytes;
        entries.erase(ages.front()); ages.pop_front();
    }
    ages.push_back(key);
    entry.age = std::prev(ages.end());
    try {
        auto inserted = entries.emplace(key, std::move(entry));
        cachedBytes += inserted.first->second.bytes;
        ++rasterizations;
        return inserted.first->second;
    } catch (...) { ages.pop_back(); throw; }
}

DirectX::XMVECTOR OverlayTextFont::MeasureString(const wchar_t* text, bool wrap)
{
    std::lock_guard<std::mutex> guard(mutex);
    const auto& entry = Get(text, wrap);
    return DirectX::XMVectorSet(entry.size.x, entry.size.y, 0, 0);
}

void OverlayTextFont::DrawString(DirectX::SpriteBatch* batch, const wchar_t* text,
    const DirectX::XMFLOAT2& position, DirectX::FXMVECTOR color, bool wrap)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto& entry = Get(text, wrap);
    batch->Draw(entry.texture.Get(), position, nullptr, color);
}
