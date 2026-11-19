#include "slot_range_test_support.h"
#include "ResourceHash.h"
#include "ShaderBindings.h"
#include "lock.h"
#include <d3dcompiler.h>

namespace {
using namespace dx11_slot_test;

ComPtr<ID3D11Texture2D> match_texture(BatchDevice& device, UINT color)
{
    std::vector<UINT> pixels(9 * 8, color);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 9; desc.Height = 8; desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = { pixels.data(), 9 * sizeof(UINT), 0 };
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, &data, &result)), "MatchTextures: real tracked texture");
    return result;
}

class MatchShader {
    BatchDevice& device;
    ComPtr<ID3D11PixelShader> pixel;
public:
    explicit MatchShader(BatchDevice& owner, bool empty = false) : device(owner)
    {
        const char* text = empty ? "float4 main():SV_Target{return 0;}" : "Texture2D a:register(t0);Texture2D b:register(t2);Texture2D c:register(t5);Texture2D d:register(t127);"
            "float4 main():SV_Target{return a.Load(int3(0,0,0))+b.Load(int3(0,0,0))+c.Load(int3(0,0,0))+d.Load(int3(0,0,0));}";
        ComPtr<ID3DBlob> code, errors;
        require_batch(SUCCEEDED(D3DCompile(text, strlen(text), "MatchTextures", nullptr, nullptr, "main", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &code, &errors)), "MatchTextures: compile actual sparse PS");
        require_batch(SUCCEEDED(device.device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel)),
            "MatchTextures: register actual shader and production bindings");
        Bind();
    }
    void Bind() { device.wrapper->PSSetShader(pixel.Get(), nullptr, 0); }
    ~MatchShader() { device.wrapper->PSSetShader(nullptr, nullptr, 0); }
};

void slot_is(BatchDevice& device, unsigned slot, ID3D11ShaderResourceView* expected, const char* message)
{
    ComPtr<ID3D11ShaderResourceView> actual;
    device.context->PSGetShaderResources(slot, 1, &actual);
    require_batch(actual.Get() == expected, message);
}

void builtin(CommandList& pre, CommandList& post, const wchar_t* value)
{
    std::wstring text(value), ns;
    require_batch(ParseCommandListGeneralCommands(L"CommandListMatchTest", L"run", &text, nullptr, &pre, &post, &ns),
        "MatchTextures: production builtin parser");
}

void draw_gate_reuse(BatchDevice& device)
{
    auto miss = match_texture(device, 0xff112233);
    auto hit = match_texture(device, 0xff334455);
    auto miss_view = srv(device, miss.Get());
    auto hit_view = srv(device, hit.Get());
    const auto hit_hash = GetResourceHash(hit.Get());
    require_batch(hit_hash && hit_hash != GetResourceHash(miss.Get()) &&
        !G->mTextureOverrideMap.count(hit_hash), "draw proof: distinct tracked resources");
    auto old_ranges = G->draw_override_slot_ranges;
    G->draw_override_slot_ranges = {{{ResourceCopyTargetType::SHADER_RESOURCE, 14, L'p'}, 1}};
    ++G->draw_override_slot_generation;
    G->mTextureOverrideMap[hit_hash].resize(1);
    ShaderResourceBatchStats stats;
    device.state.slot_batch_stats = &stats;
    const std::wstring text = L"draw_has_override", ns;
    CommandListScope scope;
    CommandListExpression gate;
    require_batch(gate.parse(&text, &ns, &scope), "draw proof: parse production predicate");
    const auto evaluate = [&] {
        DrawCallInfo info;
        info.type = DrawCall::DrawIndexed;
        device.state.call_info = &info;
        const auto value = gate.evaluate(&device.state);
        device.state.call_info = nullptr;
        return value != 0;
    };

    set_slot(device, L'p', 14, miss_view.Get());
    require_batch(!evaluate() && !evaluate() && stats.draw_gate_reuses == 1,
        "draw proof: a second draw reuses a verified empty bound range");
    set_slot(device, L'p', 14, hit_view.Get());
    require_batch(evaluate() && stats.draw_gate_reuses == 1,
        "draw proof: a native SRV rebind invalidates the cached miss");
    set_slot(device, L'p', 14, miss_view.Get());
    require_batch(!evaluate() && !evaluate() && stats.draw_gate_reuses == 2,
        "draw proof: the cache can warm again after a rebind");
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        G->mResources.at(miss.Get()).hash = hit_hash;
    }
    require_batch(evaluate() && stats.draw_gate_reuses == 2,
        "draw proof: resource metadata changes invalidate a miss without rebinding");
    set_slot(device, L'p', 14, nullptr);
    device.state.slot_batch_stats = nullptr;
    InvalidateTextureOverrideCandidates();
    G->mTextureOverrideMap.erase(hit_hash);
    G->draw_override_slot_ranges = std::move(old_ranges);
    ++G->draw_override_slot_generation;
}

void exercise_match(bool hooked = false)
{
    BatchDevice device;
    if (hooked) {
        G->enable_hooks = EnableHooks::ALL;
        device.device->HookDevice(); device.wrapper->HookContext();
        device.hooked_lifetime = true;
        device.state.mOrigDevice1 = device.device->GetPassThroughOrigDevice1();
        device.state.mOrigContext1 = device.wrapper->GetPassThroughOrigContext1();
        draw_gate_reuse(device);
    }
    Registry registry;
    const int savedMip = G->dhash_mip_size;
    G->dhash_mip_size = 0;
    MatchShader shader(device);
    auto original = match_texture(device, 0xff102030);
    auto other = match_texture(device, 0xff807060);
    auto replacement1 = match_texture(device, 0xffabcdef);
    auto replacement2 = match_texture(device, 0xff123456);
    auto originalView = srv(device, original.Get());
    auto otherView = srv(device, other.Get());
    auto firstView = srv(device, replacement1.Get());
    auto secondView = srv(device, replacement2.Get());
    uint64_t originalHash = 0;
    require_batch(GetDhash(device.wrapper.get(), original.Get(), 0, false, originalHash, originalView.Get()) && originalHash != 0,
        "MatchTextures: compute production GPU/flat fallback reference hash");
    auto& filter = registry.resource(L"resourcematchfilter", device);
    filter.match_dhash = originalHash; filter.match_dhash_set = true; filter.match_confidence = 1;
    filter.match_format = DXGI_FORMAT_UNKNOWN;
    auto& first = registry.resource(L"resourcematchfirst", device, replacement1.Get());
    first.view = firstView.Get(); first.view->AddRef();
    auto& second = registry.resource(L"resourcematchsecond", device, replacement2.Get());
    second.view = secondView.Get(); second.view->AddRef();
    registry.resource(L"resourcematchnull", device);
    CommandList one, two, failedSource, copied;
    require_batch(parse(one, L"ps->matchtextures(resourcematchfilter)", L"ref resourcematchfirst") &&
        parse(two, L"ps->matchtextures(resourcematchfilter)", L"ref resourcematchsecond") &&
        parse(failedSource, L"ps->matchtextures(resourcematchfilter)", L"ref resourcematchnull") &&
        parse(copied, L"ps->matchtextures(resourcematchfilter)", L"copy resourcematchfirst"),
        "MatchTextures: actual PS member assignment accepts default/ref/copy sources");
    CommandList rejected;
    require_batch(!parse(rejected, L"vs->matchtextures(resourcematchfilter)", L"ref resourcematchfirst") &&
        !parse(rejected, L"ps->matchtextures(ps-t0)", L"ref resourcematchfirst") &&
        !parse(rejected, L"ps->matchtextures(resourcematchfilter,1)", L"ref resourcematchfirst"),
        "MatchTextures: invalid stage/candidate/argument syntax rejected");
    CommandList batchBarrier;
    require_batch(parse(batchBarrier, L"ps->matchtextures(resourcematchfilter)", L"ref resourcematchfirst") &&
        parse(batchBarrier, L"ps->matchtextures(resourcematchfilter)", L"ref resourcematchsecond"), "MatchTextures: parse candidate sequence");
    merge_shader_resource_batches(&batchBarrier);
    require_batch(batchBarrier.commands.size() == 2 && dynamic_cast<TextureOverrideOperation*>(batchBarrier.commands[0].get()),
        "MatchTextures: local batching preserves candidate order and runtime matching");
    CommandList pre, post;
    builtin(pre, post, L"snapshotoriginaltextures"); builtin(pre, post, L"restoreoriginaltextures");
    require_batch(pre.commands.size() == 1 && post.commands.size() == 1,
        "MatchTextures: snapshot defaults to pre and restore to post");
    auto bindOriginals = [&] {
        device.wrapper->ResetCallCounters(); shader.Bind();
        set_slot(device, L'p', 0, originalView.Get()); set_slot(device, L'p', 2, originalView.Get());
        set_slot(device, L'p', 5, nullptr); set_slot(device, L'p', 127, otherView.Get());
        set_slot(device, L'p', 1, secondView.Get()); // Undeclared gap must survive all operations.
    };
    bindOriginals();
    auto slotFilter = std::make_shared<ResourceFilter>();
    slotFilter->metadata = {DXGI_FORMAT_UNKNOWN, originalHash, 1, true, true};
    slotFilter->empty = false;
    require_batch(resourceFilters.emplace(L"filtermatchfixture", slotFilter).second, "Slot filter: reserve test name");
    CommandList slotMatch, initialPre, initialPost;
    require_batch(parse(slotMatch, L"ps-t[filtermatchfixture]", L"ref resourcematchfirst") &&
        !parse(rejected, L"vs-t[filtermatchfixture]", L"ref resourcematchfirst") &&
        !parse(rejected, L"ps-t[filtermatchfixture]", L"copy oD reuse_unchanged"),
        "Slot filter: parse the PS destination and reject unsupported stage and copy options");
    builtin(initialPre, initialPost, L"snapshotinitialtextures");
    builtin(initialPre, initialPost, L"restoreinitialtextures");
    execute(initialPre, device);
    execute(slotMatch, device);
    slot_is(device, 0, firstView.Get(), "Slot filter: matching PS slot is replaced");
    slot_is(device, 2, firstView.Get(), "Slot filter: tied slot is replaced");
    slot_is(device, 127, otherView.Get(), "Slot filter: unmatched slot is unchanged");
    execute(initialPost, device);
    slot_is(device, 0, originalView.Get(), "Slot filter: new restore command restores original view");
    const std::wstring expression = L"ps-t0->hammingdistance(filtermatchfixture)", nsFilter;
    CommandListScope scopeFilter;
    scopeFilter.emplace_front();
    CommandListExpression distance;
    require_batch(distance.parse(&expression, &nsFilter, &scopeFilter) && distance.evaluate(&device.state) == 0,
        "Slot filter: HammingDistance uses the exact Filter hash");
    resourceFilters.erase(L"filtermatchfixture");

    execute(failedSource, device);
    require_batch(device.wrapper->GetTextureOverrideCache().hamming_distance[0] == 65,
        "MatchTextures: unresolved replacement cannot claim a best match");
    execute(one, device);
    slot_is(device, 0, firstView.Get(), "MatchTextures: automatic snapshot replaces best slot");
    slot_is(device, 2, firstView.Get(), "MatchTextures: all equal-best original slots replaced");
    slot_is(device, 127, otherView.Get(), "MatchTextures: worse original is not replaced");
    execute(pre, device); execute(two, device);
    slot_is(device, 0, firstView.Get(), "MatchTextures: repeated snapshot preserves strict tie-first-wins priority");
    set_slot(device, L'p', 5, firstView.Get());
    execute(post, device);
    slot_is(device, 0, originalView.Get(), "MatchTextures: exact original view restored");
    slot_is(device, 2, originalView.Get(), "MatchTextures: tied slot restored");
    slot_is(device, 5, nullptr, "MatchTextures: originally-null declared slot restored");
    slot_is(device, 1, secondView.Get(), "MatchTextures: undeclared gap unaffected");
    require_batch(device.wrapper->GetTextureOverrideCache().hamming_distance[0] == 65,
        "MatchTextures: restore resets candidate competition");
    execute(two, device); slot_is(device, 0, secondView.Get(), "MatchTextures: competition restarts after restore");

    bindOriginals(); set_slot(device, L'p', 127, originalView.Get());
    execute(one, device);
    slot_is(device, 127, firstView.Get(), "MatchTextures: highest legal slot participates in best matches");
    execute(post, device); slot_is(device, 127, originalView.Get(), "MatchTextures: highest slot restored exactly");

    bindOriginals(); filter.match_dhash = originalHash ^ 1; filter.match_confidence = 1;
    execute(one, device); slot_is(device, 0, originalView.Get(), "MatchTextures: confidence 1 requires exact hash");
    filter.match_confidence = 63.0f/64.0f;
    execute(one, device); slot_is(device, 0, firstView.Get(), "MatchTextures: inclusive one-bit confidence boundary");
    filter.match_dhash = originalHash; execute(two, device);
    slot_is(device, 0, secondView.Get(), "MatchTextures: better candidate wins against original, not prior replacement");

    bindOriginals(); filter.match_format = DXGI_FORMAT_R32_UINT;
    execute(one, device); slot_is(device, 0, originalView.Get(), "MatchTextures: format mismatch rejected");
    filter.match_format = static_cast<DXGI_FORMAT>(-1); // Legacy omitted sentinel is also wildcard.
    execute(one, device); slot_is(device, 0, firstView.Get(), "MatchTextures: omitted legacy format remains wildcard");
    bindOriginals(); filter.match_format_valid = false;
    execute(one, device); slot_is(device, 0, originalView.Get(), "MatchTextures: malformed format is not a wildcard");
    filter.match_format_valid = true; filter.match_dhash_set = false;
    execute(one, device); slot_is(device, 0, originalView.Get(), "MatchTextures: absent hash is not a valid zero hash");
    filter.match_dhash_set = true;
    execute(copied, device);
    ComPtr<ID3D11ShaderResourceView> copiedView;
    device.context->PSGetShaderResources(0, 1, &copiedView);
    ComPtr<ID3D11Resource> copiedTexture;
    require_batch(copiedView != nullptr && copiedView.Get() != firstView.Get(), "MatchTextures: copy creates independent replacement view");
    copiedView->GetResource(&copiedTexture);
    require_pixels(device, copiedTexture.Get(), 0xffabcdef);

    bindOriginals(); execute(pre, device);
    device.wrapper->ResetCallCounters();
    set_slot(device, L'p', 0, secondView.Get());
    execute(post, device);
    slot_is(device, 0, secondView.Get(), "MatchTextures: prior-frame counter collision cannot restore stale snapshot");
    execute(pre, device); InvalidateTextureMatchSnapshots();
    set_slot(device, L'p', 0, firstView.Get()); execute(post, device);
    slot_is(device, 0, firstView.Get(), "MatchTextures: reload invalidates snapshot before restore");
    execute(pre, device); device.wrapper->ClearState();
    require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: ClearState releases snapshots");
    shader.Bind();
    // No shader => no capture, but later binding in the same call can retry.
    device.wrapper->PSSetShader(nullptr, nullptr, 0); execute(pre, device);
    require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: missing shader safely declines snapshot");
    shader.Bind(); set_slot(device, L'p', 0, originalView.Get()); execute(pre, device);
    require_batch(device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: missing-shader failure can retry");
    device.wrapper->GetTextureOverrideCache().Clear();

    bindOriginals();
    const auto shaderIt = lookup_shader_hash(device.wrapper->mCurrentPixelShaderHandle);
    require_batch(shaderIt != G->mShaders.end(), "MatchTextures: actual registered shader hash");
    const auto shaderHash = shaderIt->second;
    require_batch(!G->mShaderOverrideMap.count(shaderHash), "MatchTextures: exclusive shader override fixture");
    auto& override = G->mShaderOverrideMap[shaderHash];
    override.command_list.commands = one.commands;
    override.post_command_list.commands = post.commands;
    std::wstring skip = L"skip", ns;
    require_batch(ParseCommandListGeneralCommands(L"ShaderOverrideMatchTest", L"handling", &skip, nullptr,
        &override.command_list, &override.post_command_list, &ns), "MatchTextures: actual skip parser");
    // Invoke the real public draw entry; production handling=skip avoids a GPU draw without a VS/RTV.
    device.wrapper->Draw(3, 0);
    slot_is(device, 0, originalView.Get(), "MatchTextures: real draw post list restores original binding");
    require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: real AfterDraw releases original views");
    G->mShaderOverrideMap.erase(shaderHash);
    execute(pre, device); device.wrapper->Dispatch(0, 0, 0);
    require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: dispatch lifecycle releases snapshots");

    bindOriginals();
    device.context->PSSetShader(nullptr, nullptr, 0);
    execute(pre, device);
    require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: actually unbound PS is not captured from stale metadata");
    shader.Bind();
    {
        MatchShader empty(device, true);
        execute(pre, device);
        require_batch(!device.wrapper->GetTextureOverrideCache().snapshot_active, "MatchTextures: shader without declared resources safely declines capture");
    }
    shader.Bind();
    {
        ComPtr<ID3D11DeviceContext> deferredBase;
        ComPtr<ID3D11DeviceContext1> deferredNative;
        require_batch(SUCCEEDED(device.device->GetPassThroughOrigDevice1()->CreateDeferredContext(0, &deferredBase)) &&
            SUCCEEDED(deferredBase.As(&deferredNative)), "MatchTextures: actual deferred context");
        HackerContext deferred(device.native.Get(), deferredNative.Get());
        deferred.SetHackerDevice(device.device);
        CommandListState deferredState;
        deferredState.mHackerDevice = device.device; deferredState.mHackerContext = &deferred;
        deferredState.mOrigDevice1 = device.state.mOrigDevice1; deferredState.mOrigContext1 = deferredNative.Get();
        pre.commands.front()->run(&deferredState);
        require_batch(!deferred.GetTextureOverrideCache().snapshot_active, "MatchTextures: deferred context avoids unsupported SRV getter/dHash operations");
    }

    auto black = match_texture(device, 0);
    auto blackView = srv(device, black.Get());
    bindOriginals(); set_slot(device, L'p', 0, blackView.Get()); set_slot(device, L'p', 2, nullptr);
    filter.match_dhash = 0;
    execute(one, device); slot_is(device, 0, firstView.Get(), "MatchTextures: explicit zero hash can match real black texture");
    execute(post, device);
    device.wrapper->GetTextureOverrideCache().Clear();
    require_batch(!device.wrapper->GetTextureOverrideCache().original_textures[0], "MatchTextures: explicit release clears retained COM views");
    G->dhash_mip_size = savedMip;
}
}

void test_dx11_texture_match_regression()
{
    exercise_match();
    std::puts("PASS: real MatchTextures parser/GPU matching, confidence, exact restore, sparse/null/high slots, copy/ref, failures and invalidation");
}

// Executed only in its own CTest process: native method hooks persist for process lifetime.
void test_dx11_texture_match_hooked_regression()
{
    exercise_match(true);
    std::puts("PASS: real MatchTextures through native device/context hooks and trampoline identity");
}
