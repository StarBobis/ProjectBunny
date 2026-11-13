#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"
#include "ExpressionFunctions.h"
#include "IniHandler.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdarg>
#include <fstream>

extern bool ParseFloatValue(const wchar_t*, const wchar_t*, const std::wstring&, float&, bool, const std::wstring*);

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

using Bytes = std::vector<unsigned char>;

void append_u32(Bytes& bytes, uint32_t value)
{
    for (int shift : {24, 16, 8, 0}) bytes.push_back(static_cast<unsigned char>(value >> shift));
}

void chunk(Bytes& png, const char* type, const Bytes& data)
{
    append_u32(png, static_cast<uint32_t>(data.size()));
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    uint32_t crc = UINT32_MAX;
    for (size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    append_u32(png, ~crc);
}

Bytes png_image(bool srgb = false, bool late_srgb = false, bool profile = false)
{
    Bytes png = {137, 80, 78, 71, 13, 10, 26, 10};
    chunk(png, "IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
    // iCCP-only is a parser fixture, not a valid ICC profile/decoder test.
    if (profile) chunk(png, "iCCP", {'p', 0, 0, 0});
    if (srgb) chunk(png, "sRGB", {0});
    const Bytes row = {0, 0x12, 0x34, 0x56, 0xff};
    Bytes compressed = {0x78, 0x01, 0x01, 5, 0, 0xfa, 0xff};
    compressed.insert(compressed.end(), row.begin(), row.end());
    uint32_t a = 1, b = 0;
    for (unsigned char sample_byte : row) { a = (a + sample_byte) % 65521; b = (b + a) % 65521; }
    append_u32(compressed, (b << 16) | a);
    chunk(png, "IDAT", compressed);
    if (late_srgb) chunk(png, "sRGB", {0});
    chunk(png, "IEND", {});
    return png;
}

class RangeContext final : public HackerContext {
public:
    std::string captured;
    RangeContext(ID3D11Device1* device, ID3D11DeviceContext1* context) : HackerContext(device, context) {}
    void FrameAnalysisLog(char* format, ...) override
    {
        char buffer[4096];
        va_list arguments;
        va_start(arguments, format);
        const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        if (length > 0) captured.append(buffer, (std::min)(static_cast<size_t>(length), sizeof(buffer) - 1));
    }
};

class RangeDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<RangeContext> wrapper;
    CommandListState state;
    std::vector<std::wstring> resources;
    int saved_dirty = G->user_config_dirty;

    RangeDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate texture property WARP runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load texture property WARP runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve texture property device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            base.GetAddressOf(), nullptr, immediate.GetAddressOf())) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(immediate.As(&context)), "create texture property real DX11 device/context");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<RangeContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
        state.mHackerDevice = device;
        state.mHackerContext = wrapper.get();
        state.mOrigDevice1 = native.Get();
        state.mOrigContext1 = context.Get();
    }

    ~RangeDevice()
    {
        wrapper->ClearState();
        ClearDeferredBindFlags();
        for (auto it = persistent_variables.begin(); it != persistent_variables.end();)
            if ((*it)->name.find(L"$poolrange") == 0) it = persistent_variables.erase(it); else ++it;
        for (auto it = command_list_globals.begin(); it != command_list_globals.end();)
            if (it->first.find(L"$poolrange") == 0) it = command_list_globals.erase(it); else ++it;
        for (auto it = customResourcePools.begin(); it != customResourcePools.end();)
            if (it->first.find(L"poolrange") == 0) it = customResourcePools.erase(it); else ++it;
        for (auto it = customResources.begin(); it != customResources.end();)
            if (it->first.find(L"poolrange") == 0) it = customResources.erase(it); else ++it;
        G->user_config_dirty = saved_dirty;
        for (const auto& name : resources)
            customResources.erase(name);
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }

    CustomResource& add(const wchar_t* id)
    {
        const std::wstring name = id;
        check(customResources.count(name) == 0, "texture property resource fixture must be unique");
        resources.push_back(name);
        auto& resource = customResources[name];
        resource.name = name;
        return resource;
    }

    void seed(const wchar_t* name, ID3D11Resource* resource, ID3D11View* view = nullptr,
        UINT stride = 0, UINT offset = 0, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, UINT size = 0)
    {
        add(name);
        ResourceCopyTarget target;
        const std::wstring ini_namespace;
        check(target.ParseTarget(name, false, &ini_namespace, nullptr), "parse actual custom target for seeding");
        target.SetResource(&state, resource, view, stride, offset, format, size);
    }
};


bool parse_range(CommandList& commands, const std::wstring& lhs, const std::wstring& rhs)
{
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    return ParseCommandListResourceCopyTargetDirective(L"CommandListPoolRangeFlagsRegression", lhs.c_str(),
        &value, &commands, &ini_namespace);
}

void run_range(RangeDevice& device, const std::wstring& lhs, const std::wstring& rhs)
{
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    check(parse_range(commands, lhs, rhs), "parse real pool range/flags command");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

CustomResourcePool& make_pool(RangeDevice& device, const std::wstring& name, PoolIndexType mode,
    bool mixed = true, size_t size = 2)
{
    check(customResourcePools.count(name) == 0, "range pool fixture name is unused");
    auto& pool = customResourcePools[name];
    pool.name = name;
    pool.index_type = mode;
    pool.lazy_initialization = true;
    pool.element_type_switch_reset = !mixed;
    pool.resource_template = &device.add((L"resource" + name + L"template").c_str());
    pool.resource_template->pool = &pool;
    pool.resource_template->pool_index = -1;
    pool.variable_template = std::make_unique<CommandListVariable>(name + L"template", 3,
        mode == PoolIndexType::RING ? VariableFlags::PERSIST : VariableFlags::NONE);
    pool.Initialize(size);
    return pool;
}

void full_range_tests(RangeDevice& device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 64; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &buffer)), "create actual range source buffer");
    device.seed(L"resourcepoolrangebuffer", buffer.Get(), nullptr, 4, 0, DXGI_FORMAT_R32_FLOAT, 64);
    unsigned sequence = 0;
    for (PoolIndexType mode : {PoolIndexType::STATIC, PoolIndexType::RING, PoolIndexType::FIFO, PoolIndexType::SPATIAL}) {
        const auto name = L"poolrange" + std::to_wstring(sequence++);
        auto& pool = make_pool(device, name, mode);
        run_range(device, name + L"[*]", L"ref resourcepoolrangebuffer");
        run_range(device, L"$" + name + L"[*]", L"1 + 8");
        for (unsigned slot = 0; slot < 2; ++slot)
            check(pool.GetResource(static_cast<float>(slot), false, true, false)->resource == buffer.Get() &&
                pool.GetVariable(static_cast<float>(slot), false, true, false)->fval == 9,
                "real fullrange expression populates variables while preserving mixed resource slots in every index mode");
        for (unsigned slot = 0; slot < 2; ++slot) {
            void* cached = std::malloc(64);
            check(cached != nullptr, "allocate actual pool CPU resource cache");
            pool.GetResource(static_cast<float>(slot), false, true, false)->InitializeHandleInfo(cached, 64);
            check(pool.GetResource(static_cast<float>(slot), false, true, false)->handle_info != nullptr,
                "real resource handle cache exists before type-filtered reset");
        }
        run_range(device, name + L"[*]", L"null");
        for (unsigned slot = 0; slot < 2; ++slot) {
            const auto* resource = pool.GetResource(static_cast<float>(slot), false, true, false);
            ResourceCopyTarget observed;
            observed.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
            observed.SetCustomResource(const_cast<CustomResource*>(resource));
            ComPtr<ID3D11View> observed_view;
            ComPtr<ID3D11Resource> observed_resource;
            observed_resource.Attach(observed.GetResource(&device.state, observed_view.GetAddressOf(), nullptr,
                nullptr, nullptr, nullptr));
            check(resource->is_null && resource->resource == buffer.Get() && !resource->handle_info &&
                !resource->stride && !resource->offset && !resource->buf_size && resource->format == DXGI_FORMAT_UNKNOWN &&
                !observed_resource && !observed_view &&
                pool.GetVariable(static_cast<float>(slot), false, true, false)->fval == 9,
                "resource-only null clears metadata and CPU cache, retains native reuse cache and exposes null without changing mixed variables");
        }
        run_range(device, name + L"[*]", L"ref resourcepoolrangebuffer");
        run_range(device, L"$" + name + L"[*]", L"null");
        check(pool.GetVariable(0, false, true, false)->fval == 3 && pool.GetResource(0, false, true, false)->resource == buffer.Get(),
            "variable-only null restores the variable template without resetting mixed resources");
        auto* locked = pool.GetVariable(0, false, true, true);
        locked->fval = 17;
        locked->flags |= VariableFlags::LOCKED;
        G->user_config_dirty = 2;
        run_range(device, L"$" + name + L"[*]", L"7");
        check(locked->fval == 17 && pool.GetVariable(1, false, true, false)->fval == 7 &&
            G->user_config_dirty == (mode == PoolIndexType::RING ? 3 : 2),
            "fullrange assignments preserve LOCKED values and independent pending dirty bits");
        run_range(device, L"$" + name + L"[*]", L"null");
        check(locked->fval == 17 && pool.GetResource(0, false, true, false)->resource == buffer.Get(),
            "variable-only reset also respects LOCKED and mixed resource state");
        auto& proxy = make_pool(device, name + L"proxy", mode);
        check(proxy.SetSourcePool(&pool), "bind real range proxy");
        run_range(device, proxy.name + L"[*]", L"null");
        check(pool.GetResource(0, false, true, false)->is_null && locked->fval == 17,
            "proxy resource reset affects root resource slots while retaining root variables");
        run_range(device, proxy.name, L"null");
        check(proxy.ResolvePool() == &proxy, "whole-pool null resets proxy metadata independently of element filters");

        for (const auto& invalid : std::vector<std::pair<std::wstring, std::wstring>>{
            {name + L"[*]", L"1 + 2"}, {name + L"[*]", L"$" + name + L"[0]"},
            {L"$" + name + L"[*]", L"ref resourcepoolrangebuffer"},
            {L"#" + name + L"[*]", L"null"}, {L"$" + name + L"[*]", L"1 + ()"}}) {
            CommandListScope scope;
            CommandList commands;
            commands.scope = &scope;
            check(!parse_range(commands, invalid.first, invalid.second) && commands.commands.empty(),
                "bad RHS and missing dollar syntax cannot silently fall back to variable fullrange");
        }
    }
    auto& none = make_pool(device, L"poolrangenone", PoolIndexType::STATIC, false);
    const auto resource_count = customResources.size(), variable_count = command_list_globals.size();
    none.ResetElements(PoolElement::ResetType::Resource);
    none.ResetElements(PoolElement::ResetType::Variable);
    check(resource_count == customResources.size() && variable_count == command_list_globals.size(),
        "type-filtered reset of unallocated None elements does not allocate either type");
    auto& empty = make_pool(device, L"poolrangeempty", PoolIndexType::RING, true, 0);
    empty.ResetElements(PoolElement::ResetType::All);

    auto& dynamic = make_pool(device, L"poolrangedynamic", PoolIndexType::FIFO);
    dynamic.PropagateFlags(D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    check(parse_range(commands, dynamic.name + L"[random(1000)]", L"ref resourcepoolrangebuffer"),
        "parse real dynamic random-index destination for analysis logging");
    const bool previous_analysis = G->analyse_frame;
    G->analyse_frame = false;
    SeedExpressionRandom(17, 23);
    auto before = GetExpressionRandomSampleCount();
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
    const auto without_analysis = GetExpressionRandomSampleCount() - before;
    G->analyse_frame = true;
    SeedExpressionRandom(17, 23);
    before = GetExpressionRandomSampleCount();
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
    const auto with_analysis = GetExpressionRandomSampleCount() - before;
    G->analyse_frame = previous_analysis;
    check(with_analysis > 0 && with_analysis == without_analysis &&
        device.wrapper->captured.find("src bind_flags=") != std::string::npos &&
        device.wrapper->captured.find("dst bind_flags=") != std::string::npos,
        "actual frame-analysis copy log formats both flags without adding any dynamic-index evaluations");
}

void graph_tests(RangeDevice& device)
{
    auto& source = device.add(L"resourcepoolrangegraphsource");
    auto& alias = device.add(L"resourcepoolrangegraphalias");
    auto& slot = device.add(L"resourcepoolrangegraphslot");
    auto& copied = device.add(L"resourcepoolrangegraphcopy");
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    check(parse_range(commands, alias.name, L"ref " + source.name) &&
        parse_range(commands, slot.name, L"ref " + alias.name) &&
        parse_range(commands, source.name, L"ref " + slot.name) &&
        parse_range(commands, copied.name, L"copy " + source.name) &&
        parse_range(commands, L"ps-t3", L"ref " + slot.name), "parse reverse-order multihop/cycle ref graph and independent copy");
    PropagateDeferredBindFlags();
    check((source.bind_flags & D3D11_BIND_SHADER_RESOURCE) && (alias.bind_flags & D3D11_BIND_SHADER_RESOURCE) &&
        (slot.bind_flags & D3D11_BIND_SHADER_RESOURCE), "fixed point resolves reverse chains and cycles to pipeline SRV requirements");
    check(!(copied.bind_flags & D3D11_BIND_SHADER_RESOURCE), "copy graph edges do not propagate unrelated downstream requirements");

    wchar_t executable[MAX_PATH] = {};
    check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate actual WIC graph fixture executable");
    const auto directory = std::filesystem::path(executable).parent_path();
    bool in_build = false;
    for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
    check(in_build, "WIC graph fixture must stay inside build");
    const auto png_path = directory / (L"pool_range_flags_" + std::to_wstring(GetCurrentProcessId()) + L".png");
    HANDLE file = CreateFileW(png_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "exclusively create actual graph WIC texture");
    const auto bytes = png_image();
    DWORD written = 0;
    const bool complete = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    if (!complete) DeleteFileW(png_path.c_str());
    check(complete, "write actual graph PNG bytes");
    source.filename = png_path.wstring();
    source.Substantiate(device.native.Get(), static_cast<D3D11_BIND_FLAG>(0), static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    DeleteFileW(png_path.c_str());
    ComPtr<ID3D11Texture2D> texture;
    check(source.resource && SUCCEEDED(source.resource->QueryInterface(IID_PPV_ARGS(&texture))),
        "reference graph supplies effective flags to actual file-backed WIC texture creation");
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture->GetDesc(&texture_desc);
    check(texture_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE,
        "actual native WIC texture carries shader-resource bind flag from reverse reference chain");


    auto& pool = make_pool(device, L"poolrangegraphpool", PoolIndexType::RING);
    auto* existing = pool.GetResource(0, false, true, true);
    auto& graph_alias = device.add(L"resourcepoolrangepoolalias");
    CommandList pool_commands;
    pool_commands.scope = &scope;
    check(parse_range(pool_commands, graph_alias.name, L"ref " + pool.name + L"[0]") &&
        parse_range(pool_commands, L"ps-t4", L"ref " + graph_alias.name), "parse pool root to alias reverse flag graph");
    PropagateDeferredBindFlags();
    check((pool.resource_template->bind_flags & D3D11_BIND_SHADER_RESOURCE) &&
        (existing->bind_flags & D3D11_BIND_SHADER_RESOURCE) &&
        (pool.GetResource(1, false, true, true)->bind_flags & D3D11_BIND_SHADER_RESOURCE),
        "graph propagates to pool template, existing slots and later lazy slots");

    CustomResource conflict;
    conflict.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    conflict.misc_flags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    check(!conflict.AddFlags(D3D11_BIND_CONSTANT_BUFFER, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
        conflict.bind_flags == D3D11_BIND_SHADER_RESOURCE && conflict.misc_flags == D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        "CB conflict validates before changing either bind or misc metadata");
    auto& transactional = make_pool(device, L"poolrangeconflict", PoolIndexType::RING);
    auto* conflicting_slot = transactional.GetResource(0, false, true, true);
    conflicting_slot->bind_flags = D3D11_BIND_SHADER_RESOURCE;
    check(!transactional.PropagateFlags(D3D11_BIND_CONSTANT_BUFFER, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
        transactional.resource_template->bind_flags == 0 && transactional.resource_template->misc_flags == 0 &&
        conflicting_slot->bind_flags == D3D11_BIND_SHADER_RESOURCE && conflicting_slot->misc_flags == 0,
        "pool propagation rejects conflicts atomically before modifying template or any slot");

    // The lifecycle entry is used before maps are replaced on configuration reload.
    CommandList stale;
    stale.scope = &scope;
    auto& stale_source = device.add(L"resourcepoolrangestalesource");
    auto& stale_destination = device.add(L"resourcepoolrangestaledestination");
    check(parse_range(stale, stale_destination.name, L"ref " + stale_source.name), "register old config edge");
    ClearDeferredBindFlags();
    stale_destination.bind_flags = D3D11_BIND_SHADER_RESOURCE;
    PropagateDeferredBindFlags();
    check(stale_source.bind_flags == 0, "clearing config edges prevents stale references from propagating after reload");
}

class RangeReloadFiles {
public:
    std::filesystem::path config, user;
    std::vector<std::filesystem::path> owned;
    HINSTANCE module = migoto_handle;
    std::wstring previous_user = G->user_config;
    int dirty = G->user_config_dirty;
    bool initialized = G->gConfigInitialized, pending = G->gReloadConfigPending, wipe = G->gWipeUserConfig;
    LogVerbosity level = gLogVerbosity;
    bool debug = gLogDebug;
    ~RangeReloadFiles()
    {
        ClearDeferredBindFlags();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        customResourcePools.clear(); customResources.clear();
        persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; G->user_config = previous_user; G->user_config_dirty = dirty;
        G->gConfigInitialized = initialized; G->gReloadConfigPending = pending; G->gWipeUserConfig = wipe;
        gLogVerbosity = level; gLogDebug = debug;
        for (const auto& path : owned) DeleteFileW(path.c_str());
    }
    void create(const std::filesystem::path& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse pre-existing range graph reload fixture");
        CloseHandle(file); owned.push_back(path);
    }
    void initialize()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate real reload test executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "graph reload files stay under build");
        config = directory / L"d3dx.ini";
        user = directory / (L"pool_range_flags_user_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        create(config); create(user);
        migoto_handle = nullptr; G->gConfigInitialized = false; G->user_config_dirty = 0;
        G->gReloadConfigPending = false; G->gWipeUserConfig = false;
    }
    void configure(bool original)
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "clear_unknown_settings=0\n[Include]\nuser_config=" << user.filename().string() << "\n";
        if (original)
            file << "[ResourceRangeReloadA]\n[ResourceRangeReloadB]\n";
        else
            file << "[ResourceRangeReloadCurrent]\n";
        file.close();
        check(file.good(), "write real graph reload configuration");
    }
};

void actual_reload_graph_test(RangeDevice& device)
{
    RangeReloadFiles files;
    files.initialize();
    files.configure(true);
    LoadConfigFile();
    CommandListScope scope;
    CommandList pending;
    pending.scope = &scope;
    check(parse_range(pending, L"resourcerangereloada", L"ref resourcerangereloadb"),
        "record actual pending pointer edge from old loaded registry");
    files.configure(false);
    ReloadConfig(device.device);
    PropagateDeferredBindFlags();
    check(!G->gReloadConfigPending && customResources.count(L"resourcerangereloada") == 0 &&
        customResources.count(L"resourcerangereloadb") == 0 && customResources.count(L"resourcerangereloadcurrent") == 1 &&
        customResources.at(L"resourcerangereloadcurrent").bind_flags == 0,
        "real reload clears graph before old pointer owners and retains only current registry flags");
}
} // namespace

void test_dx11_pool_range_flags_regression()
{
    try {
        RangeDevice device;
        full_range_tests(device);
        graph_tests(device);
        actual_reload_graph_test(device);
        std::puts("PASS: real pool resource/variable range filters, proxy/locked reset and fixed-point reference flags");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: pool range/flags regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
