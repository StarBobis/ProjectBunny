#pragma once
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

class CommandListState;
void InvalidateTextureMatchSnapshots();

// Context-owned original bindings. Never captures replacements from a prior call.
class TextureOverrideCache {
    uint64_t call = 0, generation = 0;
    ID3D11PixelShader* shader = nullptr;
public:
    static constexpr UINT SLOT_COUNT = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, SLOT_COUNT> original_textures;
    std::array<uint8_t, SLOT_COUNT> hamming_distance{};
    std::array<bool, SLOT_COUNT> declared{};
    bool snapshot_active = false;
    TextureOverrideCache() { hamming_distance.fill(65); }
    TextureOverrideCache(const TextureOverrideCache&) = delete;
    TextureOverrideCache& operator=(const TextureOverrideCache&) = delete;
    void Clear();
    void ResetMatches() { hamming_distance.fill(65); }
    bool IsCurrent(CommandListState* state) const;
    void SnapshotOriginalTextures(CommandListState* state);
    void RestoreOriginalTextures(CommandListState* state);
};
