#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "lock.h"
#include <thread>
using namespace dx11_slot_test;

namespace {
class OverrideFixture {
    decltype(G->mTextureOverrideMap) exact;
    decltype(G->mFuzzyTextureOverrides) fuzzy;
    unsigned frame = G->frame_no;
    int hunting = G->hunting;
    bool tracking = G->track_region_hashes;
    DataCacheTestScope cache_policy;
public:
    OverrideFixture()
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates();
        exact = std::move(G->mTextureOverrideMap); fuzzy = std::move(G->mFuzzyTextureOverrides);
        G->mTextureOverrideMap.clear(); G->mFuzzyTextureOverrides.clear();
        G->frame_no = 100; G->hunting = 0; G->track_region_hashes = false;
    }
    ~OverrideFixture()
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        InvalidateTextureOverrideCandidates();
        G->mTextureOverrideMap = std::move(exact); G->mFuzzyTextureOverrides = std::move(fuzzy);
        G->frame_no = frame; G->hunting = hunting; G->track_region_hashes = tracking;
        ClearRegionHashesGlobalCache();
    }
    TextureOverride& add(ID3D11Resource* native, const wchar_t* name)
    {
        const auto hash = GetResourceHash(native);
        require_batch(hash != 0, "CheckTO: actual tracked resource hash exists");
        auto& entries = G->mTextureOverrideMap[hash];
        require_batch(entries.empty(), "CheckTO: fixture resources have distinct hashes");
        entries.resize(1); entries.front().ini_section = name;
        InvalidateTextureOverrideCandidates();
        return entries.front();
    }
};

class OrderCommand : public CommandListCommand {
    std::vector<int>& order;
    int value;
public:
    OrderCommand(std::vector<int>& order, int value) : order(order), value(value) {}
    void run(CommandListState*) override { order.push_back(value); }
};
class ThrowCommand : public CommandListCommand {
public:
    void run(CommandListState*) override { throw std::runtime_error("injected matched-child exception"); }
};

std::shared_ptr<CheckTextureOverrideCommand> parse_check(std::wstring source, bool paired)
{
    std::transform(source.begin(), source.end(), source.begin(), ::towlower);
    CommandList pre, post, explicit_list;
    CommandListScope scope; scope.emplace_front();
    pre.scope = post.scope = &scope;
    const std::wstring ns;
    const bool ok = ParseCommandListGeneralCommands(L"CommandListCheckTORange", L"checktextureoverride", &source,
        paired ? &explicit_list : nullptr, &pre, &post, &ns);
    if (!ok) return nullptr;
    auto& commands = paired ? explicit_list.commands : pre.commands;
    require_batch(commands.size() == 1, "CheckTO: parser emits one actual range command");
    return std::dynamic_pointer_cast<CheckTextureOverrideCommand>(commands.front());
}

ComPtr<ID3D11Buffer> tracked_source(BatchDevice& device, UINT seed)
{
    UINT data[16]; for (UINT i = 0; i < 16; ++i) data[i] = seed + i * 37;
    D3D11_BUFFER_DESC desc = {}; desc.ByteWidth = sizeof(data);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {}; initial.pSysMem = data;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &result)), "CheckTO: actual tracked source buffer");
    require_batch(GetRegionHash(device.wrapper.get(), result.Get(), 0, sizeof(data)) != 0, "CheckTO: real source snapshot readback");
    return result;
}
ComPtr<ID3D11ShaderResourceView> source_view(BatchDevice& device, ID3D11Buffer* buffer)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {}; desc.Format = DXGI_FORMAT_R32_UINT;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; desc.Buffer.NumElements = 16;
    ComPtr<ID3D11ShaderResourceView> result;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(buffer, &desc, &result)), "CheckTO: actual typed source view");
    return result;
}
} // namespace

void test_dx11_check_to_range_regression()
{
    BatchDevice device;
    Registry registry;
    OverrideFixture overrides;
    auto a = tracked_source(device, 111), b = tracked_source(device, 999), replacement = tracked_source(device, 777);
    auto va = source_view(device, a.Get()), vb = source_view(device, b.Get());
    registry.resource(L"resourcecheckto_replacement", device, replacement.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
    registry.resource(L"resourcecheckto_seen_a", device);
    registry.resource(L"resourcecheckto_seen_b", device);
    registry.resource(L"resourcecheckto_b", device, b.Get(), DXGI_FORMAT_R32_UINT, 4, 64);
    auto& oa = overrides.add(a.Get(), L"check-a"); auto& ob = overrides.add(b.Get(), L"check-b");
    std::vector<int> order;
    append(oa.command_list, L"resourcecheckto_seen_a", L"ref this");
    append(ob.command_list, L"resourcecheckto_seen_b", L"ref this");
    oa.command_list.commands.push_back(std::make_shared<OrderCommand>(order, 1));
    oa.post_command_list.commands.push_back(std::make_shared<OrderCommand>(order, 2));
    ob.command_list.commands.push_back(std::make_shared<OrderCommand>(order, 3));
    ob.post_command_list.commands.push_back(std::make_shared<OrderCommand>(order, 4));
    append(oa.post_command_list, L"this", L"ref resourcecheckto_replacement");
    append(ob.post_command_list, L"this", L"ref resourcecheckto_replacement");
    auto paired = parse_check(L"ps-t[3:4]", true);
    require_batch(paired != nullptr, "CheckTO: explicit parser accepts slot range");
    ResourceCopyTarget sentinel; sentinel.type = ResourceCopyTargetType::SHADER_RESOURCE; sentinel.shader_type = L'c'; sentinel.slot = 12;
    device.state.this_target = &sentinel; device.state.post = true;
    set_slot(device, L'p', 3, va.Get()); set_slot(device, L'p', 4, vb.Get());
    paired->run(&device.state);
    require_batch(order == std::vector<int>({1,2,3,4}) && device.state.this_target == &sentinel && device.state.post,
        "CheckTO: each slot runs pre then post in order and restores entering state");
    require_batch(customResources.at(L"resourcecheckto_seen_a").resource == a.Get() &&
        customResources.at(L"resourcecheckto_seen_b").resource == b.Get() &&
        bound_resource(device, L'p', 3).Get() == replacement.Get() && bound_resource(device, L'p', 4).Get() == replacement.Get(),
        "CheckTO: child this reads/writes the actual corresponding slot");
    order.clear(); set_slot(device, L'p', 3, va.Get()); set_slot(device, L'p', 4, vb.Get());
    auto phases = parse_check(L"ps-t[3:4]", false);
    device.state.post = false; phases->run(&device.state);
    require_batch(order == std::vector<int>({1,3}), "CheckTO: normal pre phase runs no post children");
    order.clear(); device.state.post = true; phases->run(&device.state);
    require_batch(order == std::vector<int>({2,4}), "CheckTO: normal post phase runs no pre children");

    // The same real command interprets THIS using the entering concrete target.
    set_slot(device, L'p', 3, va.Get()); sentinel.shader_type = L'p'; sentinel.slot = 3;
    auto this_check = parse_check(L"this", true); order.clear(); this_check->run(&device.state);
    require_batch(order == std::vector<int>({1,2}) && device.state.this_target == &sentinel,
        "CheckTO: scalar this remains concrete and never becomes a self-reference");
    // Run the existing indirect-draw fallback with a real DrawCallInfo and
    // native buffer. Only read from THIS here: its older write ownership
    // contract is a separate issue and is not fabricated by this fixture.
    ID3D11Buffer* arguments = a.Get();
    DrawCallInfo indirect; indirect.type = DrawCall::DispatchIndirect; indirect.indirect_buffer = &arguments;
    device.state.this_target = nullptr;
    device.state.resource = reinterpret_cast<ID3D11Resource**>(&arguments);
    device.state.view = va.Get(); device.state.call_info = &indirect; device.state.post = false;
    auto fallback = parse_check(L"this", false); order.clear(); fallback->run(&device.state);
    require_batch(order == std::vector<int>({1}) && arguments == a.Get() && device.state.this_target == nullptr &&
        customResources.at(L"resourcecheckto_seen_a").resource == a.Get(),
        "CheckTO: no-explicit-target THIS still reads the actual indirect argument resource without self recursion");
    device.state.resource = nullptr; device.state.view = nullptr; device.state.call_info = nullptr;
    device.state.this_target = &sentinel; device.state.post = true;
    auto invalid = parse_check(L"ps-t[4:3]", true); order.clear(); invalid->run(&device.state);
    require_batch(order.empty() && device.state.this_target == &sentinel && device.state.post,
        "CheckTO: invalid range performs no children and restores state");
    auto malformed = parse_check(L"ps-t[0:]", true);
    require_batch(!malformed && !parse_check(L"ps-t", true), "CheckTO: malformed/bare range syntax is rejected");

    // Throw after a real pre child has run. Both command state and the G lock
    // must unwind; the helper thread probes only the lock, never the D3D context.
    oa.command_list.commands.push_back(std::make_shared<ThrowCommand>());
    set_slot(device, L'p', 3, va.Get()); bool threw = false;
    const int entering_recursion = device.state.recursion, entering_indent = device.state.extra_indent;
    try { paired->run(&device.state); } catch (const std::runtime_error&) { threw = true; }
    require_batch(threw && device.state.this_target == &sentinel && device.state.post &&
        device.state.recursion == entering_recursion && device.state.extra_indent == entering_indent,
        "CheckTO: exception restores caller this/post/log recursion and indentation");
    bool acquired = false;
    std::thread probe([&]() { acquired = TryEnterCriticalSection(&G->mCriticalSection) != FALSE;
        if (acquired) LeaveCriticalSection(&G->mCriticalSection); }); probe.join();
    require_batch(acquired, "CheckTO: matched-child exception releases global lifetime guard");
    oa.command_list.commands.pop_back();

    // Read-only pool range traversal is physical for FIFO/SPATIAL and does not
    // force assignment refresh. Ordinary read-refresh remains configurable.
    oa.post_command_list.commands.clear(); ob.post_command_list.commands.clear();
    for (auto type : {PoolIndexType::FIFO, PoolIndexType::SPATIAL}) {
        const auto name = L"poolcheckto_" + std::to_wstring(static_cast<unsigned>(type));
        auto& pool = registry.pool(name, device, type, 2);
        auto* first = pool.GetResource(123, false, false, true);
        auto* second = pool.GetResource(456, false, false, true);
        assign(device, first, a.Get(), va.Get(), 0, 64, DXGI_FORMAT_R32_UINT);
        assign(device, second, b.Get(), vb.Get(), 0, 64, DXGI_FORMAT_R32_UINT);
        unsigned frame_a = 0, frame_b = 0;
        require_batch(pool.GetLastUpdateFrame(123, false, &frame_a) && pool.GetLastUpdateFrame(456, false, &frame_b), "CheckTO: original keyed update frames");
        G->frame_no = 101; order.clear();
        auto check = parse_check(name + L"[1:2]", true); require_batch(check != nullptr, "CheckTO: pool range accepted");
        check->run(&device.state);
        require_batch(order == std::vector<int>({3,1}) && pool.GetResource(123,false,false,false) == first &&
            pool.GetResource(456,false,false,false) == second, "CheckTO: physical wrapped pool order preserves keys");
        unsigned after_a = 0, after_b = 0;
        require_batch(pool.GetLastUpdateFrame(123,false,&after_a) && pool.GetLastUpdateFrame(456,false,&after_b) &&
            after_a == frame_a && after_b == frame_b, "CheckTO: checking does not force assignment expiration updates");
        require_batch(!parse_check(name, true), "CheckTO: whole pool without element/range rejected");
    }
    auto& destination_pool = registry.pool(L"poolcheckto_this_write", device, PoolIndexType::RING, 1);
    set_slot(device, L'p', 3, va.Get());
    scalar(device, L"poolcheckto_this_write[0]", L"ref ps-t3");
    auto* destination = destination_pool.GetResource(0, false, true, false);
    require_batch(destination->GetHandleInfo() != nullptr, "CheckTO: actual scalar capture prepares original custom snapshot");
    destination->source_stride = 999;
    oa.command_list.commands.clear();
    append(oa.command_list, L"this", L"ref resourcecheckto_b");
    auto write = parse_check(L"poolcheckto_this_write[0:0]", true); write->run(&device.state);
    require_batch(destination->resource == b.Get() && destination->source_stride == 4 && destination->GetHandleInfo() &&
        GetRegionHash(device.wrapper.get(), b.Get(), 0, 64, destination) == GetRegionHash(device.wrapper.get(), b.Get(), 0, 64),
        "CheckTO: writing pool this updates real resource, source stride and cached snapshot together");
    device.state.this_target = nullptr;
}
