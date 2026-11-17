#include "IniHandler.h"
#include <atomic>
#include <exception>
#include <thread>

extern "C" size_t dx11_audit_create_texture2d_slot(void);

namespace {
class OverrideAuditEvent {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
public:
    OverrideAuditEvent() { require_batch(handle != nullptr, "F176: create owned scheduling event"); }
    ~OverrideAuditEvent() { CloseHandle(handle); }
    OverrideAuditEvent(const OverrideAuditEvent&) = delete;
    OverrideAuditEvent& operator=(const OverrideAuditEvent&) = delete;
    void signal() noexcept { if (!SetEvent(handle)) std::abort(); }
    void wait() { require_batch(WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0, "F176: scheduling deadline"); }
};

using OverrideNativeCreate = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device1*, const D3D11_TEXTURE2D_DESC*,
    const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
OverrideNativeCreate override_native_create = nullptr;
class OverrideReloadRace;
thread_local OverrideReloadRace* override_reload_race = nullptr;
HRESULT STDMETHODCALLTYPE observe_override_create(ID3D11Device1*, const D3D11_TEXTURE2D_DESC*,
    const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);

class OverrideCreateObserver {
    void** slot;
    void* original;
    static void set(void** address, void* value)
    {
        DWORD protection = 0;
        require_batch(VirtualProtect(address, sizeof(*address), PAGE_READWRITE, &protection) != FALSE,
            "F176: protect actual SDK CreateTexture2D slot");
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(address), value);
        DWORD ignored = 0;
        if (!VirtualProtect(address, sizeof(*address), protection, &ignored)) std::abort();
    }
public:
    explicit OverrideCreateObserver(ID3D11Device1* device)
    {
        require_batch(!override_native_create, "F176: exclusive native observer");
        auto** table = *reinterpret_cast<void***>(device);
        slot = table + dx11_audit_create_texture2d_slot();
        original = *slot;
        override_native_create = reinterpret_cast<OverrideNativeCreate>(original);
        try { set(slot, reinterpret_cast<void*>(&observe_override_create)); }
        catch (...) { override_native_create = nullptr; throw; }
    }
    ~OverrideCreateObserver()
    {
        try { set(slot, original); } catch (...) { std::abort(); }
        override_native_create = nullptr;
    }
};

std::vector<int> override_iteration_values()
{
    CriticalSectionGuard(&G->mCriticalSection);
    std::vector<int> result;
    for (const auto& pair : G->mTextureOverrideMap)
        for (const auto& rule : pair.second)
            if (!rule.iterations.empty()) result.push_back(rule.iterations.front());
    for (const auto& matcher : G->mFuzzyTextureOverrides)
        if (!matcher->texture_override->iterations.empty())
            result.push_back(matcher->texture_override->iterations.front());
    return result;
}

// The match hook waits only for a nonblocking lock probe. After acknowledging
// that probe, the writer attempts real reload while production consumes matches.
class OverrideReloadRace {
    HackerDevice* device;
    bool fuzzy;
    OverrideCreateObserver observer;
    OverrideAuditEvent matched, match_probed, native_entered, native_probed;
    std::atomic<bool> cancelled{false}, match_protected{false}, native_unlocked{false}, writer_failed{false};
    std::thread writer;
    std::exception_ptr writer_failure;
    void reload()
    {
        ReloadConfig(device);
        CriticalSectionGuard(&G->mCriticalSection);
        if (fuzzy) {
            require_batch(G->mTextureOverrideMap.empty() && G->mFuzzyTextureOverrides.size() == 1,
                "F176 fixture: real reload replaced the old fuzzy registry");
            const auto* rule = (*G->mFuzzyTextureOverrides.begin())->texture_override;
            require_batch(rule->width == 12 && rule->iterations == std::vector<int>{0, 1},
                "F176 fixture: real reload published the new fuzzy rule and reset its counter");
        } else {
            require_batch(G->mTextureOverrideMap.size() == 1 && G->mTextureOverrideMap.begin()->second.size() == 1,
                "F176 fixture: real reload destroyed the old two-rule exact list");
            const auto& rule = G->mTextureOverrideMap.begin()->second.front();
            require_batch(rule.width == 12 && rule.iterations == std::vector<int>{0, 1},
                "F176 fixture: real reload published the new exact rule and reset its counter");
        }
        reloaded = true;
        std::fprintf(stderr, "F176: real %s reload replacement verified\n", fuzzy ? "fuzzy" : "exact");
        std::fflush(stderr);
    }
    void run_writer()
    {
        try {
            matched.wait();
            if (cancelled) return;
            const bool entered = TryEnterCriticalSection(&G->mCriticalSection) != FALSE;
            if (entered) {
                LeaveCriticalSection(&G->mCriticalSection);
                reload();
                match_probed.signal();
                return;
            }
            match_protected = true;
            match_probed.signal();
            reload();
            native_entered.wait();
            if (cancelled) return;
            const bool global = TryEnterCriticalSection(&G->mCriticalSection) != FALSE;
            const bool resources = TryEnterCriticalSection(&G->mResourcesLock) != FALSE;
            native_unlocked = global && resources;
            if (resources) LeaveCriticalSection(&G->mResourcesLock);
            if (global) LeaveCriticalSection(&G->mCriticalSection);
            native_probed.signal();
        } catch (...) {
            writer_failure = std::current_exception();
            writer_failed = true;
            match_probed.signal();
            native_probed.signal();
        }
    }
public:
    ID3D11Device1* native;
    unsigned match_hits = 0, native_hits = 0;
    std::atomic<bool> reloaded{false};
    OverrideReloadRace(BatchDevice& owner, bool fuzzy_match)
        : device(owner.device), fuzzy(fuzzy_match), observer(owner.native.Get()), native(owner.native.Get())
    {
        require_batch(!override_reload_race, "F176: exclusive per-thread match hook");
        writer = std::thread([this] { run_writer(); });
        override_reload_race = this;
    }
    ~OverrideReloadRace()
    {
        cancelled = true;
        matched.signal(); native_entered.signal();
        if (writer.joinable()) writer.join();
        override_reload_race = nullptr;
    }
    void at_match()
    {
        ++match_hits;
        matched.signal();
        match_probed.wait();
        if (writer_failed) std::rethrow_exception(writer_failure);
        // On old code real ReloadConfig has already freed the borrowed matches.
        // Fail before consuming them, so the red test does not require a crash.
        require_batch(match_protected,
            "F176: real reload replaced configuration while creation still borrowed its matches");
    }
    void at_native()
    {
        ++native_hits;
        native_entered.signal();
        native_probed.wait();
        if (writer_failed) std::rethrow_exception(writer_failure);
        require_batch(native_unlocked, "F176: G and resource-map locks are released before native Create");
    }
    void finish()
    {
        if (writer.joinable()) writer.join();
        if (writer_failure) std::rethrow_exception(writer_failure);
        require_batch(reloaded && match_hits == 1 && native_hits == 1,
            "F176: both scheduling boundaries and the real reload were reached exactly once");
    }
};

HRESULT STDMETHODCALLTYPE observe_override_create(ID3D11Device1* native, const D3D11_TEXTURE2D_DESC* desc,
    const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture2D** output)
{
    if (override_reload_race && native == override_reload_race->native)
        override_reload_race->at_native();
    return override_native_create(native, desc, data, output);
}

void write_override_config(const std::filesystem::path& config, const std::filesystem::path& settings,
    const std::string& rules)
{
    std::ofstream output(config, std::ios::binary | std::ios::trunc);
    output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nconfig_initialization_delay=-1\n"
        "skip_early_includes_load=0\n[Hunting]\nhunting=0\n[Include]\nuser_config="
        << settings.filename().string() << "\n[Constants]\n" << rules;
    output.close();
    require_batch(output.good(), "F176: write complete owned configuration");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
}

std::string override_rules(uint32_t hash, bool fuzzy, int width, bool pair)
{
    char key[128]{};
    if (fuzzy) std::snprintf(key, sizeof(key), "match_type=Texture2D\nmatch_width=4\nmatch_height=3\n");
    else std::snprintf(key, sizeof(key), "hash=%08x\n", hash);
    std::string result;
    if (pair) result = std::string("[TextureOverrideF176Low]\n") + key +
        "match_priority=0\nwidth=0\niteration=1\n";
    result += std::string("[TextureOverrideF176High]\n") + key +
        "match_priority=1\nwidth=" + std::to_string(width) + "\niteration=1\n";
    if (!fuzzy) result += "[TextureOverrideF176Fuzzy]\nmatch_type=Texture2D\nmatch_width=4\n"
        "match_height=3\nwidth=24\niteration=1\n";
    return result;
}

D3D11_TEXTURE2D_DESC override_original_desc()
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 4; desc.Height = 3; desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return desc;
}

UINT override_created_width(BatchDevice& device, const D3D11_TEXTURE2D_DESC& original)
{
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&original, nullptr, &texture)) && texture,
        "F176: create through actual HackerDevice and WARP");
    D3D11_TEXTURE2D_DESC actual{}; texture->GetDesc(&actual);
    return actual.Width;
}

void exercise_override_reload(const std::filesystem::path& config, const std::filesystem::path& settings, bool fuzzy)
{
    BatchDevice device;
    const auto original = override_original_desc();
    const auto hash = CalcTexture2DDescHash(0, &original);
    write_override_config(config, settings, override_rules(hash, fuzzy, 8, true));
    ReloadConfig(device.device);
    write_override_config(config, settings, override_rules(hash, fuzzy, 12, false));
    {
        OverrideReloadRace race(device, fuzzy);
        std::fprintf(stderr, "F176: real concurrent %s reload must wait for the creation value snapshot\n", fuzzy ? "fuzzy" : "exact");
        std::fflush(stderr);
        require_batch(override_created_width(device, original) == 8,
            "F176: in-flight creation retains old priority-composed descriptor across reload");
        race.finish();
    }
    const std::vector<int> reset = fuzzy ? std::vector<int>{0} : std::vector<int>{0, 0};
    require_batch(override_iteration_values() == reset, "F176: real reload resets new configuration counters");
    require_batch(override_created_width(device, original) == 12 && override_created_width(device, original) == 4,
        "F176: subsequent calls use new rules and preserve inactive iteration behavior");
    require_batch(original.Width == 4 && original.Height == 3, "F176: caller descriptor remains unchanged");
}

void exercise_override_concurrent_iterations(const std::filesystem::path& config, const std::filesystem::path& settings)
{
    BatchDevice device;
    const auto original = override_original_desc();
    char hash[32]{}; std::snprintf(hash, sizeof(hash), "%08x", CalcTexture2DDescHash(0, &original));
    write_override_config(config, settings, std::string("[TextureOverrideF176Concurrent]\nhash=") + hash +
        "\nwidth=8\niteration=1,3,5,7,9,11,13,15,17,19\n");
    ReloadConfig(device.device);
    OverrideAuditEvent start;
    std::array<std::exception_ptr, 2> failures{};
    std::atomic<unsigned> changed{0}, unchanged{0};
    auto create = [&](unsigned index) {
        try {
            start.wait();
            for (unsigned i = 0; i < 10; ++i) {
                const UINT width = override_created_width(device, original);
                if (width == 8) ++changed;
                else if (width == 4) ++unchanged;
                else throw std::runtime_error("F176: concurrent creation descriptor is inconsistent");
            }
        } catch (...) { failures[index] = std::current_exception(); }
    };
    std::thread first(create, 0), second;
    try { second = std::thread(create, 1); }
    catch (...) { start.signal(); first.join(); throw; }
    start.signal(); first.join(); second.join();
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    require_batch(changed == 10 && unchanged == 10 && override_iteration_values() == std::vector<int>{20},
        "F176: two real creation threads share exact, once-only iteration consumption");
}

void exercise_override_buffer_and_1d()
{
    ResourceOverrideAuditScope config;
    config.clear();
    BatchDevice device;
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 16; buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    auto& resize = G->mTextureOverrideMap[crc32c_hw(0, &buffer, sizeof(buffer))].emplace_back();
    resize.ini_section = L"TextureOverrideF176Buffer"; resize.override_byte_width = 32; resize.iterations = {0, 1};
    for (const UINT expected : {32u, 16u}) {
        ComPtr<ID3D11Buffer> created;
        require_batch(SUCCEEDED(device.device->CreateBuffer(&buffer, nullptr, &created)), "F176: actual buffer creation without initial data");
        D3D11_BUFFER_DESC actual{}; created->GetDesc(&actual);
        require_batch(actual.ByteWidth == expected, "F176: buffer snapshot preserves iteration and ByteWidth override");
    }
    require_batch(resize.iterations.front() == 2, "F176: buffer rules consume skipped iterations");
    config.clear();
    D3D11_TEXTURE1D_DESC line{}; line.Width = 4; line.ArraySize = line.MipLevels = 1;
    line.Format = DXGI_FORMAT_R8G8B8A8_UNORM; line.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    auto& noop = G->mTextureOverrideMap[crc32c_hw(0, &line, sizeof(line))].emplace_back();
    noop.ini_section = L"TextureOverrideF176Noop1D"; noop.width = 8; noop.iterations = {0, 1};
    for (unsigned i = 0; i < 2; ++i) {
        ComPtr<ID3D11Texture1D> created;
        require_batch(SUCCEEDED(device.device->CreateTexture1D(&line, nullptr, &created)), "F176: actual 1D creation without initial data");
        D3D11_TEXTURE1D_DESC actual{}; created->GetDesc(&actual);
        require_batch(actual.Width == 4, "F176: preserve the existing 1D no-op descriptor override");
    }
    require_batch(noop.iterations.front() == 2, "F176: 1D no-op rules still consume iteration");
}
}

static void resource_override_audit_match_boundary()
{
    if (override_reload_race) override_reload_race->at_match();
}

void test_dx11_resource_override_lifetime_regression(const std::filesystem::path& config, const std::filesystem::path& settings)
{
    test_dx11_resource_override_validation_regression();
    exercise_override_buffer_and_1d();
    exercise_override_reload(config, settings, false);
    exercise_override_reload(config, settings, true);
    exercise_override_concurrent_iterations(config, settings);
}
