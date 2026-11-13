#include "Logging.h"
#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "ExpressionFunctions.h"
#include "globals.h"
#include "Input.h"
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class ConfigGenerationFiles {
public:
    HINSTANCE module = migoto_handle;
    LogVerbosity log_level = gLogVerbosity;
    bool log_debug = gLogDebug;
    bool warnings = G->gShowWarnings;
    std::wstring user = G->user_config;
    int dirty = G->user_config_dirty;
    bool saved_resize = G->allow_buffer_resize;
    bool initialized = G->gConfigInitialized;
    bool automatic = G->clear_unknown_settings, clear = G->clear_saved_persist_vars;
    std::filesystem::path config, settings;
    ConfigGenerationFiles()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate parser fixture executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "parser fixture must stay under build");
        config = directory / L"d3dx.ini";
        settings = directory / (L"config_generation_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing parser settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing parser configuration");
        CloseHandle(file); migoto_handle = nullptr;
    }
    ~ConfigGenerationFiles()
    {
        ClearKeyBindings();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        registered_command_lists.clear();
        explicitCommandListSections.clear(); customResourcePools.clear(); customResources.clear();
        persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; gLogVerbosity = log_level; gLogDebug = log_debug;
        G->allow_buffer_resize = saved_resize;
        G->gShowWarnings = warnings; G->user_config = user;
        G->user_config_dirty = dirty; G->gConfigInitialized = initialized;
        G->clear_unknown_settings = automatic; G->clear_saved_persist_vars = clear;
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants)
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Hunting]\nhunting=0\n[Rendering]\nallow_buffer_resize=$round4_generation\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants
            << "[PoolRound4Generation]\npool_size=$round4_size\npool_variable_default_value=$round4_size+2\n"
               "[ResourceRound4Generation]\nwidth=$round4_size+4\n";
        file.close(); check(file.good(), "write owned parser config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};
} // namespace
void test_dx11_round4_config_regression()
{
    ConfigGenerationFiles files;
    files.load("global locked $round4_generation=0\nglobal locked $round4_size=3\n");
    const bool first = G->allow_buffer_resize;
    check(customResourcePools.at(L"poolround4generation").GetPoolSize() == 3 &&
        customResourcePools.at(L"poolround4generation").variable_template->fval == 5 &&
        customResources.at(L"resourceround4generation").override_width == 7,
        "resource and pool options after Constants read current-generation declarations");
    files.load("global locked $round4_generation=0\nglobal locked $round4_size=3\n");
    const bool repeated = G->allow_buffer_resize;
    std::fprintf(stderr, "round4 identical config loads: first_resize=%d second_resize=%d\n", first, repeated);
    files.load("global locked $round4_generation=0\nglobal locked $round4_size=5\n");
    check(customResourcePools.at(L"poolround4generation").GetPoolSize() == 5 &&
        customResourcePools.at(L"poolround4generation").variable_template->fval == 7 &&
        customResources.at(L"resourceround4generation").override_width == 9,
        "changed declarations refresh later resource/pool option snapshots on reload");
    check(first == repeated, "identical early options must not read the previous generation registry on reload");
}
