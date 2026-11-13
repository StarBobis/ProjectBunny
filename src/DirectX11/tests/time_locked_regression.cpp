#include "CommandList.h"
#include "globals.h"
#include "IniHandler.h"
#include "Override.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void require_time_locked(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL time/locked: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void test_dx11_time_locked_regression()
{
    const auto saved_frame = G->frame_no;
    const auto saved_time = G->gTime;
    const auto saved_dirty = G->user_config_dirty;
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListState state;
    CommandListExpression expression;
    const std::wstring text = L"frame_number + time";
    require_time_locked(expression.parse(&text, &ini_namespace, &scope), "parse time/frame");
    G->frame_no = 5; G->gTime = 2;
    float value = -1;
    require_time_locked(!expression.static_evaluate(&value) && value == -1, "default remains dynamic");
    require_time_locked(expression.static_evaluate(nullptr, nullptr, true) &&
        expression.static_evaluate(&value, nullptr, true) && value == 7, "explicit snapshot and null probe");
    expression.optimise(nullptr);
    G->frame_no = 12; G->gTime = 3;
    require_time_locked(expression.evaluate(&state) == 15, "optimise does not freeze time/frame");
    CommandListExpression time_minus, frame_minus, function_minus;
    const std::wstring time_text = L"time-1", frame_text = L"frame_number-1", function_text = L"sin(0)-1";
    require_time_locked(time_minus.parse(&time_text, &ini_namespace, &scope) &&
        frame_minus.parse(&frame_text, &ini_namespace, &scope) &&
        function_minus.parse(&function_text, &ini_namespace, &scope) &&
        time_minus.evaluate(&state) == 2 && frame_minus.evaluate(&state) == 11 &&
        function_minus.evaluate(&state) == -1, "keyword and function subtraction need no surrounding whitespace");
    G->frame_no = UINT_MAX; G->gTime = 0;
    require_time_locked(expression.evaluate(&state) == static_cast<float>(UINT_MAX), "frame precision follows float model");
    G->frame_no = 0;
    require_time_locked(expression.evaluate(&state) == 0, "frame wraps to zero");
    std::wstring name = L"$time_locked_regression";
    float initial = 7;
    auto* variable = RegisterGlobalVariable(name, &initial,
        static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::LOCKED));
    require_time_locked(variable && variable->fval == 7, "locked declaration initialized");
    CommandList commands;
    commands.scope = &scope;
    std::wstring rhs = L"99", raw = name + L" = 99";
    require_time_locked(!ParseCommandListVariableAssignment(L"TestLocked", name.c_str(), &rhs, &raw,
        &commands, &commands, &commands, &ini_namespace) && commands.commands.empty(), "locked assignment rejected before queue");
    std::wstring store = name + L",vs-cb0,0";
    require_time_locked(!ParseCommandListGeneralCommands(L"TestLocked", L"store", &store,
        &commands, &commands, &commands, &ini_namespace), "locked store rejected before GPU access");
    VariableAssignment assignment;
    assignment.var = variable;
    require_time_locked(assignment.expression.parse(&rhs, &ini_namespace, &scope), "parse runtime assignment");
    assignment.run(&state);
    StoreCommand direct_store;
    direct_store.var = variable;
    direct_store.run(&state);
    require_time_locked(variable->fval == 7, "runtime locked guards precede context access");
    OverrideTransition transition;
    OverrideParams params;
    OverrideVars targets;
    targets[variable] = 22;
    transition.ScheduleTransition(nullptr, &params, &targets, 0, TransitionType::LINEAR);
    require_time_locked(transition.vars.empty(), "locked transition not scheduled");
    variable->flags = VariableFlags::GLOBAL;
    transition.ScheduleTransition(nullptr, &params, &targets, 0, TransitionType::LINEAR);
    transition.UpdateTransitions(nullptr);
    require_time_locked(variable->fval == 22, "unlocked transition preserved");
    transition.ScheduleTransition(nullptr, &params, &targets, 1000, TransitionType::LINEAR);
    variable->flags = static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::LOCKED);
    transition.UpdateTransitions(nullptr);
    require_time_locked(transition.vars.empty() && variable->fval == 22, "locked pending transition removed");
    command_list_globals.erase(name);
    G->frame_no = saved_frame; G->gTime = saved_time; G->user_config_dirty = saved_dirty;
    std::fputs("PASS: time/frame snapshots and locked mutation guards\n", stderr);
}
