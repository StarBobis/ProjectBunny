#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "Globals.h"
#include "lock.h"
#include <atomic>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace {
std::atomic<uint64_t> textureMatchGeneration{1};

bool PixelBindings(CommandListState* state, ShaderBindings& bindings)
{
    if (!state || !state->mHackerDevice || !state->mHackerContext || !state->mOrigContext1)
        return false;
    if (state->mOrigContext1->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    ComPtr<ID3D11PixelShader> bound;
    state->mOrigContext1->PSGetShader(&bound, nullptr, nullptr);
    if (!bound) return false;
    const auto shader = state->mHackerContext->mCurrentPixelShaderHandle;
    if (!shader) return false;
    UINT64 hash;
    {
        CriticalSectionLockGuard guard(&G->mCriticalSection, __FUNCTION__, __LINE__);
        const auto found = lookup_shader_hash(shader);
        if (found == G->mShaders.end()) return false;
        hash = found->second;
    }
    // Copy metadata under its real owner lock; do not retain a map element pointer.
    return state->mHackerDevice->GetShaderBindings(hash, &bindings);
}
}

void InvalidateTextureMatchSnapshots()
{
    textureMatchGeneration.fetch_add(1, std::memory_order_relaxed);
}

void TextureOverrideCache::Clear()
{
    if (!snapshot_active) return;
    for (auto& view : original_textures) view.Reset();
    declared.fill(false); ResetMatches();
    snapshot_active = false; shader = nullptr;
}

bool TextureOverrideCache::IsCurrent(CommandListState* state) const
{
    return snapshot_active && state && state->mHackerContext &&
        call == state->mHackerContext->GetTextureMatchCall() &&
        shader == state->mHackerContext->mCurrentPixelShaderHandle &&
        generation == textureMatchGeneration.load(std::memory_order_relaxed);
}

void TextureOverrideCache::SnapshotOriginalTextures(CommandListState* state)
{
    CriticalSectionLockGuard configLifetime(&G->mCriticalSection, __FUNCTION__, __LINE__);
    if (IsCurrent(state)) return; // Same-call snapshots do not reset competition.
    Clear();
    const auto epoch = textureMatchGeneration.load(std::memory_order_relaxed);
    ShaderBindings bindings;
    if (!PixelBindings(state, bindings)) return;
    const auto range = bindings.resource_slots;
    if (!range.count || range.first > range.last || range.last >= SLOT_COUNT) return;
    for (UINT slot = range.first; slot <= range.last; ++slot) {
        if (bindings.resources[slot].type == ShaderResourceType::NONE) continue;
        declared[slot] = true;
        state->mOrigContext1->PSGetShaderResources(slot, 1, original_textures[slot].GetAddressOf());
    }
    call = state->mHackerContext->GetTextureMatchCall();
    shader = state->mHackerContext->mCurrentPixelShaderHandle;
    generation = epoch;
    snapshot_active = true;
    if (epoch != textureMatchGeneration.load(std::memory_order_relaxed)) Clear();
}

void TextureOverrideCache::RestoreOriginalTextures(CommandListState* state)
{
    CriticalSectionLockGuard configLifetime(&G->mCriticalSection, __FUNCTION__, __LINE__);
    if (!IsCurrent(state)) { Clear(); return; }
    ResourceCopyTarget destination;
    destination.type = ResourceCopyTargetType::SHADER_RESOURCE;
    destination.shader_type = L'p';
    for (UINT slot = 0; slot < SLOT_COUNT; ++slot) {
        if (!declared[slot]) continue;
        ComPtr<ID3D11Resource> resource;
        if (original_textures[slot]) original_textures[slot]->GetResource(&resource);
        destination.slot = slot;
        // Preserve exact SRV subresources/formats and restore declared null slots too.
        destination.SetResource(state, resource.Get(), original_textures[slot].Get(), 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    }
    ResetMatches();
}

void SnapshotOriginalTexturesOperation::run(CommandListState* state)
{
    if (state && state->mHackerContext)
        state->mHackerContext->GetTextureOverrideCache().SnapshotOriginalTextures(state);
}

void RestoreOriginalTexturesOperation::run(CommandListState* state)
{
    if (state && state->mHackerContext)
        state->mHackerContext->GetTextureOverrideCache().RestoreOriginalTextures(state);
}

void TextureOverrideOperation::run(CommandListState* state)
{
    if (!state || !state->mHackerContext || !state->mOrigContext1 || !match_resource) return;
    // Follow the existing config/command lifetime lock order, including direct invocations.
    CriticalSectionLockGuard commandLifetime(&G->mCriticalSection, __FUNCTION__, __LINE__);
    ResourceCopyTarget candidateTarget = *match_resource;
    const auto* candidate = candidateTarget.GetCustomResource(state);
    if (!candidate || !candidate->match_dhash_set || !candidate->match_format_valid ||
        !std::isfinite(candidate->match_confidence)) return;
    const uint64_t reference = candidate->match_dhash;
    const auto requiredFormat = candidate->match_format;
    const float confidence = (std::max)(0.0f, (std::min)(1.0f, candidate->match_confidence));
    const unsigned limit = static_cast<unsigned>((1.0 - confidence) * 64.0);
    auto& cache = state->mHackerContext->GetTextureOverrideCache();
    cache.SnapshotOriginalTextures(state);
    if (!cache.IsCurrent(state)) return;
    std::array<uint8_t, TextureOverrideCache::SLOT_COUNT> distances;
    distances.fill(65);
    unsigned best = 65;
    UINT mipSize;
    bool rehash;
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        mipSize = static_cast<UINT>((std::max)(0, G->dhash_mip_size));
        rehash = G->dhash_allow_rehash;
    }
    for (UINT slot = 0; slot < distances.size(); ++slot) {
        const auto* view = cache.original_textures[slot].Get();
        if (!view) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc;
        cache.original_textures[slot]->GetDesc(&desc);
        if (requiredFormat != DXGI_FORMAT_UNKNOWN && requiredFormat != static_cast<DXGI_FORMAT>(-1) && desc.Format != requiredFormat)
            continue;
        ComPtr<ID3D11Resource> original;
        cache.original_textures[slot]->GetResource(&original);
        uint64_t hash;
        if (!GetDhash(state->mHackerContext, original.Get(), mipSize, rehash, hash, cache.original_textures[slot].Get())) continue;
        distances[slot] = static_cast<uint8_t>(HammingDistance(reference, hash));
        best = (std::min)(best, static_cast<unsigned>(distances[slot]));
    }
    if (best > limit) return;
    ResourceCopyTarget source = src;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11View> view;
    ID3D11View* rawView = nullptr;
    UINT stride = 0, offset = 0, size = 0, originalStride = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool sizeIsLength = false;
    ResourceOriginalMetadata metadata;
    resource.Attach(source.GetResource(state, &rawView, &stride, &offset, &format, &size, &dst,
        &sizeIsLength, &originalStride, &metadata));
    view.Attach(rawView);
    if (!resource) return;
    for (UINT slot = 0; slot < distances.size(); ++slot) {
        if (distances[slot] != best || best >= cache.hamming_distance[slot]) continue;
        ResourceCopyTarget destination = dst;
        destination.slot = slot;
        DeferredBinding prepared;
        CopyResourceToResource(state, resource.Get(), view.Get(), stride, offset, format, size,
            &destination, &prepared, false, sizeIsLength, originalStride, metadata);
        ComPtr<ID3D11ShaderResourceView> replacement;
        if (!prepared.assigned || !prepared.resource || !prepared.view || FAILED(prepared.view.As(&replacement))) continue;
        destination.SetResource(state, prepared.resource.Get(), replacement.Get(), prepared.stride,
            prepared.offset, prepared.format, prepared.size);
        ComPtr<ID3D11ShaderResourceView> bound;
        state->mOrigContext1->PSGetShaderResources(slot, 1, &bound);
        // Failed view creation or driver binding must not block a later valid candidate.
        if (bound.Get() == replacement.Get()) cache.hamming_distance[slot] = static_cast<uint8_t>(best);
    }
}
