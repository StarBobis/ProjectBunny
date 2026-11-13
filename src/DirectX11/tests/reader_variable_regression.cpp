#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include <cstdio>
#include <cstdlib>

static void variable_reader_check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL variable reader: %s\n", message); std::exit(EXIT_FAILURE); }
}

void test_dx11_reader_variable_regression()
{
    const std::wstring ns = L"reader-variable-regression.ini";
    std::wstring name = L"$reader_variable_regression";
    float initial = 13;
    variable_reader_check(command_list_globals.count(name) == 0, "unique fixture global");
    auto* global = RegisterGlobalVariable(name, &initial, VariableFlags::GLOBAL);
    global->flags = static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::LOCKED);
    CommandListVariable* resolved = nullptr;
    CommandArgumentReader source(L"test", name, L"ReaderVariableRegression", &ns, nullptr);
    variable_reader_check(source.GetVariable(resolved, true) && resolved == global && source.Finished(),
        "locked global can be read without local scope");
    CommandArgumentReader destination(L"test", name, L"ReaderVariableRegression", &ns, nullptr);
    variable_reader_check(!destination.GetVariable(resolved, false) && !destination.Error().empty(),
        "locked global rejected as target");
    std::wstring token;
    variable_reader_check(destination.PeekToken(&token) && token == name,
        "locked target rejection does not consume token");
    variable_reader_check(destination.GetVariable(resolved, true) && destination.Error().empty(),
        "same token remains readable as source after rejected target probe");
    for (const wchar_t* bad : {L"", L"$missing_reader_variable", L"$reader_variable_regression[0]", L"$reader_variable_regression!"}) {
        CommandArgumentReader reader(L"test", bad, L"ReaderVariableRegression", &ns, nullptr);
        variable_reader_check(!reader.GetVariable(resolved, true), "unknown or malformed variable rejected");
    }
    CommandListScope scope;
    scope.emplace_front();
    CommandList pre, post;
    pre.scope = &scope; post.scope = &scope;
    auto parse = [&](const std::wstring& target, const std::wstring& value) {
        std::wstring rhs = value;
        return ParseCommandListVariableAssignment(L"ReaderVariableRegression", target.c_str(), &rhs,
            nullptr, &pre, &pre, &post, &ns);
    };
    variable_reader_check(parse(L"local\t$reader_variable_regression", L"2 + 3"),
        "complete local token with tab declares initialized shadow");
    CommandArgumentReader local(L"test", name, L"ReaderVariableRegression", &ns, &scope);
    variable_reader_check(local.GetVariable(resolved, false) && resolved != global,
        "local shadow takes precedence over locked global");
    HackerContext context(nullptr, nullptr);
    CommandListState state; state.mHackerContext = &context;
    pre.commands.back()->run(&state);
    variable_reader_check(resolved->fval == 5 && global->fval == 13,
        "real assignment initializes local without altering locked global");
    variable_reader_check(parse(L"local $reader_uninitialized_regression", L""), "local declaration without initializer");
    const auto count = scope.front().size();
    const auto commands = pre.commands.size();
    for (const wchar_t* malformed : {L"localfoo $bad", L"local", L"local $bad garbage", L"local $bad[0]", L""}) {
        variable_reader_check(!parse(malformed, L"1") && scope.front().size() == count &&
            pre.commands.size() == commands, "invalid local declaration leaves scope and command list unchanged");
    }
    variable_reader_check(!parse(L"local $reader_failed_initializer", L"1 +") &&
        scope.front().count(L"$reader_failed_initializer") == 0 && scope.front().size() == count,
        "failed initializer rolls back local registration");
    variable_reader_check(parse(L"local $reader_failed_initializer", L"4"),
        "failed declaration can be retried successfully");
    variable_reader_check(!parse(L"$reader_variable_regression trailing", L"9"),
        "assignment target rejects trailing garbage");
    variable_reader_check(!parse(L"$poolreaderregression[0]", L"9"),
        "pool variable target remains delegated to resource-target parser");
    CommandArgumentReader shared_post(L"test", name, L"ReaderVariableRegression", &ns, post.scope);
    variable_reader_check(shared_post.GetVariable(resolved, false) && resolved != global,
        "pre and post share the same local scope");
    command_list_globals.erase(name);
    std::fputs("PASS: source/target variable reader, local declarations and rollback\n", stderr);
}
