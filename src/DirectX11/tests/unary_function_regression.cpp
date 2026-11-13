#include "CommandList.h"
#include "ExpressionFunctions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <vector>

static void require_unary(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: unary expression: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct UnaryFunctionCase
{
    const wchar_t* expression;
    float expected;
};

static void parse_unary(CommandListExpression& expression, const std::wstring& text,
    const std::wstring& ini_namespace, CommandListScope& scope)
{
    if (!expression.parse(&text, &ini_namespace, &scope)) {
        std::fprintf(stderr, "FAIL: unary expression did not parse: %ls\n", text.c_str());
        std::exit(EXIT_FAILURE);
    }
}

static bool near_unary(float actual, float expected)
{
    return std::isfinite(actual) &&
        std::fabs(actual - expected) <= 0.00001f * (1.0f + std::fabs(expected));
}

static void check_random_range(float actual, float bound)
{
    require_unary(std::isfinite(actual), "finite random bound produces finite result");
    if (bound > 0.0f)
        require_unary(actual >= 0.0f && actual < bound, "positive random bound has exclusive upper endpoint");
    else if (bound < 0.0f)
        require_unary(actual <= 0.0f && actual > bound, "negative random bound has exclusive lower endpoint");
    else
        require_unary(actual == 0.0f, "zero random bound returns zero");
}

void test_dx11_unary_function_regression()
{
    std::fputs("Testing unary expression functions\n", stderr);
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListState state;
    const UnaryFunctionCase cases[] = {
        {L"countbits(7)", 3.0f},
        {L"sin(0)", 0.0f},
        {L"cos(0)", 1.0f},
        {L"tan(0)", 0.0f},
        {L"asin(1)", 1.5707963268f},
        {L"acos(1)", 0.0f},
        {L"atan(1)", 0.7853981634f},
        {L"abs(-3.25)", 3.25f},
        {L"sign(-3)", -1.0f},
        {L"sign(0)", 0.0f},
        {L"sign(3)", 1.0f},
        {L"ceil(-1.25)", -1.0f},
        {L"floor(-1.25)", -2.0f},
        {L"trunc(-1.75)", -1.0f},
        {L"round(0.5)", 1.0f},
        {L"round(-0.5)", -1.0f},
        {L"frac(-1.25)", 0.75f},
        {L"sqrt(9)", 3.0f},
        {L"rsqrt(4)", 0.5f},
        {L"exp(0)", 1.0f},
        {L"exp2(3)", 8.0f},
        {L"log(1)", 0.0f},
        {L"log2(8)", 3.0f},
        {L"saturate(-2)", 0.0f},
        {L"saturate(0.25)", 0.25f},
        {L"saturate(2)", 1.0f},
        {L"abs(-sqrt(9)) + exp2(log2(8))", 11.0f},
        {L"2 * sin(0) + 3 * cos(0)", 3.0f},
        {L"countbits(0)", 0.0f},
        {L"countbits(1)", 1.0f},
        {L"countbits(7.9)", 3.0f},
        {L"countbits(16777215)", 24.0f},
        {L"countbits(4294967040)", 24.0f},
        {L"countbits(4294967296)", 0.0f},
        {L"countbits(-1)", 32.0f},
        {L"countbits(-2.75)", 31.0f},
        {L"countbits(-4294967296)", 0.0f},
        {L"countbits(inf)", 0.0f},
        {L"countbits(-inf)", 0.0f},
        {L"countbits(nan)", 0.0f},
    };
    for (const auto& test : cases) {
        CommandListExpression expression;
        parse_unary(expression, test.expression, ini_namespace, scope);
        float value = 0.0f;
        require_unary(expression.static_evaluate(&value) && near_unary(value, test.expected),
            "pure function statically evaluates to expected result");
        require_unary(expression.static_evaluate(nullptr), "pure function null-output static probe");
        require_unary(near_unary(expression.evaluate(&state), test.expected), "runtime pure function result");
        expression.optimise(nullptr);
        require_unary(near_unary(expression.evaluate(&state), test.expected), "pure function result after optimisation");
    }

    std::fputs("Testing unary expression IEEE boundaries\n", stderr);
    for (const wchar_t* text : {L"sqrt(-1)", L"log(-1)", L"asin(2)", L"acos(-2)", L"sin(inf)"}) {
        CommandListExpression expression;
        parse_unary(expression, text, ini_namespace, scope);
        require_unary(std::isnan(expression.evaluate(&state)), "math domain error preserves NaN");
    }
    for (const wchar_t* text : {L"rsqrt(0)", L"exp(1000)", L"exp2(1000)"}) {
        CommandListExpression expression;
        parse_unary(expression, text, ini_namespace, scope);
        const float value = expression.evaluate(&state);
        require_unary(std::isinf(value) && value > 0.0f, "positive mathematical overflow preserves infinity");
    }
    CommandListExpression log_zero;
    parse_unary(log_zero, L"log(0)", ini_namespace, scope);
    const float negative_infinity = log_zero.evaluate(&state);
    require_unary(std::isinf(negative_infinity) && negative_infinity < 0.0f, "log zero preserves negative infinity");

    for (const wchar_t* text : {L"sqrt()", L"sin( )", L"sin(1,2)", L"sqrt(9", L"abs(-sqrt())", L"noise(1)"}) {
        const std::wstring invalid(text);
        CommandListExpression expression;
        require_unary(!expression.parse(&invalid, &ini_namespace, &scope),
            "malformed functions and unimplemented noise are rejected");
    }

    std::fputs("Testing random expression snapshots and bounds\n", stderr);
    const std::wstring variable = L"$unary_function_regression_bound";
    require_unary(command_list_globals.count(variable) == 0, "random fixture owns unique global variable");
    auto bound = command_list_globals.emplace(variable,
        CommandListVariable(variable, 5.0f, VariableFlags::GLOBAL)).first;
    CommandListExpression random;
    parse_unary(random, L"random($unary_function_regression_bound)", ini_namespace, scope);
    float value = -123.0f;
    require_unary(!random.static_evaluate(&value) && value == -123.0f,
        "random with variable bound is not implicitly snapshotted");
    require_unary(!random.static_evaluate(nullptr), "default random static probe rejects dynamic result");
    require_unary(random.static_evaluate(nullptr, nullptr, true), "explicit random snapshot supports null-output probing");
    require_unary(random.static_evaluate(&value, nullptr, true), "explicit random snapshot produces a value");
    check_random_range(value, bound->second.fval);

    // A literal argument must not cause the optimiser to freeze random().
    CommandListExpression literal_random;
    parse_unary(literal_random, L"random(5)", ini_namespace, scope);
    require_unary(!literal_random.static_evaluate(nullptr), "literal random is not a default static expression");
    literal_random.optimise(nullptr);
    require_unary(!literal_random.static_evaluate(nullptr), "optimisation preserves random runtime evaluation");
    require_unary(literal_random.static_evaluate(&value, nullptr, true), "optimised random supports explicit snapshot");
    check_random_range(value, 5.0f);
    random.optimise(nullptr);
    require_unary(!random.static_evaluate(nullptr), "optimisation preserves variable random runtime evaluation");

    const float smallest = std::numeric_limits<float>::denorm_min();
    const float largest = (std::numeric_limits<float>::max)();
    for (float maximum : {0.0f, -0.0f, 5.0f, -5.0f, smallest, -smallest, largest, -largest}) {
        bound->second.fval = maximum;
        for (int i = 0; i < 256; ++i) {
            check_random_range(random.evaluate(&state), maximum);
            require_unary(random.static_evaluate(&value, nullptr, true), "explicit random snapshot across finite bounds");
            check_random_range(value, maximum);
        }
    }
    // Uniqueness is intentionally not asserted: 24-bit outputs can collide.
    // Probe safety is covered above; proving that a null-output probe does not
    // advance a private sequence needs an observable sequence/seed contract.
    command_list_globals.erase(variable);

    CommandListExpression nested_random;
    parse_unary(nested_random, L"abs(random(-5))", ini_namespace, scope);
    require_unary(!nested_random.static_evaluate(nullptr), "outer pure operator preserves random dynamism");
    nested_random.optimise(nullptr);
    require_unary(!nested_random.static_evaluate(nullptr), "nested random is not folded during optimisation");
    require_unary(nested_random.static_evaluate(nullptr, nullptr, true), "nested random supports safe snapshot probe");

    // Exercise the production seed publication and call counter concurrently.
    // Workers read a coherent atomic snapshot, without reading mutable Globals.
    std::vector<std::thread> workers;
    workers.emplace_back([]() {
        for (uint32_t i = 0; i < 4000; ++i)
            SeedExpressionRandom(i, i ^ 0xa5a5a5a5u);
    });
    for (unsigned i = 0; i < 4; ++i) {
        workers.emplace_back([]() {
            for (unsigned j = 0; j < 1000; ++j) {
                check_random_range(RandomExpressionValue(5), 5);
                check_random_range(RandomExpressionValue(-5), -5);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    require_unary(std::isnan(RandomExpressionValue(std::numeric_limits<float>::infinity())) &&
        std::isnan(RandomExpressionValue(std::numeric_limits<float>::quiet_NaN())),
        "nonfinite random bounds return NaN safely");
}
