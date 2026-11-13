#include "ResourceHash.h"
#include "HackerDevice.h"
#include "HackerContext.h"
#include "IniHandler.h"
#include "Input.h"
#include "globals.h"
#include "cache_policy_test_scope.h"
#include "lock.h"
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C" const GUID ResourceReleaseTrackerGuid;

namespace {
using Microsoft::WRL::ComPtr;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class CacheDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> immediate;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> context;
    ComPtr<ID3D11Texture2D> texture, untracked;
    ComPtr<ID3D11Texture1D> texture1;
    ComPtr<ID3D11Texture3D> texture3;
    ComPtr<ID3D11Buffer> buffer;
    D3D11_TEXTURE2D_DESC desc{};
    CacheDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(runtime != nullptr, "load native cache WARP runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve actual WARP creation");
        ComPtr<ID3D11Device> base; ComPtr<ID3D11DeviceContext> raw;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &base, nullptr, &raw)) && SUCCEEDED(base.As(&native)) && SUCCEEDED(raw.As(&immediate)), "create cache D3D11.1 interfaces");
        device = new HackerDevice(native.Get(), immediate.Get()); native->AddRef();
        context = std::make_unique<HackerContext>(native.Get(), immediate.Get());
        context->SetHackerDevice(device); device->SetHackerContext(context.get()); device->SetHackerSwapChain(nullptr);
        desc.Width = 64; desc.Height = 32; desc.ArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "actual wrapper registers tracked 2D texture");
        check(SUCCEEDED(native->CreateTexture2D(&desc, nullptr, &untracked)), "native texture intentionally follows untracked fallback");
        D3D11_TEXTURE1D_DESC one{}; one.Width = 16; one.ArraySize = one.MipLevels = 1;
        one.Format = desc.Format; one.BindFlags = desc.BindFlags;
        check(SUCCEEDED(device->CreateTexture1D(&one, nullptr, &texture1)), "actual tracked 1D texture");
        D3D11_TEXTURE3D_DESC three{}; three.Width = three.Height = three.Depth = 4; three.MipLevels = 1;
        three.Format = desc.Format; three.BindFlags = desc.BindFlags;
        check(SUCCEEDED(device->CreateTexture3D(&three, nullptr, &texture3)), "actual tracked 3D texture");
        const float values[4] = {1, 2, 3, 4};
        D3D11_BUFFER_DESC bytes{}; bytes.ByteWidth = sizeof(values); bytes.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = values;
        check(SUCCEEDED(device->CreateBuffer(&bytes, &initial, &buffer)), "actual tracked buffer with region data");
    }
    ~CacheDevice()
    {
        context->ClearState(); buffer.Reset(); texture3.Reset(); texture1.Reset(); texture.Reset(); untracked.Reset();
        context.reset(); immediate.Reset(); native.Reset(); device->Release(); FreeLibrary(runtime);
    }
};
class CacheConfiguration {
public:
    const Profiling::Mode mode = Profiling::mode.load();
    const decltype(G->hunting) hunting = G->hunting;
    const decltype(G->mResolutionInfo) resolution = G->mResolutionInfo;
    bool region = G->track_region_hashes;
    DataCacheTestScope cache_policy;
    ResourceInfoMap resource_info = G->mResourceInfo;
    std::shared_ptr<FuzzyMatchResourceDesc> low, draw, dynamic;
    CacheConfiguration()
    {
        check(G->mTextureOverrideMap.empty() && G->mFuzzyTextureOverrides.empty(), "cache fixture owns empty override registries");
        G->hunting = HUNTING_MODE_ENABLED; G->track_region_hashes = true;
    }
    ~CacheConfiguration()
    {
        InvalidateTextureOverrideCandidates();
        G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
        G->mResourceInfo = std::move(resource_info); G->hunting = hunting; G->mResolutionInfo = resolution;
        G->track_region_hashes = region; Profiling::mode = mode; Profiling::clear();
    }
    void populate()
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
        auto& zero = G->mTextureOverrideMap[0]; zero.resize(2);
        zero[0].ini_section = L"exact-low"; zero[0].priority = 0;
        zero[1].ini_section = L"exact-draw"; zero[1].priority = 5; zero[1].has_draw_context_match = true;
        zero[1].match_index_count.op = FuzzyMatchOp::EQUAL; zero[1].match_index_count.val = 3;
        auto& maximum = G->mTextureOverrideMap[UINT32_MAX]; maximum.resize(1); maximum[0].ini_section = L"exact-max";
        low = std::make_shared<FuzzyMatchResourceDesc>(L"fuzzy-low");
        low->Width.op = FuzzyMatchOp::EQUAL; low->Width.val = 64; low->texture_override->priority = 1;
        draw = std::make_shared<FuzzyMatchResourceDesc>(L"fuzzy-draw");
        draw->Width.op = FuzzyMatchOp::EQUAL; draw->Width.val = 64; draw->texture_override->priority = 6;
        draw->texture_override->has_draw_context_match = true;
        draw->texture_override->match_index_count.op = FuzzyMatchOp::EQUAL; draw->texture_override->match_index_count.val = 3;
        dynamic = std::make_shared<FuzzyMatchResourceDesc>(L"fuzzy-resolution");
        dynamic->Width.op = FuzzyMatchOp::EQUAL; dynamic->Width.rhs_type1 = FuzzyMatchOperandType::RES_WIDTH;
        dynamic->texture_override->priority = 9;
        G->mFuzzyTextureOverrides.insert(low); G->mFuzzyTextureOverrides.insert(draw); G->mFuzzyTextureOverrides.insert(dynamic);
        G->mResolutionInfo.width = 64; G->mResolutionInfo.height = 32;
    }
};
void set_hash(ID3D11Resource* resource, uint32_t hash)
{
    CriticalSectionLockGuard state_lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
    CriticalSectionLockGuard resources_lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
    G->mResources.at(resource).hash = hash;
}
std::vector<std::wstring> lookup(ID3D11Resource* resource, unsigned indices = 3)
{
    CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
    DrawCallInfo info; info.IndexCount = indices;
    TextureOverrideMatches matches; find_texture_overrides_for_resource(resource, &matches, &info);
    std::vector<std::wstring> result;
    for (const auto* match : matches) result.push_back(match->ini_section);
    return result;
}
void verify_lookup(CacheDevice& device, CacheConfiguration& config)
{
    config.populate(); set_hash(device.texture.Get(), 0); Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear();
    check(lookup(device.texture.Get()) == std::vector<std::wstring>({L"exact-low", L"exact-draw", L"fuzzy-low", L"fuzzy-draw", L"fuzzy-resolution"}),
        "exact hash zero then fuzzy priority ordering both contribute");
    check(Profiling::texture_override_fuzzy_match_overhead.count == 1 && Profiling::texture_override_candidates_lookup_overhead.count == 1,
        "first tracked lookup rebuilds once and records lookup once");
    check(lookup(device.texture.Get(), 7) == std::vector<std::wstring>({L"exact-low", L"fuzzy-low", L"fuzzy-resolution"}) &&
        Profiling::texture_override_fuzzy_match_overhead.count == 1, "draw info is filtered every time without descriptor rescan");
    { CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__); G->mResolutionInfo.width = 128; }
    check(lookup(device.texture.Get(), 7) == std::vector<std::wstring>({L"exact-low", L"fuzzy-low"}) &&
        Profiling::texture_override_fuzzy_match_overhead.count == 2, "resolution participates in cache key and dynamic matcher remains current");
    set_hash(device.texture.Get(), UINT32_MAX);
    check(lookup(device.texture.Get(), 7) == std::vector<std::wstring>({L"exact-max", L"fuzzy-low"}), "hash UINT32_MAX rebuild uses validity bit instead of overflowing sentinel");
    set_hash(device.texture.Get(), 0);
    check(lookup(device.texture.Get(), 7) == std::vector<std::wstring>({L"exact-low", L"fuzzy-low"}), "returning to zero invalidates exact candidates");
    const auto before = Profiling::texture_override_fuzzy_match_overhead.count;
    const auto lookups_before = Profiling::texture_override_candidates_lookup_overhead.count;
    const auto hits_before_untracked = Profiling::texture_override_candidates_lookup_overhead.hits;
    check(lookup(device.untracked.Get(), 7).empty() &&
        Profiling::texture_override_fuzzy_match_overhead.count == before &&
        Profiling::texture_override_candidates_lookup_overhead.count == lookups_before + 1 &&
        Profiling::texture_override_candidates_lookup_overhead.hits == hits_before_untracked,
        "untracked native resource matches nothing, does no descriptor scan, and counts lookup without hit");
    TextureOverrideMatches preserved{config.low->texture_override};
    DrawCallInfo untracked_info; untracked_info.IndexCount = 7;
    find_texture_overrides_for_resource(device.untracked.Get(), &preserved, &untracked_info);
    find_fuzzy_texture_overrides_for_resource(device.untracked.Get(), &preserved, &untracked_info);
    check(preserved == TextureOverrideMatches{config.low->texture_override} &&
        Profiling::texture_override_candidates_lookup_overhead.count == lookups_before + 3 &&
        Profiling::texture_override_candidates_lookup_overhead.hits == hits_before_untracked &&
        Profiling::texture_override_fuzzy_match_overhead.count == before,
        "both untracked entry points preserve existing output and accurate miss counts without descriptor scans");
    DrawCallInfo miss; miss.IndexCount = 7;
    TextureOverrideMatches preset{config.low->texture_override};
    const auto hits_before = Profiling::texture_override_fuzzy_match_overhead.hits;
    const auto scans_before = Profiling::texture_override_fuzzy_match_overhead.count;
    find_fuzzy_texture_overrides_for_resource(device.texture1.Get(), &preset, &miss);
    check(Profiling::texture_override_fuzzy_match_overhead.hits == hits_before &&
        Profiling::texture_override_fuzzy_match_overhead.count == scans_before + 1,
        "an existing output match is not counted as a new fuzzy hit");
    const auto count = Profiling::texture_override_candidates_lookup_overhead.count;
    Profiling::mode = Profiling::Mode::NONE; lookup(device.texture.Get());
    check(Profiling::texture_override_candidates_lookup_overhead.count == count, "NONE lookups do not add SUMMARY samples");
}
void verify_contamination_and_regions(CacheDevice& device)
{
    Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear();
    MarkResourceHashContaminated(device.buffer.Get(), 0, nullptr, 0, 'U', 0, 0, 0, nullptr);
    MarkResourceHashContaminated(device.texture1.Get(), 0, nullptr, 0, 'U', 0, 0, 0, nullptr);
    check(Profiling::hash_tracking_overhead.count == 2 && Profiling::hash_tracking_overhead.hits == 0,
        "buffer/1D early returns are counted without fake contamination hits");
    const auto original = GetOrigResourceHash(device.texture.Get());
    G->mResourceInfo[original] = device.desc;
    MarkResourceHashContaminated(device.texture.Get(), 0, nullptr, 0, 'U', 0, 0, 0, nullptr);
    check(G->mResourceInfo.at(original).update_contamination.count(0) == 1 && Profiling::hash_tracking_overhead.hits == 1,
        "tracked 2D update contamination retains original semantics");
    MarkResourceHashContaminated(device.texture3.Get(), 0, nullptr, 0, 'U', 0, 0, 0, nullptr);
    check(Profiling::hash_tracking_overhead.hits == 2, "tracked 3D remains supported");
    const auto first = GetRegionHash(device.context.get(), device.buffer.Get(), 0, sizeof(float));
    const float changed[4] = {9, 2, 3, 4};
    device.context->UpdateSubresource(device.buffer.Get(), 0, nullptr, changed, 0, 0);
    const auto second = GetRegionHash(device.context.get(), device.buffer.Get(), 0, sizeof(float));
    check(first != 0 && second != 0 && first != second, "texture contamination early return does not bypass actual buffer region invalidation");
}
void verify_concurrent_region_cache(CacheDevice& device)
{
    const auto expected = GetRegionHash(device.context.get(), device.buffer.Get(), 0, sizeof(float));
    check(expected != 0, "warm real GPU snapshot and region cache before concurrent CPU-only reads");
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto* info = GetResourceHandleInfo(device.buffer.Get());
        check(info && info->cached_data && info->cached_data_size && info->GetCachedRegionHash({0, sizeof(float)}) == expected,
            "real snapshot and L2 are warm so concurrent readers never invoke native Map");
    }
    std::atomic<bool> start{false}; std::atomic<unsigned> errors{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) workers.emplace_back([&] {
        ComPtr<ID3D11Buffer> retained(device.buffer.Get());
        while (!start.load()) std::this_thread::yield();
        for (int j = 0; j < 1000; ++j)
            if (GetRegionHash(device.context.get(), retained.Get(), 0, sizeof(float)) != expected) ++errors;
    });
    std::thread clear([&] {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 1000; ++i) ClearRegionHashesGlobalCache();
    });
    start = true;
    for (auto& worker : workers) worker.join(); clear.join();
    check(errors == 0, "concurrent L3 clear/read/insert retains actual warmed buffer hash");
}
void verify_concurrent_invalidation(CacheDevice& device, CacheConfiguration& config)
{
    config.populate(); set_hash(device.texture.Get(), 0); Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear();
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(device.texture.Get(), nullptr, &view)), "create actual view for deferred context ownership");
    ComPtr<ID3D11DeviceContext> contexts[2];
    for (auto& context : contexts) check(SUCCEEDED(device.native->CreateDeferredContext(0, &context)), "create independent real deferred context");
    std::atomic<bool> start{false}; std::atomic<unsigned> errors{0};
    std::vector<std::thread> workers;
    for (auto& context : contexts) workers.emplace_back([&, context] {
        ID3D11ShaderResourceView* bound = view.Get(); context->PSSetShaderResources(0, 1, &bound);
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 100; ++i) {
            // Deferred contexts do not support state Get methods. Hold a
            // genuine independent AddRef while both contexts record binds.
            ComPtr<ID3D11Resource> resource(device.texture.Get());
            const auto matches = lookup(resource.Get(), 7);
            if (matches != std::vector<std::wstring>({L"exact-low", L"fuzzy-low", L"fuzzy-resolution"})) ++errors;
        }
        context->ClearState();
    });
    start = true;
    for (int i = 0; i < 40; ++i) config.populate(); // Invalidate before destroying old nodes, under the shared lock.
    for (auto& worker : workers) worker.join();
    check(errors == 0 && Profiling::texture_override_candidates_lookup_overhead.count == 200 &&
        Profiling::texture_override_candidates_lookup_overhead.hits == 200,
        "stable per-context COM references survive concurrent candidate invalidation/config map replacement");
}
CommandListState consumer_state(CacheDevice& device, DrawCallInfo* info = nullptr)
{
    CommandListState state;
    state.mHackerDevice = device.device; state.mHackerContext = device.context.get();
    state.mOrigDevice1 = device.native.Get(); state.mOrigContext1 = device.immediate.Get(); state.call_info = info;
    return state;
}
void verify_tracking_lifetime(CacheDevice& device, CacheConfiguration& config)
{
    config.populate(); Profiling::mode = Profiling::Mode::NONE;
    ComPtr<ID3D11Texture2D> tracked;
    check(SUCCEEDED(device.device->CreateTexture2D(&device.desc, nullptr, &tracked)), "actual tracked lifetime texture");
    set_hash(tracked.Get(), 0);
    check(lookup(tracked.Get(), 7) == std::vector<std::wstring>({L"exact-low", L"fuzzy-low", L"fuzzy-resolution"}),
        "tracked zero hash has exact and descriptor candidates before tracker removal");
    check(SUCCEEDED(tracked->SetPrivateDataInterface(ResourceReleaseTrackerGuid, nullptr)), "actual COM tracker removal");
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        check(G->mResources.count(tracked.Get()) == 0, "real tracker Release removes handle and its memoized candidates");
    }
    check(lookup(tracked.Get(), 7).empty(), "resource stays alive but never matches after its tracker is removed");
    auto state = consumer_state(device);
    const std::wstring ns, name = L"resourcecache_untracked_custom";
    check(customResources.count(name) == 0, "unique actual untracked custom reference");
    customResources[name].name = name;
    ResourceCopyTarget custom;
    check(custom.ParseTarget(name.c_str(), false, &ns, nullptr), "parse actual custom target");
    custom.SetResource(&state, tracked.Get(), nullptr, 0, 0, device.desc.Format, 0);
    TextureOverrideMatches matches;
    custom.FindTextureOverrides(&state, nullptr, &matches);
    check(matches.empty(), "actual custom target referencing untracked resource does not fabricate candidates");
    customResources.erase(name); tracked.Reset();
    ComPtr<ID3D11Texture2D> fresh;
    check(SUCCEEDED(device.device->CreateTexture2D(&device.desc, nullptr, &fresh)), "create fresh wrapper resource after tracked cache destruction");
    set_hash(fresh.Get(), UINT32_MAX);
    check(lookup(fresh.Get(), 7) == std::vector<std::wstring>({L"exact-max", L"fuzzy-low", L"fuzzy-resolution"}),
        "fresh handle has its own current hash candidates, even if allocator reuses the old address");
    auto* released_address = fresh.Get(); fresh.Reset();
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        check(G->mResources.count(released_address) == 0, "actual resource destruction releases the candidate cache with the tracked handle");
    }
}
void verify_distinct_region_candidates(CacheDevice& device, CacheConfiguration& config)
{
    config.populate(); G->track_region_hashes = true; Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear();
    const auto first_hash = GetRegionHash(device.context.get(), device.buffer.Get(), 0, 8);
    const auto second_hash = GetRegionHash(device.context.get(), device.buffer.Get(), 8, 8);
    check(first_hash && second_hash && first_hash != second_hash, "actual two buffer meshes have different region hashes");
    uint32_t whole_hash = 0xcace0020;
    while (whole_hash == first_hash || whole_hash == second_hash) ++whole_hash;
    set_hash(device.buffer.Get(), whole_hash);
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
        G->mTextureOverrideDrawVertexMap.clear(); G->mTextureOverrideDrawIndexMap.clear();
        auto add = [&](uint32_t hash, const wchar_t* name) {
            auto& list = G->mTextureOverrideMap[hash]; list.resize(1);
            auto& match = list.front(); match.ini_section = name; match.has_draw_context_match = true;
            match.match_vertex_count.op = FuzzyMatchOp::EQUAL; match.match_vertex_count.val = 2;
            G->mTextureOverrideDrawVertexMap[2].push_back({hash, &match});
        };
        add(first_hash, L"region-first"); add(second_hash, L"region-second"); add(whole_hash, L"whole-resource");
        auto fuzzy = std::make_shared<FuzzyMatchResourceDesc>(L"region-fuzzy-buffer");
        fuzzy->set_resource_type(D3D11_RESOURCE_DIMENSION_BUFFER);
        fuzzy->ByteWidth.op = FuzzyMatchOp::EQUAL; fuzzy->ByteWidth.val = 16;
        G->mFuzzyTextureOverrides.insert(fuzzy);
    }
    DrawCallInfo info; info.VertexCount = 2;
    auto state = consumer_state(device, &info);
    ResourceCopyTarget target; const std::wstring ns;
    check(target.ParseTarget(L"vb0", false, &ns, nullptr), "parse actual region buffer target");
    auto region_matches = [&](UINT offset) {
        ID3D11Buffer* buffer = device.buffer.Get(); UINT stride = 4;
        device.immediate->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        TextureOverrideMatches found; target.FindTextureOverrides(&state, nullptr, &found);
        std::vector<std::wstring> names; for (const auto* entry : found) names.push_back(entry->ini_section);
        return names;
    };
    const auto before = Profiling::texture_override_candidates_lookup_overhead.count;
    check(region_matches(0) == std::vector<std::wstring>({L"region-first", L"region-fuzzy-buffer"}) &&
        region_matches(8) == std::vector<std::wstring>({L"region-second", L"region-fuzzy-buffer"}),
        "actual two-region target uses each region exact hash plus shared descriptor fuzzy list, never cached whole hash");
    check(Profiling::texture_override_candidates_lookup_overhead.count == before + 2 &&
        Profiling::texture_override_candidates_lookup_overhead.hits == 2 &&
        Profiling::texture_override_fuzzy_match_overhead.count == 1,
        "two region lookups count separately while rebuilding descriptor candidates only once");
    check(SUCCEEDED(device.buffer->SetPrivateDataInterface(ResourceReleaseTrackerGuid, nullptr)),
        "remove actual region buffer tracker while both regions remain warm in L3");
    check(region_matches(0) == std::vector<std::wstring>({L"region-first"}) &&
        region_matches(8) == std::vector<std::wstring>({L"region-second"}),
        "upstream region exact matching remains independent after tracker detach; cached descriptor fuzzy matches disappear");
    ID3D11Buffer* none = nullptr; UINT zero = 0; device.immediate->IASetVertexBuffers(0, 1, &none, &zero, &zero);
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        G->mTextureOverrideDrawVertexMap.clear(); G->mTextureOverrideDrawIndexMap.clear();
    }
    config.populate(); Profiling::clear();
}
float evaluate_consumer(CacheDevice& device, const wchar_t* source, DrawCallInfo* info)
{
    const std::wstring expression_text = source, ns;
    CommandListScope scope; CommandListExpression expression;
    check(expression.parse(&expression_text, &ns, &scope), "parse actual resource filter expression");
    auto state = consumer_state(device, info);
    return expression.evaluate(&state);
}
class ConsumerLockProbe : public CommandListCommand {
    HANDLE request_, attempted_;
public:
    ConsumerLockProbe(HANDLE request, HANDLE attempted) : request_(request), attempted_(attempted) {}
    void run(CommandListState*) override
    {
        SetEvent(request_);
        check(WaitForSingleObject(attempted_, 5000) == WAIT_OBJECT_0, "second thread attempted lock during real CheckTO child execution");
    }
};
void verify_filter_and_child_consumers(CacheDevice& device, CacheConfiguration& config)
{
    Profiling::mode = Profiling::Mode::NONE; Profiling::clear(); config.populate(); set_hash(device.texture.Get(), 0);
    DrawCallInfo info; info.IndexCount = 3;
    ID3D11ShaderResourceView* null_view = nullptr;
    device.immediate->PSSetShaderResources(0, 1, &null_view);
    const auto unbound = evaluate_consumer(device, L"ps-t0", &info);
    check(unbound == 0 && std::signbit(unbound), "actual resource filter preserves unbound negative zero");
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(device.texture.Get(), nullptr, &view)), "actual bound filter view");
    ID3D11ShaderResourceView* bound = view.Get(); device.immediate->PSSetShaderResources(0, 1, &bound);
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        G->mTextureOverrideMap.at(0)[0].filter_index = 2;
        config.low->texture_override->filter_index = 4;
        config.dynamic->texture_override->filter_index = 9;
    }
    check(evaluate_consumer(device, L"ps-t0", &info) == 9, "actual filter consumes highest-priority matching filter index");
    config.dynamic->texture_override->filter_index = FLT_MAX;
    check(evaluate_consumer(device, L"ps-t0", &info) == 4, "highest priority without filter does not hide earlier explicit filter");
    config.low->texture_override->filter_index = -0.0f;
    const auto explicit_zero = evaluate_consumer(device, L"ps-t0", &info);
    check(explicit_zero == 0 && std::signbit(explicit_zero), "actual bound filter preserves explicit signed zero");
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
    }
    const auto missing = evaluate_consumer(device, L"ps-t0", &info);
    check(missing == 0 && !std::signbit(missing), "bound resource without overrides returns positive zero");
    const std::wstring variable_name = L"$cache_consumer_result";
    check(command_list_globals.emplace(variable_name, CommandListVariable(variable_name, 0, VariableFlags::GLOBAL)).second,
        "unique actual child assignment variable");
    auto& overrides = G->mTextureOverrideMap[0]; overrides.resize(1); auto& target = overrides.front();
    const std::wstring ns;
    std::wstring pre = L"11", post = L"22";
    check(ParseCommandListVariableAssignment(L"TextureOverrideCacheConsumer", variable_name.c_str(), &pre, nullptr,
        &target.command_list, &target.command_list, &target.command_list, &ns) &&
        ParseCommandListVariableAssignment(L"TextureOverrideCacheConsumer", variable_name.c_str(), &post, nullptr,
        &target.post_command_list, &target.post_command_list, &target.post_command_list, &ns), "parse actual CheckTO pre/post assignments");
    HANDLE request = CreateEventW(nullptr, FALSE, FALSE, nullptr), attempted = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    check(request && attempted, "create deterministic consumer lifetime handshake");
    auto probe = std::make_shared<ConsumerLockProbe>(request, attempted);
    target.command_list.commands.push_back(probe);
    CheckTextureOverrideCommand command;
    check(command.target.ParseTarget(L"ps-t0", false, &ns, nullptr), "actual CheckTO bound target");
    auto state = consumer_state(device, &info);
    std::atomic<bool> entered{false};
    std::thread contender([&] {
        if (WaitForSingleObject(request, 5000) == WAIT_OBJECT_0) {
            if (TryEnterCriticalSection(&G->mCriticalSection)) {
                entered = true; LeaveCriticalSection(&G->mCriticalSection);
            }
        } else entered = true;
        SetEvent(attempted);
    });
    command.run_pre_and_post_together = true; state.post = true;
    command.run(&state); contender.join();
    check(!entered && state.post && state.this_target == nullptr && command_list_globals.at(variable_name).fval == 22,
        "actual CheckTO retains G throughout child pre/post execution and restores state");
    target.command_list.commands.pop_back(); probe.reset(); CloseHandle(request); CloseHandle(attempted);
    command.run_pre_and_post_together = false; state.post = false; command.run(&state);
    check(command_list_globals.at(variable_name).fval == 11, "actual CheckTO pre-only dispatch");
    state.post = true; command.run(&state);
    check(command_list_globals.at(variable_name).fval == 22, "actual CheckTO post-only dispatch");
    device.immediate->PSSetShaderResources(0, 1, &null_view);
    config.populate(); command_list_globals.erase(variable_name);
}
std::vector<unsigned> read_buffer(CacheDevice& device, ID3D11Buffer* resource)
{
    D3D11_BUFFER_DESC desc{}; resource->GetDesc(&desc);
    const auto size = desc.ByteWidth;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "create real consumer readback");
    device.immediate->CopyResource(staging.Get(), resource);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(device.immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map real consumer result");
    std::vector<unsigned> result(size / sizeof(unsigned)); std::memcpy(result.data(), mapped.pData, size);
    device.immediate->Unmap(staging.Get(), 0); return result;
}
void verify_uav_and_copy_consumers(CacheDevice& device, CacheConfiguration& config)
{
    config.populate(); Profiling::mode = Profiling::Mode::NONE;
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = 64; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = 4;
    ComPtr<ID3D11Buffer> uav_buffer;
    check(SUCCEEDED(device.device->CreateBuffer(&desc, nullptr, &uav_buffer)), "actual tracked UAV consumer buffer");
    const uint32_t uav_hash = 0xcace0001; set_hash(uav_buffer.Get(), uav_hash);
    auto& uav_override = G->mTextureOverrideMap[uav_hash]; uav_override.resize(1); uav_override[0].override_num_elements = 12;
    D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc{}; view_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view_desc.Buffer.FirstElement = 4; view_desc.Buffer.NumElements = 2;
    ComPtr<ID3D11UnorderedAccessView> view;
    check(SUCCEEDED(device.device->CreateUnorderedAccessView(uav_buffer.Get(), &view_desc, &view)), "actual wrapper creates resized UAV");
    D3D11_UNORDERED_ACCESS_VIEW_DESC actual{}; view->GetDesc(&actual);
    check(actual.Buffer.FirstElement == 4 && actual.Buffer.NumElements == 8, "UAV consumer copies override count and preserves nonzero FirstElement");
    InvalidateTextureOverrideCandidates(); G->mTextureOverrideMap.erase(uav_hash); view.Reset();
    check(SUCCEEDED(device.device->CreateUnorderedAccessView(uav_buffer.Get(), &view_desc, &view)), "actual wrapper forwards unmodified UAV without override");
    view->GetDesc(&actual); check(actual.Buffer.FirstElement == 4 && actual.Buffer.NumElements == 2, "UAV fallback keeps original window");
    const unsigned source_data[4] = {17, 29, 41, 53}, destination_data[8] = {};
    desc = {}; desc.ByteWidth = sizeof(source_data); desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = source_data;
    ComPtr<ID3D11Buffer> source, equal, enlarged;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &data, &source)), "actual source copy buffer");
    data.pSysMem = destination_data;
    check(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &equal)), "actual equal-sized copy destination");
    desc.ByteWidth = sizeof(destination_data);
    check(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &enlarged)), "actual enlarged copy destination");
    set_hash(equal.Get(), 0xcace0002); set_hash(enlarged.Get(), 0xcace0003);
    auto& copy_override = G->mTextureOverrideMap[0xcace0003]; copy_override.resize(1); copy_override[0].override_byte_width = sizeof(destination_data);
    device.context->CopyResource(equal.Get(), source.Get());
    device.context->CopyResource(enlarged.Get(), source.Get());
    const auto copied = read_buffer(device, enlarged.Get());
    check(read_buffer(device, equal.Get()) == std::vector<unsigned>({17, 29, 41, 53}) &&
        copied == std::vector<unsigned>({17, 29, 41, 53, 0, 0, 0, 0}),
        "actual CopyResource consumer selects native equal-size copy or enlarged CopySubresourceRegion and preserves tail bytes");
    config.populate();
}
class ReloadFiles {
public:
    HINSTANCE module = migoto_handle;
    std::wstring user = G->user_config;
    bool initialized = G->gConfigInitialized, warnings = G->gShowWarnings, clear = G->clear_unknown_settings;
    int dirty = G->user_config_dirty;
    bool pending = G->gReloadConfigPending, disabled = G->disable_input, input_initialized = G->disable_input_initialized;
    InputDisableScope input_scope = G->input_disable_scope;
    std::filesystem::path config, settings;
    std::vector<std::filesystem::path> owned;
    ReloadFiles()
    {
        wchar_t exe[MAX_PATH]{}; check(GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0, "locate cache reload fixture");
        const auto directory = std::filesystem::path(exe).parent_path(); bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "cache reload fixture belongs under build");
        config = directory / L"d3dx.ini"; settings = directory / (L"cache_reload_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        try {
            for (const auto& path : {config, settings}) {
                HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                check(file != INVALID_HANDLE_VALUE, "refuse preexisting cache config");
                CloseHandle(file); owned.push_back(path);
            }
        } catch (...) {
            for (const auto& path : owned) DeleteFileW(path.c_str());
            throw;
        }
        migoto_handle = nullptr;
    }
    ~ReloadFiles()
    {
        ClearKeyBindings(); registered_command_lists.clear(); G->constants_command_list.clear(); G->post_constants_command_list.clear();
        customResourcePools.clear(); customResources.clear(); persistent_variables.clear(); command_list_globals.clear(); explicitCommandListSections.clear();
        migoto_handle = module; G->user_config = user; G->gConfigInitialized = initialized; G->gShowWarnings = warnings;
        G->clear_unknown_settings = clear; G->user_config_dirty = dirty;
        G->gReloadConfigPending = pending; G->disable_input = disabled;
        G->disable_input_initialized = input_initialized; G->input_disable_scope = input_scope;
        for (const auto& path : owned) DeleteFileW(path.c_str());
    }
    void write(const char* name)
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[System]\nskip_early_includes_load=0\nclear_unknown_settings=0\n"
            "[Hunting]\nhunting=0\n[Include]\nuser_config=" << settings.filename().string() << "\n[TextureOverride" << name << "]\nhash=00000000\n";
        file.close(); check(file.good(), "write owned cache reload config"); WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    }
};
void verify_real_reload(CacheDevice& device)
{
    ReloadFiles files; files.write("CacheOld"); LoadConfigFile(); set_hash(device.texture.Get(), 0);
    check(lookup(device.texture.Get()) == std::vector<std::wstring>({L"TextureOverrideCacheOld"}), "warm actual INI candidate pointer");
    files.write("CacheNew"); G->gReloadConfigPending = true; ReloadConfig(device.device);
    check(lookup(device.texture.Get()) == std::vector<std::wstring>({L"TextureOverrideCacheNew"}),
        "real ReloadConfig invalidates before old TextureOverride map nodes are destroyed");
}
}
void test_dx11_texture_override_cache_regression()
{
    try {
        CacheConfiguration config; CacheDevice device;
        verify_lookup(device, config); verify_contamination_and_regions(device); verify_concurrent_region_cache(device);
        verify_concurrent_invalidation(device, config);
        verify_tracking_lifetime(device, config); verify_distinct_region_candidates(device, config);
        verify_filter_and_child_consumers(device, config); verify_uav_and_copy_consumers(device, config);
        verify_real_reload(device);
        std::fputs("PASS: real tracked/untracked TO cache, independent region exact/fuzzy, lifetime/reload/concurrency and four consumers\n", stderr);
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL texture override cache: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
