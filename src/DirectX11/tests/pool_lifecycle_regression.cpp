#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class LifecycleDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    CommandListState state;

    LifecycleDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate lifecycle system runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load lifecycle system runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve lifecycle WARP creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            base.GetAddressOf(), nullptr, immediate.GetAddressOf())) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(immediate.As(&context)), "create lifecycle real DX11 context");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
        state.mHackerDevice = device;
        state.mHackerContext = wrapper.get();
        state.mOrigDevice1 = native.Get();
        state.mOrigContext1 = context.Get();
    }

    ~LifecycleDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class LifecycleFixture {
public:
    const std::wstring index_name = L"$pool_lifecycle_regression_index";
    std::vector<std::wstring> pools, resources, globals;
    unsigned saved_frame = G->frame_no;
    float saved_frame_time = G->gFrameTime;

    LifecycleFixture() { G->gFrameTime = 1.0f / 120.0f; }

    ~LifecycleFixture()
    {
        for (const auto& name : pools)
            customResourcePools.erase(name);
        for (const auto& name : resources)
            customResources.erase(name);
        for (const auto& name : globals)
            command_list_globals.erase(name);
        G->frame_no = saved_frame;
        G->gFrameTime = saved_frame_time;
    }

    CustomResourcePool& make(const wchar_t* name, PoolIndexType type, size_t size, unsigned timeout, bool lazy = true)
    {
        const std::wstring id = name;
        check(customResourcePools.count(id) == 0 && customResources.count(id + L"_template") == 0,
            "lifecycle pool fixture must be unique");
        auto& resource_template = customResources[id + L"_template"];
        resources.push_back(id + L"_template");
        auto& pool = customResourcePools[id];
        pools.push_back(id);
        pool.name = id;
        pool.resource_template = &resource_template;
        resource_template.pool = &pool;
        resource_template.pool_index = -1;
        pool.variable_template = std::make_unique<CommandListVariable>(id + L"_template", 0, VariableFlags::NONE);
        pool.index_type = type;
        pool.keep_alive_frames = timeout;
        pool.reset_expired_elements = true;
        pool.lazy_initialization = lazy;
        for (size_t i = 0; i < size; ++i) {
            const std::wstring suffix = std::to_wstring(i);
            const std::wstring variable = L"$" + id + L"[" + suffix + L"]";
            check(customResources.count(id + L"_" + suffix) == 0 && command_list_globals.count(variable) == 0,
                "lifecycle slot fixture must be unique");
            resources.push_back(id + L"_" + suffix);
            globals.push_back(variable);
        }
        pool.Initialize(size);
        return pool;
    }

    void add_index()
    {
        check(command_list_globals.count(index_name) == 0, "lifecycle index fixture must be unique");
        command_list_globals.emplace(index_name, CommandListVariable(index_name, 0, VariableFlags::GLOBAL));
        globals.push_back(index_name);
    }
};

void parse_directive(CommandList& commands, const wchar_t* lhs, const wchar_t* rhs)
{
    CommandListScope scope;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    check(ParseCommandListResourceCopyTargetDirective(L"CommandListPoolLifecycleRegression", lhs, &value,
        &commands, &ini_namespace), "parse actual pool lifecycle directive");
    commands.scope = nullptr;
}

void run(CommandList& commands, CommandListState& state)
{
    for (const auto& command : commands.commands)
        command->run(&state);
}

float last_frame(const wchar_t* text, CommandListState& state)
{
    CommandListScope scope;
    CommandListExpression expression;
    const std::wstring source = text;
    const std::wstring ini_namespace;
    check(expression.parse(&source, &ini_namespace, &scope), "parse production LastFrame expression");
    return expression.evaluate(&state);
}

void test_proxy_retry(LifecycleFixture& fixture, CommandListState& state)
{
    auto& a = fixture.make(L"poollifecyclea", PoolIndexType::RING, 1, UINT32_MAX);
    auto& b = fixture.make(L"poollifecycleb", PoolIndexType::RING, 2, UINT32_MAX);
    auto& c = fixture.make(L"poollifecyclec", PoolIndexType::RING, 3, UINT32_MAX);
    auto& d = fixture.make(L"poollifecycled", PoolIndexType::RING, 4, UINT32_MAX);
    auto& e = fixture.make(L"poollifecyclee", PoolIndexType::RING, 5, UINT32_MAX);
    check(a.SetSourcePool(&b) && b.SetSourcePool(&c) && c.SetSourcePool(&d) &&
        a.ResolvePool() == &d && a.GetPoolSize() == 4, "iterative proxy chain resolves final source");
    check(!d.SetSourcePool(&a) && a.ResolvePool() == &d && d.ResolvePool() == &d,
        "long cycle rejects atomically without changing old sources");
    CommandList retry;
    parse_directive(retry, L"poollifecycled", L"ref poollifecyclea");
    run(retry, state);
    check(d.ResolvePool() == &d, "parsed cyclic reference leaves destination unchanged");
    check(c.SetSourcePool(&e) && a.ResolvePool() == &e && a.GetPoolSize() == 5,
        "nodes captured while they were roots preserve links added after the initial binding");
    run(retry, state);
    check(d.ResolvePool() == &e, "same parsed reference retries after temporary cycle is removed");
    check(d.SetSourcePool(nullptr) && d.GetPoolSize() == 4 && d.SetSourcePool(nullptr),
        "proxy detachment is repeatable and restores own metadata");

    auto& old_root = fixture.make(L"poollifecyclesnapshotold", PoolIndexType::RING, 6, UINT32_MAX);
    auto& new_root = fixture.make(L"poollifecyclesnapshotnew", PoolIndexType::RING, 7, UINT32_MAX);
    auto& intermediate = fixture.make(L"poollifecyclesnapshotmiddle", PoolIndexType::RING, 1, UINT32_MAX);
    auto& captured = fixture.make(L"poollifecyclesnapshotcaptured", PoolIndexType::RING, 1, UINT32_MAX);
    check(intermediate.SetSourcePool(&old_root) && captured.SetSourcePool(&intermediate) &&
        intermediate.SetSourcePool(&new_root) && captured.ResolvePool() == &old_root &&
        captured.GetPoolSize() == 6 && intermediate.ResolvePool() == &new_root,
        "binding to an existing proxy snapshots its current root instead of retaining the intermediate node");
    check(captured.SetSourcePool(&captured) && captured.ResolvePool() == &old_root,
        "self binding on an existing proxy can snapshot its already resolved root");
    check(!new_root.SetSourcePool(&new_root) && new_root.ResolvePool() == &new_root,
        "self binding on an unproxied root rejects a real cycle");
}

void test_read_expiration(LifecycleFixture& fixture)
{
    auto& ordinary = fixture.make(L"poollifecycleordinary", PoolIndexType::RING, 2, 1);
    check(!ordinary.read_refreshes_expiration, "expiration refresh on read defaults to false");
    G->frame_no = 100;
    auto* value = ordinary.GetVariable(0, false, false, true);
    value->fval = 11;
    unsigned frame = 999;
    check(ordinary.GetLastUpdateFrame(0, false, &frame) && frame == 100, "write records initial frame");
    G->frame_no = 101;
    check(ordinary.GetVariable(0, false, false, false)->fval == 11 &&
        ordinary.GetLastUpdateFrame(0, false, &frame) && frame == 100, "default read and LastFrame retain prior write frame at inclusive timeout");
    G->frame_no = 102;
    frame = 777;
    check(!ordinary.GetLastUpdateFrame(0, false, &frame) && frame == 777 && value->fval == 0,
        "expired query invalidates safely, leaves output untouched and resets contents");

    auto& refreshed = fixture.make(L"poollifecyclerefreshed", PoolIndexType::RING, 2, 1);
    refreshed.read_refreshes_expiration = true;
    G->frame_no = 200;
    auto* retained = refreshed.GetVariable(0, false, false, true);
    retained->fval = 22;
    G->frame_no = 201;
    check(refreshed.GetLastUpdateFrame(0, false, &frame) && frame == 201,
        "LastFrame is an existing-element read and refreshes when enabled");
    G->frame_no = 202;
    check(refreshed.GetVariable(0, false, false, false)->fval == 22 &&
        refreshed.GetLastUpdateFrame(0, false, &frame) && frame == 202, "ordinary read refresh postpones expiration");
    G->frame_no = 204;
    check(!refreshed.GetLastUpdateFrame(0, false, &frame) && retained->fval == 0,
        "refresh read cannot revive an already expired element");
    const size_t globals_before = command_list_globals.size();
    const size_t resources_before = customResources.size();
    frame = 777;
    check(!refreshed.GetLastUpdateFrame(1, false, &frame) && frame == 777 &&
        command_list_globals.size() == globals_before && customResources.size() == resources_before,
        "unwritten LastFrame read creates no object and no fabricated update record");
}

void test_unlimited_and_wrap(LifecycleFixture& fixture, CommandListState& state)
{
    auto& unlimited = fixture.make(L"poollifecycleunlimited", PoolIndexType::RING, 2, UINT32_MAX);
    G->frame_no = 0;
    unlimited.GetVariable(0, false, false, true)->fval = 7;
    unsigned frame = 777;
    check(unlimited.GetLastUpdateFrame(0, false, &frame) && frame == 0 &&
        last_frame(L"poollifecycleunlimited[0]->lastframe", state) == 0,
        "disabled expiration still tracks a valid write at actual frame zero");
    G->frame_no = UINT32_MAX;
    auto* final = unlimited.GetVariable(1, false, false, true);
    final->fval = 8;
    check(unlimited.GetLastUpdateFrame(1, false, &frame) && frame == UINT32_MAX &&
        last_frame(L"poollifecycleunlimited[1]->lastframe", state) == static_cast<float>(UINT32_MAX),
        "UINT_MAX is a valid frame rather than an unused sentinel");
    unlimited.ResetPool(false);
    check(!unlimited.GetLastUpdateFrame(1, false, &frame) && final->fval == 8,
        "metadata reset clears timestamps even for unlimited RING while retaining contents");
    frame = 777;
    check(!unlimited.GetLastUpdateFrame(BitCastToFloat(0x7fc00000), false, &frame) && frame == 777,
        "RING nonfinite timestamp index fails without conversion or fabricated frame");
    auto& wrapped = fixture.make(L"poollifecyclewrapped", PoolIndexType::RING, 1, 2);
    G->frame_no = UINT32_MAX - 1;
    wrapped.GetVariable(0, false, false, true)->fval = 3;
    G->frame_no = 0;
    check(wrapped.GetLastUpdateFrame(0, false, &frame) && frame == UINT32_MAX - 1,
        "unsigned frame wrap preserves inclusive expiration window");
    G->frame_no = 1;
    check(!wrapped.GetLastUpdateFrame(0, false, &frame), "unsigned frame wrap expires beyond timeout");
    auto& empty = fixture.make(L"poollifecycleempty", PoolIndexType::RING, 0, UINT32_MAX);
    check(!empty.GetLastUpdateFrame(0, false, &frame) &&
        last_frame(L"poollifecycleempty[0]->lastframe", state) == -1,
        "empty LastFrame target fails safely without fake frame zero");
}

uint32_t spatial_key(uint32_t x, uint32_t y, uint32_t z)
{
    return ((x & 4095) << 20) | ((y & 255) << 12) | (z & 4095);
}

void test_keyed_queries(LifecycleFixture& fixture)
{
    auto& fifo = fixture.make(L"poollifecyclefifo", PoolIndexType::FIFO, 2, 5);
    const float all_ones = BitCastToFloat(UINT32_MAX);
    const float nan_key = BitCastToFloat(0x7fc12345);
    G->frame_no = 10;
    auto* first = fifo.GetVariable(all_ones, false, false, true);
    first->fval = 100;
    G->frame_no = 11;
    fifo.GetVariable(nan_key, false, false, true)->fval = 200;
    unsigned frame = 777;
    check(fifo.GetLastUpdateFrame(all_ones, false, &frame) && frame == 10 &&
        fifo.GetLastUpdateFrame(nan_key, false, &frame) && frame == 11, "FIFO distinct NaN bit keys retain distinct frames including UINT_MAX key");
    const size_t variables_before = command_list_globals.size();
    check(!fifo.GetLastUpdateFrame(99, false, &frame) && command_list_globals.size() == variables_before,
        "missing FIFO timestamp read does not allocate or evict");
    G->frame_no = 12;
    fifo.read_refreshes_expiration = true;
    check(fifo.GetLastUpdateFrame(all_ones, false, &frame) && frame == 12,
        "FIFO read refresh resolves actual bit-key slot");
    check(fifo.GetVariable(99, false, false, true) == first,
        "missing timestamp lookup did not advance FIFO replacement order");
    check(!fifo.GetLastUpdateFrame(all_ones, false, &frame), "evicted FIFO key cannot query stale slot frame");

    auto& spatial = fixture.make(L"poollifecyclespatial", PoolIndexType::SPATIAL, 2, 5);
    spatial.spatial_radius = 1;
    const float key = BitCastToFloat(spatial_key(10, 20, 30));
    const float near_key = BitCastToFloat(spatial_key(11, 21, 31));
    const float far_key = BitCastToFloat(spatial_key(100, 100, 100));
    G->frame_no = 30;
    auto* nearby = spatial.GetVariable(key, false, false, true);
    nearby->fval = 77;
    check(spatial.GetLastUpdateFrame(near_key, false, &frame) && frame == 30 &&
        spatial.GetLastUpdateFrame(key, false, &frame) && frame == 30,
        "SPATIAL timestamp uses same nearest-cell read selection without rekeying original slot");
    check(!spatial.GetLastUpdateFrame(far_key, false, &frame), "distant spatial timestamp read does not populate unused slot");
    G->frame_no = 31;
    spatial.read_refreshes_expiration = true;
    check(spatial.GetLastUpdateFrame(near_key, false, &frame) && frame == 31,
        "near spatial timestamp read refreshes existing nearest resource");
    check(spatial.GetVariable(far_key, false, false, true) != nearby,
        "missing spatial query left next unused resource available for actual assignment");
}

void test_target_indices_and_metadata(LifecycleFixture& fixture, CommandListState& state)
{
    auto& fixed = fixture.make(L"poollifecyclestatic", PoolIndexType::STATIC, 2, UINT32_MAX);
    G->frame_no = 40;
    fixed.GetVariable(0, false, false, true)->fval = 5;
    G->frame_no = 41;
    fixed.GetVariable(1, false, false, true)->fval = 6;
    const size_t resources_before = customResources.size();
    check(last_frame(L"poollifecyclestatic[1]->lastframe", state) == 41 &&
        last_frame(L"poollifecyclestatic[0]->lastframe", state) == 40 &&
        customResources.size() == resources_before, "STATIC LastFrame uses literal target index and never instantiates resource metadata");
    auto& dynamic = fixture.make(L"poollifecycledynamic", PoolIndexType::RING, 2, UINT32_MAX);
    G->frame_no = 50;
    dynamic.GetVariable(0, false, false, true)->fval = 1;
    G->frame_no = 51;
    dynamic.GetVariable(1, false, false, true)->fval = 2;
    fixture.add_index();
    CommandListExpression expression;
    CommandListScope scope;
    const std::wstring ini_namespace;
    const std::wstring text = L"poollifecycledynamic[$pool_lifecycle_regression_index]->lastframe";
    check(expression.parse(&text, &ini_namespace, &scope) && expression.evaluate(&state) == 50,
        "dynamic LastFrame reads actual target index expression rather than nonexistent member argument");
    command_list_globals.at(fixture.index_name).fval = 1;
    check(expression.evaluate(&state) == 51, "same LastFrame expression observes dynamic index changes");
    auto& proxy = fixture.make(L"poollifecyclemetaproxy", PoolIndexType::RING, 1, UINT32_MAX);
    check(proxy.SetSourcePool(&dynamic), "set timestamp metadata proxy");
    check(last_frame(L"poollifecyclemetaproxy[1]->lastframe", state) == 51, "LastFrame follows active proxy target");
    dynamic.read_refreshes_expiration = true;
    auto& copied = fixture.make(L"poollifecyclemetacopy", PoolIndexType::RING, 1, UINT32_MAX);
    CommandList descriptor;
    parse_directive(descriptor, L"poollifecyclemetacopy", L"copy_desc poollifecyclemetaproxy");
    run(descriptor, state);
    check(copied.GetPoolSize() == 2 && copied.read_refreshes_expiration,
        "actual copy_desc command copies effective source read-refresh policy through proxy");
    check(last_frame(L"poollifecyclemetacopy[0]->lastframe", state) == -1,
        "metadata-only copy does not copy source runtime timestamp records");
}

void run_pool_lifecycle_regression()
{
    LifecycleDevice device;
    LifecycleFixture fixture;
    test_proxy_retry(fixture, device.state);
    test_read_expiration(fixture);
    test_unlimited_and_wrap(fixture, device.state);
    test_keyed_queries(fixture);
    test_target_indices_and_metadata(fixture, device.state);
}
} // namespace

void test_dx11_pool_lifecycle_regression()
{
    try {
        run_pool_lifecycle_regression();
        std::fputs("PASS: real pool proxy retry, read expiration and LastFrame targets\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: pool lifecycle regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
