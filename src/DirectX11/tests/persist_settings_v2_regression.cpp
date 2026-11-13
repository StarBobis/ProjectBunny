#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"
#include "Override.h"
#include "Input.h"

#include <wrl/client.h>
#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

bool contains_log_text(const std::string& bytes, const std::string& text)
{
    if (bytes.find(text) != std::string::npos)
        return true;
    // Real UCRT binary tmpfiles can contain narrow bytes and UTF-16LE wide
    // stdio writes. Match each complete positive marker in either encoding.
    std::string wide;
    wide.reserve(text.size() * 2);
    for (char character : text) {
        wide.push_back(character);
        wide.push_back('\0');
    }
    return bytes.find(wide) != std::string::npos;
}

class UnknownFiles {
public:
    std::filesystem::path directory, config, first, second, mod;
    std::vector<std::filesystem::path> owned;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user = G->user_config;
    unsigned saved_frame = G->frame_no;
    int saved_dirty = G->user_config_dirty;
    bool saved_auto_clear = G->clear_unknown_settings;
    bool saved_cleanup = G->clear_saved_persist_vars;
    uint32_t saved_unknown_count = G->unknown_persist_vars_count;
    bool saved_wipe = G->gWipeUserConfig;
    bool saved_pending = G->gReloadConfigPending;
    bool saved_initialized = G->gConfigInitialized;

    ~UnknownFiles()
    {
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user;
        G->frame_no = saved_frame;
        G->user_config_dirty = saved_dirty;
        G->clear_unknown_settings = saved_auto_clear;
        G->clear_saved_persist_vars = saved_cleanup;
        G->unknown_persist_vars_count = saved_unknown_count;
        G->gWipeUserConfig = saved_wipe;
        G->gReloadConfigPending = saved_pending;
        G->gConfigInitialized = saved_initialized;
        for (const auto& path : owned)
            DeleteFileW(path.c_str());
    }

    void create(const std::filesystem::path& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse to replace pre-existing preservation fixture");
        CloseHandle(file);
        owned.push_back(path);
    }

    void initialize()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate preservation test executable");
        directory = std::filesystem::path(executable).parent_path();
        bool build_directory = false;
        for (const auto& part : directory)
            build_directory |= _wcsicmp(part.c_str(), L"build") == 0;
        check(build_directory, "preservation fixtures must stay in a build directory");
        const std::wstring prefix = L"persist_settings_v2_" + std::to_wstring(GetCurrentProcessId());
        config = directory / L"d3dx.ini";
        first = directory / (prefix + L"_a.ini");
        second = directory / (prefix + L"_b.ini");
        mod = directory / (prefix + L"_mod.ini");
        create(config);
        create(first);
        create(second);
        create(mod);
        write(mod, "[Constants]\nglobal persist $preservation_mod = 3\n");
        migoto_handle = nullptr;
        G->gWipeUserConfig = false;
        G->gReloadConfigPending = false;
        G->gConfigInitialized = false;
        G->frame_no = 0;
    }

    static void write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << value;
        output.close();
        check(output.good(), "write owned preservation fixture");
    }

    static std::string read(const std::filesystem::path& path)
    {
        // Compare INI text, allowing the production writer's Windows CRLF.
        std::ifstream input(path);
        check(input.good(), "read actual preserved user file");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    std::wstring mod_variable() const
    {
        const std::wstring ini_namespace = mod.filename().wstring();
        return get_namespaced_var_name_lower(L"$preservation_mod", &ini_namespace);
    }

    std::string mod_assignment(const char* value) const
    {
        const std::wstring name = mod_variable();
        return std::string(name.begin(), name.end()) + " = " + value + "\n";
    }

    std::wstring pool_variable() const
    {
        std::wstring section = L"PoolPersistUnknown", result;
        const std::wstring ini_namespace = mod.filename().wstring();
        check(get_namespaced_section_name_lower(&section, &ini_namespace, &result), "namespace real included pool section");
        return L"$" + result + L"[0]";
    }

    std::string pool_assignment(const char* value) const
    {
        const auto variable = pool_variable();
        return std::string(variable.begin(), variable.end()) + " = " + value + "\n";
    }

    void configure(const std::filesystem::path& user, bool auto_clear, bool enabled_mod, bool alias = false,
        const char* extra_constants = "")
    {
        std::wstring relative = user.filename().wstring();
        if (alias) {
            std::transform(relative.begin(), relative.end(), relative.begin(), ::towupper);
            relative = L".\\" + relative;
        }
        std::string text = "[Logging]\nlog_level=warning\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "config_initialization_delay=0\nclear_unknown_settings=" + std::to_string(auto_clear ? 1 : 0) +
            "\n[Include]\nuser_config=" + std::string(relative.begin(), relative.end()) + "\n";
        if (enabled_mod)
            text += "include=" + mod.filename().string() + "\n";
        text += "[Constants]\nglobal persist $preservation_active = 1\n"
            "global persist locked $preservation_locked = 17\n"
            "[PoolPreservationDefault]\npool_size=2\n"
            "[PoolPreservationAllocate]\npool_size=2\npool_allocate_slot_on_missing=1\n";
        const auto constants_end = text.find("[PoolPreservationDefault]");
        text.insert(constants_end, extra_constants);
        write(config, text);
    }
};

class UnknownDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    UnknownDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate preservation system runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load preservation D3D runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve preservation WARP creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            base.GetAddressOf(), nullptr, immediate.GetAddressOf())) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(immediate.As(&context)), "create real preservation WARP context");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~UnknownDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

void assign(UnknownDevice& device, const std::wstring& variable, const wchar_t* expression)
{
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    std::wstring value = expression;
    const std::wstring ini_namespace;
    const std::wstring raw = variable + L" = " + value;
    check(ParseCommandListVariableAssignment(L"CommandListPreservationRegression", variable.c_str(), &value, &raw,
        &commands, &commands, nullptr, &ini_namespace), "parse actual preservation variable assignment");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

void reload(UnknownDevice& device)
{
    G->gReloadConfigPending = true;
    ReloadConfig(device.device);
}


class RetainedBinding final : public InputAction {
    std::shared_ptr<int> destructions;
public:
    explicit RetainedBinding(std::shared_ptr<int> count) : InputAction(nullptr, nullptr), destructions(std::move(count)) {}
    ~RetainedBinding() override { ++*destructions; }
    bool Dispatch(HackerDevice*) override { return false; }
};

class UnknownLogCapture {
    FILE* previous = LogFile;
    LogVerbosity previous_level = gLogVerbosity;
    bool previous_debug = gLogDebug;
public:
    UnknownLogCapture()
    {
        LogFile = std::tmpfile();
        check(LogFile != nullptr, "open real unknown persistence notification capture");
    }
    ~UnknownLogCapture()
    {
        std::fclose(LogFile);
        LogFile = previous;
        gLogVerbosity = previous_level;
        gLogDebug = previous_debug;
    }
    std::string contents()
    {
        std::fflush(LogFile);
        check(std::fseek(LogFile, 0, SEEK_SET) == 0, "rewind real warning capture");
        std::string text;
        char buffer[1024];
        size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), LogFile)) != 0)
            text.append(buffer, count);
        check(std::fseek(LogFile, 0, SEEK_END) == 0, "restore warning capture append position");
        return text;
    }
};


std::string raw_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(input.good(), "read exact user file transaction bytes");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void replace_option(UnknownFiles& files, const std::string& source, const std::string& destination)
{
    auto text = raw_text(files.config);
    const auto at = text.find(source);
    check(at != std::string::npos, "find owned fixture option for actual config variant");
    text.replace(at, source.size(), destination);
    UnknownFiles::write(files.config, text);
}

void known_nonpersistent_settings(UnknownFiles& files, UnknownDevice& device)
{
    UnknownFiles::write(files.mod, "[PoolPersistUnknown]\npool_size=2\npool_persist_variables=0\n"
        "pool_variable_default_value=3\n");
    files.configure(files.first, false, true, false, "global $v2_nonpersist=3\n");
    UnknownFiles::write(files.first, "[Constants]\n$v2_nonpersist=27\n" + files.pool_assignment("23.75") +
        "$preservation_locked=99\n$poolbad[0]=5junk\n");
    LoadConfigFile();
    device.wrapper->InitIniParams();
    check(command_list_globals.at(L"$v2_nonpersist").fval == 3 &&
        command_list_globals.at(L"$preservation_locked").fval == 17,
        "user settings cannot overwrite declarations which lost persist or are LOCKED");
    std::wstring pool_name = files.pool_variable().substr(1);
    pool_name.resize(pool_name.find(L'['));
    check(customResourcePools.at(pool_name).GetVariable(0, false, true, false)->fval == 3,
        "user settings cannot apply stale values to a pool whose persist option is disabled");
    check(G->unknown_persist_vars_count == 2, "nonpersistent scalar and pool values are both real unknown settings");
    SavePersistentSettings();
    const auto saved = UnknownFiles::read(files.first);
    check(saved.find("$v2_nonpersist = 27") != std::string::npos &&
        saved.find(files.pool_assignment("23.75")) != std::string::npos &&
        saved.find("preservation_locked") == std::string::npos && saved.find("poolbad") == std::string::npos,
        "merged known/unknown write preserves valid obsolete settings without applying or inventing values");
    files.configure(files.first, false, true, false, "global persist $v2_nonpersist=3\n");
    UnknownFiles::write(files.mod, "[PoolPersistUnknown]\npool_size=2\npool_persist_variables=1\n"
        "pool_variable_default_value=3\n");
    reload(device);
    check(command_list_globals.at(L"$v2_nonpersist").fval == 27 &&
        command_list_globals.at(files.pool_variable()).fval == 23.75f && G->unknown_persist_vars_count == 0,
        "reinstating persist recognizes old scalar and pool values after actual mod reload");
}

void value_sensitive_grace_and_io(UnknownFiles& files, UnknownDevice& device)
{
    files.configure(files.second, false, false);
    UnknownFiles::write(files.second, "[Constants]\n$v2_unknown=10\n");
    reload(device);
    check(!G->clear_unknown_settings && G->unknown_persist_vars_count == 1,
        "clear_unknown_settings=0 retains actual unknown values");
    // A deterministic old timestamp proves that successful no-op reloads did
    // not rewrite the file, independently of coarse timestamp granularity.
    HANDLE file = CreateFileW(files.second.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "open owned settings timestamp");
    FILETIME old_time = {0x13572468, 0x01c00000};
    const bool dated = SetFileTime(file, nullptr, nullptr, &old_time) != FALSE;
    CloseHandle(file);
    check(dated, "set stable old last-write timestamp");
    const auto before = raw_text(files.second);
    reload(device);
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    check(GetFileAttributesExW(files.second.c_str(), GetFileExInfoStandard, &attributes) &&
        CompareFileTime(&attributes.ftLastWriteTime, &old_time) == 0 && raw_text(files.second) == before,
        "unchanged retained names/values and clean known settings do not rewrite on reload");

    // When the user removes the last unknown entry, upstream returns before
    // force-saving. Updating observation state must not create extra I/O.
    UnknownFiles::write(files.second, "[Constants]\n");
    file = CreateFileW(files.second.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "open now-empty unknown settings timestamp");
    const bool empty_dated = SetFileTime(file, nullptr, nullptr, &old_time) != FALSE;
    CloseHandle(file);
    check(empty_dated, "set stable timestamp after removing the final unknown");
    const auto empty_before = raw_text(files.second);
    reload(device);
    check(!G->user_config_dirty && G->unknown_persist_vars_count == 0 &&
        GetFileAttributesExW(files.second.c_str(), GetFileExInfoStandard, &attributes) &&
        CompareFileTime(&attributes.ftLastWriteTime, &old_time) == 0 && raw_text(files.second) == empty_before,
        "nonempty-to-empty unknown collection updates count without force-rewriting a clean retained file");

    UnknownFiles::write(files.second, "[Constants]\n$v2_unknown=10\n");
    reload(device);

    // Change only a value, with the identical key set, then enable cleanup.
    UnknownFiles::write(files.second, "[Constants]\n$v2_unknown=11\n");
    files.configure(files.second, true, false);
    reload(device);
    check(G->unknown_persist_vars_count == 1 && UnknownFiles::read(files.second).find("$v2_unknown=11") != std::string::npos,
        "same unknown names with a changed value receive a fresh reload grace period");
    check(!G->user_config_dirty, "valid retained unknown values alone do not forge a dirty write request");
    check(!SavePersistentSettings(), "clean autosave reports no write and never advances grace");
    reload(device);
    check(G->unknown_persist_vars_count == 0 && UnknownFiles::read(files.second).find("v2_unknown") == std::string::npos,
        "unchanged names and exact value bits are cleared after the new grace period");

    assign(device, L"$preservation_active", L"12");
    assign(device, L"$preservation_active", L"12");
    check(G->user_config_dirty & 1, "unchanged assignment cannot erase earlier pending persistent changes");
    check(SavePersistentSettings() && UnknownFiles::read(files.second).find("$preservation_active = 12") != std::string::npos,
        "pending changed-to-unchanged value still commits through the merged save API");
    G->user_config_dirty = 2;
    assign(device, L"$preservation_active", L"12");
    check(G->user_config_dirty == 2, "unchanged assignments retain the independent sanitation request");
    SavePersistentSettings();

    assign(device, L"$preservation_active", L"-0");
    SavePersistentSettings();
    assign(device, L"$preservation_active", L"0");
    check(!G->user_config_dirty, "signed-zero assignment retains the existing numeric-equality dirty contract");
    assign(device, L"$preservation_active", L"nan");
    SavePersistentSettings();
    assign(device, L"$preservation_active", L"nan");
    check(G->user_config_dirty & 1, "NaN assignment retains the existing unequal-to-itself dirty contract");
    SavePersistentSettings();

    UnknownFiles::write(files.second, "[Constants]\n$v2_zero=-0\n");
    reload(device);
    UnknownFiles::write(files.second, "[Constants]\n$v2_zero=0\n");
    reload(device);
    check(G->unknown_persist_vars_count == 1,
        "unknown grace compares signed-zero value bits independently of ordinary assignment dirty equality");
    reload(device);
    check(G->unknown_persist_vars_count == 0, "unchanged positive-zero unknown clears after its own grace period");

    files.configure(files.second, false, false);
    replace_option(files, "clear_unknown_settings=0", "auto_clear_persist_vars=0");
    reload(device);
    check(G->clear_unknown_settings, "removed legacy option cannot override the true missing-key default");
}

class SettingsLock {
public:
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~SettingsLock() { release(); }
    void release()
    {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
};

class LockSettingsOnDestruction final : public InputAction {
    std::shared_ptr<SettingsLock> lock;
    std::wstring path;
public:
    LockSettingsOnDestruction(std::shared_ptr<SettingsLock> state, std::wstring filename) :
        InputAction(nullptr, nullptr), lock(std::move(state)), path(std::move(filename)) {}
    ~LockSettingsOnDestruction() override
    {
        lock->handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    bool Dispatch(HackerDevice*) override { return false; }
};

void atomic_failure_and_retry(UnknownFiles& files, UnknownDevice& device)
{
    files.configure(files.first, true, false);
    UnknownFiles::write(files.first, "[Constants]\n$v2_transaction=19\n");
    reload(device); // New owner and first observed unknown collection.
    const auto before = raw_text(files.first);
    auto* old_variable = &command_list_globals.at(L"$preservation_active");
    assign(device, L"$preservation_active", L"29");
    OverrideSave.vars[old_variable].Save(1);
    auto destructions = std::make_shared<int>(0);
    RegisterInputAction(std::make_unique<RetainedBinding>(destructions), InputDisableScope::NONE);
    SettingsLock pre_save;
    pre_save.handle = CreateFileW(files.first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(pre_save.handle != INVALID_HANDLE_VALUE, "block atomic replacement with an actual target file lock");
    reload(device);
    check(G->gReloadConfigPending && G->user_config_dirty && *destructions == 0 &&
        &command_list_globals.at(L"$preservation_active") == old_variable && OverrideSave.vars.count(old_variable) == 1 &&
        raw_text(files.first) == before,
        "pre-save replacement failure preserves exact old disk bytes and old live registry, bindings and override state");
    pre_save.release();
    reload(device);
    check(!G->gReloadConfigPending && *destructions == 1 && G->unknown_persist_vars_count == 0,
        "pre-save retry commits old changes and then safely rebuilds config and clears unchanged unknowns");

    // Establish another grace collection in a fresh owner, then acquire the
    // real target lock only when old bindings are successfully released.
    files.configure(files.second, true, false);
    UnknownFiles::write(files.second, "[Constants]\n$v2_late_transaction=31\n");
    reload(device);
    const auto late_before = raw_text(files.second);
    auto late_lock = std::make_shared<SettingsLock>();
    RegisterInputAction(std::make_unique<LockSettingsOnDestruction>(late_lock, files.second.wstring()), InputDisableScope::NONE);
    reload(device);
    check(late_lock->handle != INVALID_HANDLE_VALUE && G->gReloadConfigPending && (G->user_config_dirty & 2) &&
        G->unknown_persist_vars_count == 1 && command_list_globals.count(L"$preservation_active") == 1 &&
        raw_text(files.second) == late_before,
        "post-Constants cleanup replacement failure preserves exact disk bytes and current loaded registry with retry pending");
    late_lock->release();
    reload(device);
    check(!G->gReloadConfigPending && G->unknown_persist_vars_count == 0 &&
        UnknownFiles::read(files.second).find("v2_late_transaction") == std::string::npos,
        "late cleanup failure retries without losing its unchanged name-and-value collection");
    G->gWipeUserConfig = true;
    reload(device);
    assign(device, L"$preservation_active", L"2");
    SavePersistentSettings();
    check(UnknownFiles::read(files.second).find("v2_") == std::string::npos,
        "wipe and path ownership cannot resurrect an earlier retained collection");
}
} // namespace

void test_dx11_persist_settings_v2_regression()
{
    try {
        UnknownLogCapture log;
        UnknownFiles files;
        files.initialize();
        UnknownDevice device;
        known_nonpersistent_settings(files, device);
        value_sensitive_grace_and_io(files, device);
        atomic_failure_and_retry(files, device);
        std::fputs("PASS: persistent settings v2, value-sensitive grace, clean no-rewrite and atomic replacement failures\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: persistent settings v2: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
