#include "Logging.h"
#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "ExpressionFunctions.h"
#include "globals.h"
#include "Input.h"
#include "ShaderRegex.h"
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class RegexCacheScope {
    std::wstring previous_path = G->SHADER_CACHE_PATH;
    bool previous_cache = G->CACHE_SHADERS;
    bool previous_export = G->EXPORT_FIXED;
    std::filesystem::path path;
public:
    explicit RegexCacheScope(const std::filesystem::path& directory)
        : path(directory / (L"regex_exclusion_cache_" + std::to_wstring(GetCurrentProcessId())))
    {
        check(!std::filesystem::exists(path) && std::filesystem::create_directory(path), "regex: exclusive cache fixture under build");
        wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, path.c_str());
        G->CACHE_SHADERS = true; G->EXPORT_FIXED = false;
    }
    ~RegexCacheScope()
    {
        wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, previous_path.c_str());
        G->CACHE_SHADERS = previous_cache; G->EXPORT_FIXED = previous_export;
        std::error_code error; std::filesystem::remove_all(path, error);
    }
};
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
    void load(const char* constants, const char* hunting = "", const char* regex = "")
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Hunting]\nhunting=2\n" << hunting << "[Rendering]\nallow_buffer_resize=$round5_generation\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants
            << "[PoolRound5Generation]\npool_size=3\npool_index_type=static\npool_lazy_initialization=1\npool_variable_default_value=7\n"
               "[ResourceRound5Generation]\nwidth=$round5_size+4\n" << regex;
        file.close(); check(file.good(), "write owned parser config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};
} // namespace
void test_dx11_round5_config_regression()
{
    ConfigGenerationFiles files;
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\n", "",
        "[ShaderRegexExclusionTest]\nshader_model=vs_5_0\nfilter_index=42\n"
        "exclude_hash=5927c81f6515eb11\nexclude_hash=0XABCDEF0123456789\nexclude_hash=5927C81F6515EB11\n");
    auto& group = shader_regex_groups.at(L"ShaderRegexExclusionTest");
    check(group.excluded_hashes.size() == 2 && group.excludes(0x5927c81f6515eb11ULL) &&
        group.excludes(0xabcdef0123456789ULL), "regex: actual INI parser collects all repeated hashes");
    check(!group.add_excluded_hash(L"") && !group.add_excluded_hash(L"0x") &&
        !group.add_excluded_hash(L"10000000000000000") && !group.add_excluded_hash(L"-1") &&
        !group.add_excluded_hash(L"1234xyz"), "regex: malformed hashes rejected without insertion");
    check(group.excluded_hashes.size() == 2, "regex: failed parsing does not alter exclusions");
    group.link_command_lists_and_filter_index(0x5927c81f6515eb11ULL);
    check(!G->mShaderOverrideMap.count(0x5927c81f6515eb11ULL), "regex: excluded hash has no filter or linked commands");
    std::string model = "vs_5_0", assembly = "vs_5_0\nmov o0, v0\n";
    bool decompile = false;
    link_shader_regex_groups_without_patterns(L"vs", &model, 0x5927c81f6515eb11ULL, &decompile);
    check(!decompile && !G->mShaderOverrideMap.count(0x5927c81f6515eb11ULL), "regex: no-pattern processing excludes hash");
    apply_shader_regex_groups(&assembly, L"vs", &model, 0xabcdef0123456789ULL, nullptr);
    check(!G->mShaderOverrideMap.count(0xabcdef0123456789ULL), "regex: disassembled processing excludes second hash");
    group.link_command_lists_and_filter_index(0x123456789abcdef0ULL);
    check(G->mShaderOverrideMap.at(0x123456789abcdef0ULL).filter_index == 42, "regex: unexcluded hash retains filter linking");
    {
        RegexCacheScope cache(files.config.parent_path());
        decompile = false;
        link_shader_regex_groups_without_patterns(L"vs", &model, 0x5927c81f6515eb11ULL, &decompile);
        std::vector<byte_t> bytecode;
        check(load_shader_regex_cache(0x5927c81f6515eb11ULL, L"vs", &bytecode, nullptr) == ShaderRegexCache::NO_MATCH,
            "regex: excluded no-pattern result round-trips through actual cache");
        decompile = false;
        link_shader_regex_groups_without_patterns(L"vs", &model, 0x123456789abcdef0ULL, &decompile);
        check(load_shader_regex_cache(0x123456789abcdef0ULL, L"vs", &bytecode, nullptr) == ShaderRegexCache::MATCH,
            "regex: unexcluded result round-trips through actual cache");
        group.excluded_hashes.insert(0x123456789abcdef0ULL);
        check(load_shader_regex_cache(0x123456789abcdef0ULL, L"vs", &bytecode, nullptr) == ShaderRegexCache::NO_CACHE,
            "regex: inconsistent excluded cache match is rejected before linking");
    }
    {
        auto& patterned = shader_regex_groups[L"ShaderRegexPatternExclusionTest"];
        patterned.shader_models.insert("vs_5_0");
        patterned.excluded_hashes.insert(0x5927c81f6515eb11ULL);
        std::string pattern = "mov o0, v0", replacement = "mov o0, v1";
        auto& regex = patterned.patterns[L"Pattern"];
        check(regex.compile(&pattern), "regex: compile actual PCRE pattern");
        regex.replace = replacement; regex.do_replace = true;
        // Rebuild the cache's group indices after adding the owned test group.
        shader_regex_group_index.clear();
        for (auto& entry : shader_regex_groups) shader_regex_group_index.push_back(&entry.second);
        const auto original_assembly = assembly;
        check(!apply_shader_regex_groups(&assembly, L"vs", &model, 0x5927c81f6515eb11ULL, nullptr) && assembly == original_assembly,
            "regex: excluded patterned shader is neither modified nor linked");
        decompile = false;
        link_shader_regex_groups_without_patterns(L"vs", &model, 0x5927c81f6515eb11ULL, &decompile);
        check(!decompile, "regex: excluded patterns do not request disassembly");
    }
    const auto excluded_generation = shader_regex_hash;
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\n", "",
        "[ShaderRegexExclusionTest]\nshader_model=vs_5_0\nfilter_index=42\nexclude_hash=1234\n");
    check(shader_regex_hash != excluded_generation &&
        !shader_regex_groups.at(L"ShaderRegexExclusionTest").excludes(0x5927c81f6515eb11ULL),
        "regex: reload replaces exclusions and invalidates cache generation");
    files.load("global locked $round5_generation=0\nglobal locked $round5_size=3\n", "",
        "[ShaderRegexExclusionTest]\nshader_model=vs_5_0\nexclude_hash=invalid\n");
    check(!shader_regex_groups.count(L"ShaderRegexExclusionTest"), "regex: malformed INI disables group");

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
