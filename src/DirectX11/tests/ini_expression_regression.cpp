#include "CommandList.h"
#include "IniHandler.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Exercise the production parsers without exposing implementation helpers as
// part of the public INI API solely for the regression executable.
extern bool ParseFloatValue(const wchar_t*, const wchar_t*, const std::wstring&, float&, bool, const std::wstring*);
extern bool ParseIntValue(const wchar_t*, const wchar_t*, const std::wstring&, int&, bool, const std::wstring*);
extern bool ParseBoolValue(const wchar_t*, const wchar_t*, const std::wstring&, bool&, bool, const std::wstring*);

static void require_ini_expression(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: INI expression: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void test_dx11_ini_expression_regression()
{
    std::fputs("Testing INI expression variable snapshots\n", stderr);
    const std::wstring ini_namespace = L"dx11_ini_expression_regression.ini";
    const std::wstring variable = L"$ini_expression_regression_value";
    const std::wstring namespaced_name = get_namespaced_var_name_lower(variable, &ini_namespace);
    require_ini_expression(command_list_globals.count(variable) == 0 &&
        command_list_globals.count(namespaced_name) == 0, "fixture names must be unique");
    command_list_globals.emplace(variable,
        CommandListVariable(variable, 99.0f, VariableFlags::GLOBAL));
    auto namespaced = command_list_globals.emplace(namespaced_name,
        CommandListVariable(namespaced_name, 3.0f, VariableFlags::GLOBAL)).first;

    CommandListScope scope;
    CommandListState state;
    const std::wstring expression_text = L"-($ini_expression_regression_value * 2) + 10";
    CommandListExpression expression;
    require_ini_expression(expression.parse(&expression_text, &ini_namespace, &scope), "parse nested variable expression");
    float value = -100.0f;
    require_ini_expression(!expression.static_evaluate(&value) && value == -100.0f,
        "default static evaluation rejects dynamic variables and preserves output");
    require_ini_expression(!expression.static_evaluate(nullptr), "default null-output variable probe is dynamic");
    require_ini_expression(expression.static_evaluate(&value, nullptr, true) && value == 4.0f,
        "explicit static evaluation uses namespaced variable snapshot");
    require_ini_expression(expression.static_evaluate(nullptr, nullptr, true), "recursive null-output snapshot probe");
    namespaced->second.fval = 7.0f;
    require_ini_expression(expression.evaluate(&state) == -4.0f, "runtime observes variable changes");
    require_ini_expression(expression.static_evaluate(&value, nullptr, true) && value == -4.0f,
        "repeated snapshots observe variable changes");
    expression.optimise(nullptr);
    namespaced->second.fval = 4.0f;
    require_ini_expression(expression.evaluate(&state) == 2.0f, "ordinary optimisation does not freeze variables");

    std::fputs("Testing INI expression literals\n", stderr);
    const std::wstring literal_text = L"(2 + 3) * -4";
    CommandListExpression literal;
    require_ini_expression(literal.parse(&literal_text, &ini_namespace, &scope) &&
        literal.static_evaluate(nullptr) && literal.static_evaluate(&value) && value == -20.0f,
        "literal expressions support null-output probing and unary/binary evaluation");

    auto parse_float = [&](const std::wstring& text, float& out) {
        return ParseFloatValue(L"Constants", L"expression_regression", text, out, false, &ini_namespace);
    };
    auto parse_int = [&](const std::wstring& text, int& out) {
        return ParseIntValue(L"Constants", L"expression_regression", text, out, false, &ini_namespace);
    };
    auto parse_bool = [&](const std::wstring& text, bool& out) {
        return ParseBoolValue(L"Constants", L"expression_regression", text, out, false, &ini_namespace);
    };

    std::fputs("Testing INI expression parser snapshots\n", stderr);
    std::wstring long_expression = L"1";
    for (int i = 0; i < 64; ++i)
        long_expression += L" + 1";
    require_ini_expression(parse_float(long_expression, value) && value == 65.0f,
        "configuration expression exceeds former 32-character buffer");
    require_ini_expression(parse_float(L"$INI_EXPRESSION_REGRESSION_VALUE + 2", value) && value == 6.0f,
        "configuration expression is case-insensitive and selects namespace before global");
    namespaced->second.fval = 8.0f;
    require_ini_expression(parse_float(L"$ini_expression_regression_value / 2", value) && value == 4.0f,
        "configuration snapshot reads current earlier constant");

    std::fputs("Testing INI special float conversions\n", stderr);
    require_ini_expression(parse_float(L"inf", value) && std::isinf(value) && value > 0,
        "float positive infinity literal");
    require_ini_expression(parse_float(L"-inf", value) && std::isinf(value) && value < 0,
        "float negative infinity literal");
    require_ini_expression(parse_float(L"nan", value) && std::isnan(value), "float NaN literal");
    require_ini_expression(parse_float(L"1 / 0", value) && std::isinf(value), "float expression infinity");
    require_ini_expression(parse_float(L"0 / 0", value) && std::isnan(value), "float expression NaN");
    require_ini_expression(parse_float(L"1e-99", value) && value == 0.0f && !std::signbit(value),
        "positive underflow stays positive zero");
    require_ini_expression(parse_float(L"-1e-99", value) && value == 0.0f && std::signbit(value),
        "negative underflow preserves signed zero");
    require_ini_expression(parse_float(L"1e99", value) && std::isinf(value) && value > 0,
        "literal overflow stays infinity");

    std::fputs("Testing INI integer conversions\n", stderr);
    int integer = 0;
    require_ini_expression(parse_int(L"2147483647", integer) && integer == INT_MAX, "exact INT_MAX literal");
    require_ini_expression(parse_int(L"-2147483648", integer) && integer == INT_MIN, "exact INT_MIN literal");
    require_ini_expression(parse_int(L"999999999999999999999999999999999", integer) && integer == INT_MAX,
        "integer literal overflow saturates positive");
    require_ini_expression(parse_int(L"-999999999999999999999999999999999", integer) && integer == INT_MIN,
        "integer literal overflow saturates negative");
    require_ini_expression(parse_int(L"2147483647 + 0", integer) && integer == INT_MAX,
        "float-rounded INT_MAX expression saturates safely");
    require_ini_expression(parse_int(L"-2147483648 - 1000", integer) && integer == INT_MIN,
        "negative out-of-range expression saturates");
    require_ini_expression(parse_int(L"inf", integer) && integer == INT_MAX, "infinity converts to INT_MAX");
    require_ini_expression(parse_int(L"-inf", integer) && integer == INT_MIN, "negative infinity converts to INT_MIN");
    require_ini_expression(parse_int(L"nan", integer) && integer == 0, "NaN converts to integer zero");
    require_ini_expression(parse_int(L"-7 / 2", integer) && integer == -3, "integer expression truncates toward zero");

    std::fputs("Testing INI bool conversions\n", stderr);
    bool boolean = false;
    for (const wchar_t* text : {L"1", L"TrUe", L"YES", L"on", L"inf", L"-inf", L"-2", L"1 + 2"})
        require_ini_expression(parse_bool(text, boolean) && boolean, "true aliases and nonzero expressions");
    for (const wchar_t* text : {L"0", L"FaLsE", L"NO", L"off", L"nan", L"2 - 2"})
        require_ini_expression(parse_bool(text, boolean) && !boolean, "false aliases, NaN and zero expressions");

    std::fputs("Testing INI invalid expressions\n", stderr);
    for (const wchar_t* invalid : {L"", L" ", L"()", L"( )", L"1 + ()", L"1 +", L"$ini_expression_regression_missing", L"1 garbage"}) {
        std::fprintf(stderr, "Testing invalid float: '%ls'\n", invalid);
        require_ini_expression(!parse_float(invalid, value), "malformed float expression rejected");
        std::fprintf(stderr, "Testing invalid int: '%ls'\n", invalid);
        require_ini_expression(!parse_int(invalid, integer), "malformed integer expression rejected");
        std::fprintf(stderr, "Testing invalid bool: '%ls'\n", invalid);
        require_ini_expression(!parse_bool(invalid, boolean), "malformed boolean expression rejected");
    }

    // Erase only this fixture's variables so other regression fixtures retain
    // ownership of their globals and persistent-variable pointers.
    command_list_globals.erase(namespaced_name);
    command_list_globals.erase(variable);
}
