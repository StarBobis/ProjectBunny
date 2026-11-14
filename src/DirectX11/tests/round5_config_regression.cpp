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
    bool saved_dump_mips = G->dump_mips;
    int saved_hunting = G->hunting;
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
        G->dump_mips = saved_dump_mips;
        G->hunting = saved_hunting;
        G->gShowWarnings = warnings; G->user_config = user;
        G->user_config_dirty = dirty; G->gConfigInitialized = initialized;
        G->clear_unknown_settings = automatic; G->clear_saved_persist_vars = clear;
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants, const char* hunting = "")
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Hunting]\nhunting=2\n" << hunting << "[Rendering]\nallow_buffer_resize=$round5_generation\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants
            << "[PoolRound5Generation]\npool_size=3\npool_index_type=static\npool_lazy_initialization=1\npool_variable_default_value=7\n"
               "[ResourceRound5Generation]\nwidth=$round5_size+4\n";
        file.close(); check(file.good(), "write owned parser config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};
} // namespace
void test_dx11_round5_config_regression()
{
    ConfigGenerationFiles files;
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\n");
    check(G->dump_mips, "DDS dump_mips defaults to enabled in real config parser");
    check(command_list_globals.count(L"$poolround5generation[0]") == 0,
        "first load keeps STATIC pool slot lazy");
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\nglobal locked $probe=$poolround5generation[0]\n", "dump_mips=0\n");
    check(!G->dump_mips, "DDS dump_mips=0 disables mip export through real config parser");
    auto& pool = customResourcePools.at(L"poolround5generation");
    const auto ghosts = command_list_globals.count(L"$poolround5generation[0]");
    auto* slot = pool.GetVariable(0, false, false, false);
    std::fprintf(stderr, "round5 old STATIC ghost=%zu new slot=%p\n",
        ghosts, static_cast<void*>(slot));
    check(ghosts == 0,
        "Constants initializer must not instantiate prior-generation STATIC pool slot in new globals");
    check(slot != nullptr, "current-generation STATIC pool lazy slot initializes");
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\n");
    check(G->dump_mips, "DDS omitted dump_mips resets to default on reload");
}
