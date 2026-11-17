#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <cstdarg>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include "SpriteBatch.h"

std::wstring DecodeOverlayUtf8(const std::string& text);
std::wstring FormatOverlayText(const char* format, va_list args);
std::wstring FormatOverlayText(const wchar_t* format, va_list args);

// GDI Unicode font linking rasterizes on cache misses only; no COM or new DLLs.
// Independent of the game's BGRA device flag and swap-chain pixel format.
class OverlayTextFont {
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
        DirectX::XMFLOAT2 size;
        size_t bytes = 0;
        std::list<std::wstring>::iterator age;
    };
    ID3D11Device* device; // Borrowed trampoline: Overlay owns the wrapper lifetime.
    std::wstring family;
    int fontHeight;
    bool bold;
    mutable std::mutex mutex;
    std::unordered_map<std::wstring, Entry> entries;
    std::list<std::wstring> ages;
    size_t cachedBytes = 0;
    size_t rasterizations = 0;
    unsigned width = 1920, height = 1024;
    Entry& Get(const wchar_t* text, bool wrap);
public:
    OverlayTextFont(ID3D11Device* device, const wchar_t* family, float size, bool bold = false);
    void SetViewport(unsigned width, unsigned height);
    DirectX::XMVECTOR MeasureString(const wchar_t* text, bool wrap = true);
    void DrawString(DirectX::SpriteBatch* batch, const wchar_t* text,
        const DirectX::XMFLOAT2& position, DirectX::FXMVECTOR color, bool wrap = true);
    size_t CacheBytes() const { std::lock_guard<std::mutex> guard(mutex); return cachedBytes; }
    size_t CacheEntries() const { std::lock_guard<std::mutex> guard(mutex); return entries.size(); }
    size_t Rasterizations() const { std::lock_guard<std::mutex> guard(mutex); return rasterizations; }
};
