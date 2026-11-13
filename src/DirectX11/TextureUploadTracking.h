#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <vector>

// Semantic commands own only GPU objects/parameters, never mapped CPU pointers.
enum class TextureUploadEventKind { Update, Copy, CopyRegion, Resolve, Generate };
struct TextureUploadEvent {
    TextureUploadEventKind kind = TextureUploadEventKind::Update;
    Microsoft::WRL::ComPtr<ID3D11Resource> destination, source;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    UINT destination_subresource = 0, source_subresource = 0;
    UINT x = 0, y = 0, z = 0;
    D3D11_BOX box{};
    bool has_box = false, full = false, discard = false;
};

// Per-context recording, immutable per-command-list publication; no global latch.
class TextureUploadTracker {
    std::vector<TextureUploadEvent> pending;
    bool unknown_prefix = false;
    void Submit(ID3D11DeviceContext1*, TextureUploadEvent&&) noexcept;
public:
    void Forget(ID3D11DeviceContext1*) noexcept;
    void Update(ID3D11DeviceContext1*, ID3D11Resource*, UINT, const D3D11_BOX*, bool discard = false) noexcept;
    void Copy(ID3D11DeviceContext1*, ID3D11Resource*, ID3D11Resource*) noexcept;
    void CopyRegion(ID3D11DeviceContext1*, ID3D11Resource*, UINT, UINT, UINT, UINT,
        ID3D11Resource*, UINT, const D3D11_BOX*, UINT flags = 0, bool allow_overlap = false) noexcept;
    void Resolve(ID3D11DeviceContext1*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT) noexcept;
    void Generate(ID3D11DeviceContext1*, ID3D11ShaderResourceView*) noexcept;
    void Finish(ID3D11CommandList*) noexcept;
    void Execute(ID3D11DeviceContext1*, ID3D11CommandList*) noexcept;
};
