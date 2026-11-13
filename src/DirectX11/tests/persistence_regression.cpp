#include "CommandList.h"
#include "globals.h"
#include "HackerContext.h"
#include "IniHandler.h"
#include "Override.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// LoadConfigFile deliberately resolves d3dx.ini relative to its module. Refuse
// to create fixtures unless the executable itself resides in a build directory.
class PersistenceFixture {
public:
    std::filesystem::path directory;
    std::filesystem::path config;
    std::filesystem::path settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    bool owns_config = false;

    PersistenceFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate test executable");
        directory = std::filesystem::path(executable).parent_path();
        bool inside_build = false;
        for (const auto& component : directory)
            inside_build |= _wcsicmp(component.c_str(), L"build") == 0;
        check(inside_build, "INI regression executable must be inside build/");
        config = directory / L"d3dx.ini";
        settings = directory / (L"persistence_regression_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse to replace pre-existing user fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse to replace existing d3dx.ini");
        CloseHandle(file);
        owns_config = true;
        migoto_handle = nullptr;
    }

    ~PersistenceFixture()
    {
        // Commands and pool objects retain raw global-variable pointers.
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        if (owns_config) {
            DeleteFileW(config.c_str());
            DeleteFileW(settings.c_str());
        }
    }

    void write_config(bool include_callback_source = true)
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n"
            "[System]\nskip_early_includes_load=0\n"
            "[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[Hunting]\nhunting=0\n"
            "[Constants]\nglobal persist $persistence_regression = 9\n"
            "global persist locked $persistence_locked = 7\n"
            "global $persistence_lock_control = 4\n"
            "global $persistence_callback_result = 0\n"
            "$poolpersistence[0] = 1.25\n"
            "[PresetLockedRegression]\n$persistence_locked=99\n$persistence_lock_control=6\n"
            "$persistence_lock_control trailing=999\n"
            "[PoolPersistence]\npool_size=1+2\npool_index_type=ring\n"
            "pool_variable_default_value=$persistence_regression-7.75\npool_persist_variables=1\n"
            "pool_lazy_initialization=1\npool_element_type_switch_reset=0\n"
            "pool_keep_alive_frames=5\npool_reset_expired_elements=0\n"
            "[PoolPersistenceProxy]\npool_size=1\n"
            "[PoolPersistenceFIFO]\npool_size=2\npool_index_type=fifo\n"
            "pool_persist_variables=1\npool_lazy_initialization=0\n"
            "[PoolExpirationLegacyRegression]\npool_keep_alive_frames=2\npool_reset_expired_elements=0\n"
            "[PoolExpirationNewRegression]\npool_expiration_timeout_frames=4\npool_expiration_reset_elements=0\n"
            "pool_expiration_refresh_on_read=1\n"
            "[PoolExpirationPriorityRegression]\npool_keep_alive_frames=2\npool_reset_expired_elements=0\n"
            "pool_expiration_timeout_frames=7\npool_expiration_reset_elements=1\n"
            "[PoolExpirationInvalidPriorityRegression]\npool_keep_alive_frames=2\npool_reset_expired_elements=0\n"
            "pool_expiration_timeout_frames=invalid_timeout\npool_expiration_reset_elements=invalid_reset\n";
        output << "[CommandListPersistenceCallbackDestination]\n$persistence_callback_result = 11\n";
        if (include_callback_source)
            output << "[CommandListPersistenceCallbackSource]\n$persistence_callback_result = 22\n";
        output.close();
        check(output.good(), "write isolated INI fixture");
    }
};

class PersistenceDevice {
public:
    HMODULE runtime = nullptr;
    ID3D11Device1* device = nullptr;
    ID3D11DeviceContext1* context = nullptr;
    HackerDevice* wrapper = nullptr;
    std::unique_ptr<HackerContext> hacker_context;

    PersistenceDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate Windows D3D runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load system D3D11 runtime");
        using CreateDevice = decltype(&D3D11CreateDevice);
        auto create = reinterpret_cast<CreateDevice>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve native device creation");
        ID3D11Device* base = nullptr;
        ID3D11DeviceContext* immediate = nullptr;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create persistence WARP device");
        const HRESULT device_result = base->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device));
        const HRESULT context_result = immediate->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&context));
        immediate->Release();
        base->Release();
        check(SUCCEEDED(device_result) && SUCCEEDED(context_result), "query production DX11 interfaces");
        wrapper = new HackerDevice(device, context);
        hacker_context = std::make_unique<HackerContext>(device, context);
        hacker_context->SetHackerDevice(wrapper);
        wrapper->SetHackerContext(hacker_context.get());
    }

    ~PersistenceDevice()
    {
        hacker_context.reset();
        if (context)
            context->Release();
        if (wrapper)
            wrapper->Release();
        else if (device)
            device->Release();
        if (runtime)
            FreeLibrary(runtime);
    }
};

void execute(PersistenceDevice& device, const wchar_t* lhs, const wchar_t* rhs)
{
    CommandListScope scope;
    CommandList commands;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    check(ParseCommandListResourceCopyTargetDirective(L"CommandListPersistenceRegression", lhs,
        &value, &commands, &ini_namespace), "parse production persistence assignment");
    RunCommandList(device.wrapper, device.hacker_context.get(), &commands, nullptr, false);
}

std::string read_settings(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(input.good(), "read saved user configuration");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void run_persistence_regression()
{
    PersistenceFixture fixture;
    fixture.write_config();
    PersistenceDevice device;
    LoadConfigFile();
    device.hacker_context->InitIniParams();
    check(G->user_config == fixture.settings.wstring(), "INI includes select isolated persistence path");
    {
        CommandListScope scope;
        CommandList callback_copy;
        callback_copy.scope = &scope;
        const std::wstring ini_namespace;
        std::wstring source = L"ref commandlistpersistencecallbacksource";
        check(ParseCommandListGeneralCommands(L"CallbackReloadRegression",
            L"commandlistpersistencecallbackdestination", &source, &callback_copy,
            &callback_copy, &callback_copy, &ini_namespace), "parse real callback assignment for reload");
        RunCommandList(device.wrapper, device.hacker_context.get(), &callback_copy, nullptr, false);
        auto& destination = explicitCommandListSections.at(L"commandlistpersistencecallbackdestination");
        RunCommandList(device.wrapper, device.hacker_context.get(), &destination.command_list, nullptr, false);
        check(command_list_globals.at(L"$persistence_callback_result").fval == 22,
            "real loaded callback forwards destination execution to source");
        callback_copy.clear(); // Never retain the source map pointer across reload.
    }
    auto* locked = &command_list_globals.at(L"$persistence_locked");
    check(locked->fval == 7 && (locked->flags & VariableFlags::LOCKED), "locked persist declaration initializes normally");
    Override locked_override;
    locked_override.ParseIniSection(L"PresetLockedRegression");
    check(locked_override.mOverrideVars.count(locked) == 0 &&
        locked_override.mOverrideVars.size() == 1, "Key/Preset parser excludes locked targets and retains unlocked");
    KeyOverrideCycle cycle;
    cycle.ParseIniSection(L"PresetLockedRegression");
    cycle.DownEvent(device.wrapper);
    CurrentTransition.UpdateTransitions(device.wrapper);
    check(locked->fval == 7 && command_list_globals.at(L"$persistence_lock_control").fval == 6,
        "cycle writes unlocked control and leaves locked value unchanged");
    bool found = false;
    check(GetIniInt(L"PoolPersistence", L"pool_size", -1, &found) == 3 && found,
        "INI kv_map resolves an arithmetic option and reports found");
    found = true;
    check(GetIniFloat(L"PoolPersistence", L"missing_option", 17.5f, &found) == 17.5f && !found,
        "missing INI option preserves fallback and clears found");
    auto& pool = customResourcePools.at(L"poolpersistence");
    check(!pool.read_refreshes_expiration &&
        customResourcePools.at(L"poolexpirationnewregression").read_refreshes_expiration,
        "real INI preserves default read-refresh off and enables the explicit option");
    check(pool.GetPoolSize() == 3 && pool.index_type == PoolIndexType::RING &&
        pool.lazy_initialization && !pool.element_type_switch_reset &&
        pool.keep_alive_frames == 5 && !pool.reset_expired_elements &&
        pool.variable_template->fval == 1.25f && pool.variable_template->flags == VariableFlags::PERSIST,
        "real INI loader applies all pool options");
    check(customResourcePools.at(L"poolpersistencefifo").variable_template->flags == VariableFlags::NONE,
        "INI loader refuses persistence for non-RING pools");
    check(customResourcePools.at(L"poolexpirationlegacyregression").keep_alive_frames == 2 &&
        !customResourcePools.at(L"poolexpirationlegacyregression").reset_expired_elements,
        "legacy expiration INI aliases retain timeout and reset behavior");
    check(customResourcePools.at(L"poolexpirationnewregression").keep_alive_frames == 4 &&
        !customResourcePools.at(L"poolexpirationnewregression").reset_expired_elements,
        "new upstream expiration INI options load timeout and reset behavior");
    check(customResourcePools.at(L"poolexpirationpriorityregression").keep_alive_frames == 7 &&
        customResourcePools.at(L"poolexpirationpriorityregression").reset_expired_elements,
        "new expiration INI names take priority over conflicting legacy aliases");
    check(customResourcePools.at(L"poolexpirationinvalidpriorityregression").keep_alive_frames == UINT32_MAX &&
        customResourcePools.at(L"poolexpirationinvalidpriorityregression").reset_expired_elements,
        "present invalid new expiration names use defaults instead of legacy values");
    check(!G->user_config_dirty, "Constants initialization does not dirty user configuration");
    execute(device, L"$poolpersistence[0]", L"12.375");
    execute(device, L"poolpersistenceproxy", L"ref poolpersistence");
    execute(device, L"$poolpersistenceproxy[1]", L"-7.5");
    pool.GetResource(1, false, true, true);
    check(pool.GetVariable(1, false, true, false)->fval == -7.5f,
        "mixed pool preserves persistent variable during resource use");
    check(G->user_config_dirty && persistent_variables.size() == 4,
        "only global and allocated RING slots register persistence");

    SavePersistentSettings();
    check(!G->user_config_dirty, "successful persistence clears dirty state");
    const std::string saved = read_settings(fixture.settings);
    check(saved.find("$poolpersistence[0] = 12.375") != std::string::npos &&
        saved.find("$poolpersistence[1] = -7.5") != std::string::npos &&
        saved.find("$persistence_regression = 9") != std::string::npos &&
        saved.find("$poolpersistence[2]") == std::string::npos &&
        saved.find("$poolpersistenceproxy[") == std::string::npos &&
        saved.find("$poolpersistencefifo[") == std::string::npos,
        "actual saved INI contains exactly persistent owners and allocated slots");
    check(saved.find("$persistence_locked =") == std::string::npos,
        "locked declaration has no runtime persistence assignment to serialize");
    SavePersistentSettings();
    check(read_settings(fixture.settings) == saved, "clean save leaves persisted data unchanged");

    fixture.write_config(false);
    ReloadConfig(device.wrapper);
    check(customResourcePools.at(L"poolexpirationnewregression").read_refreshes_expiration,
        "real reload reconstructs the configured read-refresh policy");
    check(explicitCommandListSections.count(L"commandlistpersistencecallbacksource") == 0,
        "real reload removes callback source section from registry");
    {
        auto& destination = explicitCommandListSections.at(L"commandlistpersistencecallbackdestination");
        check(destination.command_list.ResolveCommandList() == &destination.command_list,
            "real reload resets old destination callback pointer");
        RunCommandList(device.wrapper, device.hacker_context.get(), &destination.command_list, nullptr, false);
        check(command_list_globals.at(L"$persistence_callback_result").fval == 11,
            "reloaded destination executes its own body after old source was deleted");
    }

    check(command_list_globals.at(L"$persistence_locked").fval == 7, "locked persisted declaration survives reload");
    std::fprintf(stderr, "Persistence reload diagnostics: slot0=%.9g slot1=%.9g dirty=%d count=%zu locked=%.9g\n",
        customResourcePools.at(L"poolpersistence").GetVariable(0, false, true, false)->fval,
        customResourcePools.at(L"poolpersistence").GetVariable(1, false, true, false)->fval,
        G->user_config_dirty, persistent_variables.size(), command_list_globals.at(L"$persistence_locked").fval);
    check(customResourcePools.at(L"poolpersistence").GetVariable(0, false, true, false)->fval == 12.375f &&
        customResourcePools.at(L"poolpersistence").GetVariable(1, false, true, false)->fval == -7.5f &&
        !G->user_config_dirty && persistent_variables.size() == 4,
        "full production reload restores saved pool values without duplicate registrations");
    ReloadConfig(device.wrapper);
    check(command_list_globals.at(L"$persistence_locked").fval == 7, "locked persisted declaration survives reload");
    check(persistent_variables.size() == 4 &&
        customResourcePools.at(L"poolpersistence").GetVariable(1, false, true, false)->fval == -7.5f,
        "second full reload has no stale registry pointers or duplicates");

    {
        std::ofstream legacy(fixture.settings, std::ios::app);
        legacy << "$persistence_locked = 99\n";
        legacy.close();
        check(legacy.good(), "append legacy locked persisted assignment");
    }
    ReloadConfig(device.wrapper);
    check(command_list_globals.at(L"$persistence_locked").fval == 7 && G->user_config_dirty,
        "legacy saved assignment cannot overwrite locked declaration and requests cleanup");
    ReloadConfig(device.wrapper);
    check(command_list_globals.at(L"$persistence_locked").fval == 7 && !G->user_config_dirty &&
        read_settings(fixture.settings).find("$persistence_locked =") == std::string::npos,
        "legacy locked assignment is removed once and following reload stays clean");

    execute(device, L"$poolpersistence[0]", L"42.25");
    const auto missing_parent = fixture.directory /
        (L"persistence_missing_" + std::to_wstring(GetCurrentProcessId()));
    check(!std::filesystem::exists(missing_parent), "failure fixture parent must be absent");
    G->user_config = (missing_parent / L"settings.ini").wstring();
    SavePersistentSettings();
    check(G->user_config_dirty, "failed persistence retains dirty state for retry");
    G->user_config = fixture.settings.wstring();
    SavePersistentSettings();
    check(!G->user_config_dirty && read_settings(fixture.settings).find("$poolpersistence[0] = 42.25") != std::string::npos,
        "failed persistence retries successfully without another assignment");

    execute(device, L"$poolpersistence[*]", L"null");
    check(G->user_config_dirty, "reset persistent pool marks configuration dirty");
    ReloadConfig(device.wrapper);
    check(command_list_globals.at(L"$persistence_locked").fval == 7, "locked persisted declaration survives reload");
    check(customResourcePools.at(L"poolpersistence").GetVariable(0, false, true, false)->fval == 1.25f &&
        customResourcePools.at(L"poolpersistence").GetVariable(1, false, true, false)->fval == 1.25f &&
        !G->user_config_dirty, "reload saves reset defaults before reconstructing persistent pool");
}

} // namespace

void test_dx11_persistence_regression()
{
    try {
        run_persistence_regression();
        std::fputs("PASS: real INI pool loading, persistence, failure retry and config reload\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: persistence regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
