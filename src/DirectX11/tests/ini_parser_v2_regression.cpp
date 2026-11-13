#include "Logging.h"
#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "Input.h"
#include "Override.h"
#include "globals.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
class IniV2Files {
public:
    HINSTANCE module = migoto_handle;
    std::wstring user = G->user_config;
    int dirty = G->user_config_dirty;
    bool initialized = G->gConfigInitialized, warnings = G->gShowWarnings;
    bool clear_unknown = G->clear_unknown_settings;
    bool cleanup = G->clear_saved_persist_vars;
    uint32_t unknown_count = G->unknown_persist_vars_count;
    bool disabled = G->disable_input, input_initialized = G->disable_input_initialized;
    InputDisableScope scope = G->input_disable_scope;
    LogVerbosity level = gLogVerbosity;
    bool debug = gLogDebug;
    std::filesystem::path base, directory, config, settings;
    std::vector<std::filesystem::path> files, directories;
    IniV2Files()
    {
        try {
        wchar_t exe[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0, "locate INI v2 executable");
        base = std::filesystem::path(exe).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "INI v2 fixtures stay under build");
        config = base / L"d3dx.ini";
        directory = base / (L"ini_v2_" + std::to_wstring(GetCurrentProcessId()));
        settings = directory / L"settings.ini";
        mkdir(directory); mkdir(directory / L"Mods"); mkdir(directory / L"ModsOther"); mkdir(directory / L"Nested");
        create(config); create(settings);
        const auto direct = directory / L"Mods" / L"direct.ini";
        create(direct); write(direct,
            "namespace=Ini V2 Direct-Alias\n[System]\nini_v2_probe=2\n[KeyDirect]\nkey=F15\n$ini_v2_result=1\n");
        const auto mod = directory / L"Mods" / L"mod.ini";
        create(mod); write(mod,
            "namespace=Ini V2 \xe6\xa8\xa1\xe5\x9e\x8b-hyphen\n[PoolVars]\npool_size=3\npool_lazy_initialization=0\n"
            "pool_variable_default_value=4\n[KeyMod]\nkey=F16\n$ini_v2_result=2\n");
        const auto skip = directory / L"Mods" / L"skip.ini";
        create(skip); write(skip, "[ResourceSkipV2]\nwidth=999\n");
        const auto other = directory / L"ModsOther" / L"other.ini";
        create(other); write(other, "namespace=Ini V2 Other\n[KeyOther]\nkey=F17\n$ini_v2_result=3\n");
        const auto loader = directory / L"loader.ini";
        create(loader); write(loader, "namespace=Loader Renamed V2\n[Include]\ninclude_recursive=Nested\n");
        const auto nested = directory / L"Nested" / L"child.ini";
        create(nested); write(nested, "namespace=Ini V2 Nested\n[KeyNested]\nkey=F18\n$ini_v2_result=4\n");
        const auto preamble = directory / L"preamble.ini";
        create(preamble); write(preamble,
            "ini_v2_should_not_attach=99\n[ResourcePreambleV2]\nwidth=33\nwidth=44\n");
        const auto prefix = directory.filename().string();
        write(config, "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\nclear_unknown_settings=0\nini_v2_probe=1\n"
            "[Input]\ninput=1\ninput_disable_mode=mods\n"
            "[Include]\ninclude=" + prefix + "\\Mods\\direct.ini\ninclude_recursive=" + prefix + "\\Mods\n"
            "include=" + prefix + "\\ModsOther\\other.ini\ninclude=" + prefix + "\\loader.ini\n"
            "include=" + prefix + "\\preamble.ini\nexclude_recursive=*skip.ini\nuser_config=" + prefix + "\\settings.ini\n"
            "[Constants]\nglobal persist $ini_v2_value=1\nglobal $ini_v2_result=0\nglobal $pool_test=7\nglobal $ini_v2_a=3\n"
            "[PoolIndexV2]\npool_size=2\npool_lazy_initialization=0\npool_variable_default_value=1\n"
            "[ResourceEntryV2]\nwidth=11\nwidth=12\n[ResourceEntryV2]\nwidth=99\n[]\nwidth=88\n"
            "[ResourceNextV2]\nwidth=22\n[CommandListOrderV2]\nlocal $v2_local=2\nif 1\n"
            "$ini_v2_result=$v2_local+3\nelse\n$ini_v2_result=99\nendif\n");
        write(settings, "[System]\nini_v2_probe=9\n[Constants]\n$ini_v2_value=9\n$poolfuturev2[0]=44\n");
        migoto_handle = nullptr;
        G->disable_input_initialized = false;
        G->gConfigInitialized = false;
        G->gShowWarnings = false;
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        } catch (...) {
            cleanup_files();
            throw;
        }
    }
    void cleanup_files()
    {
        for (auto it = files.rbegin(); it != files.rend(); ++it) DeleteFileW(it->c_str());
        for (auto it = directories.rbegin(); it != directories.rend(); ++it) RemoveDirectoryW(it->c_str());
    }
    ~IniV2Files()
    {
        ClearKeyBindings();
        registered_command_lists.clear();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        explicitCommandListSections.clear(); presetOverrides.clear();
        customResourcePools.clear(); customResources.clear(); persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; G->user_config = user; G->user_config_dirty = dirty;
        G->gConfigInitialized = initialized; G->gShowWarnings = warnings;
        G->clear_unknown_settings = clear_unknown;
        G->clear_saved_persist_vars = cleanup; G->unknown_persist_vars_count = unknown_count;
        G->disable_input = disabled; G->disable_input_initialized = input_initialized; G->input_disable_scope = scope;
        gLogVerbosity = level; gLogDebug = debug;
        cleanup_files();
    }
    void mkdir(const std::filesystem::path& path)
    {
        check(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "refuse an existing INI v2 directory");
        directories.push_back(path);
    }
    void create(const std::filesystem::path& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse an existing INI v2 file");
        CloseHandle(file); files.push_back(path);
    }
    static void write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file << text; file.close();
        check(file.good(), "write owned INI v2 file");
    }
};
float evaluate(const std::wstring& text, CommandListState& state)
{
    CommandListExpression expression; const std::wstring ns;
    CommandListScope scope;
    check(expression.parse(&text, &ns, &scope), "parse INI v2 expression through production tokenizer");
    return expression.evaluate(&state);
}
void test_section_and_include(IniV2Files& files, CommandListState& state)
{
    LoadConfigFile();
    check(customResources.at(L"resourceentryv2").override_width == 11 &&
        customResources.at(L"resourcenextv2").override_width == 22,
        "duplicate section and empty header cannot retain the previous section pointer");
    const std::wstring preamble_ns = (files.directory.filename() / L"preamble.ini").wstring();
    const std::wstring plain = L"ResourcePreambleV2";
    std::wstring preamble_resource;
    check(get_namespaced_section_name_lower(&plain, &preamble_ns, &preamble_resource), "resolve included section namespace");
    check(customResources.at(preamble_resource).override_width == 33 &&
        GetIniInt(preamble_resource.c_str(), L"ini_v2_should_not_attach", -1, nullptr) == -1,
        "cross-file preamble does not reuse a prior map entry and duplicate key remains first");
    check(GetIniInt(L"System", L"ini_v2_probe", -1, nullptr) == 9,
        "included global section override is followed by user-config override last");
    check(GetIniKeyBindingScope(L"Key\\Ini V2 Direct-Alias\\Direct") == InputDisableScope::ALL &&
        GetIniKeyBindingScope(L"Key\\Ini V2 \u6a21\u578b-hyphen\\Mod") == InputDisableScope::MODS &&
        GetIniKeyBindingScope(L"Key\\Ini V2 Other\\Other") == InputDisableScope::ALL &&
        GetIniKeyBindingScope(L"Key\\Ini V2 Nested\\Nested") == InputDisableScope::MODS,
        "direct-first and nested recursive provenance survive aliases and include switch dispatch");
    for (const auto& resource : customResources)
        check(resource.first.find(L"skipv2") == std::wstring::npos, "exclude_recursive branch remains effective");
    for (const auto& command : G->constants_command_list.commands) command->run(&state);
    check(command_list_globals.at(L"$ini_v2_value").fval == 9, "real persisted scalar is applied after declaration");
    auto& order = explicitCommandListSections.at(L"commandlistorderv2").command_list;
    for (const auto& command : order.commands) command->run(&state);
    check(command_list_globals.at(L"$ini_v2_result").fval == 5, "variable and flow priority preserve real local/If execution");
    SavePersistentSettings();
    std::ifstream saved(files.settings, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    check(text.find("$poolfuturev2[0] = 44") != std::string::npos && text.find("$ini_v2_value = 9") != std::string::npos,
        "unknown-pool capture remains reachable through reordered parsing and user-path ownership");
}
void test_pool_lexical(CommandListState& state)
{
    const std::wstring plain = L"PoolVars", ns = L"Ini V2 \u6a21\u578b-hyphen";
    std::wstring pool;
    check(get_namespaced_section_name_lower(&plain, &ns, &pool), "resolve real Unicode pool section");
    auto& target = customResourcePools.at(pool);
    auto* variable = target.GetVariable(1, false, false, true);
    check(variable != nullptr, "initialize real target pool variable"); variable->fval = 13;
    auto* index = customResourcePools.at(L"poolindexv2").GetVariable(0, false, false, true);
    check(index != nullptr, "initialize real nested-index pool slot"); index->fval = 1;
    const auto globals_before = command_list_globals.size();
    check(evaluate(L"$" + pool + L"[$poolindexv2[0]+(0)]+1", state) == 14,
        "pool candidates accept Unicode spaced/hyphen namespace and nested dynamic index");
    check(evaluate(L"$pool_test+1", state) == 8 && evaluate(L"$ini_v2_a-1", state) == 2,
        "failed pool candidate falls back to ordinary pool-prefixed global and unspaced variable subtraction");
    check(command_list_globals.size() == globals_before, "successful nested reads do not fabricate extra pool globals");
    const std::wstring empty;
    CommandListScope scope;
    for (const auto& text : {std::wstring(L"$poolmissingv2[0]"), L"$" + pool + L"[($poolindexv2[0])", L"$" + pool + L"[(1]]"}) {
        CommandListExpression bad; const std::wstring expression(text);
        check(!bad.parse(&expression, &empty, &scope), "unknown pool and malformed nested brackets are rejected");
    }
    check(command_list_globals.size() == globals_before, "failed pool probes do not register variables");
}
void test_failed_parser_transactions()
{
    const size_t registered = registered_command_lists.size();
    CommandList pre, post; CommandListScope scope; scope.emplace_front();
    pre.scope = post.scope = &scope;
    const std::wstring ns;
    for (const wchar_t* raw : {L"if missing_v2_operand", L"else if missing_v2_operand", L"if \t"}) {
        const std::wstring line(raw);
        check(!ParseCommandListFlowControl(L"IniV2Failed", &line, &pre, &post, &ns) &&
            pre.commands.empty() && post.commands.empty() && std::distance(scope.begin(), scope.end()) == 1 && registered_command_lists.size() == registered,
            "failed flow parser does not publish branch-list pointers or mutate scope");
    }
    std::wstring bad_rhs = L"missing_v2_operand";
    check(!ParseCommandListVariableAssignment(L"IniV2Failed", L"local $v2_failed", &bad_rhs, nullptr,
        &pre, &pre, &post, &ns) && pre.static_vars.empty() && scope.front().empty() && pre.commands.empty(),
        "failed local initializer rolls back before later parser fallback");
}
}
void test_dx11_ini_parser_v2_regression()
{
    try {
        IniV2Files files;
        HackerContext context(nullptr, nullptr); CommandListState state; state.mHackerContext = &context;
        test_section_and_include(files, state); test_pool_lexical(state); test_failed_parser_transactions();
        std::fputs("PASS: real INI section pointers, include provenance/user precedence and pool/flow parser ordering\n", stderr);
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL INI parser v2: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
