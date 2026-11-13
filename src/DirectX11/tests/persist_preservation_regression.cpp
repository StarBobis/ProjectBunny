#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"

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

class PreservationFiles {
public:
    std::filesystem::path directory, config, first, second, mod;
    std::vector<std::filesystem::path> owned;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user = G->user_config;
    unsigned saved_frame = G->frame_no;
    int saved_dirty = G->user_config_dirty;
    bool saved_auto_clear = G->clear_unknown_settings;
    bool saved_cleanup = G->clear_saved_persist_vars;
    bool saved_wipe = G->gWipeUserConfig;
    bool saved_pending = G->gReloadConfigPending;
    bool saved_initialized = G->gConfigInitialized;

    ~PreservationFiles()
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
        const std::wstring prefix = L"persist_preservation_" + std::to_wstring(GetCurrentProcessId());
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

    void configure(const std::filesystem::path& user, bool auto_clear, bool enabled_mod, bool alias = false)
    {
        std::wstring relative = user.filename().wstring();
        if (alias) {
            std::transform(relative.begin(), relative.end(), relative.begin(), ::towupper);
            relative = L".\\" + relative;
        }
        std::string text = "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "config_initialization_delay=0\nclear_unknown_settings=" + std::to_string(auto_clear ? 1 : 0) +
            "\n[Include]\nuser_config=" + std::string(relative.begin(), relative.end()) + "\n";
        if (enabled_mod)
            text += "include=" + mod.filename().string() + "\n";
        text += "[Constants]\nglobal persist $preservation_active = 1\n"
            "global persist locked $preservation_locked = 17\n"
            "[PoolPreservationDefault]\npool_size=2\n"
            "[PoolPreservationAllocate]\npool_size=2\npool_allocate_slot_on_missing=1\n";
        write(config, text);
    }
};

class PreservationDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    PreservationDevice()
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

    ~PreservationDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

void assign(PreservationDevice& device, const std::wstring& variable, const wchar_t* expression)
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

void reload(PreservationDevice& device)
{
    G->gReloadConfigPending = true;
    ReloadConfig(device.device);
}

void test_initial_and_cleanup(PreservationFiles& files, PreservationDevice& device)
{
    files.configure(files.first, true, false);
    PreservationFiles::write(files.first, "[Constants]\n$preservation_active = 2.5\n"
        "$preservation_unknown = 12.5\n$PRESERVATION_CASE_UNKNOWN = 6.25\n"
        "$preservation_invalid = invalid_literal\n$preservation_garbage = 4garbage\n"
        "$preservation_expression = 2 + 3\n$preservation_locked = 99\n"
        "$\\namespace with spaces\\saved_value = 9.75\n"
        "$\\namespace \xE6\xB5\x8B\xE8\xAF\x95\\saved_utf = 7.5\n" + files.mod_assignment("23.75"));
    LoadConfigFile();
    device.wrapper->InitIniParams();
    check(G->frame_no == 0 && !G->gConfigInitialized && G->clear_unknown_settings &&
        command_list_globals.at(L"$preservation_active").fval == 2.5f &&
        command_list_globals.at(L"$preservation_locked").fval == 17,
        "initial pre-frame configuration loads known values and rejects locked assignment");
    check(!customResourcePools.at(L"poolpreservationdefault").allocate_slot_on_missing &&
        customResourcePools.at(L"poolpreservationallocate").allocate_slot_on_missing,
        "real INI loading wires default and explicit pool missing-slot allocation policy");
    SavePersistentSettings();
    std::string saved = PreservationFiles::read(files.first);
    check(saved.find("$preservation_unknown = 12.5") != std::string::npos &&
        saved.find("$preservation_case_unknown = 6.25") != std::string::npos &&
        saved.find("$\\namespace with spaces\\saved_value = 9.75") != std::string::npos &&
        saved.find("$\\namespace \xE6\xB5\x8B\xE8\xAF\x95\\saved_utf = 7.5") != std::string::npos &&
        saved.find(files.mod_assignment("23.75")) != std::string::npos && !G->clear_saved_persist_vars,
        "autosave before config initialization preserves valid unknown literals and normalized namespaces");
    check(saved.find("$preservation_invalid") == std::string::npos && saved.find("$preservation_garbage") == std::string::npos &&
        saved.find("$preservation_expression") == std::string::npos && saved.find("$preservation_locked") == std::string::npos,
        "bad unknown values never become zero or executable expressions and LOCKED is never serialized");
    assign(device, L"$preservation_active", L"4.5");
    SavePersistentSettings();
    SavePersistentSettings();
    check(PreservationFiles::read(files.first).find("$preservation_unknown = 12.5") != std::string::npos &&
        !G->clear_saved_persist_vars, "repeated dirty and clean autosaves do not advance unknown-value cleanup");
    reload(device);
    check(G->clear_saved_persist_vars && PreservationFiles::read(files.first).find("$preservation_unknown = 12.5") != std::string::npos,
        "first explicit reload arms cleanup even after autosave cleared dirty bit");
    SavePersistentSettings();
    check(G->clear_saved_persist_vars && PreservationFiles::read(files.first).find("$preservation_unknown = 12.5") != std::string::npos,
        "autosave between reloads preserves data and armed cleanup stage");
    files.configure(files.first, true, false, true);
    reload(device);
    check(!G->clear_saved_persist_vars && PreservationFiles::read(files.first).find("$preservation_unknown = 12.5") == std::string::npos,
        "second reload removes confirmed unknown values in its successful write, including equivalent uppercase dot path at frame zero");
    SavePersistentSettings();
    saved = PreservationFiles::read(files.first);
    check(saved.find("$preservation_unknown") == std::string::npos && saved.find("preservation_mod") == std::string::npos &&
        saved.find("saved_value") == std::string::npos && saved.find("saved_utf") == std::string::npos &&
        saved.find("$preservation_active = 4.5") != std::string::npos,
        "following save removes unknown values from the actual file without frame-zero recapture");
}

void test_disabled_and_reenabled(PreservationFiles& files, PreservationDevice& device)
{
    files.configure(files.second, false, false);
    PreservationFiles::write(files.second, "[Constants]\n$preservation_active = 6\n$preservation_second_unknown = 16\n" + files.mod_assignment("23.75"));
    G->frame_no = 77;
    reload(device);
    SavePersistentSettings();
    reload(device);
    reload(device);
    SavePersistentSettings();
    check(!G->clear_unknown_settings && !G->clear_saved_persist_vars &&
        PreservationFiles::read(files.second).find("$preservation_second_unknown = 16") != std::string::npos,
        "disabled cleanup preserves initial unknown values across autosaves and multiple reloads after frame zero");
    files.configure(files.second, false, true);
    reload(device);
    check(command_list_globals.at(files.mod_variable()).fval == 23.75f,
        "re-enabled real included mod restores its saved namespaced value");
    assign(device, files.mod_variable(), L"34.5");
    SavePersistentSettings();
    files.configure(files.second, true, false);
    reload(device);
    check(command_list_globals.count(files.mod_variable()) == 0 && G->clear_unknown_settings,
        "disabled included mod removes live variable while owning preservation keeps its last value");
    SavePersistentSettings();
    check(PreservationFiles::read(files.second).find(files.mod_assignment("34.5")) != std::string::npos,
        "autosave while mod is disabled keeps value captured before registry destruction");
    check(G->clear_saved_persist_vars, "reload removing the mod confirms the unknown collection for the next reload");
    files.configure(files.second, true, true);
    reload(device);
    check(command_list_globals.at(files.mod_variable()).fval == 34.5f,
        "post-Constants cleanup recognizes re-enabled mods before deciding which old settings are unknown");
    reload(device); // The remaining unknown-only set receives its own grace period.
    assign(device, files.mod_variable(), L"4");
    SavePersistentSettings();
    check(PreservationFiles::read(files.second).find(files.mod_assignment("4")) != std::string::npos &&
        PreservationFiles::read(files.second).find("$preservation_second_unknown") == std::string::npos,
        "restored current variable survives cleanup while still-unknown setting is removed");
}

void test_path_wipe_and_retry(PreservationFiles& files, PreservationDevice& device)
{
    files.configure(files.first, false, false);
    PreservationFiles::write(files.first, "[Constants]\n$preservation_active = 8\n$preservation_first_only = 71\n");
    reload(device);
    SavePersistentSettings();
    check(PreservationFiles::read(files.first).find("$preservation_first_only = 71") != std::string::npos &&
        PreservationFiles::read(files.first).find("preservation_mod") == std::string::npos,
        "changing user_config path captures only new file values without leaking previous owner");
    assign(device, L"$preservation_active", L"9");
    const std::wstring actual = G->user_config;
    const std::string locale_before = setlocale(LC_CTYPE, nullptr);
    const auto missing = files.directory / (L"preservation_missing_" + std::to_wstring(GetCurrentProcessId()));
    check(!std::filesystem::exists(missing), "save failure parent must not exist");
    G->user_config = (missing / L"settings.ini").wstring();
    SavePersistentSettings();
    check(G->user_config_dirty && std::string(setlocale(LC_CTYPE, nullptr)) == locale_before,
        "failed save retains dirty state and restores locale");
    G->user_config = actual;
    SavePersistentSettings();
    check(!G->user_config_dirty && PreservationFiles::read(files.first).find("$preservation_first_only = 71") != std::string::npos,
        "failed path save preserves original owner's unknown values for successful retry");

    assign(device, L"$preservation_active", L"10");
    HANDLE blocked = CreateFileW(files.first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(blocked != INVALID_HANDLE_VALUE, "lock owned file to inject real reload save failure");
    reload(device);
    const bool kept_live = G->user_config_dirty && G->gReloadConfigPending &&
        command_list_globals.at(L"$preservation_active").fval == 10;
    CloseHandle(blocked);
    check(kept_live, "failed reload save retains pending request and live unsaved variable instead of loading stale disk value");
    reload(device);
    check(!(G->user_config_dirty & 1) && !G->gReloadConfigPending && command_list_globals.at(L"$preservation_active").fval == 10,
        "reload retry saves live value before rebuilding configuration");

    blocked = CreateFileW(files.first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(blocked != INVALID_HANDLE_VALUE, "lock owned file to inject real wipe failure");
    G->gWipeUserConfig = true;
    reload(device);
    const bool wipe_pending = G->gWipeUserConfig && command_list_globals.at(L"$preservation_active").fval == 10;
    CloseHandle(blocked);
    check(wipe_pending, "failed wipe keeps original values and request available for retry");
    reload(device);
    check(!G->gWipeUserConfig && command_list_globals.at(L"$preservation_active").fval == 1,
        "successful wipe reloads declaration defaults");
    assign(device, L"$preservation_active", L"2");
    SavePersistentSettings();
    check(PreservationFiles::read(files.first).find("$preservation_first_only") == std::string::npos &&
        PreservationFiles::read(files.first).find("$preservation_active = 2") != std::string::npos,
        "save after wipe cannot resurrect preserved unknowns or old live values");
}

void run_persist_preservation_regression()
{
    PreservationFiles files;
    files.initialize();
    PreservationDevice device;
    test_initial_and_cleanup(files, device);
    test_disabled_and_reenabled(files, device);
    test_path_wipe_and_retry(files, device);
}
} // namespace

void test_dx11_persist_preservation_regression()
{
    try {
        run_persist_preservation_regression();
        std::fputs("PASS: real saved-variable preservation, cleanup, path ownership, wipe and retry\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: persistent preservation regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
