#include "Logging.h"
#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "ExpressionFunctions.h"
#include "globals.h"
#include "Input.h"
#include "profiling.h"
#include "slot_range_test_support.h"
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
void executor_tests()
{
    ParserFiles files;
    files.load("global $compile_gate=0\nglobal $compile_other=0\nglobal $compile_result=0\n"
        "[CommandListCompiledExecutor]\n$compile_gate=1\n$compile_other=2\n"
        "if $compile_gate==1 && $compile_other==2\n$compile_result=$compile_result+1\nendif\n"
        "$compile_gate=0\n$compile_other=2\n"
        "if $compile_gate==1 && $compile_other==2\n$compile_result=$compile_result+100\nendif\n"
        "$compile_gate=1\n"
        "if $compile_gate==1 && $compile_other==2\n$compile_result=$compile_result+1\nendif\n");
    optimise_command_lists(nullptr);
    auto& list = explicitCommandListSections.at(L"commandlistcompiledexecutor").command_list;
    std::shared_ptr<const ScalarIfGuard> shared;
    unsigned guards = 0;
    for (const auto& command : list.commands) {
        if (auto* branch = dynamic_cast<IfCommand*>(command.get())) {
            if (!guards) shared = branch->scalar_guard;
            check(shared && shared == branch->scalar_guard, "identical comparisons share one typed guard");
            ++guards;
        }
    }
    check(guards == 3 && !list.literal_optimized_commands.empty() &&
        list.literal_optimized_commands.size() < list.commands.size(),
        "typed executor batches consecutive literal writes and retains the original sequence");
    HackerContext context(nullptr, nullptr);
    CommandListState state;
    state.mHackerContext = &context;
    RunLinkedCommandList run(&list);
    const bool old = G->mod_runtime_optimizations.exchange(true);
    run.run(&state);
    auto& result = command_list_globals.at(L"$compile_result").fval;
    check(result == 2, "shared condition rechecks input values after a literal assignment batch changes them");
    result = 0;
    G->mod_runtime_optimizations = false;
    run.run(&state);
    G->mod_runtime_optimizations = old;
    check(result == 2, "typed executor matches the original path with optimizations disabled");
}

void early_draw_gate_tests()
{
    const auto saved_params = G->iniParams;
    const int saved_reserved = G->iniParamsReserved;
    ParserFiles files;
    files.load("global $gate=1\nglobal $result=0\nglobal $after=0\n"
        "[Rendering]\ndraw_has_override_slots=ib vb[0:2] ps-t0\n"
        "[TextureOverrideEarlyDrawGateOtherResource]\nhash=ee9afd38\nfilter_index=1\n"
        "[CommandListEarlyDrawGate]\nif $gate\nlocal $kind=draw_type\n"
        "if draw_has_override\n$result=$result+100\npost $result=$result+1000\nendif\n"
        "x1=$kind\n$result=$result*10+1\npost ps-t7=null\npost $after=$after+1\nendif\n"
        "[CommandListUnsafeEarlyDrawGate]\nif $gate\n$result=$result+1\n"
        "if draw_has_override\n$result=$result+100\nendif\nendif\n");
    optimise_command_lists(nullptr);
    auto& section = explicitCommandListSections.at(L"commandlistearlydrawgate");
    check(section.command_list.early_draw_gate && section.post_command_list.early_draw_gate,
        "draw gate: compile pre and post from the production parser");
    check(!explicitCommandListSections.at(L"commandlistunsafeearlydrawgate").command_list.early_draw_gate,
        "draw gate: do not move across writes to ordinary variables");
    check(G->draw_gate_early_reject && !G->profile_draw_gate,
        "draw gate: default on, opt-in detailed profiling");

    dx11_slot_test::BatchDevice gpu;
    check(SUCCEEDED(gpu.device->CreateIniParamResources()) && G->iniParams.size() > 1,
        "draw gate: allocate IniParams through the normal device path");
    auto texture = dx11_slot_test::texture(gpu, 0xff33cc99);
    auto view = dx11_slot_test::srv(gpu, texture.Get());
    auto& state = gpu.state;
    auto& result = command_list_globals.at(L"$result").fval;
    auto& after = command_list_globals.at(L"$after").fval;
    const auto old_mode = Profiling::mode.exchange(Profiling::Mode::SUMMARY);
    const bool old_master = G->mod_runtime_optimizations.exchange(true);
    G->profile_draw_gate = true;
    RunLinkedCommandList run_pre(&section.command_list), run_post(&section.post_command_list);
    const auto initial_reads = Profiling::draw_gate_ib.count;
    const auto initial_fast_misses = Profiling::draw_gate_pre_entry_miss.hits;
    const auto initial_post_fast_misses = Profiling::draw_gate_post_entry_miss.hits;
    const auto run = [&](bool fast, bool hit) {
        G->draw_gate_early_reject = fast;
        result = after = 0;
        DrawCallInfo draw;
        draw.type = DrawCall::DrawIndexed;
        if (hit)
            draw.override_candidate = DrawOverrideCandidate::Possible;
        state.call_info = &draw;
        dx11_slot_test::set_slot(gpu, L'p', 7, view.Get());
        state.post = false;
        run_pre.run(&state);
        check(result == (hit ? 1001.0f : 1.0f) && G->iniParams[1].x == 2.0f && state.extra_indent == 0,
            "draw gate: preserve local DRAW_TYPE, hit commands and IniParams writes in order");
        state.post = true;
        run_post.run(&state);
        check(result == (hit ? 2001.0f : 1.0f) && after == 1.0f && !dx11_slot_test::get_slot(gpu, L'p', 7) &&
            state.extra_indent == 0 && state.command_list_depth == 0,
            "draw gate: preserve post order, slot writes and branch depth");
        state.call_info = nullptr;
    };
    run(true, false);
    check(Profiling::draw_gate_pre_entry_miss.hits == initial_fast_misses + 1 &&
        Profiling::draw_gate_post_entry_miss.hits == initial_post_fast_misses + 1 &&
        Profiling::draw_gate_ib.count == initial_reads + 1, "draw gate: native read once and fast route in both phases");
    run(false, false);
    check(Profiling::draw_gate_pre_entry_miss.hits == initial_fast_misses + 1 &&
        Profiling::draw_gate_post_entry_miss.hits == initial_post_fast_misses + 1 &&
        Profiling::draw_gate_ib.count == initial_reads + 2, "draw gate: independent opt-out uses legacy route");
    run(true, true);
    run(false, true);
    G->mod_runtime_optimizations = false;
    run(true, false);
    check(Profiling::draw_gate_pre_entry_miss.hits == initial_fast_misses + 1 &&
        Profiling::draw_gate_post_entry_miss.hits == initial_post_fast_misses + 1 &&
        Profiling::draw_gate_ib.count == initial_reads + 3, "draw gate: master switch still uses legacy route");
    G->mod_runtime_optimizations = old_master;
    G->draw_gate_early_reject = true;
    G->profile_draw_gate = false;
    Profiling::mode = old_mode;
    Profiling::clear();
    G->iniParams = saved_params;
    G->iniParamsReserved = saved_reserved;
}

void shader_regex_empty_link_tests()
{
    ParserFiles files;
    files.load("global $dirty=0\nglobal $safe=1\nglobal $present=0\nglobal $sim=0\nglobal $runs=0\n"
        "[CommandListGuardedRegexFixture]\n"
        "if $dirty || !$safe\n$runs=$runs+1\nendif\n"
        "if $present && $sim && draw_type % 2 == 0 && vs-cb1 === null\n$runs=$runs+10\nendif\n"
        "[CommandListUnsafeRegexFixture]\nif $present && random(10)\n$runs=$runs+100\nendif\n"
        "[CommandListElseRegexFixture]\nif $present\n$runs=$runs+100\nelse\n$runs=$runs+1000\nendif\n"
        "[CommandListWritesRegexFixture]\n$runs=$runs+10000\n"
        "if $present\n$runs=$runs+100\nendif\n");
    optimise_command_lists(nullptr);
    CommandList caller;
    const std::wstring synthetic_line = L"run = linked command list";
    const auto link_fixture = [&](const wchar_t* name) {
        auto& target = explicitCommandListSections.at(name).command_list;
        return LinkCommandLists(&caller, &target, &synthetic_line);
    };
    const auto guarded = link_fixture(L"commandlistguardedregexfixture");
    const auto unsafe = link_fixture(L"commandlistunsaferegexfixture");
    const auto has_else = link_fixture(L"commandlistelseregexfixture");
    const auto has_write = link_fixture(L"commandlistwritesregexfixture");
    check(guarded->link->shader_regex_empty_guard != nullptr && !unsafe->link->shader_regex_empty_guard &&
        !has_else->link->shader_regex_empty_guard && !has_write->link->shader_regex_empty_guard,
        "shader regex link: every false branch must be empty and every tested condition safe");
    check(G->shader_regex_skip_empty_links && !G->profile_shader_regex_links,
        "shader regex link: enabled by default with diagnostics off");

    dx11_slot_test::BatchDevice gpu;
    DrawCallInfo draw;
    draw.type = DrawCall::DrawIndexed;
    gpu.state.call_info = &draw;
    auto& runs = command_list_globals.at(L"$runs").fval;
    auto& dirty = command_list_globals.at(L"$dirty").fval;
    auto& present = command_list_globals.at(L"$present").fval;
    auto& sim = command_list_globals.at(L"$sim").fval;
    const auto old_mode = Profiling::mode.exchange(Profiling::Mode::SUMMARY);
    const auto master = G->mod_runtime_optimizations.exchange(true);
    const auto short_circuit = G->expression_short_circuit.exchange(true);
    Profiling::clear();

    auto* target = guarded->link;
    const auto before = target->executions;
    const auto cb_queries = Profiling::expression_cb_query_overhead.count;
    guarded->run(&gpu.state);
    check(target->executions == before && runs == 0 && Profiling::expression_cb_query_overhead.count == cb_queries,
        "shader regex link: both inactive conditions bypass list and native queries");

    G->shader_regex_skip_empty_links = false;
    guarded->run(&gpu.state);
    check(target->executions == before + 1 && runs == 0 && Profiling::expression_cb_query_overhead.count == cb_queries,
        "shader regex link: independent switch restores dispatch with the same result");
    G->shader_regex_skip_empty_links = true;
    dirty = 1;
    guarded->run(&gpu.state);
    check(target->executions == before + 2 && runs == 1,
        "shader regex link: possible first branch is executed at its original entry");
    dirty = 0;
    present = sim = 1;
    guarded->run(&gpu.state);
    check(target->executions == before + 3 && runs == 11 && Profiling::expression_cb_query_overhead.count == cb_queries + 1,
        "shader regex link: possible second branch retains the native query and body");

    present = 0;
    const auto random_count = GetExpressionRandomSampleCount();
    unsafe->run(&gpu.state);
    has_else->run(&gpu.state);
    has_write->run(&gpu.state);
    check(runs == 11011 && GetExpressionRandomSampleCount() == random_count + 1,
        "shader regex link: eager side effects, else and top-level writes are preserved");
    G->mod_runtime_optimizations = false;
    guarded->run(&gpu.state);
    check(target->executions == before + 4 && runs == 11011,
        "shader regex link: master switch also restores the original dispatch");
    G->mod_runtime_optimizations = true;
    target->DiscardExecutionPlans();
    guarded->run(&gpu.state);
    check(target->executions == before + 5 && !target->shader_regex_empty_guard && runs == 11011,
        "shader regex link: invalidated plan cannot suppress an existing link");
    G->mod_runtime_optimizations = master;
    G->expression_short_circuit = short_circuit;
    Profiling::mode = old_mode;
    Profiling::clear();
}
}
void test_dx11_parser_optimization_regression()
{
    try { constants_tests(); expression_tests(); executor_tests(); early_draw_gate_tests(); shader_regex_empty_link_tests(); std::fputs("PASS: Constants compaction, typed command execution, Draw gate and ShaderRegex empty links\n", stderr); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL parser optimization: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
