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
class ParserFiles {
public:
    HINSTANCE module = migoto_handle;
    LogVerbosity log_level = gLogVerbosity;
    bool log_debug = gLogDebug;
    bool warnings = G->gShowWarnings;
    std::wstring user = G->user_config;
    int dirty = G->user_config_dirty;
    bool initialized = G->gConfigInitialized;
    bool automatic = G->clear_unknown_settings, clear = G->clear_saved_persist_vars;
    std::filesystem::path config, settings;
    ParserFiles()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate parser fixture executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "parser fixture must stay under build");
        config = directory / L"d3dx.ini";
        settings = directory / (L"parser_compaction_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing parser settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing parser configuration");
        CloseHandle(file); migoto_handle = nullptr;
    }
    ~ParserFiles()
    {
        ClearKeyBindings();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        registered_command_lists.clear();
        explicitCommandListSections.clear(); customResourcePools.clear(); customResources.clear();
        persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; gLogVerbosity = log_level; gLogDebug = log_debug;
        G->gShowWarnings = warnings; G->user_config = user;
        G->user_config_dirty = dirty; G->gConfigInitialized = initialized;
        G->clear_unknown_settings = automatic; G->clear_saved_persist_vars = clear;
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants)
    {
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Hunting]\nhunting=0\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants;
        file.close(); check(file.good(), "write owned parser config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};
void constants_tests()
{
    ParserFiles files;
    files.load("GLOBAL $parser_compact=1\n$parser_compact=$parser_compact+2\n"
        "global $parser_after=3\n$parser_compact=$parser_compact*3\n"
        "GLOBAL LOCKED $parser_locked=7\nglobal $parser_after=99\n"
        "global $9bad=4\nglobal $parser_failed=unrecognized_parser_initializer\n");
    check(command_list_globals.size() == 3 && command_list_globals.at(L"$parser_after").fval == 3 &&
        command_list_globals.at(L"$parser_locked").fval == 7 &&
        (command_list_globals.at(L"$parser_locked").flags & VariableFlags::LOCKED),
        "uppercase flags, failed/redeclared globals and declaration order remain valid");
    check(G->constants_command_list.commands.size() == 2, "stable compaction retains exactly the two ordinary assignments");
    HackerContext context(nullptr, nullptr);
    CommandListState state; state.mHackerContext = &context;
    for (const auto& command : G->constants_command_list.commands) command->run(&state);
    check(command_list_globals.at(L"$parser_compact").fval == 9, "retained ordinary lines execute in original order");
    files.load("");
    check(command_list_globals.empty() && G->constants_command_list.commands.empty(), "empty Constants remains empty");
    files.load("x=1\ny=2\n");
    check(command_list_globals.empty() && G->constants_command_list.commands.size() == 2, "all-ordinary Constants retains runtime lines");
    files.load("global $parser_only=5\n");
    check(command_list_globals.at(L"$parser_only").fval == 5 && G->constants_command_list.commands.empty(), "all-global compaction leaves no runtime declarations");
}
class ExpressionFixture {
public:
    const std::wstring name = L"$parser_optimization_value";
    const std::wstring ns = L"M\u00d3D\\Folder With Spaces\\UPPER.ini";
    std::wstring qualified;
    std::string locale;
    const size_t registered = registered_command_lists.size();
    ExpressionFixture()
    {
        const char* old = std::setlocale(LC_CTYPE, nullptr); locale = old ? old : "C";
        check(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr, "enable production Unicode namespace case folding");
        qualified = get_namespaced_var_name_lower(L"$PARSER_OPTIMIZATION_VALUE", &ns);
        check(!command_list_globals.count(name) && !command_list_globals.count(qualified), "expression globals must be unique");
        command_list_globals.emplace(name, CommandListVariable(name, 4, VariableFlags::GLOBAL));
        command_list_globals.emplace(qualified, CommandListVariable(qualified, 8, VariableFlags::GLOBAL));
    }
    ~ExpressionFixture()
    {
        registered_command_lists.resize(registered);
        command_list_globals.erase(name); command_list_globals.erase(qualified);
        std::setlocale(LC_CTYPE, locale.c_str());
    }
};
float evaluate(const wchar_t* text)
{
    CommandListExpression expression; const std::wstring value(text), ns;
    CommandListScope scope;
    check(expression.parse(&value, &ns, &scope), "parse actual optimized expression");
    CommandListState state;
    return expression.evaluate(&state);
}
void expression_tests()
{
    ExpressionFixture fixture;
    check(evaluate(L"abs(-sqrt(9))+2**(3+1)") == 19, "nested functions and mixed operator children");
    check(evaluate(L"!0 + ~0") == 0 && evaluate(L"8//3 + 7%3") == 3, "unary and multiplication masks");
    check(evaluate(L"(1<<3) + (16>>2)") == 12, "shift mask and grouped children");
    check(evaluate(L"(2<3) && (4>=4) || 0") == 1, "relational and logical masks");
    check(evaluate(L"(7&3) ^ (2|1)") == 0 && evaluate(L"1==1") == 1 &&
        evaluate(L"1!=2") == 1 && evaluate(L"0===0") == 1 && evaluate(L"0!==-0") == 1,
        "bitwise, equality and binary identity masks");
    std::wstring long_text = L"1";
    for (int i = 0; i < 256; ++i) long_text += L"+1";
    check(evaluate(long_text.c_str()) == 257, "long expression direct traversal");
    const std::wstring empty_namespace;
    CommandListScope scope;
    CommandListExpression repeated;
    for (const wchar_t* invalid : {L"", L"()", L"(())", L"1+", L"sqrt()", L"1 **", L"(1+2"}) {
        const std::wstring good = L"5", bad(invalid);
        check(repeated.parse(&good, &empty_namespace, &scope), "initial valid expression before failed reparse");
        check(!repeated.parse(&bad, &empty_namespace, &scope) && !repeated.evaluatable &&
            !repeated.static_evaluate(nullptr), "failed/empty reparse cannot reuse old expression root");
    }
    CommandListVariable* target = nullptr;
    check(parse_command_list_var_name(L"$PARSER_OPTIMIZATION_VALUE", &fixture.ns, &target) &&
        target == &command_list_globals.at(fixture.qualified), "ASCII basename fast path retains namespaced priority");
    const std::wstring explicit_name = L"$\\" + fixture.ns + L"\\PARSER_OPTIMIZATION_VALUE";
    check(parse_command_list_var_name(explicit_name, nullptr, &target) && target == &command_list_globals.at(fixture.qualified),
        "explicit Unicode namespace retains case folding and null namespace fallback");
    check(get_namespaced_var_name_lower(L"$PARSER_OPTIMIZATION_VALUE", &fixture.ns) == fixture.qualified &&
        fixture.qualified.find(L"m\u00f3d") != std::wstring::npos && ascii_tolower(L'Z') == L'z',
        "public helper accepts uppercase names and folds Unicode namespace across translation units");
    target = &command_list_globals.at(fixture.name);
    check(!parse_command_list_var_name(L"", nullptr, &target) && !target, "empty variable lookup safely clears output");
    IfCommand branch(L"ParserOptimizationIf");
    branch.pre_finalised = branch.post_finalised = true;
    auto refill = [&]() {
        branch.true_commands_pre->runtime_populated = branch.true_commands_post->runtime_populated = true;
        branch.false_commands_pre->runtime_populated = branch.false_commands_post->runtime_populated = true;
    };
    const std::wstring one = L"1", zero = L"0";
    check(branch.expression.parse(&one, &empty_namespace, &scope), "parse initial If constant");
    refill(); check(!branch.noop(false, false, false) && branch.is_static && branch.static_val == 1, "If caches true and retains runtime callback branch");
    check(!branch.noop(true, false, false) && branch.static_evaluated, "If cache reused for post phase");
    check(branch.expression.parse(&zero, &empty_namespace, &scope), "reparse If constant");
    refill(); check(!branch.noop(false, false, false) && branch.is_static && branch.static_val == 0 &&
        !branch.true_commands_pre->runtime_populated, "If reparse invalidates cached choice and prunes new opposite branch");
    check(branch.expression.parse(&fixture.name, &empty_namespace, &scope), "parse dynamic If variable");
    refill(); branch.noop(false, false, false); branch.noop(true, false, false);
    check(!branch.is_static && branch.true_commands_pre->runtime_populated && branch.false_commands_pre->runtime_populated,
        "dynamic If preserves both callback branches");
    const std::wstring random = L"random(1)";
    check(branch.expression.parse(&random, &empty_namespace, &scope), "parse random If");
    const uint32_t samples_before = GetExpressionRandomSampleCount();
    branch.noop(false, false, false); branch.noop(true, false, false);
    check(!branch.is_static && GetExpressionRandomSampleCount() == samples_before,
        "If cache probes leave random dynamic and do not consume samples");
}
}
void test_dx11_parser_optimization_regression()
{
    try { constants_tests(); expression_tests(); std::fputs("PASS: Constants compaction, masked/direct expression passes and If/lookup cache boundaries\n", stderr); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL parser optimization: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
