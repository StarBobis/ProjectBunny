#include "slot_range_test_support.h"
#include "lock.h"
#include <limits>
using namespace dx11_slot_test;

namespace {
class UploadGlobals {
    decltype(G->mTextureOverrideMap) overrides;
    decltype(G->mFuzzyTextureOverrides) fuzzy;
    int hunting = G->hunting, updates = G->track_texture_updates;
    bool deferred = G->deferred_contexts_enabled;
public:
    UploadGlobals()
    {
        CriticalSectionGuard(&G->mCriticalSection);
        overrides = std::move(G->mTextureOverrideMap); fuzzy = std::move(G->mFuzzyTextureOverrides);
        G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear(); InvalidateTextureOverrideCandidates();
        G->hunting = 0; G->track_texture_updates = 0; G->deferred_contexts_enabled = true;
    }
    ~UploadGlobals()
    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->mTextureOverrideMap = std::move(overrides); G->mFuzzyTextureOverrides = std::move(fuzzy);
        InvalidateTextureOverrideCandidates(); G->hunting = hunting; G->track_texture_updates = updates; G->deferred_contexts_enabled = deferred;
    }
};

D3D11_TEXTURE2D_DESC description(UINT mips = 3, UINT slices = 2)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 8; desc.Height = 4; desc.MipLevels = mips; desc.ArraySize = slices;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_DEFAULT;
    return desc;
}
ComPtr<ID3D11Texture2D> tracked(BatchDevice& device, D3D11_TEXTURE2D_DESC desc, bool initial = false, UINT colour = 0x87654321)
{
    std::vector<std::vector<UINT>> bytes;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    if (initial) {
        require_batch(desc.MipLevels != 0, "upload: explicit initial mip count");
        bytes.resize(desc.MipLevels * desc.ArraySize); data.resize(bytes.size());
        for (UINT i = 0; i < data.size(); ++i) {
            const UINT mip = i % desc.MipLevels;
            const UINT width = (std::max)(1u, desc.Width >> mip), height = (std::max)(1u, desc.Height >> mip);
            bytes[i].assign(width * height, colour);
            data[i].pSysMem = bytes[i].data(); data[i].SysMemPitch = width * sizeof(UINT);
        }
    }
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, initial ? data.data() : nullptr, &result)),
        "upload: actual HackerDevice creates tracked texture");
    return result;
}
TextureUploadStateSnapshot snapshot(ID3D11Resource* resource)
{
    TextureUploadStateSnapshot result;
    require_batch(GetTextureUploadStateSnapshot(resource, result), "upload: locked actual registered texture snapshot");
    return result;
}
void upload(HackerContext& context, ID3D11Texture2D* texture, UINT subresource, UINT colour)
{
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    const UINT mip = subresource % desc.MipLevels, width = (std::max)(1u, desc.Width >> mip), height = (std::max)(1u, desc.Height >> mip);
    std::vector<UINT> bytes(width * height, colour);
    context.UpdateSubresource(texture, subresource, nullptr, bytes.data(), width * sizeof(UINT), 0);
}
void pixels(BatchDevice& device, ID3D11Texture2D* texture, UINT subresource, UINT colour, const char* label)
{
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "upload: real staging texture");
    device.context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped)), "upload: native texture readback");
    const UINT mip = subresource % desc.MipLevels, width = (std::max)(1u, desc.Width >> mip), height = (std::max)(1u, desc.Height >> mip);
    bool same = true;
    for (UINT y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < width; ++x) {
            if (same && row[x] != colour)
                std::fprintf(stderr, "upload pixels mismatch: %s sub=%u x=%u y=%u actual=%08x expected=%08x width=%u height=%u pitch=%u\n",
                    label, subresource, x, y, row[x], colour, width, height, mapped.RowPitch);
            same &= row[x] == colour;
        }
    }
    device.context->Unmap(staging.Get(), subresource);
    require_batch(same, "upload: actual pixels match uploaded/copied subresource");
}

void creation_and_updates(BatchDevice& device)
{
    auto texture = tracked(device, description());
    auto initial = tracked(device, description(), true);
    auto empty = snapshot(texture.Get()), full = snapshot(initial.Get());
    require_batch(empty.mip_levels == 3 && empty.array_size == 2 && empty.subresource_generations.size() == 6 &&
        empty.resource_generation == 0 && full.resource_generation == 1 &&
        std::all_of(full.subresource_generations.begin(), full.subresource_generations.end(), [](uint64_t v) { return v == 1; }),
        "upload: creation distinguishes all actual initialized subresources");
    upload(*device.wrapper, texture.Get(), 4, 0x10203040);
    auto state = snapshot(texture.Get());
    require_batch(state.resource_generation == 1 && state.subresource_generations[4] == 1 && state.subresource_generations[0] == 0,
        "upload: nonzero mip/slice changes independently without old hash tracking enabled");
    pixels(device, texture.Get(), 4, 0x10203040, "initial nonzero mip upload");
    const UINT sample = 9; D3D11_BOX partial_box{0,0,0,1,1,1};
    device.wrapper->UpdateSubresource(texture.Get(), 0, &partial_box, &sample, 4, 0);
    state = snapshot(texture.Get());
    require_batch(state.resource_generation == 2 && state.subresource_generations[0] == 0 && state.subresource_generations[4] == 1,
        "upload: partial write does not claim uninitialized subresource complete");
    device.wrapper->UpdateSubresource(texture.Get(), 4, &partial_box, &sample, 4, 0);
    require_batch(snapshot(texture.Get()).subresource_generations[4] == 3, "upload: partial known upload preserves initialization");
    D3D11_BOX none{0,0,0,0,1,1};
    device.wrapper->UpdateSubresource(texture.Get(), 0, &none, &sample, 4, 0);
    require_batch(snapshot(texture.Get()).resource_generation == 3, "upload: empty box is no event");
    // Update1 discard is a full upload event and invalidates earlier mips/slices.
    std::vector<UINT> discard_bytes(4 * 2, 0x33445566);
    device.wrapper->UpdateSubresource1(texture.Get(), 1, nullptr, discard_bytes.data(), 4 * sizeof(UINT), 0, D3D11_COPY_DISCARD);
    auto discarded = snapshot(texture.Get());
    require_batch(discarded.resource_generation == 4 && discarded.subresource_generations[1] == 4 &&
        discarded.subresource_generations[4] == 0, "upload: real Update1 discard resets prior initialization");
    const auto owned_snapshot = snapshot(texture.Get());
    auto* identity = texture.Get(); texture.Reset(); device.context->Flush();
    TextureUploadStateSnapshot missing;
    require_batch(!GetTextureUploadStateSnapshot(identity, missing) && owned_snapshot.subresource_generations[1] == 4,
        "upload: snapshot owns data beyond real resource lifetime/release cleanup");

    // fdee treats every non-null Update box conservatively as partial, even
    // geometric full coverage. CopyRegion has a separate full-coverage rule.
    auto boxed = tracked(device, description(1,1));
    D3D11_BOX entire{0,0,0,8,4,1};
    std::vector<UINT> boxed_bytes(8 * 4, 0xff112244);
    device.wrapper->UpdateSubresource(boxed.Get(), 0, &entire, boxed_bytes.data(), 8 * sizeof(UINT), 0);
    auto boxed_state = snapshot(boxed.Get());
    require_batch(boxed_state.resource_generation == 1 && boxed_state.subresource_generations[0] == 0,
        "upload: explicit full Update box retains upstream conservative unknown initialization");
    pixels(device, boxed.Get(), 0, 0xff112244, "explicit full Update box");
    upload(*device.wrapper, boxed.Get(), 0, 0xff112244);
    require_batch(snapshot(boxed.Get()).subresource_generations[0] == 2,
        "upload: subsequent nullptr-box full upload initializes the same physical subresource");

    auto automatic = tracked(device, description(0, 1));
    auto actual = snapshot(automatic.Get());
    require_batch(actual.mip_levels == 4 && actual.width == 8 && actual.height == 4,
        "upload: native MipLevels zero expands to actual layout");
    D3D11_TEXTURE2D_DESC original = description(1, 1);
    const auto hash = CalcTexture2DDescHash(CalcTexture2DDataHash(&original, nullptr), &original);
    {
        CriticalSectionGuard(&G->mCriticalSection);
        auto& rule = G->mTextureOverrideMap[hash].emplace_back(); rule.ini_section = L"TextureOverrideUploadActual"; rule.width = 4;
        InvalidateTextureOverrideCandidates();
    }
    auto resized = tracked(device, original);
    D3D11_TEXTURE2D_DESC resized_desc{}; resized->GetDesc(&resized_desc);
    require_batch(resized_desc.Width == 4 && snapshot(resized.Get()).width == 4,
        "upload: actual overridden width is tracked independently of original descriptor");
    {
        CriticalSectionGuard(&G->mCriticalSection); G->mTextureOverrideMap.erase(hash); InvalidateTextureOverrideCandidates();
    }
    auto source = tracked(device, original, true, 0xabcdef12);
    D3D11_BOX half{0,0,0,4,4,1};
    device.wrapper->CopySubresourceRegion(resized.Get(), 0, 0,0,0, source.Get(), 0, &half);
    require_batch(snapshot(resized.Get()).subresource_generations[0] != 0, "upload: full destination detection uses overridden physical dimensions");
    pixels(device, resized.Get(), 0, 0xabcdef12, "actual override CopyRegion");
}

void maps_and_copies(BatchDevice& device)
{
    auto desc = description(); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    auto mapped_texture = tracked(device, desc);
    D3D11_MAPPED_SUBRESOURCE first{}, second{};
    require_batch(SUCCEEDED(device.wrapper->Map(mapped_texture.Get(), 1, D3D11_MAP_WRITE, 0, &first)), "upload: real writable first mapped mip");
    require_batch(SUCCEEDED(device.wrapper->Map(mapped_texture.Get(), 4, D3D11_MAP_WRITE, 0, &second)), "upload: real independently mapped second slice/mip");
    std::memset(first.pData, 0x11, first.RowPitch * 2); std::memset(second.pData, 0x22, second.RowPitch * 2);
    device.wrapper->Unmap(mapped_texture.Get(), 4); device.wrapper->Unmap(mapped_texture.Get(), 1);
    auto state = snapshot(mapped_texture.Get());
    require_batch(state.resource_generation == 2 && state.subresource_generations[4] == 1 && state.subresource_generations[1] == 2,
        "upload: reverse Unmap preserves independent registry records");
    D3D11_MAPPED_SUBRESOURCE read{};
    require_batch(SUCCEEDED(device.wrapper->Map(mapped_texture.Get(), 1, D3D11_MAP_READ, 0, &read)), "upload: actual readonly map");
    device.wrapper->Unmap(mapped_texture.Get(), 1);
    require_batch(snapshot(mapped_texture.Get()).resource_generation == 2, "upload: readonly map does not stamp generation");
    auto source = tracked(device, description(), true, 0x1234abcd), destination = tracked(device, description());
    device.wrapper->CopyResource(destination.Get(), source.Get());
    state = snapshot(destination.Get());
    require_batch(state.resource_generation == 1 && std::all_of(state.subresource_generations.begin(), state.subresource_generations.end(),
        [](uint64_t v) { return v == 1; }), "upload: real whole-resource copy propagates initialization");
    pixels(device, destination.Get(), 4, 0x1234abcd, "CopyResource mip");
    auto unknown = tracked(device, description());
    device.wrapper->CopySubresourceRegion(destination.Get(), 4, 0,0,0, unknown.Get(), 4, nullptr);
    require_batch(snapshot(destination.Get()).subresource_generations[4] == 0, "upload: copying unknown source clears destination certainty");
    device.wrapper->CopySubresourceRegion1(destination.Get(), 4, 0,0,0, source.Get(), 4, nullptr, D3D11_COPY_DISCARD);
    state = snapshot(destination.Get());
    require_batch(state.subresource_generations[4] != 0 && state.subresource_generations[0] == 0,
        "upload: Copy1 discard resets other subresources before known full copy");
    pixels(device, destination.Get(), 4, 0x1234abcd, "CopyRegion1 discard mip");
}

// Seed through native calls so the pixel oracle is deterministic without
// claiming these deliberately untracked fixture writes as tracked uploads.
void seed_mip_control(BatchDevice& device, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    for (UINT sub = 0; sub < desc.MipLevels * desc.ArraySize; ++sub) {
        const UINT width = (std::max)(1u, desc.Width >> (sub % desc.MipLevels));
        const UINT height = (std::max)(1u, desc.Height >> (sub % desc.MipLevels));
        std::vector<UINT> bytes(width * height, 0xff123456);
        device.context->UpdateSubresource(texture, sub, nullptr, bytes.data(), width * sizeof(UINT), 0);
    }
}
std::vector<UINT> mip_pixels(BatchDevice& device, ID3D11Texture2D* texture, UINT sub)
{
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    const UINT width = (std::max)(1u, desc.Width >> (sub % desc.MipLevels));
    const UINT height = (std::max)(1u, desc.Height >> (sub % desc.MipLevels));
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "mip control: actual staging texture");
    device.context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), sub, D3D11_MAP_READ, 0, &mapped)), "mip control: actual readback");
    std::vector<UINT> result(width * height);
    for (UINT y = 0; y < height; ++y)
        std::memcpy(result.data() + y * width, static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, width * sizeof(UINT));
    device.context->Unmap(staging.Get(), sub);
    return result;
}

void generation_and_mips(BatchDevice& device)
{
    auto desc = description(); desc.BindFlags |= D3D11_BIND_RENDER_TARGET; desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    auto texture = tracked(device, desc);
    seed_mip_control(device, texture.Get());
    ComPtr<ID3D11Texture2D> control;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &control)), "mip control: native-only same descriptor");
    seed_mip_control(device, control.Get());
    std::vector<UINT> native_source(desc.Width * desc.Height, 0xff556677);
    device.context->UpdateSubresource(control.Get(), 3, nullptr, native_source.data(), desc.Width * sizeof(UINT), 0);
    upload(*device.wrapper, texture.Get(), 3, 0xff556677);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
    view_desc.Format = desc.Format; view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view_desc.Texture2DArray.MostDetailedMip = 0; view_desc.Texture2DArray.MipLevels = 3;
    view_desc.Texture2DArray.FirstArraySlice = 1; view_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(texture.Get(), &view_desc, &view)), "upload: real restricted generation SRV");
    ComPtr<ID3D11ShaderResourceView> control_view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(control.Get(), &view_desc, &control_view)), "mip control: same native restricted SRV");
    device.context->GenerateMips(control_view.Get());
    pixels(device, texture.Get(), 3, 0xff556677, "Generate source before native generation");
    device.wrapper->GenerateMips(view.Get());
    auto state = snapshot(texture.Get());
    require_batch(state.resource_generation == 2 && state.subresource_generations[3] == 1 && state.subresource_generations[4] == 2 &&
        state.subresource_generations[5] == 2 && state.subresource_generations[0] == 0,
        "upload: generation initializes only selected lower mips/slices in one event");
    pixels(device, texture.Get(), 3, 0xff556677, "Generate source unchanged after native generation");
    // This WARP runtime fails nonzero-first-slice generation even for pure
    // native calls (debug-layer probe archived in native-mips-probe/result.log).
    // Require exact native equivalence here, never fake generated color data.
    bool native_generated = true;
    for (UINT sub : {4u, 5u}) {
        const auto expected = mip_pixels(device, control.Get(), sub);
        require_batch(mip_pixels(device, texture.Get(), sub) == expected,
            "upload: nonzero-first-slice generated bytes exactly match independent native control");
        native_generated &= std::all_of(expected.begin(), expected.end(), [](UINT value) { return value == 0xff556677; });
    }
    if (!native_generated)
        std::fprintf(stderr, "[LIMITATION] Native WARP nonzero-first-slice GenerateMips does not produce expected colors; wrapper matches native bytes, selected-slice metadata verified.\n");
    // Keep strict generated-color readback for supported slice-zero and whole
    // array views, including both slices when the whole array is selected.
    for (UINT slices : {1u, 2u}) {
        auto generated = tracked(device, desc);
        upload(*device.wrapper, generated.Get(), 0, 0xff112233);
        upload(*device.wrapper, generated.Get(), 3, 0xff556677);
        auto supported_desc = view_desc;
        supported_desc.Texture2DArray.FirstArraySlice = 0;
        supported_desc.Texture2DArray.ArraySize = slices;
        ComPtr<ID3D11ShaderResourceView> supported_view;
        require_batch(SUCCEEDED(device.native->CreateShaderResourceView(generated.Get(), &supported_desc, &supported_view)), "upload: supported native generation view");
        device.wrapper->GenerateMips(supported_view.Get());
        const auto generated_state = snapshot(generated.Get());
        require_batch(generated_state.resource_generation == 3 && generated_state.subresource_generations[1] == 3 &&
            generated_state.subresource_generations[2] == 3 && generated_state.subresource_generations[4] == (slices == 2 ? 3u : 0u),
            "upload: supported generation preserves strict selected-array metadata");
        pixels(device, generated.Get(), 1, 0xff112233, "Generate slice zero mip1");
        pixels(device, generated.Get(), 2, 0xff112233, "Generate slice zero mip2");
        if (slices == 2) {
            pixels(device, generated.Get(), 4, 0xff556677, "Generate whole array slice one mip1");
            pixels(device, generated.Get(), 5, 0xff556677, "Generate whole array slice one mip2");
        }
    }
    auto unsupported = tracked(device, description()); auto unsupported_view = srv(device, unsupported.Get());
    device.wrapper->GenerateMips(unsupported_view.Get());
    require_batch(snapshot(unsupported.Get()).resource_generation == 0, "upload: native GenerateMips missing flags remains no-op metadata");
    // A one-mip SRV is a native GenerateMips no-op, yet enters the tracker helper.
    // Native-only resource is intentionally absent from the real registry.
    ComPtr<ID3D11Texture2D> native_only;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &native_only)), "upload: native-only generation texture");
    auto one_desc = view_desc; one_desc.Texture2DArray.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> one_view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(native_only.Get(), &one_desc, &one_view)), "upload: native-only one-mip SRV");
    const ULONG refs = native_only->AddRef(); native_only->Release();
    for (unsigned i = 0; i < 32; ++i) device.wrapper->GenerateMips(one_view.Get());
    const ULONG after = native_only->AddRef(); native_only->Release();
    require_batch(after == refs, "upload: untracked GenerateMips early return releases acquired resource");
    ResourceHandleInfo direct;
    direct.InitializeSubresourceTracking(2, 1, true); direct.resource_generation = UINT64_MAX;
    direct.ProcessSubresourceUpdate(0, true, false);
    require_batch(direct.resource_generation == 1 && direct.GetSubresourceGeneration(0) == 1 && direct.GetSubresourceGeneration(1) == 0,
        "upload: overflow never treats initialized generation as reserved unknown zero");
}

void resolve_path(BatchDevice& device)
{
    auto multisampled = description(1,1); multisampled.BindFlags = D3D11_BIND_RENDER_TARGET;
    multisampled.SampleDesc.Count = 2;
    UINT quality = 0;
    require_batch(SUCCEEDED(device.native->CheckMultisampleQualityLevels(multisampled.Format, 2, &quality)) && quality,
        "upload: WARP supports actual 2x RGBA resolve");
    auto source = tracked(device, multisampled), destination = tracked(device, description(1,1), true);
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(source.Get(), nullptr, &view)), "upload: actual multisampled RTV");
    const float green[4] = {0,1,0,1}; device.context->ClearRenderTargetView(view.Get(), green);
    // Rendered pixels are defined, but upstream upload metadata intentionally
    // does not infer CPU-upload initialization from arbitrary rendering.
    device.wrapper->ResolveSubresource(destination.Get(), 0, source.Get(), 0, multisampled.Format);
    const auto state = snapshot(destination.Get());
    require_batch(state.resource_generation == 2 && state.subresource_generations[0] == 0,
        "upload: actual Resolve from upload-unknown MSAA source clears destination certainty");
    pixels(device, destination.Get(), 0, 0xff00ff00, "actual MSAA Resolve");
}

void copy_overlap_supported(BatchDevice& device)
{
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const bool supported = SUCCEEDED(device.native->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
        &options, sizeof(options))) && options.CopyWithOverlap;
    auto desc = description(1,1);
    std::vector<UINT> original(desc.Width * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        for (UINT x = 0; x < desc.Width; ++x) original[y * desc.Width + x] = 0xff000000u | y * 256u | x;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = original.data(); data.SysMemPitch = desc.Width * sizeof(UINT);
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, &data, &texture)), "upload overlap: actual gradient texture");
    const auto before = snapshot(texture.Get());
    D3D11_BOX source_box{0,0,0,7,4,1};
    device.wrapper->CopySubresourceRegion1(texture.Get(), 0, 1,0,0, texture.Get(), 0, &source_box, 0);
    auto after = snapshot(texture.Get());
    require_batch(after.resource_generation == before.resource_generation + (supported ? 1 : 0) &&
        after.subresource_generations[0] == after.resource_generation,
        "upload overlap: actual driver capability controls alias-copy generation without losing source initialization");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "upload overlap: actual staging texture");
    device.context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "upload overlap: actual pixel readback");
    bool equal = true;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < desc.Width; ++x) {
            const UINT source_x = supported && x ? x - 1 : x;
            equal &= row[x] == original[y * desc.Width + source_x];
        }
    }
    device.context->Unmap(staging.Get(), 0);
    require_batch(equal, "upload overlap: native scroll bytes (or documented unsupported dropped call) match metadata");
    std::fprintf(stderr, "upload overlap: actual CopyWithOverlap=%u; %s\n", supported ? 1u : 0u,
        supported ? "supported overlap branch exercised" : "only unsupported dropped-call branch exercised");
}

void deferred_commands(BatchDevice& device)
{
    // Independent native-only control for the deferred dynamic texture upload.
    {
        auto desc = description(1,1);
        desc.Usage = D3D11_USAGE_DYNAMIC; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Texture2D> control;
        ComPtr<ID3D11DeviceContext1> recorder;
        require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &control)) &&
            SUCCEEDED(device.native->CreateDeferredContext1(0, &recorder)), "deferred Map control: native objects");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require_batch(SUCCEEDED(recorder->Map(control.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)), "deferred Map control: native Map");
        std::fprintf(stderr, "deferred native Map control: pointer=%p rowPitch=%u depthPitch=%u\n", mapped.pData, mapped.RowPitch, mapped.DepthPitch);
        // Deferred WARP reports zero pitches for this explicit RGBA fixture.
        // Use its known tightly packed layout only in the test; the native
        // readback below independently verifies every pixel of that choice.
        const UINT control_pitch = mapped.RowPitch ? mapped.RowPitch : desc.Width * sizeof(UINT);
        for (UINT y = 0; y < desc.Height; ++y)
            std::memset(static_cast<unsigned char*>(mapped.pData) + y * control_pitch, 0x77, desc.Width * sizeof(UINT));
        recorder->Unmap(control.Get(), 0);
        recorder->ClearState();
        ComPtr<ID3D11CommandList> list;
        require_batch(SUCCEEDED(recorder->FinishCommandList(FALSE, &list)), "deferred Map control: native Finish");
        device.context->ExecuteCommandList(list.Get(), TRUE);
        pixels(device, control.Get(), 0, 0x77777777, "native-only deferred dynamic Map");
    }
    auto source = tracked(device, description(1,1)), destination = tracked(device, description(1,1));
    ComPtr<ID3D11CommandList> commands;
    {
        ComPtr<ID3D11DeviceContext1> native;
        require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &native)), "upload: actual deferred context");
        HackerContext deferred(device.native.Get(), native.Get()); deferred.SetHackerDevice(device.device);
        deferred.CopyResource(destination.Get(), source.Get());
        require_batch(snapshot(destination.Get()).resource_generation == 0, "upload: recording copy publishes nothing");
        require_batch(SUCCEEDED(deferred.FinishCommandList(TRUE, &commands)), "upload: actual Finish seals immutable metadata journal");
        require_batch(snapshot(destination.Get()).resource_generation == 0, "upload: Finish without Execute publishes nothing");
        // Source initialization changes after recording and is observed only at execution.
        upload(*device.wrapper, source.Get(), 0, 0x11223344);
    }
    device.wrapper->ExecuteCommandList(commands.Get(), TRUE);
    require_batch(snapshot(destination.Get()).resource_generation == 1 && snapshot(destination.Get()).subresource_generations[0] == 1,
        "upload: execution after recording context destruction reads current source initialization");
    pixels(device, destination.Get(), 0, 0x11223344, "deferred source change after recording");
    device.wrapper->ExecuteCommandList(commands.Get(), FALSE);
    require_batch(snapshot(destination.Get()).resource_generation == 2, "upload: replay is a distinct generation event");

    // Real deferred dynamic Map records an upload only when its list executes.
    auto dynamic_desc = description(1,1); dynamic_desc.Usage = D3D11_USAGE_DYNAMIC;
    dynamic_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    auto dynamic_texture = tracked(device, dynamic_desc);
    ComPtr<ID3D11DeviceContext1> native;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &native)), "upload: second actual deferred context");
    HackerContext deferred(device.native.Get(), native.Get()); deferred.SetHackerDevice(device.device);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(deferred.Map(dynamic_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)), "upload: native deferred texture Map");
    std::fprintf(stderr, "deferred wrapped Map: pointer=%p rowPitch=%u depthPitch=%u\n", mapped.pData, mapped.RowPitch, mapped.DepthPitch);
    const UINT fixture_pitch = mapped.RowPitch ? mapped.RowPitch : dynamic_desc.Width * sizeof(UINT);
    for (UINT y = 0; y < dynamic_desc.Height; ++y)
        std::memset(static_cast<unsigned char*>(mapped.pData) + y * fixture_pitch, 0x77, dynamic_desc.Width * sizeof(UINT));
    require_batch(*static_cast<const UINT*>(mapped.pData) == 0x77777777, "deferred Map: actual mapped bytes filled before Unmap");
    deferred.Unmap(dynamic_texture.Get(), 0);
    require_batch(snapshot(dynamic_texture.Get()).resource_generation == 0, "upload: deferred Unmap does not publish globally");
    upload(deferred, source.Get(), 0, 0x55667788);
    deferred.ClearState(); // Pipeline state reset must not discard recorded resource actions.
    deferred.CopyResource(destination.Get(), source.Get());
    ComPtr<ID3D11CommandList> ordered;
    require_batch(SUCCEEDED(deferred.FinishCommandList(FALSE, &ordered)), "upload: seal ordered update then copy");
    const auto before = snapshot(destination.Get()).resource_generation;
    G->deferred_contexts_enabled = false; device.wrapper->ExecuteCommandList(ordered.Get(), TRUE);
    require_batch(snapshot(destination.Get()).resource_generation == before, "upload: gated native execution does not publish metadata");
    G->deferred_contexts_enabled = true; device.wrapper->ExecuteCommandList(ordered.Get(), TRUE);
    require_batch(snapshot(source.Get()).resource_generation == 2 && snapshot(destination.Get()).resource_generation == before+1,
        "upload: actual update then copy replay preserves semantic event order");
    require_batch(snapshot(dynamic_texture.Get()).resource_generation == 1, "upload: executed deferred Map publishes upload");
    pixels(device, dynamic_texture.Get(), 0, 0x77777777, "deferred dynamic Map");
    pixels(device, destination.Get(), 0, 0x55667788, "deferred ordered Update Copy");
    ComPtr<ID3D11CommandList> empty;
    require_batch(SUCCEEDED(deferred.FinishCommandList(TRUE, &empty)), "upload: next recording may be empty");
    device.wrapper->ExecuteCommandList(empty.Get(), TRUE);
    require_batch(snapshot(destination.Get()).resource_generation == before+1, "upload: next Finish does not duplicate previous actions");

    // Deferred Execute merges journals; publication is still deferred until immediate submission.
    deferred.ExecuteCommandList(commands.Get(), TRUE);
    ComPtr<ID3D11CommandList> merged;
    require_batch(SUCCEEDED(deferred.FinishCommandList(TRUE, &merged)), "upload: seal merged native command list");
    require_batch(snapshot(destination.Get()).resource_generation == before+1, "upload: deferred Execute remains recording only");
    device.wrapper->ExecuteCommandList(merged.Get(), TRUE);
    require_batch(snapshot(destination.Get()).resource_generation == before+2, "upload: merged list replays child metadata exactly once");

    ComPtr<ID3D11CommandList> untracked;
    native->CopyResource(destination.Get(), source.Get());
    require_batch(SUCCEEDED(native->FinishCommandList(TRUE, &untracked)), "upload: real unwrapped command list");
    device.wrapper->ExecuteCommandList(untracked.Get(), TRUE);
    require_batch(snapshot(destination.Get()).subresource_generations[0] == 0, "upload: unknown submission invalidates certainty conservatively");
    upload(*device.wrapper, destination.Get(), 0, 0x12345678);
    require_batch(snapshot(destination.Get()).subresource_generations[0] != 0,
        "upload: known later upload recovers after unknown submission without permanent disable");
}
} // namespace

void test_dx11_texture_upload_state_regression()
{
    UploadGlobals globals;
    BatchDevice device;
    creation_and_updates(device);
    maps_and_copies(device);
    copy_overlap_supported(device);
    generation_and_mips(device);
    resolve_path(device);
    deferred_commands(device);
}
