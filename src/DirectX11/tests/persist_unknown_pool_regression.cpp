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
        const std::wstring prefix = L"persist_unknown_pool_" + std::to_wstring(GetCurrentProcessId());
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

void cold_pool_and_cleanup(UnknownFiles& files, UnknownDevice& device, UnknownLogCapture& log)
{
    UnknownFiles::write(files.mod, "[PoolPersistUnknown]\npool_size=2\npool_persist_variables=1\n"
        "pool_variable_default_value=3\n");
    files.configure(files.first, false, false);
    UnknownFiles::write(files.first, "[Constants]\n$preservation_active=5\n" + files.pool_assignment("23.75") +
        "$unknown_scalar=7\n$\\namespace with spaces\\value=9\n"
        "$poolbad[0]=5junk\n$poolbad[-1]=5\n$poolbad[1 + 2]=5\n$poolbad[0][1]=5\n"
        "$poolbad[184467440737095516160]=5\n$poolbad[0]=2 + 3\n$notpool[0]=5\n"
        "$preservation_locked=100\n");
    LoadConfigFile();
    device.wrapper->InitIniParams();
    check(G->unknown_persist_vars_count == 3 && (G->user_config_dirty & 2),
        "cold load counts only real valid unknown pool/scalar/namespace values while cleanup is disabled");
    SavePersistentSettings();
    auto text = UnknownFiles::read(files.first);
    check(text.find(files.pool_assignment("23.75")) != std::string::npos &&
        text.find("$unknown_scalar = 7") != std::string::npos && text.find("namespace with spaces") != std::string::npos,
        "autosave preserves cold-start unknown pool and namespace values");
    check(text.find("poolbad") == std::string::npos && text.find("notpool") == std::string::npos &&
        text.find("preservation_locked") == std::string::npos,
        "malformed pool targets, incomplete literals, expressions and LOCKED assignments are never preserved");
    reload(device);
    check(G->unknown_persist_vars_count == 3 && !G->clear_saved_persist_vars,
        "disabled cleanup reports actual unknown count without arming removal");
    const auto warning = log.contents();
    check(contains_log_text(warning, "Detected 3 unknown user settings") &&
        contains_log_text(warning, "Automatic clean-up is disabled") &&
        contains_log_text(warning, "Unrecognised persistent variable: $pool"),
        "real warning output reports actual disabled-cleanup count and unknown pool entries");
    files.configure(files.first, false, true);
    LoadConfigFile();
    check(command_list_globals.count(files.pool_variable()) == 0 && G->unknown_persist_vars_count == 2,
        "declared persistent physical pool slot is recognized without eagerly allocating its lazy variable");
    device.wrapper->InitIniParams();
    reload(device);
    if (command_list_globals.at(files.pool_variable()).fval != 23.75f || G->unknown_persist_vars_count != 2) {
        std::fwprintf(stderr, L"Unknown pool restore diagnostic: name=%ls value=%.9g count=%u\n",
            files.pool_variable().c_str(), command_list_globals.at(files.pool_variable()).fval, G->unknown_persist_vars_count);
        for (const auto& variable : command_list_globals)
            std::fwprintf(stderr, L"  variable=%ls value=%.9g flags=%u\n", variable.first.c_str(), variable.second.fval,
                static_cast<unsigned>(variable.second.flags));
        for (const auto& pool : customResourcePools)
            std::fwprintf(stderr, L"  pool=%ls flags=%u\n", pool.first.c_str(), static_cast<unsigned>(pool.second.variable_template->flags));
        std::fprintf(stderr, "Actual saved user text:\n%s\nActual mod text:\n%s\n",
            UnknownFiles::read(files.first).c_str(), UnknownFiles::read(files.mod).c_str());
    }
    check(command_list_globals.at(files.pool_variable()).fval == 23.75f && G->unknown_persist_vars_count == 2,
        "enabling an actual included pool mod restores its cold saved slot and removes it from unknown collection");
    files.configure(files.first, false, false);
    reload(device);
    check(command_list_globals.count(files.pool_variable()) == 0 && G->unknown_persist_vars_count == 3,
        "stopping a real pool mod retains its saved slot as unknown");
    files.configure(files.first, true, false);
    reload(device);
    check(G->unknown_persist_vars_count == 0 && !G->clear_saved_persist_vars,
        "enabling cleanup removes a name-and-value collection already unchanged across reloads");
    G->user_config_dirty |= 2;
    assign(device, L"$preservation_active", L"6");
    check((G->user_config_dirty & 3) == 3, "ordinary changes preserve independent pending sanitation dirty bit");
    SavePersistentSettings();
    text = UnknownFiles::read(files.first);
    check(text.find("unknown_scalar") == std::string::npos &&
        text.find(files.pool_assignment("23.75")) == std::string::npos &&
        text.find("$preservation_active = 6") != std::string::npos,
        "post-Constants cleanup removes stale settings while subsequent live persistent changes save");
}

void changed_equal_collection(UnknownFiles& files, UnknownDevice& device)
{
    files.configure(files.second, true, false, false, "global persist $unknown_a=1\n");
    UnknownFiles::write(files.second, "[Constants]\n$unknown_a=11\n$unknown_b=22\n");
    reload(device);
    check(G->unknown_persist_vars_count == 1 && G->clear_saved_persist_vars, "first owner load confirms only unknown b");
    SavePersistentSettings();
    const auto original_settings = UnknownFiles::read(files.second);
    // Exercise the public load entry with a changed declaration set before the
    // next saving reload. This produces equal-size, different unknown sets.
    files.configure(files.second, true, false, false, "global persist $unknown_b=2\n");
    LoadConfigFile();
    device.wrapper->InitIniParams();
    check(G->unknown_persist_vars_count == 1 && command_list_globals.count(L"$unknown_a") == 0,
        "actual reload parsing changes unknown b into known and unknown a into missing");
    reload(device);
    check(UnknownFiles::read(files.second) == original_settings && G->unknown_persist_vars_count == 1 &&
        command_list_globals.at(L"$unknown_b").fval == 22,
        "same unknown count with different names retains the complete original file and independently confirms new unknown values");
    reload(device);
    check(UnknownFiles::read(files.second).find("$unknown_a") == std::string::npos &&
        command_list_globals.at(L"$unknown_b").fval == 22,
        "only the subsequently confirmed exact collection is removed");
}

void transactional_failure(UnknownFiles& files, UnknownDevice& device)
{
    files.configure(files.first, false, false);
    reload(device);
    assign(device, L"$preservation_active", L"19");
    auto* variable = &command_list_globals.at(L"$preservation_active");
    OverrideSave.vars[variable].Save(5);
    auto destructions = std::make_shared<int>(0);
    RegisterInputAction(std::make_unique<RetainedBinding>(destructions), InputDisableScope::NONE);
    HANDLE locked = CreateFileW(files.first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock owned user settings for actual save failure");
    reload(device);
    const bool intact = G->gReloadConfigPending && (G->user_config_dirty & 1) &&
        &command_list_globals.at(L"$preservation_active") == variable && variable->fval == 19 &&
        OverrideSave.vars.count(variable) == 1 && *destructions == 0;
    CloseHandle(locked);
    check(intact, "failed save retains live variables, pending reload, bindings and override saved state");
    reload(device);
    check(!G->gReloadConfigPending && *destructions == 1 && OverrideSave.vars.empty(),
        "successful save retry then clears bindings and override state");
    locked = CreateFileW(files.first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock owned settings for actual wipe failure");
    G->gWipeUserConfig = true;
    reload(device);
    const bool wipe_pending = G->gWipeUserConfig && G->gReloadConfigPending;
    CloseHandle(locked);
    check(wipe_pending, "failed wipe preserves request for retry");
    reload(device);
    check(!G->gWipeUserConfig && !G->gReloadConfigPending && G->unknown_persist_vars_count == 0,
        "successful wipe resets unknown owner state and pending requests");
    assign(device, L"$preservation_active", L"2");
    SavePersistentSettings();
    check(UnknownFiles::read(files.first).find("$preservation_active = 2") != std::string::npos &&
        UnknownFiles::read(files.first).find("unknown_") == std::string::npos,
        "subsequent save cannot resurrect unknown values from the previous path or wipe");
}
} // namespace

void test_dx11_persist_unknown_pool_regression()
{
    try {
        UnknownLogCapture log;
        UnknownFiles files;
        files.initialize();
        UnknownDevice device;
        cold_pool_and_cleanup(files, device, log);
        changed_equal_collection(files, device);
        transactional_failure(files, device);
        std::fputs("PASS: real unknown pool persistence, exact-set cleanup, path ownership and reload transactions\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: unknown pool persistence regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
