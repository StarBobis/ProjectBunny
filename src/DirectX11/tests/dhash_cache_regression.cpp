#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "lock.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <clocale>
#include <cstdlib>
extern std::vector<CommandList*> pending_post_command_lists;
namespace {
using namespace dx11_slot_test;
void check(bool value, const char* message) { require_batch(value, message); }
class DhashConfig {
    std::filesystem::path config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    unsigned saved_frame = G->frame_no;
    bool saved_disable = G->disable_input;
    bool saved_input_initialized = G->disable_input_initialized;
    InputDisableScope saved_scope = G->input_disable_scope;
    int saved_mip = G->dhash_mip_size;
    bool saved_rehash = G->dhash_allow_rehash;
    bool owns_config = false;
    bool owns_settings = false;
public:
    DhashConfig()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate metadata-test executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "metadata fixtures must remain in build/");
        config = base / L"d3dx.ini";
        settings = base / (L"dhash_cache_config_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing metadata settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing metadata d3dx.ini");
        CloseHandle(file);
        owns_config = true;
        file = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileW(config.c_str()); owns_config = false;
            throw std::runtime_error("refuse existing original metadata user fixture");
        }
        CloseHandle(file); owns_settings = true;
        migoto_handle = nullptr;
    }

    void load(const std::string& rendering = "")
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[Rendering]\n" << rendering;
        out.close();
        check(out.good(), "write actual resource metadata INI");
        ClearKeyBindings();
        LoadConfigFile();

    }

    ~DhashConfig()
    {
        ClearKeyBindings();
        pending_post_command_lists.clear();
        CurrentTransition.Stop();
        OverrideSave.Reset(nullptr);
        presetOverrides.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        G->dhash_mip_size = saved_mip; G->dhash_allow_rehash = saved_rehash;
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->frame_no = saved_frame;
        G->disable_input = saved_disable;
        G->disable_input_initialized = saved_input_initialized;
        G->input_disable_scope = saved_scope;
        if (owns_config) DeleteFileW(config.c_str());
        if (owns_settings) DeleteFileW(settings.c_str());
    }
};

class DhashOutputScope {
    std::wstring saved_path = G->ANALYSIS_PATH;
    bool saved_analysis = G->analyse_frame, saved_registered = G->frame_analysis_registered;
    bool saved_foreground = G->check_foreground_window;
    std::wstring saved_title = G->additionalForegroundWindowTitle;
    int saved_hunting = G->hunting;
    unsigned saved_frame = G->frame_no, saved_ini = G->IniParamsReg;
    FrameAnalysisOptions saved_current = G->cur_analyse_options, saved_default = G->def_analyse_options;
    std::string saved_locale = std::setlocale(LC_CTYPE, nullptr);
public:
    std::filesystem::path path;
    DhashOutputScope()
    {
        DWORD foreground_process = 0;
        const HWND foreground = GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground, &foreground_process);
        require_batch(!foreground || foreground_process != GetCurrentProcessId(),
            "PS dump: headless regression process is in the background");
        wchar_t executable[MAX_PATH] = {};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "PS dump: locate actual test binary");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool build = false;
        for (const auto& part : base) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "PS dump: output must remain under build directory");
        path = base / (L"dhash_cache_log_" + std::to_wstring(GetCurrentProcessId()));
        require_batch(path.parent_path() == base && !std::filesystem::exists(path) && std::filesystem::create_directory(path),
            "PS dump: create exclusive owned build subtree");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        G->check_foreground_window = true;
        G->additionalForegroundWindowTitle.clear();
        G->analyse_frame = true;
        G->frame_analysis_registered = true;
        G->hunting = HUNTING_MODE_DISABLED;
        G->IniParamsReg = 11;
        G->frame_no = 100;
        G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~DhashOutputScope()
    {
        G->analyse_frame = saved_analysis; G->frame_analysis_registered = saved_registered;
        G->check_foreground_window = saved_foreground;
        G->additionalForegroundWindowTitle = std::move(saved_title);
        G->hunting = saved_hunting; G->frame_no = saved_frame; G->IniParamsReg = saved_ini;
        G->cur_analyse_options = saved_current; G->def_analyse_options = saved_default;
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, saved_path.c_str());
        std::setlocale(LC_CTYPE, saved_locale.c_str());
        std::error_code error;
        std::filesystem::remove_all(path, error); // Checked exclusive child of the actual build directory.
        if (error) std::fprintf(stderr, "PS dump cleanup: %s\n", error.message().c_str());
    }
    std::filesystem::path select(const wchar_t* name)
    {
        const auto child = path / name;
        require_batch(child.parent_path() == path && std::filesystem::create_directory(child),
            "PS dump: exclusive case directory");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, child.c_str());
        return child;
    }
};

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
ID3D11Texture2D* change_during_readback = nullptr;
unsigned read_maps = 0;
bool locks_free = false;
bool wrap_during_readback = false;

HRESULT STDMETHODCALLTYPE observe_map(ID3D11DeviceContext* context, ID3D11Resource* resource,
    UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
    const HRESULT hr = original_map(context, resource, subresource, type, flags, mapped);
    if (context != observed_context || type != D3D11_MAP_READ || FAILED(hr)) return hr;
    ++read_maps;
    // A different thread is required: Windows critical sections are recursive.
    std::thread probe([] {
        const bool global = TryEnterCriticalSection(&G->mCriticalSection) != FALSE;
        const bool resources = TryEnterCriticalSection(&G->mResourcesLock) != FALSE;
        locks_free = global && resources;
        if (resources) LeaveCriticalSection(&G->mResourcesLock);
        if (global) LeaveCriticalSection(&G->mCriticalSection);
    });
    probe.join();
    if (change_during_readback) {
        auto* texture = change_during_readback;
        change_during_readback = nullptr;
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        std::vector<UINT> values(desc.Width * desc.Height, 0xffffffff);
        context->UpdateSubresource(texture, 0, nullptr, values.data(), desc.Width * sizeof(UINT), 0);
        if (wrap_during_readback) {
            CriticalSectionGuard(&G->mResourcesLock);
            G->mResources.at(texture).resource_generation = UINT64_MAX;
        }
        UpdateTextureSubresourceState(texture, 0, true, false);
    }
    return hr;
}
class DhashMapObserver {
    std::unique_ptr<PointerPatch> patch;
public:
    explicit DhashMapObserver(ID3D11DeviceContext* context)
    {
        check(!original_map, "dhash cache: exclusive actual Map observer");
        auto** table = *reinterpret_cast<void***>(context);
        original_map = reinterpret_cast<MapFunction>(table[14]);
        observed_context = context;
        try { patch = std::make_unique<PointerPatch>(&table[14], reinterpret_cast<void*>(&observe_map)); }
        catch (...) { original_map = nullptr; observed_context = nullptr; throw; }
        reset();
    }
    ~DhashMapObserver()
    {
        patch.reset(); original_map = nullptr; observed_context = nullptr;
        change_during_readback = nullptr; wrap_during_readback = false;
    }
    void reset() { read_maps = 0; locks_free = false; }
};

ComPtr<ID3D11Texture2D> tracked(BatchDevice& device, UINT mips = 1,
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, bool initial = true, UINT colour = 0)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 32; desc.MipLevels = mips; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
    std::vector<std::vector<UINT>> pixels(mips);
    std::vector<D3D11_SUBRESOURCE_DATA> data(mips);
    for (UINT mip = 0; mip < mips; ++mip) {
        const UINT width = (std::max)(1u, desc.Width >> mip), height = (std::max)(1u, desc.Height >> mip);
        pixels[mip].assign(width * height, colour);
        data[mip].pSysMem = pixels[mip].data(); data[mip].SysMemPitch = width * sizeof(UINT);
    }
    ComPtr<ID3D11Texture2D> result;
    check(SUCCEEDED(device.device->CreateTexture2D(&desc, initial ? data.data() : nullptr, &result)),
        "dhash cache: real tracked multi-mip texture");
    return result;
}
void update(BatchDevice& device, ID3D11Texture2D* texture, UINT mip, UINT value)
{
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    const UINT width = (std::max)(1u, desc.Width >> mip), height = (std::max)(1u, desc.Height >> mip);
    std::vector<UINT> pixels(width * height, value);
    device.wrapper->UpdateSubresource(texture, mip, nullptr, pixels.data(), width * sizeof(UINT), 0);
}
void require_cache(ID3D11Resource* resource, UINT mip, DXGI_FORMAT format, UINT preference)
{
    CriticalSectionGuard(&G->mResourcesLock);
    const auto& info = G->mResources.at(resource);
    check(info.dhash_valid && info.dhash_mip_level == mip && info.dhash_format == format &&
        info.dhash_prefer_mip_size == preference, "dhash cache: actual published mip, format and policy identity");
}
void cache_cases(BatchDevice& device)
{
    auto black = tracked(device);
    DhashMapObserver monitor(device.context.Get());
    uint64_t value = 123;
    check(GetDhash(device.wrapper.get(), black.Get(), 256, false, value) && value == 0 && read_maps == 1 && locks_free,
        "dhash cache: valid zero from real GPU, first readback and GPU outside both locks");
    monitor.reset();
    check(GetDhash(device.wrapper.get(), black.Get(), 256, false, value) && value == 0 && read_maps == 0,
        "dhash cache: valid zero cache hit avoids Map");
    update(device, black.Get(), 0, 0xffffffff);
    check(GetDhash(device.wrapper.get(), black.Get(), 256, false, value) && value == 0 && read_maps == 0,
        "dhash cache: first no-rehash call after upload immediately retains old zero cache");
    check(GetDhash(device.wrapper.get(), black.Get(), 256, true, value) && value != 0 && read_maps == 1,
        "dhash cache: enabling rehash computes changed data rather than marking retained value fresh");

    auto mips = tracked(device, 3);
    monitor.reset();
    check(GetDhash(device.wrapper.get(), mips.Get(), 8, false, value), "dhash cache: smallest eligible initialized mip");
    require_cache(mips.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM, 8);
    check(GetDhash(device.wrapper.get(), mips.Get(), 0, false, value) && read_maps == 2,
        "dhash cache: changed mip preference cannot reuse a different policy cache");
    require_cache(mips.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    update(device, mips.Get(), 1, 0xffffffff);
    check(GetDhash(device.wrapper.get(), mips.Get(), 8, true, value) && value != 0,
        "dhash cache: rehash chooses latest uploaded mip rather than an older small mip");
    require_cache(mips.Get(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, 8);

    auto typeless = tracked(device, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, true, 0x80402010);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> linear, srgb;
    check(SUCCEEDED(device.native->CreateShaderResourceView(typeless.Get(), &sd, &linear)), "dhash cache: typed linear view");
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    check(SUCCEEDED(device.native->CreateShaderResourceView(typeless.Get(), &sd, &srgb)), "dhash cache: sRGB view of same resource");
    monitor.reset(); uint64_t linear_hash = 0, srgb_hash = 0;
    check(GetDhash(device.wrapper.get(), typeless.Get(), 256, false, linear_hash, linear.Get()) &&
        GetDhash(device.wrapper.get(), typeless.Get(), 256, false, srgb_hash, srgb.Get()) &&
        read_maps == 2 && linear_hash != srgb_hash, "dhash cache: actual sampling format changes hash and misses cache");
    D3D11_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = sd.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    check(SUCCEEDED(device.native->CreateRenderTargetView(typeless.Get(), &rd, &rtv)), "dhash cache: real generic RTV");
    check(GetDhash(device.wrapper.get(), typeless.Get(), 256, false, value, rtv.Get()) && value == srgb_hash && read_maps == 2,
        "dhash cache: generic RTV queries correct descriptor and shares same-format cache");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R8G8B8A8_UNORM; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11UnorderedAccessView> uav;
    check(SUCCEEDED(device.native->CreateUnorderedAccessView(typeless.Get(), &ud, &uav)), "dhash cache: real generic UAV");
    check(GetDhash(device.wrapper.get(), typeless.Get(), 256, false, value, uav.Get()) && value == linear_hash,
        "dhash cache: generic UAV queries correct descriptor without SRV cast");
    value = 123;
    check(!GetDhash(device.wrapper.get(), black.Get(), 256, false, value, linear.Get()) && value == 0,
        "dhash cache: foreign view rejected before cache hit");

    auto unsupported = tracked(device, 1, DXGI_FORMAT_R32_UINT);
    monitor.reset(); value = 123;
    check(!GetDhash(device.wrapper.get(), unsupported.Get(), 256, false, value) && value == 0 && read_maps == 0,
        "dhash cache: unsupported filtered integer sampling cannot become a valid zero cache");
    { CriticalSectionGuard(&G->mResourcesLock);
      check(!G->mResources.at(unsupported.Get()).dhash_valid, "dhash cache: engine failure never publishes cache"); }
    D3D11_TEXTURE2D_DESC rejected_desc{};
    black->GetDesc(&rejected_desc); rejected_desc.ArraySize = 2;
    ComPtr<ID3D11Texture2D> array_texture;
    check(SUCCEEDED(device.device->CreateTexture2D(&rejected_desc, nullptr, &array_texture)), "dhash cache: actual array texture");
    check(!GetDhash(device.wrapper.get(), array_texture.Get(), 256, false, value), "dhash cache: array resource unavailable");
    rejected_desc.ArraySize = 1; rejected_desc.SampleDesc.Count = 2;
    rejected_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> multisample_texture;
    check(SUCCEEDED(device.device->CreateTexture2D(&rejected_desc, nullptr, &multisample_texture)), "dhash cache: actual MSAA texture");
    check(!GetDhash(device.wrapper.get(), multisample_texture.Get(), 256, false, value), "dhash cache: MSAA resource unavailable");

    auto uninitialized = tracked(device, 1, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    auto untracked = texture(device, 0);
    auto native_buffer = buffer(device, 64, D3D11_BIND_VERTEX_BUFFER);
    monitor.reset();
    for (ID3D11Resource* absent : {static_cast<ID3D11Resource*>(nullptr), static_cast<ID3D11Resource*>(uninitialized.Get()),
        static_cast<ID3D11Resource*>(untracked.Get()), static_cast<ID3D11Resource*>(native_buffer.Get())}) {
        value = 123;
        check(!GetDhash(device.wrapper.get(), absent, 256, false, value) && value == 0,
            "dhash cache: missing/uninitialized/untracked/buffer are unavailable rather than valid zero");
    }
    check(read_maps == 0, "dhash cache: unavailable paths never read back");
    ComPtr<ID3D11DeviceContext1> deferred;
    check(SUCCEEDED(device.native->CreateDeferredContext1(0, &deferred)), "dhash cache: actual deferred context");
    HackerContext deferred_wrapper(device.native.Get(), deferred.Get());
    deferred_wrapper.SetHackerDevice(device.device);
    check(!GetDhash(&deferred_wrapper, black.Get(), 256, false, value), "dhash cache: deferred cache hit cannot bypass context validation");
    {
        BatchDevice other;
        check(!GetDhash(other.wrapper.get(), black.Get(), 256, false, value), "dhash cache: foreign device cache hit rejected");
    }

    auto racing = tracked(device);
    monitor.reset(); change_during_readback = racing.Get();
    check(!GetDhash(device.wrapper.get(), racing.Get(), 256, false, value) && value == 0 && locks_free,
        "dhash cache: actual upload during readback prevents stale publication");
    { CriticalSectionGuard(&G->mResourcesLock);
      check(!G->mResources.at(racing.Get()).dhash_valid, "dhash cache: race leaves cache unavailable"); }
    check(GetDhash(device.wrapper.get(), racing.Get(), 256, false, value) && value != 0,
        "dhash cache: query after rejected publication retries current content");
    // Capture generation 1 again, then force wrap back to 1 during GPU readback.
    { CriticalSectionGuard(&G->mResourcesLock);
      auto& info = G->mResources.at(racing.Get()); info.InitializeSubresourceTracking(1, 1, true); }
    monitor.reset(); change_during_readback = racing.Get(); wrap_during_readback = true;
    check(!GetDhash(device.wrapper.get(), racing.Get(), 256, false, value) && value == 0,
        "dhash cache: identity token rejects generation-wrap ABA even when generation and mip return to one");
    wrap_during_readback = false;
    check(GetDhash(device.wrapper.get(), racing.Get(), 256, false, value), "dhash cache: wrapped state can compute fresh cache");
}

std::string log_bytes(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    check(input.good(), "dhash cache: actual frame-analysis log exists");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
void analysis_cases(BatchDevice& device)
{
    DhashOutputScope output;
    auto available = tracked(device);
    auto absent = texture(device, 0);
    auto available_view = srv(device, available.Get()), absent_view = srv(device, absent.Get());
    const auto directory = output.select(L"real-log");
    {
        FrameAnalysisContext analysis(device.native.Get(), device.context.Get());
        analysis.SetHackerDevice(device.device);
        DhashMapObserver monitor(device.context.Get());
        // The tracked resource deliberately has no optional mResourceInfo entry.
        { CriticalSectionGuard(&G->mResourcesLock);
          G->mResources.at(available.Get()).hash = G->mResources.at(available.Get()).orig_hash = 0x51cafe01; }
        check(!G->mResourceInfo.count(0x51cafe01), "dhash cache: unique missing contamination metadata");
        ID3D11ShaderResourceView* view = available_view.Get();
        analysis.PSSetShaderResources(0, 1, &view);
        check(locks_free && read_maps == 1, "dhash cache: real FrameAnalysis path computes outside both locks");
        view = absent_view.Get(); analysis.PSSetShaderResources(1, 1, &view);
    }
    const auto text = log_bytes(directory / L"log.txt");
    const auto zero = text.find("dhash=0000000000000000");
    check(zero != std::string::npos && text.find("dhash=", zero + 1) == std::string::npos &&
        text.find("hash=51cafe01") != std::string::npos,
        "dhash cache: real log distinguishes valid zero from unavailable even without optional contamination info");
    device.context->ClearState();
}
} // namespace

void test_dx11_dhash_cache_regression()
{
    try {
        DhashConfig config;
        config.load();
        check(G->dhash_mip_size == 256 && !G->dhash_allow_rehash, "dhash cache: Rendering missing defaults");
        config.load("dhash_mip_size=8\ndhash_allow_rehash=1\n");
        check(G->dhash_mip_size == 8 && G->dhash_allow_rehash, "dhash cache: real repeated load observes Rendering settings");
        config.load();
        check(G->dhash_mip_size == 256 && !G->dhash_allow_rehash, "dhash cache: repeated load resets omitted options");
        BatchDevice device;
        cache_cases(device);
        analysis_cases(device);
        std::fputs("PASS: real GPU dHash cache identity, zero/unavailable, rehash, mip/view policy, publication races and frame log\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: dhash cache regression: %s\n", error.what()); std::exit(EXIT_FAILURE);
    }
}
