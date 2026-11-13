#include "CommandList.h"
#include "DLLMainHook.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
void require_snapshot(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class ReloadSnapshotFiles {
    HINSTANCE module = migoto_handle;
    std::wstring user = G->user_config;
    unsigned frame = G->frame_no;
    int dirty = G->user_config_dirty;
    bool auto_clear = G->clear_unknown_settings;
    bool clear_saved = G->clear_saved_persist_vars;
    bool wipe = G->gWipeUserConfig;
    bool pending = G->gReloadConfigPending;
    bool initialized = G->gConfigInitialized;
    decltype(G->gSettingsSaveTime) save_time = G->gSettingsSaveTime;
    decltype(G->iniParamsReserved) reserved = G->iniParamsReserved;
    std::string locale = std::setlocale(LC_CTYPE, nullptr);
public:
    std::filesystem::path config, settings;
    bool owns_config = false, owns_settings = false;
    ReloadSnapshotFiles()
    {
        wchar_t executable[MAX_PATH]{};
        require_snapshot(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate snapshot fixture executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool under_build = false;
        for (const auto& part : directory) under_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_snapshot(under_build, "snapshot fixture must remain in build");
        config = directory / L"d3dx.ini";
        settings = directory / (L"round2_snapshot_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    }
    void Create()
    {
        for (const auto& path : {config, settings}) {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            require_snapshot(file != INVALID_HANDLE_VALUE, "refuse to overwrite snapshot fixture files");
            CloseHandle(file);
            if (path == config) owns_config = true; else owns_settings = true;
        }
        std::ofstream output(config);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "clear_unknown_settings=0\n[Include]\nuser_config=" << settings.filename().string() <<
            "\n[Constants]\nglobal persist $round2_snapshot = 1\n";
        output.close();
        require_snapshot(output.good(), "write snapshot fixture config");
        migoto_handle = nullptr;
        G->gWipeUserConfig = false;
        G->gReloadConfigPending = false;
        G->gConfigInitialized = false;
        G->frame_no = 0;
    }
    ~ReloadSnapshotFiles()
    {
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; G->user_config = user;
        G->frame_no = frame; G->user_config_dirty = dirty;
        G->clear_unknown_settings = auto_clear; G->clear_saved_persist_vars = clear_saved;
        G->gWipeUserConfig = wipe; G->gReloadConfigPending = pending; G->gConfigInitialized = initialized;
        G->gSettingsSaveTime = save_time; G->iniParamsReserved = reserved;
        std::setlocale(LC_CTYPE, locale.c_str());
        if (owns_settings) DeleteFileW(settings.c_str());
        if (owns_config) DeleteFileW(config.c_str());
    }
};

class ReloadSnapshotDevice {
    HMODULE runtime = nullptr;
public:
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> immediate, deferred;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> main_context, worker_context;
    ReloadSnapshotDevice()
    {
        wchar_t system[MAX_PATH]{};
        require_snapshot(GetSystemDirectoryW(system, MAX_PATH) != 0, "locate snapshot WARP runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        require_snapshot(runtime != nullptr, "load snapshot WARP runtime");
        const auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_snapshot(create != nullptr, "resolve snapshot device creation");
        ComPtr<ID3D11Device> base; ComPtr<ID3D11DeviceContext> context;
        require_snapshot(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &context)) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(context.As(&immediate)) && SUCCEEDED(native->CreateDeferredContext1(0, &deferred)), "create real snapshot contexts");
        device = new HackerDevice(native.Get(), immediate.Get()); native->AddRef();
        main_context = std::make_unique<HackerContext>(native.Get(), immediate.Get());
        worker_context = std::make_unique<HackerContext>(native.Get(), deferred.Get());
        main_context->SetHackerDevice(device); worker_context->SetHackerDevice(device);
        device->SetHackerContext(main_context.get()); device->SetHackerSwapChain(nullptr);
    }
    ~ReloadSnapshotDevice()
    {
        worker_context.reset(); main_context.reset(); deferred.Reset(); immediate.Reset(); native.Reset();
        if (device) device->Release();
        if (runtime) FreeLibrary(runtime);
    }
};

struct SnapshotWriter {
    ReloadSnapshotDevice* device;
    CommandList* commands;
    bool acquired = false, ran = false, failed = false;
};
DWORD WINAPI write_during_save(void* argument)
{
    auto& writer = *static_cast<SnapshotWriter*>(argument);
    if (!TryEnterCriticalSection(&G->mCriticalSection)) return 0;
    writer.acquired = true;
    try {
        // Even a writer that explicitly cooperates with G can enter the save
        // transaction today. Use a real deferred context and production
        // assignment; this does not assert that all production callers hold G.
        RunCommandList(writer.device->device, writer.device->worker_context.get(), writer.commands, nullptr, false);
        writer.ran = true;
    } catch (...) { writer.failed = true; }
    LeaveCriticalSection(&G->mCriticalSection);
    return 0;
}

using MoveSettings = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
MoveSettings original_move = nullptr;
SnapshotWriter* observed_writer = nullptr;
const wchar_t* observed_settings = nullptr;
bool move_observed = false;
BOOL WINAPI observe_settings_commit(LPCWSTR source, LPCWSTR destination, DWORD flags)
{
    if (observed_writer && destination && !_wcsicmp(destination, observed_settings)) {
        auto* writer = observed_writer;
        observed_writer = nullptr; // Observe only the first real save transaction.
        move_observed = true;
        HANDLE thread = CreateThread(nullptr, 0, write_during_save, writer, 0, nullptr);
        if (!thread || WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) std::_Exit(EXIT_FAILURE);
        CloseHandle(thread);
    }
    return original_move(source, destination, flags);
}
class SettingsCommitObserver {
    SIZE_T hook = 0;
public:
    SettingsCommitObserver(SnapshotWriter& writer, const std::wstring& destination)
    {
        observed_writer = &writer; observed_settings = destination.c_str(); move_observed = false;
        const DWORD error = cHookMgr.Hook(&hook, reinterpret_cast<void**>(&original_move),
            reinterpret_cast<void*>(&MoveFileExW), reinterpret_cast<void*>(&observe_settings_commit));
        if (error != ERROR_SUCCESS) {
            Restore(); throw std::runtime_error("install temporary settings I/O observer");
        }
    }
    void Restore() noexcept
    {
        observed_writer = nullptr; observed_settings = nullptr;
        if (hook && cHookMgr.Unhook(hook) != ERROR_SUCCESS) std::_Exit(EXIT_FAILURE);
        hook = 0; original_move = nullptr;
    }
    ~SettingsCommitObserver() { Restore(); }
};

void run_reload_snapshot()
{
    ReloadSnapshotFiles files; files.Create();
    ReloadSnapshotDevice device;
    LoadConfigFile(); device.main_context->InitIniParams();
    CommandListScope scope; CommandList commands; commands.scope = &scope;
    const std::wstring ini_namespace, raw = L"$round2_snapshot = 99";
    std::wstring value = L"99";
    require_snapshot(ParseCommandListVariableAssignment(L"CommandListRound2Snapshot", L"$round2_snapshot", &value,
        &raw, &commands, &commands, nullptr, &ini_namespace), "parse real persistent writer command");
    G->user_config_dirty = true;
    SnapshotWriter writer{&device, &commands};
    const std::wstring destination = files.settings.wstring();
    {
        SettingsCommitObserver observer(writer, destination);
        ReloadConfig(device.device);
    }
    const float final_value = command_list_globals.at(L"$round2_snapshot").fval;
    std::fprintf(stderr, "snapshot I/O boundary: hit=%d G_available=%d command_ran=%d final_value=%g\n",
        move_observed, writer.acquired, writer.ran, final_value);
    require_snapshot(move_observed && !writer.failed, "observe real ReloadConfig settings transaction");
    // A blocked worker must not be mistaken for a broken command path. Reparse
    // against the new registry: the original command intentionally references
    // the retired pre-reload variable and must not be reused after ReloadConfig.
    CommandListScope after_scope; CommandList after; after.scope = &after_scope;
    const std::wstring after_raw = L"$round2_snapshot = 7";
    value = L"7";
    require_snapshot(ParseCommandListVariableAssignment(L"CommandListRound2SnapshotAfter", L"$round2_snapshot", &value,
        &after_raw, &after, &after, nullptr, &ini_namespace), "parse post-reload persistent writer");
    EnterCriticalSection(&G->mCriticalSection);
    try { RunCommandList(device.device, device.worker_context.get(), &after, nullptr, false); }
    catch (...) { LeaveCriticalSection(&G->mCriticalSection); throw; }
    LeaveCriticalSection(&G->mCriticalSection);
    require_snapshot(command_list_globals.at(L"$round2_snapshot").fval == 7 && G->user_config_dirty,
        "normal persistent writer remains usable after reload releases G");
    // This observes the post-snapshot/pre-commit I/O boundary, not a simultaneous
    // fval read. Combined with the source lock boundaries it proves a legal
    // writer can enter the transaction; the old save/reload overwrites its 99.
    require_snapshot(!writer.acquired, "persistent save/reload must exclude a real command writer until the snapshot transaction commits");
}
} // namespace

void test_dx11_round2_reload_snapshot_regression()
{
    try { run_reload_snapshot(); std::fputs("PASS: persistent reload snapshot transaction excludes deferred command writer\n", stderr); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: reload snapshot regression: %s\n", error.what()); std::exit(EXIT_FAILURE);
    }
}
