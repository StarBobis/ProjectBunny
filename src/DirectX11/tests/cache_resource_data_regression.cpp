#include "Logging.h"
#include "slot_range_test_support.h"
#include "IniHandler.h"
#include "Input.h"
#include "lock.h"
#include <D3DCompiler.h>
#include <filesystem>
#include <fstream>

namespace {
using namespace dx11_slot_test;

class CachePolicyState {
    const DataCacheBindFlags policy_ = G->cache_resource_data.load();
    const bool regions_ = G->track_region_hashes, warnings_ = G->gShowWarnings;
    const UINT hunting_ = G->hunting;
    const bool deferred_ = G->deferred_contexts_enabled;
    const int updates_ = G->track_texture_updates;
public:
    CachePolicyState() { G->hunting = HUNTING_MODE_DISABLED; G->track_texture_updates = 0; G->gShowWarnings = false; G->deferred_contexts_enabled = true; }
    ~CachePolicyState()
    {
        InvalidateResourceDataCaches(); G->cache_resource_data = policy_; G->track_region_hashes = regions_;
        G->hunting = hunting_; G->track_texture_updates = updates_; G->gShowWarnings = warnings_;
        G->deferred_contexts_enabled = deferred_;
    }
};
void policy(DataCacheBindFlags flags)
{
    G->cache_resource_data = flags; InvalidateResourceDataCaches();
}
ComPtr<ID3D11Buffer> tracked_buffer(BatchDevice& device, UINT bind, bool dynamic = true, UINT misc = 0)
{
    const float values[16] = {1, 2, 3, 4};
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = sizeof(values); desc.BindFlags = bind; desc.MiscFlags = misc;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = values;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &result)), "cache data: real tracked buffer creation");
    return result;
}
void write_map(BatchDevice& device, ID3D11Buffer* buffer, float first)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.wrapper->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)), "cache data: actual wrapper Map");
    float values[16]; for (unsigned i = 0; i < 16; ++i) values[i] = first + i;
    std::memcpy(mapped.pData, values, sizeof(values)); device.wrapper->Unmap(buffer, 0);
}
bool cached(ID3D11Resource* resource, float& value, UINT64 offset = 0)
{
    return TryReadResourceDataCache(resource, offset, &value, sizeof(value));
}
float gpu_value(BatchDevice& device, ID3D11Buffer* source)
{
    D3D11_BUFFER_DESC desc{}; source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "cache data: real staging creation");
    device.context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "cache data: map actual GPU result");
    float value; std::memcpy(&value, mapped.pData, sizeof(value)); device.context->Unmap(staging.Get(), 0); return value;
}
void warm(BatchDevice& device, ID3D11Buffer* buffer)
{
    require_batch(GetRegionHash(device.wrapper.get(), buffer, 0, 4) != 0, "cache data: actual allowed GPU snapshot readback");
}
class CacheConfigFiles {
    HINSTANCE module_ = migoto_handle;
    const std::wstring user_ = G->user_config;
    const int dirty_ = G->user_config_dirty;
    const bool config_initialized_ = G->gConfigInitialized, pending_ = G->gReloadConfigPending;
    const bool disabled_ = G->disable_input, input_initialized_ = G->disable_input_initialized;
    const InputDisableScope scope_ = G->input_disable_scope;
    const LogVerbosity verbosity_ = gLogVerbosity;
    const bool debug_ = gLogDebug;
    std::vector<std::filesystem::path> owned_;
public:
    std::filesystem::path config, settings;
    CacheConfigFiles()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "cache data: find owned config directory");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(in_build, "cache data: configuration fixture is confined to build");
        config = directory / L"d3dx.ini";
        settings = directory / (L"cache_data_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        try {
            for (const auto& path : {config, settings}) {
                HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                require_batch(file != INVALID_HANDLE_VALUE, "cache data: never overwrite an existing config/settings file");
                CloseHandle(file); owned_.push_back(path);
            }
        } catch (...) { for (const auto& path : owned_) DeleteFileW(path.c_str()); throw; }
        migoto_handle = nullptr;
    }
    ~CacheConfigFiles()
    {
        ClearKeyBindings(); registered_command_lists.clear();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        explicitCommandListSections.clear(); persistent_variables.clear(); command_list_globals.clear();
        customResourcePools.clear(); customResources.clear();
        migoto_handle = module_; G->user_config = user_; G->user_config_dirty = dirty_;
        G->gConfigInitialized = config_initialized_; G->gReloadConfigPending = pending_;
        G->disable_input = disabled_; G->disable_input_initialized = input_initialized_; G->input_disable_scope = scope_;
        gLogVerbosity = verbosity_; gLogDebug = debug_;
        for (const auto& path : owned_) DeleteFileW(path.c_str());
    }
    void write(const char* cache, bool regions = false)
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[System]\nskip_early_includes_load=0\nclear_unknown_settings=0\n"
            "[Hunting]\nhunting=0\n[Include]\nuser_config=" << settings.filename().string() <<
            "\n[Rendering]\ntrack_region_hashes=" << regions << '\n';
        if (cache) output << "cache_resource_data=" << cache << '\n';
        output.close(); require_batch(output.good(), "cache data: write actual config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    }
};
void configuration(BatchDevice& device)
{
    CacheConfigFiles files;
    files.write("all"); LoadConfigFile();
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::ALL_MASK, "cache data: actual initial all mode");
    auto buffer = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER); write_map(device, buffer.Get(), 10);
    float value;
    require_batch(cached(buffer.Get(), value) && value == 10, "cache data: initial policy captures actual Map bytes");
    const auto reload = [&](const char* flag, bool regions = false) {
        files.write(flag, regions); G->gReloadConfigPending = true; ReloadConfig(device.device);
    };
    reload(nullptr);
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::INVALID && !cached(buffer.Get(), value),
        "cache data: deleted key resets policy and discards previous snapshot on actual ReloadConfig");
    reload("constant_buffer");
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::CONSTANT_BUFFER, "cache data: actual specific mode");
    reload("vertex_buffer index_buffer");
    require_batch(static_cast<UINT>(G->cache_resource_data.load()) == 3, "cache data: actual combined modes");
    reload("none", true);
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::ALL_MASK, "cache data: legacy region mode forces VB IB CB despite none");
    reload("not_a_cache_flag");
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::INVALID, "cache data: invalid mode cannot retain old policy");
    reload("none");
    require_batch(G->cache_resource_data.load() == DataCacheBindFlags::INVALID, "cache data: explicit none");
}
void maps_and_modes(BatchDevice& device)
{
    auto vb = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER), ib = tracked_buffer(device, D3D11_BIND_INDEX_BUFFER);
    auto cb = tracked_buffer(device, D3D11_BIND_CONSTANT_BUFFER);
    float value;
    const DataCacheBindFlags choices[] = {DataCacheBindFlags::INVALID, DataCacheBindFlags::VERTEX_BUFFER,
        DataCacheBindFlags::INDEX_BUFFER, DataCacheBindFlags::CONSTANT_BUFFER, DataCacheBindFlags::ALL_MASK};
    for (const auto mode : choices) {
        policy(mode);
        ID3D11Buffer* buffers[] = {vb.Get(), ib.Get(), cb.Get()};
        const UINT flags[] = {D3D11_BIND_VERTEX_BUFFER, D3D11_BIND_INDEX_BUFFER, D3D11_BIND_CONSTANT_BUFFER};
        for (unsigned i = 0; i < 3; ++i) {
            write_map(device, buffers[i], 20.0f + i);
            const bool enabled = (static_cast<UINT>(mode) & flags[i]) != 0;
            require_batch(cached(buffers[i], value) == enabled && (!enabled || value == 20.0f + i),
                "cache data: actual Map/Unmap respects each selected buffer type");
        }
    }
    value = 123;
    require_batch(!TryReadResourceDataCache(vb.Get(), UINT64_MAX, &value, sizeof(value)) && value == 123 &&
        !TryReadResourceDataCache(vb.Get(), 64, &value, sizeof(value)) && value == 123,
        "cache data: overflow and physical bounds reject without changing output");
    policy(DataCacheBindFlags::VERTEX_BUFFER);
    require_batch(GetRegionHash(device.wrapper.get(), cb.Get(), 0, 4) == 0,
        "cache data: nonselected type does not silently read back a snapshot");
    policy(DataCacheBindFlags::ALL_MASK);
    BatchDevice other;
    auto foreign = tracked_buffer(other, D3D11_BIND_VERTEX_BUFFER, false);
    require_batch(GetRegionHash(device.wrapper.get(), foreign.Get(), 0, 4) == 0,
        "cache data: actual foreign-device readback is rejected");
}
void copies_and_snapshots(BatchDevice& device)
{
    policy(DataCacheBindFlags::ALL_MASK);
    auto source = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER), destination = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER, false);
    write_map(device, source.Get(), 30); warm(device, destination.Get());
    Registry registry;
    auto& snapshot = registry.resource(L"resourcecache_data_snapshot", device, source.Get());
    snapshot.SetHandleInfo(source.Get(), 4, 8);
    require_batch(snapshot.GetHandleInfo() != nullptr, "cache data: real bounded custom snapshot captured");
    write_map(device, source.Get(), 50);
    float old; std::memcpy(&old, snapshot.GetHandleInfo()->GetCachedData(), sizeof(old));
    float value;
    require_batch(old == 31 && cached(source.Get(), value, 4) && value == 51,
        "cache data: writable Map preserves custom snapshot while tracked source becomes current");
    D3D11_BOX box{}; box.left = 4; box.right = 8; box.bottom = box.back = 1;
    device.wrapper->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
    require_batch(cached(destination.Get(), value) && value == 51 && gpu_value(device, destination.Get()) == 51,
        "cache data: partial GPU copy patches existing full snapshot and matches real bytes");
    auto unknown = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER, false);
    device.wrapper->CopySubresourceRegion(unknown.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
    require_batch(!cached(unknown.Get(), value), "cache data: partial copy never fabricates untouched bytes in an unknown snapshot");
    warm(device, unknown.Get());
    require_batch(cached(unknown.Get(), value) && value == 51, "cache data: uncached partial destination safely falls back to real readback");
    auto full = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER, false);
    device.wrapper->CopySubresourceRegion(full.Get(), 0, 0, 0, 0, source.Get(), 0, nullptr);
    require_batch(cached(full.Get(), value) && value == 50 && gpu_value(device, full.Get()) == 50,
        "cache data: complete copy can establish an accurate full snapshot");
    float changed[16]{}; changed[0] = 70;
    device.wrapper->UpdateSubresource(full.Get(), 0, nullptr, changed, 0, 0);
    require_batch(!cached(full.Get(), value), "cache data: wrapper Update invalidates old CPU and region layers");
    warm(device, full.Get()); require_batch(cached(full.Get(), value) && value == 70, "cache data: updated GPU bytes are read back fresh");
    policy(DataCacheBindFlags::INVALID);
    changed[0] = 80; device.wrapper->UpdateSubresource(full.Get(), 0, nullptr, changed, 0, 0);
    policy(DataCacheBindFlags::ALL_MASK);
    require_batch(!cached(full.Get(), value), "cache data: policy transitions discard old snapshots before enabling capture again");
    warm(device, full.Get());
    changed[0] = 90; device.wrapper->UpdateSubresource1(full.Get(), 0, nullptr, changed, 0, 0, 0);
    require_batch(!cached(full.Get(), value) && gpu_value(device, full.Get()) == 90,
        "cache data: D3D11.1 Update also invalidates old tracked bytes");
    warm(device, full.Get()); device.wrapper->DiscardResource(full.Get());
    require_batch(!cached(full.Get(), value), "cache data: discarded native buffer cannot retain readable CPU snapshot");
    snapshot.SetHandleInfo(nullptr, 0, 0);
    require_batch(snapshot.GetHandleInfo() == nullptr, "cache data: null capture clears custom snapshot metadata");
}
void deferred_cpu_updates(BatchDevice& device)
{
    policy(DataCacheBindFlags::ALL_MASK);
    auto mapped_source = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER);
    write_map(device, mapped_source.Get(), 100);
    Registry registry;
    registry.resource(L"resourcecache_deferred_map", device, mapped_source.Get());
    auto& map_result = registry.variable(L"$cache_deferred_map_result", -1);
    const std::wstring ns;
    CommandList map_store; std::wstring map_args = L"$cache_deferred_map_result, resourcecache_deferred_map, 0";
    require_batch(ParseCommandListGeneralCommands(L"CacheDataDeferred", L"store", &map_args,
        &map_store, &map_store, &map_store, &ns), "cache data: actual Store for deferred Map resource");
    ComPtr<ID3D11DeviceContext> base;
    ComPtr<ID3D11DeviceContext1> native;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &base)) && SUCCEEDED(base.As(&native)),
        "cache data: actual deferred Map/copy context");
    HackerContext recorded(device.native.Get(), native.Get()); recorded.SetHackerDevice(device.device);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(recorded.Map(mapped_source.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)),
        "cache data: real deferred wrapper WRITE_DISCARD");
    float future[16]; for (unsigned i = 0; i < 16; ++i) future[i] = 200.0f + i;
    std::memcpy(mapped.pData, future, sizeof(future)); recorded.Unmap(mapped_source.Get(), 0);
    float value;
    execute(map_store, device);
    require_batch(cached(mapped_source.Get(), value) && value == 100 && map_result.fval == 100 &&
        gpu_value(device, mapped_source.Get()) == 100,
        "cache data: recording deferred Map never publishes future bytes to current cache, Store or GPU");
    ComPtr<ID3D11CommandList> list;
    require_batch(SUCCEEDED(native->FinishCommandList(FALSE, &list)), "cache data: finish actual deferred Map list");
    device.wrapper->ExecuteCommandList(list.Get(), FALSE);
    require_batch(!cached(mapped_source.Get(), value), "cache data: submitted deferred Map invalidates old snapshot");
    execute(map_store, device);
    require_batch(map_result.fval == 200 && gpu_value(device, mapped_source.Get()) == 200,
        "cache data: actual submitted deferred Map becomes visible to Store and GPU readback");
    auto copy_source = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER);
    auto copy_destination = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER, false);
    write_map(device, copy_source.Get(), 300); warm(device, copy_destination.Get());
    registry.resource(L"resourcecache_deferred_copy", device, copy_destination.Get());
    auto& copy_result = registry.variable(L"$cache_deferred_copy_result", -1);
    CommandList copy_store; std::wstring copy_args = L"$cache_deferred_copy_result, resourcecache_deferred_copy, 0";
    require_batch(ParseCommandListGeneralCommands(L"CacheDataDeferred", L"store", &copy_args,
        &copy_store, &copy_store, &copy_store, &ns), "cache data: actual Store for deferred copy destination");
    recorded.CopySubresourceRegion(copy_destination.Get(), 0, 0, 0, 0, copy_source.Get(), 0, nullptr);
    execute(copy_store, device);
    require_batch(cached(copy_destination.Get(), value) && value == 1 && copy_result.fval == 1 &&
        gpu_value(device, copy_destination.Get()) == 1,
        "cache data: recording deferred copy retains old destination bytes instead of future source snapshot");
    list.Reset(); require_batch(SUCCEEDED(native->FinishCommandList(FALSE, &list)), "cache data: finish actual deferred copy list");
    device.wrapper->ExecuteCommandList(list.Get(), FALSE);
    require_batch(!cached(copy_destination.Get(), value), "cache data: submitted deferred copy invalidates current snapshot");
    execute(copy_store, device);
    require_batch(copy_result.fval == 300 && gpu_value(device, copy_destination.Get()) == 300,
        "cache data: actual submitted deferred copy yields fresh Store and GPU values");
}
ComPtr<ID3D11ComputeShader> writer(BatchDevice& device)
{
    const char text[] = "RWByteAddressBuffer output : register(u0); [numthreads(1,1,1)] void main(){output.Store(0,asuint(99.0));}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(text, sizeof(text) - 1, "cache-data", nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)),
        "cache data: compile actual GPU writer");
    ComPtr<ID3D11ComputeShader> shader;
    require_batch(SUCCEEDED(device.native->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
        "cache data: create actual GPU writer");
    return shader;
}
void gpu_writes(BatchDevice& device)
{
    policy(DataCacheBindFlags::ALL_MASK);
    auto output = tracked_buffer(device, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        false, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    auto shader = writer(device);
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{}; desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Format = DXGI_FORMAT_R32_TYPELESS; desc.Buffer.NumElements = 16; desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ComPtr<ID3D11UnorderedAccessView> view;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(output.Get(), &desc, &view)), "cache data: real raw writer view");
    ID3D11UnorderedAccessView* bound = view.Get(); UINT keep = UINT_MAX;
    device.context->CSSetShader(shader.Get(), nullptr, 0); device.context->CSSetUnorderedAccessViews(0, 1, &bound, &keep);
    warm(device, output.Get()); float value;
    require_batch(cached(output.Get(), value) && value == 1, "cache data: snapshot starts with pre-dispatch bytes");
    device.wrapper->Dispatch(1, 1, 1);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 99,
        "cache data: real wrapper Dispatch invalidates stale cache before GPU writer");
    float reset[16]{}; reset[0] = 1;
    device.wrapper->UpdateSubresource(output.Get(), 0, nullptr, reset, 0, 0); warm(device, output.Get());
    CommandList commands; std::wstring dispatch = L"1, 1, 1"; const std::wstring ns;
    require_batch(ParseCommandListGeneralCommands(L"CacheData", L"dispatch", &dispatch, &commands, &commands, &commands, &ns),
        "cache data: parse actual native-dispatch command");
    execute(commands, device);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 99,
        "cache data: CommandList direct native Dispatch cannot reuse the warmed old snapshot");
    device.wrapper->UpdateSubresource(output.Get(), 0, nullptr, reset, 0, 0); warm(device, output.Get());
    ComPtr<ID3D11DeviceContext> deferred;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &deferred)), "cache data: actual deferred context");
    deferred->CSSetShader(shader.Get(), nullptr, 0); deferred->CSSetUnorderedAccessViews(0, 1, &bound, &keep);
    deferred->Dispatch(1, 1, 1);
    ComPtr<ID3D11CommandList> list;
    require_batch(SUCCEEDED(deferred->FinishCommandList(FALSE, &list)) && cached(output.Get(), value) && value == 1,
        "cache data: recorded GPU write does not execute before submission");
    device.wrapper->ExecuteCommandList(list.Get(), FALSE);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 99,
        "cache data: actual deferred submit discards stale tracked snapshots before recorded GPU writes");
    warm(device, output.Get());
    const UINT cleared[4] = {};
    device.wrapper->ClearUnorderedAccessViewUint(view.Get(), cleared);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 0,
        "cache data: actual UAV clear invalidates CPU snapshots");
    device.wrapper->UpdateSubresource(output.Get(), 0, nullptr, reset, 0, 0); warm(device, output.Get());
    D3D11_UNORDERED_ACCESS_VIEW_DESC typed_desc{}; typed_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    typed_desc.Format = DXGI_FORMAT_R32_FLOAT; typed_desc.Buffer.NumElements = 16;
    ComPtr<ID3D11UnorderedAccessView> typed_view;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(output.Get(), &typed_desc, &typed_view)),
        "cache data: actual typed float clear view");
    ID3D11UnorderedAccessView* typed_bound = typed_view.Get();
    device.context->CSSetUnorderedAccessViews(0, 1, &typed_bound, &keep);
    ClearViewCommand clear;
    require_batch(clear.target.ParseTarget(L"cs-u0", true, &ns, nullptr), "cache data: actual native ClearView target");
    clear.clear_uav_uint = false;
    for (auto& channel : clear.fval) channel = 33;
    clear.run(&device.state);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 33,
        "cache data: CommandList direct native float UAV clear rejects warmed old snapshot");
    warm(device, output.Get());
    const float floats[4] = {44, 44, 44, 44};
    device.wrapper->ClearUnorderedAccessViewFloat(typed_view.Get(), floats);
    require_batch(!cached(output.Get(), value) && gpu_value(device, output.Get()) == 44,
        "cache data: wrapper native float UAV clear invalidates tracked data");
    policy(DataCacheBindFlags::CONSTANT_BUFFER);
    auto constant = tracked_buffer(device, D3D11_BIND_CONSTANT_BUFFER); write_map(device, constant.Get(), 7);
    device.context->CSSetShader(shader.Get(), nullptr, 0);
    device.context->CSSetUnorderedAccessViews(0, 1, &bound, &keep);
    device.wrapper->Dispatch(1, 1, 1);
    require_batch(cached(constant.Get(), value) && value == 7 && gpu_value(device, output.Get()) == 99,
        "cache data: CB-only policy preserves its snapshot across an actual unrelated GPU dispatch");
    device.context->ClearState();
}
} // namespace

void test_dx11_cache_resource_data_regression()
{
    try {
        CachePolicyState saved;
        BatchDevice device;
        configuration(device); maps_and_modes(device); copies_and_snapshots(device); gpu_writes(device); deferred_cpu_updates(device);
        std::fputs("PASS: real cache-resource policy load/reload, Map/Unmap, bounded copies, snapshots and native GPU-write invalidation\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL cache resource data: %s\n", error.what()); std::exit(EXIT_FAILURE);
    }
}
