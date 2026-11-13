#include "CommandList.h"
#include "globals.h"
#include "HackerContext.h"
#include <wrl/client.h>

#include <cstdio>
#include <climits>
#include <cstdlib>
#include <limits>
#include <vector>

extern uint32_t PackCellCoords(float x, float y, float z, float cell_size);

static void require_pool_missing(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL pool missing-key policy: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class PoolMissingNativeContext
{
public:
    HMODULE runtime = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device1> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> native;
    std::unique_ptr<HackerContext> context;

    PoolMissingNativeContext()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_pool_missing(runtime != nullptr, "load real runtime for parsed pool commands");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_pool_missing(create != nullptr, "resolve native device creator");
        Microsoft::WRL::ComPtr<ID3D11Device> base;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        require_pool_missing(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real parsed-command context");
        require_pool_missing(SUCCEEDED(base.As(&device)) && SUCCEEDED(immediate.As(&native)), "query D3D11.1 context");
        context = std::make_unique<HackerContext>(device.Get(), native.Get());
    }

    ~PoolMissingNativeContext()
    {
        context.reset();
        native.Reset();
        device.Reset();
        FreeLibrary(runtime);
    }
};

class PoolMissingFixture
{
public:
    const std::wstring prefix = L"poolmissing_regression_";
    const unsigned saved_frame = G->frame_no;
    const float saved_frame_time = G->gFrameTime;
    std::vector<std::wstring> names;

    PoolMissingFixture()
    {
        G->gFrameTime = 1.0f / 120.0f;
        for (const auto& entry : customResources)
            require_pool_missing(entry.first.compare(0, prefix.size(), prefix) != 0, "resource fixture prefix is unused");
        const std::wstring variable_prefix = L"$" + prefix;
        for (const auto& entry : command_list_globals)
            require_pool_missing(entry.first.compare(0, variable_prefix.size(), variable_prefix) != 0,
                "variable fixture prefix is unused");
    }

    ~PoolMissingFixture()
    {
        for (const auto& name : names)
            customResourcePools.erase(name);
        for (auto it = customResources.begin(); it != customResources.end();) {
            if (it->first.compare(0, prefix.size(), prefix) == 0)
                it = customResources.erase(it);
            else
                ++it;
        }
        const std::wstring variable_prefix = L"$" + prefix;
        for (auto it = command_list_globals.begin(); it != command_list_globals.end();) {
            if (it->first.compare(0, variable_prefix.size(), variable_prefix) == 0)
                it = command_list_globals.erase(it);
            else
                ++it;
        }
        G->frame_no = saved_frame;
        G->gFrameTime = saved_frame_time;
    }

    CustomResourcePool& make(const wchar_t* suffix, PoolIndexType type, size_t size, unsigned timeout = UINT_MAX)
    {
        const std::wstring name = prefix + suffix;
        require_pool_missing(customResourcePools.count(name) == 0 && customResources.count(name + L"_template") == 0,
            "fixture owns unique pool and template");
        names.push_back(name);
        auto& pool = customResourcePools[name];
        pool.name = name;
        auto& resource = customResources[name + L"_template"];
        resource.name = name + L"_template";
        resource.pool = &pool;
        resource.pool_index = -1;
        pool.resource_template = &resource;
        pool.variable_template = std::make_unique<CommandListVariable>(name + L"_template", 17.0f, VariableFlags::NONE);
        pool.index_type = type;
        pool.keep_alive_frames = timeout;
        pool.reset_expired_elements = true;
        pool.Initialize(size);
        return pool;
    }
};

static float missing_spatial_key(float x, float y = 0.0f, float z = 0.0f)
{
    return BitCastToFloat(PackCellCoords(x, y, z, 1.0f));
}

static float missing_last_frame(CustomResourcePool& pool, const wchar_t* index)
{
    const std::wstring text = pool.name + L"[" + index + L"]->lastframe";
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListState state;
    ResourceCopyTarget target;
    require_pool_missing(target.ParseTarget(text.c_str(), true, &ini_namespace, &scope), "parse actual LastFrame target");
    return target.GetPoolElementLastFrame(&state);
}

static void assign_missing_pool(CustomResourcePool& pool, const wchar_t* index, const wchar_t* value, HackerContext* context)
{
    const std::wstring destination = L"$" + pool.name + L"[" + index + L"]";
    const std::wstring ini_namespace;
    CommandListScope scope;
    scope.emplace_front();
    CommandList commands;
    commands.scope = &scope;
    std::wstring expression = value;
    require_pool_missing(ParseCommandListResourceCopyTargetDirective(L"CommandListPoolMissingRegression",
        destination.c_str(), &expression, &commands, &ini_namespace) && commands.commands.size() == 1,
        "parse real PoolVariableOperation assignment");
    CommandListState state;
    state.mHackerContext = context;
    commands.commands.front()->run(&state);
    commands.scope = nullptr;
}

void test_dx11_pool_missing_regression()
{
    std::fputs("Testing FIFO and spatial pool read-miss policy\n", stderr);
    PoolMissingFixture fixture;
    PoolMissingNativeContext execution;
    G->frame_no = 100;
    auto& fifo = fixture.make(L"fifo", PoolIndexType::FIFO, 2, 1000);
    require_pool_missing(!fifo.allocate_slot_on_missing, "read-miss allocation defaults to false");
    require_pool_missing(fifo.GetVariable(99, false, false, false) == fifo.variable_template.get() &&
        fifo.GetResource(99, false, false, false) == fifo.resource_template,
        "legal FIFO missing reads return default variable and resource templates");
    require_pool_missing(missing_last_frame(fifo, L"99") == 0.0f, "legal FIFO missing-key LastFrame is zero");
    unsigned untouched_frame = 77;
    require_pool_missing(!fifo.GetLastUpdateFrame(99, false, &untouched_frame) && untouched_frame == 77,
        "raw missing LastFrame fails without overwriting output or fabricating a timestamp");
    auto* first = fifo.GetVariable(1, false, false, true);
    auto* second = fifo.GetVariable(2, false, false, true);
    require_pool_missing(first && second && first != second && first != fifo.variable_template.get(),
        "writes allocate real independent FIFO elements");
    first->fval = 11.0f;
    second->fval = 22.0f;
    for (int i = 0; i < 4; ++i)
        require_pool_missing(fifo.GetVariable(99, false, false, false) == fifo.variable_template.get(),
            "repeated FIFO miss does not allocate");
    require_pool_missing(fifo.GetVariable(1, false, false, false) == first && first->fval == 11.0f &&
        fifo.GetVariable(2, false, false, false) == second && second->fval == 22.0f,
        "missing reads do not evict or mutate existing data");
    require_pool_missing(fifo.GetVariable(3, false, false, true) == first &&
        fifo.GetVariable(2, false, false, false) == second && second->fval == 22.0f,
        "missing reads do not advance FIFO replacement order");
    require_pool_missing(fifo.GetVariable(1, false, false, false) == fifo.variable_template.get(), "evicted UID reads default template");

    auto& old_reads = fixture.make(L"allocate", PoolIndexType::FIFO, 1);
    old_reads.allocate_slot_on_missing = true;
    auto* allocated_read = old_reads.GetVariable(7, false, false, false);
    require_pool_missing(allocated_read && allocated_read != old_reads.variable_template.get() &&
        old_reads.GetVariable(7, false, false, false) == allocated_read, "explicit option restores allocating reads");
    require_pool_missing(missing_last_frame(old_reads, L"7") == -1.0f,
        "allocated but unwritten existing FIFO element has no LastFrame");
    untouched_frame = 88;
    require_pool_missing(!old_reads.GetLastUpdateFrame(999, false, &untouched_frame) && untouched_frame == 88,
        "raw LastFrame does not allocate a missing key even with allocating reads enabled");
    old_reads.allocate_slot_on_missing = false;
    require_pool_missing(old_reads.GetVariable(7, false, false, false) == allocated_read &&
        old_reads.GetVariable(999, false, false, false) == old_reads.variable_template.get(),
        "raw missing LastFrame query preserves the previous key instead of evicting it");

    auto& parsed_pool = fixture.make(L"parsed_variable", PoolIndexType::FIFO, 1, 1000);
    const std::wstring ini_namespace;
    CommandListScope expression_scope;
    expression_scope.emplace_front();
    const std::wstring read_text = L"$" + parsed_pool.name + L"[9]";
    CommandListExpression parsed_read;
    CommandListState expression_state;
    expression_state.mHackerContext = execution.context.get();
    require_pool_missing(parsed_read.parse(&read_text, &ini_namespace, &expression_scope) &&
        parsed_read.evaluate(&expression_state) == 17.0f, "parsed pool source initially reads the missing-key template");
    assign_missing_pool(parsed_pool, L"9", L"25", execution.context.get());
    require_pool_missing(parsed_read.evaluate(&expression_state) == 25.0f,
        "same parsed source resolves a subsequently assigned UID rather than caching the template");
    assign_missing_pool(parsed_pool, L"10", L"31", execution.context.get());
    require_pool_missing(parsed_read.evaluate(&expression_state) == 17.0f,
        "same parsed source returns template after its UID is evicted");

    auto& parsed_resources = fixture.make(L"parsed_resource", PoolIndexType::FIFO, 1, 1000);
    parsed_resources.resource_template->buf_size = 128;
    const std::wstring resource_text = parsed_resources.name + L"[9]->size";
    ResourceCopyTarget parsed_resource;
    require_pool_missing(parsed_resource.ParseTarget(resource_text.c_str(), true, &ini_namespace, &expression_scope) &&
        parsed_resource.GetResourceSize(&expression_state) == 128.0f, "parsed metadata source reads default resource template");
    auto* populated_resource = parsed_resources.GetResource(9, false, false, true);
    require_pool_missing(populated_resource && populated_resource != parsed_resources.resource_template,
        "resource UID write creates independent production metadata object");
    populated_resource->buf_size = 256;
    require_pool_missing(parsed_resource.GetResourceSize(&expression_state) == 256.0f,
        "same parsed metadata source observes new UID object instead of a cached template");
    parsed_resources.GetResource(10, false, false, true);
    require_pool_missing(parsed_resource.GetResourceSize(&expression_state) == 128.0f,
        "same parsed metadata source returns template after UID eviction");

    auto& resources = fixture.make(L"resources", PoolIndexType::FIFO, 1);
    auto* actual_resource = resources.GetResource(1, false, false, true);
    require_pool_missing(actual_resource && actual_resource != resources.resource_template && actual_resource->pool == &resources,
        "resource write allocates actual production pool object");
    require_pool_missing(resources.GetResource(2, false, false, false) == resources.resource_template &&
        resources.GetResource(1, false, false, false) == actual_resource, "resource read miss preserves existing slot");

    const float nonfinite = std::numeric_limits<float>::infinity();
    auto& ring = fixture.make(L"ring", PoolIndexType::RING, 2, 10);
    auto& empty = fixture.make(L"empty", PoolIndexType::FIFO, 0, 10);
    for (bool assignment : {false, true}) {
        require_pool_missing(ring.GetVariable(nonfinite, false, false, assignment) == nullptr &&
            ring.GetResource(nonfinite, false, false, assignment) == nullptr,
            "invalid RING index never falls back to writable/readable template");
        require_pool_missing(empty.GetVariable(1, false, false, assignment) == nullptr &&
            empty.GetResource(1, false, false, assignment) == nullptr, "empty pool never falls back to template");
    }
    require_pool_missing(missing_last_frame(ring, L"inf") == -1.0f && missing_last_frame(ring, L"0") == -1.0f &&
        missing_last_frame(empty, L"0") == -1.0f, "invalid and unwritten RING/empty LastFrame remain unavailable");
    require_pool_missing(ring.variable_template->fval == 17.0f && empty.variable_template->fval == 17.0f,
        "invalid writes preserve template values");

    auto& spatial = fixture.make(L"spatial", PoolIndexType::SPATIAL, 2, 1000);
    spatial.spatial_radius = 1;
    auto* moving = spatial.GetVariable(missing_spatial_key(0), false, false, true);
    require_pool_missing(moving != nullptr && moving != spatial.variable_template.get(), "write creates spatial slot");
    moving->fval = 71.0f;
    for (int x = 1; x <= 3; ++x)
        require_pool_missing(spatial.GetVariable(missing_spatial_key(static_cast<float>(x)), false, false, true) == moving &&
            moving->fval == 71.0f, "continuous nearby trajectory rebinds key and preserves slot beyond initial radius");
    require_pool_missing(spatial.GetVariable(missing_spatial_key(30), false, false, false) == spatial.variable_template.get() &&
        spatial.GetVariable(missing_spatial_key(3), false, false, false) == moving,
        "distant spatial read miss returns template without eviction");
    spatial.ResetPool();
    moving = spatial.GetVariable(missing_spatial_key(0), false, false, true);
    moving->fval = 72.0f;
    for (int x = 1; x <= 3; ++x)
        require_pool_missing(spatial.GetVariable(missing_spatial_key(static_cast<float>(x)), false, false, false) == moving &&
            moving->fval == 72.0f, "nearby read trajectory also rebinds each spatial key without allocating");
    auto& spatial_reads = fixture.make(L"spatial_allocate", PoolIndexType::SPATIAL, 1);
    spatial_reads.allocate_slot_on_missing = true;
    auto* allocated_spatial_read = spatial_reads.GetVariable(missing_spatial_key(80), false, false, false);
    require_pool_missing(allocated_spatial_read && allocated_spatial_read != spatial_reads.variable_template.get(),
        "explicit option restores missing spatial read allocation");
    spatial.ResetPool();
    moving = spatial.GetVariable(missing_spatial_key(0, 0, 0), false, false, true);
    moving->fval = 81.0f;
    require_pool_missing(spatial.GetVariable(missing_spatial_key(4095, 255, 4095), false, false, true) == moving &&
        moving->fval == 81.0f, "spatial matching wraps all axes of 12/8/12 grid");
    spatial.ResetPool();
    const float nan_key = BitCastToFloat(UINT_MAX);
    moving = spatial.GetVariable(nan_key, false, false, true);
    require_pool_missing(moving && moving != spatial.variable_template.get() &&
        spatial.GetVariable(nan_key, false, false, false) == moving, "32-bit NaN spatial key is occupied and retrievable");
    auto& fifo_nan = fixture.make(L"nan", PoolIndexType::FIFO, 1);
    auto* nan_value = fifo_nan.GetVariable(nan_key, false, false, true);
    require_pool_missing(nan_value && fifo_nan.GetVariable(nan_key, false, false, false) == nan_value,
        "FIFO also preserves bitwise NaN keys");

    auto& refresh = fixture.make(L"refresh", PoolIndexType::FIFO, 1, 1);
    refresh.read_refreshes_expiration = true;
    auto* refreshed = refresh.GetVariable(1, false, false, true);
    refreshed->fval = 91.0f;
    G->frame_no = 101;
    require_pool_missing(refresh.GetVariable(1, false, false, false) == refreshed, "read refreshes existing element at next frame");
    G->frame_no = 102;
    require_pool_missing(refresh.GetVariable(1, false, false, false) == refreshed && refreshed->fval == 91.0f,
        "read refresh postpones expiration without allocating new slot");
    refresh.read_refreshes_expiration = false;
    G->frame_no = 104;
    require_pool_missing(refresh.GetVariable(1, false, false, false) == refresh.variable_template.get() &&
        refreshed->fval == 17.0f, "expired element resets and subsequent missing read returns template");

    auto& metadata_source = fixture.make(L"metadata_source", PoolIndexType::FIFO, 1, 1000);
    metadata_source.allocate_slot_on_missing = true;
    metadata_source.read_refreshes_expiration = true;
    auto& metadata_copy = fixture.make(L"metadata_copy", PoolIndexType::RING, 1);
    metadata_copy.CopyMetadataFrom(metadata_source);
    require_pool_missing(metadata_copy.allocate_slot_on_missing && metadata_copy.read_refreshes_expiration &&
        metadata_copy.index_type == PoolIndexType::FIFO, "copy_desc propagates both missing-read and read-refresh policies");
    auto& proxy = fixture.make(L"proxy", PoolIndexType::RING, 1);
    require_pool_missing(proxy.SetSourcePool(&metadata_source), "establish real production pool proxy");
    auto* proxied = proxy.GetVariable(8, false, false, false);
    require_pool_missing(proxied != nullptr && proxied != metadata_source.variable_template.get() &&
        proxied == metadata_source.GetVariable(8, false, false, false), "proxy reads use resolved source allocation policy");
}
