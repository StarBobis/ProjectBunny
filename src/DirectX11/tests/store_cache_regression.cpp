#include "slot_range_test_support.h"
#include "ResourceHash.h"
#include "log.h"
#include "util.h"
#include <d3dcompiler.h>
#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>

namespace {
using namespace dx11_slot_test;

// Instrument real COM/import calls; neither observer supplies a fake result.
class PointerPatch {
    void** slot;
    void* original;
public:
    PointerPatch(void** slot, void* replacement) : slot(slot), original(*slot)
    {
        DWORD previous = 0;
        require_batch(VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous) != FALSE,
            "Store cache: make observer pointer writable");
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), replacement);
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) {
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), original);
            if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) std::terminate();
            throw std::runtime_error("Store cache: restore observer page protection");
        }
    }
    ~PointerPatch()
    {
        DWORD previous = 0, ignored = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) std::terminate();
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), original);
        if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) std::terminate();
    }
    PointerPatch(const PointerPatch&) = delete;
    PointerPatch& operator=(const PointerPatch&) = delete;
};

using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
    D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
MapFunction original_map = nullptr;
ID3D11DeviceContext* observed_context = nullptr;
unsigned observed_read_maps = 0;

HRESULT STDMETHODCALLTYPE observe_map(ID3D11DeviceContext* context, ID3D11Resource* resource,
    UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
    if (context == observed_context && type == D3D11_MAP_READ) ++observed_read_maps;
    return original_map(context, resource, subresource, type, flags, mapped);
}

class MapObserver {
    std::unique_ptr<PointerPatch> patch;
public:
    explicit MapObserver(ID3D11DeviceContext* context)
    {
        require_batch(!original_map, "Store cache: Map observer is exclusive");
        // ID3D11DeviceContext ABI: IUnknown/DeviceChild, seven pipeline calls,
        // then Map at vtable index 14. Only this scope changes that real slot.
        auto** vtable = *reinterpret_cast<void***>(context);
        original_map = reinterpret_cast<MapFunction>(vtable[14]);
        observed_context = context;
        observed_read_maps = 0;
        try { patch = std::make_unique<PointerPatch>(&vtable[14], reinterpret_cast<void*>(&observe_map)); }
        catch (...) { original_map = nullptr; observed_context = nullptr; throw; }
    }
    ~MapObserver() { patch.reset(); observed_context = nullptr; original_map = nullptr; }
    void reset() { observed_read_maps = 0; }
    unsigned count() const { return observed_read_maps; }
};

class StoreCacheGlobals {
    DataCacheBindFlags cache = G->cache_resource_data;
    bool tracking = G->track_region_hashes;
    int dirty = G->user_config_dirty;
    unsigned frame = G->frame_no;
public:
    ~StoreCacheGlobals()
    {
        G->cache_resource_data = cache;
        G->track_region_hashes = tracking;
        G->user_config_dirty = dirty;
        G->frame_no = frame;
    }
};

bool parse_cached_store(CommandList& commands, std::wstring arguments)
{
    CommandListScope scope;
    scope.emplace_front();
    commands.scope = &scope;
    const std::wstring ini_namespace;
    const bool result = ParseCommandListGeneralCommands(L"CommandListStoreCacheRegression", L"store", &arguments,
        &commands, &commands, nullptr, &ini_namespace);
    commands.scope = nullptr;
    return result;
}

void store(BatchDevice& device, const std::wstring& arguments)
{
    CommandList command;
    require_batch(parse_cached_store(command, arguments) && command.commands.size() == 1,
        "Store cache: parse actual Store command");
    execute(command, device);
}

ComPtr<ID3D11Buffer> create_tracked_cb(BatchDevice& device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 256;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, nullptr, &result)),
        "Store cache: create actual registered dynamic CB");
    return result;
}

std::vector<float> upload_cb(BatchDevice& device, ID3D11Buffer* buffer, float base)
{
    std::vector<float> values(64);
    for (size_t i = 0; i < values.size(); ++i) values[i] = base + static_cast<float>(i);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.wrapper->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)),
        "Store cache: actual wrapper writable Map");
    memcpy(mapped.pData, values.data(), values.size() * sizeof(float));
    device.wrapper->Unmap(buffer, 0);
    return values;
}

void test_store_cache_paths()
{
    StoreCacheGlobals restore;
    BatchDevice device;
    Registry registry;
    auto& output = registry.variable(L"$store_cache_output", -91);
    output.flags |= VariableFlags::PERSIST;
    auto& offset = registry.variable(L"$store_cache_offset", 3);
    auto& target_index = registry.variable(L"$store_cache_target_index", 0);
    G->track_region_hashes = false;
    G->cache_resource_data = DataCacheBindFlags::CONSTANT_BUFFER;
    auto native = create_tracked_cb(device);
    auto values = upload_cb(device, native.Get(), 100);
    ID3D11Buffer* raw = native.Get();
    device.context->VSSetConstantBuffers(0, 1, &raw);
    MapObserver monitor(device.context.Get());
    float cached = 0;
    require_batch(TryReadResourceDataCache(native.Get(), 12, &cached, sizeof(cached)) && cached == values[3],
        "Store cache: production capture publishes matching CPU snapshot");
    G->user_config_dirty = 2;
    store(device, L"$store_cache_output, vs-cb0, $store_cache_offset");
    require_batch(output.fval == values[3] && monitor.count() == 0 && G->user_config_dirty == 3,
        "Store cache: actual cache hit has no staging READ Map and preserves cleanup dirty bit");
    G->user_config_dirty = 2;
    store(device, L"$store_cache_output, vs-cb0, 3");
    require_batch(G->user_config_dirty == 2 && monitor.count() == 0,
        "Store cache: identical bits remain clean without GPU readback");

    values = upload_cb(device, native.Get(), 300);
    monitor.reset();
    store(device, L"$store_cache_output, vs-cb0, 63.75");
    require_batch(output.fval == values[63] && monitor.count() == 0,
        "Store cache: next real Map replaces snapshot and fractional final float remains cacheable");

    auto& pool = registry.pool(L"poolstore_cache_output", device, PoolIndexType::RING, 2);
    pool.variable_template->flags = static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::PERSIST);
    CommandList pooled;
    require_batch(parse_cached_store(pooled, L"$poolstore_cache_output[$store_cache_target_index], vs-cb0, 4"),
        "Store cache: actual parser accepts dynamic pool variable destination");
    monitor.reset();
    execute(pooled, device);
    require_batch(pool.GetVariable(0, false, true, false)->fval == values[4] && monitor.count() == 0,
        "Store cache: cached value writes real lazy pool variable");
    target_index.fval = 1;
    execute(pooled, device);
    auto* locked = pool.GetVariable(1, false, true, false);
    require_batch(locked && locked->fval == values[4], "Store cache: reused command resolves changed pool destination");
    locked->fval = -17;
    locked->flags |= VariableFlags::LOCKED;
    execute(pooled, device);
    require_batch(locked->fval == -17, "Store cache: locked pool element is not overwritten");

    auto& fixed = registry.pool(L"poolstore_cache_static", device, PoolIndexType::STATIC, 1);
    fixed.element_type_switch_reset = true;
    G->frame_no = 40;
    auto* fixed_resource = fixed.GetResource(0, false, false, true);
    assign(device, fixed_resource, native.Get(), nullptr, 0, 256);
    unsigned fixed_frame = 0;
    require_batch(fixed.GetLastUpdateFrame(0, false, &fixed_frame) && fixed_frame == 40,
        "Store cache: static destination begins with an actual resource update");
    CommandList fixed_store;
    require_batch(parse_cached_store(fixed_store, L"$poolstore_cache_static[0], vs-cb0, $store_cache_offset"),
        "Store cache: actual parser resolves a STATIC destination variable");
    // Baseline STATIC parsing eagerly creates the variable object. It must not
    // perform an assignment/type reset until the source read actually succeeds.
    require_batch(fixed_resource && !fixed_resource->is_null,
        "Store cache: parse-time STATIC variable resolution preserves resource state");
    offset.fval = 64;
    G->frame_no = 41;
    execute(fixed_store, device);
    require_batch(!fixed_resource->is_null && fixed.GetLastUpdateFrame(0, false, &fixed_frame) && fixed_frame == 40,
        "Store cache: failed STATIC source read preserves opposite resource and update frame");
    offset.fval = 2;
    execute(fixed_store, device);
    require_batch(fixed_resource->is_null && fixed.GetVariable(0, false, false, false)->fval == values[2] &&
        fixed.GetLastUpdateFrame(0, false, &fixed_frame) && fixed_frame == 41,
        "Store cache: successful STATIC assignment resets opposite resource and records write frame");

    auto& blocked = registry.pool(L"poolstore_cache_blocked", device, PoolIndexType::FIFO, 1);
    blocked.allocate_slot_on_missing = true;
    CommandList blocked_store;
    require_batch(parse_cached_store(blocked_store, L"$poolstore_cache_blocked[73], vs-cb0, 0"),
        "Store cache: parse missing allocating-read pool before runtime lock");
    blocked.variable_template->flags |= VariableFlags::LOCKED;
    const size_t blocked_resources = customResources.size(), blocked_variables = command_list_globals.size();
    execute(blocked_store, device);
    require_batch(customResources.size() == blocked_resources && command_list_globals.size() == blocked_variables,
        "Store cache: locked template prevents missing-read allocation before assignment lookup");

    auto& empty = registry.pool(L"poolstore_cache_empty", device, PoolIndexType::RING, 0);
    (void)empty;
    store(device, L"$poolstore_cache_empty[0], vs-cb0, 0");
    for (const auto* malformed : {L"", L", vs-cb0, 0", L"$poolstore_cache_output[*], vs-cb0, 0",
        L"poolstore_cache_output[0], vs-cb0, 0", L"$poolstore_cache_missing[0], vs-cb0, 0"}) {
        CommandList rejected;
        require_batch(!parse_cached_store(rejected, malformed) && rejected.commands.empty(),
            "Store cache: empty/invalid/fullrange destination fails without enqueuing a command");
    }
    output.fval = -71;
    G->user_config_dirty = 2;
    for (float invalid : {-1.0f, 64.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        offset.fval = invalid;
        monitor.reset();
        store(device, L"$store_cache_output, vs-cb0, $store_cache_offset");
        require_batch(output.fval == -71 && G->user_config_dirty == 2 && monitor.count() == 0,
            "Store cache: invalid absolute float index fails before cache/GPU and preserves output/dirty");
    }

    G->cache_resource_data = DataCacheBindFlags::INVALID;
    values = upload_cb(device, native.Get(), 500);
    monitor.reset();
    store(device, L"$store_cache_output, vs-cb0, 3");
    require_batch(output.fval == values[3] && monitor.count() == 1,
        "Store cache: disabled capture cannot return stale snapshot and performs actual GPU fallback");

    // A custom snapshot is an allocation slice, not a native global cache.
    // Store still uses absolute backing-buffer indices via GPU fallback.
    G->cache_resource_data = DataCacheBindFlags::CONSTANT_BUFFER;
    values = upload_cb(device, native.Get(), 700);
    registry.resource(L"resourcestore_cache_native", device, native.Get(), DXGI_FORMAT_UNKNOWN, 0, 256);
    auto& alias = registry.resource(L"resourcestore_cache_alias", device);
    assign(device, &alias, native.Get(), nullptr, 128, 64);
    alias.SetHandleInfo(native.Get(), 128, 64);
    auto untracked = buffer(device, 256, D3D11_BIND_CONSTANT_BUFFER);
    device.context->CopyResource(untracked.Get(), native.Get());
    assign(device, &alias, untracked.Get(), nullptr, 128, 64);
    monitor.reset();
    store(device, L"$store_cache_output, resourcestore_cache_alias, 0");
    require_batch(output.fval == values[0] && monitor.count() == 1,
        "Store cache: untracked custom snapshot slice cannot shift absolute backing index zero");
    monitor.reset();
    store(device, L"$store_cache_output, resourcestore_cache_alias, 63");
    require_batch(output.fval == values[63] && monitor.count() == 1,
        "Store cache: absent cache coverage falls back for valid final backing float outside custom view");
    device.context->ClearState();
}

using CompileFunction = decltype(&D3DCompile);
CompileFunction original_compile = nullptr;

class ObservedDiagnosticBlob;
struct BlobRecord {
    ObservedDiagnosticBlob* live = nullptr;
    unsigned releases = 0;
    bool destroyed = false;
    HRESULT compile_result = E_UNEXPECTED;
    bool compiled_code = false;
};
std::vector<std::unique_ptr<BlobRecord>>* observed_blobs = nullptr;

class ObservedDiagnosticBlob final : public ID3DBlob {
    std::atomic<ULONG> references{1};
    ComPtr<ID3DBlob> actual;
    BlobRecord& record;
public:
    ObservedDiagnosticBlob(ID3DBlob* blob, BlobRecord& record) : record(record)
    {
        actual.Attach(blob); // Own precisely the real compiler's output ref.
        record.live = this;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3DBlob)) return E_NOINTERFACE;
        *out = static_cast<ID3DBlob*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ++record.releases;
        const ULONG remaining = --references;
        if (!remaining) { record.live = nullptr; record.destroyed = true; delete this; }
        return remaining;
    }
    LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return actual->GetBufferPointer(); }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return actual->GetBufferSize(); }
};

HRESULT WINAPI observe_compile(LPCVOID data, SIZE_T size, LPCSTR source, const D3D_SHADER_MACRO* defines,
    ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors)
{
    const HRESULT result = original_compile(data, size, source, defines, include, entry, target, flags1, flags2, code, errors);
    if (errors && *errors && observed_blobs) {
        auto record = std::make_unique<BlobRecord>();
        record->compile_result = result;
        record->compiled_code = code && *code;
        auto* wrapper = new ObservedDiagnosticBlob(*errors, *record);
        observed_blobs->push_back(std::move(record));
        *errors = wrapper;
    }
    return result;
}

void** compile_import_slot()
{
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    require_batch(dos && dos->e_magic == IMAGE_DOS_SIGNATURE, "shader diagnostic: actual executable DOS header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    require_batch(nt->Signature == IMAGE_NT_SIGNATURE, "shader diagnostic: actual executable PE header");
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    require_batch(directory.VirtualAddress != 0, "shader diagnostic: executable imports exist");
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (!strcmp(reinterpret_cast<const char*>(name->Name), "D3DCompile"))
                return reinterpret_cast<void**>(&addresses->u1.Function);
        }
    }
    throw std::runtime_error("shader diagnostic: actual D3DCompile import must be observable");
}

class CompileObserver {
    std::unique_ptr<PointerPatch> patch;
public:
    std::vector<std::unique_ptr<BlobRecord>> records;
    CompileObserver()
    {
        require_batch(!original_compile && !observed_blobs, "shader diagnostic: import observer is exclusive");
        auto** slot = compile_import_slot();
        original_compile = reinterpret_cast<CompileFunction>(*slot);
        observed_blobs = &records;
        try { patch = std::make_unique<PointerPatch>(slot, reinterpret_cast<void*>(&observe_compile)); }
        catch (...) { observed_blobs = nullptr; original_compile = nullptr; throw; }
    }
    ~CompileObserver()
    {
        patch.reset();
        observed_blobs = nullptr;
        original_compile = nullptr;
        // If the assertion catches the old production leak, still clean up
        // only diagnostic wrappers created by this fixture after the call ends.
        for (const auto& record : records)
            while (record->live) record->live->Release();
    }
};

class OwnedBuildDirectory {
    std::filesystem::path path;
public:
    explicit OwnedBuildDirectory(const std::filesystem::path& path) : path(path)
    {
        require_batch(CreateDirectoryW(path.c_str(), nullptr) != FALSE,
            "shader diagnostic: exclusively create owned fixture directory");
    }
    ~OwnedBuildDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored); // Only the checked, exclusively owned build subtree.
    }
};

class ShaderDiagnosticEnvironment {
    UINT hunting = G->hunting;
    ShaderHashType hash_type = G->shader_hash_type;
    bool export_shaders = G->EXPORT_SHADERS, export_fixed = G->EXPORT_FIXED,
        export_binary = G->EXPORT_BINARY, cache_shaders = G->CACHE_SHADERS;
    int export_hlsl = G->EXPORT_HLSL;
    bool fix_sv = G->decompiler_settings.fixSvPosition, recompile_vs = G->decompiler_settings.recompileVs;
    std::unique_ptr<OwnedBuildDirectory> owned_directory;
    std::wstring shader_path = G->SHADER_PATH, cache_path = G->SHADER_CACHE_PATH;
    FILE* saved_log = LogFile;
    LogVerbosity saved_level = gLogVerbosity;
    FILE* owned_log = nullptr;
    UINT64 shader_hash;
    std::optional<ShaderBindings> saved_bindings;
public:
    std::filesystem::path directory;
    explicit ShaderDiagnosticEnvironment(UINT64 hash) : shader_hash(hash)
    {
        wchar_t executable[MAX_PATH] = {};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0,
            "shader diagnostic: locate actual regression executable");
        const auto parent = std::filesystem::path(executable).parent_path();
        bool build = false;
        for (const auto& part : parent) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "shader diagnostic: fixtures must stay in build directory");
        directory = parent / (L"store_shader_diagnostic_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
        require_batch(directory.parent_path() == parent && directory.wstring().size() + 64 < MAX_PATH,
            "shader diagnostic: owned fixture path remains bounded under executable directory");
        owned_directory = std::make_unique<OwnedBuildDirectory>(directory);
        {
            std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
            const auto found = G->mShaderBindingsCache.find(hash);
            if (found != G->mShaderBindingsCache.end()) saved_bindings = found->second;
        }
        G->hunting = 0;
        G->shader_hash_type = ShaderHashType::FNV;
        G->EXPORT_SHADERS = G->EXPORT_FIXED = G->EXPORT_BINARY = G->CACHE_SHADERS = false;
        G->EXPORT_HLSL = 0;
        G->decompiler_settings.fixSvPosition = G->decompiler_settings.recompileVs = false;
        const auto absent = directory / L"absent_cache";
        wcscpy_s(G->SHADER_CACHE_PATH, absent.c_str());
    }
    ~ShaderDiagnosticEnvironment()
    {
        LogFile = saved_log;
        gLogVerbosity = saved_level;
        if (owned_log) fclose(owned_log);
        G->hunting = hunting;
        G->shader_hash_type = hash_type;
        G->EXPORT_SHADERS = export_shaders;
        G->EXPORT_FIXED = export_fixed;
        G->EXPORT_BINARY = export_binary;
        G->CACHE_SHADERS = cache_shaders;
        G->EXPORT_HLSL = export_hlsl;
        G->decompiler_settings.fixSvPosition = fix_sv;
        G->decompiler_settings.recompileVs = recompile_vs;
        wcscpy_s(G->SHADER_PATH, shader_path.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, cache_path.c_str());
        {
            std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
            if (saved_bindings) G->mShaderBindingsCache[shader_hash] = *saved_bindings;
            else G->mShaderBindingsCache.erase(shader_hash);
        }
    }
    void select_case(unsigned index, const char* text, bool logging)
    {
        LogFile = saved_log;
        if (owned_log) { fclose(owned_log); owned_log = nullptr; }
        const auto path = directory / std::to_wstring(index);
        require_batch(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "shader diagnostic: exclusive case directory");
        wcscpy_s(G->SHADER_PATH, path.c_str());
        wchar_t name[64];
        swprintf_s(name, L"%016llx-vs_replace.txt", shader_hash);
        const auto file = path / name;
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require_batch(handle != INVALID_HANDLE_VALUE, "shader diagnostic: exclusive replacement source creation");
        DWORD written = 0;
        const bool okay = WriteFile(handle, text, static_cast<DWORD>(strlen(text)), &written, nullptr) != FALSE && written == strlen(text);
        CloseHandle(handle);
        require_batch(okay, "shader diagnostic: write complete actual replacement HLSL");
        if (logging) {
            const auto log_path = path / L"diagnostics.log";
            require_batch(wfopen_exclusive_ensuring_access(&owned_log, log_path.c_str(), L"w") == 0 && owned_log,
                "shader diagnostic: exclusively create owned build-directory log");
            LogFile = owned_log;
            gLogVerbosity = LogVerbosity::INFO;
        } else {
            LogFile = nullptr;
            gLogVerbosity = LogVerbosity::DISABLED;
        }
    }
};

void test_shader_diagnostic_ownership()
{
    static const char original[] = "float4 main(float4 p:POSITION):SV_Position{return p*1.03125;}";
    ComPtr<ID3DBlob> bytecode, errors;
    require_batch(SUCCEEDED(D3DCompile(original, sizeof(original) - 1, "StoreDiagnosticOriginal", nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &bytecode, &errors)), "shader diagnostic: compile real original shader");
    const auto hash = fnv_64_buf(bytecode->GetBufferPointer(), bytecode->GetBufferSize());
    ShaderDiagnosticEnvironment environment(hash);
    BatchDevice device;
    const char* cases[] = {
        "// actual warning fixture\nfloat4 main(float4 p:POSITION):SV_Position{float3 q=p;return float4(q,1);}\n",
        "// actual error fixture\nfloat4 main(float4 p:POSITION):SV_Position{return missing_identifier;}\n"
    };
    unsigned fixture = 0;
    for (bool logging : {false, true}) {
        for (const auto* text : cases) {
            environment.select_case(fixture++, text, logging);
            CompileObserver observer;
            ComPtr<ID3D11VertexShader> shader;
            const HRESULT result = device.device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader);
            require_batch(SUCCEEDED(result) && shader, "shader diagnostic: actual replacement/fallback shader creation succeeds");
            CleanupShaderMaps(shader.Get());
            shader.Reset();
            require_batch(observer.records.size() == 1 && observer.records[0]->destroyed && observer.records[0]->releases == 1,
                "shader diagnostic: real warning/error blob is released exactly once with logging on or off");
            const bool warning = text == cases[0];
            require_batch((SUCCEEDED(observer.records[0]->compile_result) != FALSE) == warning &&
                observer.records[0]->compiled_code == warning,
                "shader diagnostic: observer sees real successful-warning and failed-error compiler outcomes");
        }
    }
}
} // namespace

void test_dx11_store_cache_regression()
{
    test_store_cache_paths();
    test_shader_diagnostic_ownership();
}
