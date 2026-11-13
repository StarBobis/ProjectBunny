#include "CommandList.h"
#include "IniHandler.h"
#include "globals.h"

#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern bool ParseBinaryLiterals(const std::wstring&, size_t, uint64_t&, size_t&);
extern bool ParseFloatValue(const wchar_t*, const wchar_t*, const std::wstring&, float&, bool, const std::wstring*);
extern bool ParseIntValue(const wchar_t*, const wchar_t*, const std::wstring&, int&, bool, const std::wstring*);
extern bool ParseBoolValue(const wchar_t*, const wchar_t*, const std::wstring&, bool&, bool, const std::wstring*);

namespace {

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class TokenizerFixture {
public:
    const std::wstring ini_namespace = L"mods\\\u6d4b\u8bd5-folder.ini";
    const std::wstring variable = L"$tokenizer_regression_a";
    const std::wstring pool_name = L"pooltokenizerregression";
    const std::wstring resource_name = L"resourcetokenizerregression";
    std::vector<std::wstring> globals;
    std::vector<std::wstring> resources;
    bool owns_pool = false;

    ~TokenizerFixture()
    {
        if (owns_pool)
            customResourcePools.erase(pool_name);
        for (const auto& name : resources)
            customResources.erase(name);
        // Pool variables live in command_list_globals; release the pool first.
        for (const auto& name : globals)
            command_list_globals.erase(name);
    }

    void add_variable(const std::wstring& name, float value)
    {
        check(command_list_globals.count(name) == 0, "tokenizer global fixture must be unique");
        command_list_globals.emplace(name, CommandListVariable(name, value, VariableFlags::GLOBAL));
        globals.push_back(name);
    }

    CustomResource& add_resource(const std::wstring& name)
    {
        check(customResources.count(name) == 0, "tokenizer resource fixture must be unique");
        auto& resource = customResources[name];
        resources.push_back(name);
        resource.name = name;
        resource.override_stride = 12;
        resource.buf_size = 64;
        return resource;
    }

    void initialize()
    {
        add_variable(variable, 8.0f);
        add_variable(get_namespaced_var_name_lower(variable, &ini_namespace), 13.0f);
        add_resource(resource_name);
        std::wstring namespaced_resource;
        check(get_namespaced_section_name_lower(&resource_name, &ini_namespace, &namespaced_resource),
            "form actual namespaced resource identifier");
        add_resource(namespaced_resource).override_stride = 20;
        auto& resource_template = add_resource(pool_name + L"_template");
        check(customResourcePools.count(pool_name) == 0, "tokenizer pool fixture must be unique");
        auto& pool = customResourcePools[pool_name];
        owns_pool = true;
        pool.name = pool_name;
        pool.resource_template = &resource_template;
        resource_template.pool = &pool;
        resource_template.pool_index = -1;
        pool.index_type = PoolIndexType::RING;
        pool.lazy_initialization = true;
        pool.variable_template = std::make_unique<CommandListVariable>(pool_name + L"_template", 0, VariableFlags::NONE);
        pool.Initialize(4);
        for (size_t i = 0; i < 4; ++i) {
            const std::wstring name = L"$" + pool_name + L"[" + std::to_wstring(i) + L"]";
            check(command_list_globals.count(name) == 0, "pool variable fixture must be unique");
            globals.push_back(name);
            pool.GetVariable(static_cast<float>(i), false, true, true)->fval = i == 3 ? 17.0f : static_cast<float>(i + 1);
        }
    }
};

bool parse_expression(const std::wstring& text, const std::wstring& ini_namespace)
{
    CommandListScope scope;
    CommandListExpression expression;
    return expression.parse(&text, &ini_namespace, &scope);
}

float evaluate(const std::wstring& text, const std::wstring& ini_namespace)
{
    CommandListScope scope;
    CommandListExpression expression;
    if (!expression.parse(&text, &ini_namespace, &scope)) {
        std::fwprintf(stderr, L"Tokenizer expression rejected: %ls\n", text.c_str());
        throw std::runtime_error("parse actual tokenizer expression");
    }
    CommandListState state;
    return expression.evaluate(&state);
}

void test_binary_scanner()
{
    uint64_t value = 123;
    size_t length = 999;
    check(ParseBinaryLiterals(L"0", 0, value, length) && value == 0 && length == 1,
        "binary zero scanner");
    check(ParseBinaryLiterals(L"prefix10101", 6, value, length) && value == 21 && length == 5,
        "binary scanner honors starting position");
    check(ParseBinaryLiterals(std::wstring(64, L'1'), 0, value, length) &&
        value == UINT64_MAX && length == 64, "64 binary ones preserve exact uint64 value");
    check(ParseBinaryLiterals(L"1" + std::wstring(63, L'0'), 0, value, length) &&
        value == (uint64_t(1) << 63) && length == 64, "binary high bit avoids signed overflow");
    value = 123;
    check(!ParseBinaryLiterals(std::wstring(65, L'1'), 0, value, length) && value == 123,
        "65-bit binary scanner rejects overflow without publishing partial value");
    check(!ParseBinaryLiterals(std::wstring(65, L'0'), 0, value, length),
        "binary scanner rejects overlong zero representation");
    for (const auto& invalid : {std::wstring(), std::wstring(L"2"), std::wstring(L"junk")}) {
        length = 999;
        check(!ParseBinaryLiterals(invalid, 0, value, length) && length == 0,
            "binary scanner requires at least one digit and resets length");
    }
    check(!ParseBinaryLiterals(L"1", 1, value, length) && length == 0 &&
        !ParseBinaryLiterals(L"1", 10, value, length), "binary scanner safely rejects end/past-end start");
    check(ParseBinaryLiterals(L"111tail", 0, value, length) && value == 7 && length == 3,
        "binary scanner reports prefix length for caller grammar validation");
}

void test_binary_expressions_and_config(const std::wstring& ini_namespace)
{
    check(evaluate(L"0b0", ini_namespace) == 0 && evaluate(L"0b1", ini_namespace) == 1,
        "binary literals tokenize zero and one");
    check(evaluate(L"0b101+0b11*0b10", ini_namespace) == 11 &&
        evaluate(L"-(0b101+0b11)", ini_namespace) == -8, "binary precedence and unary negation");
    check(evaluate(L"0x10+0b10", ini_namespace) == 18, "hexadecimal and binary share expression grammar");
    const std::wstring maximum = L"0b" + std::wstring(64, L'1');
    check(evaluate(maximum, ini_namespace) == static_cast<float>(UINT64_MAX),
        "binary expression uses established float precision after exact scanning");

    float floating = 0;
    int integer = 0;
    bool boolean = false;
    check(ParseFloatValue(L"Constants", L"tokenizer_regression", L"0b10101", floating, false,
        &ini_namespace) && floating == 21, "configuration float parser accepts binary literal");
    check(ParseIntValue(L"Constants", L"tokenizer_regression", L"0b101+0b11", integer, false,
        &ini_namespace) && integer == 8, "configuration integer parser accepts binary expression");
    check(ParseBoolValue(L"Constants", L"tokenizer_regression", L"0b0", boolean, false,
        &ini_namespace) && !boolean && ParseBoolValue(L"Constants", L"tokenizer_regression", L"0b10",
        boolean, false, &ini_namespace) && boolean, "configuration bool parser accepts binary zero/nonzero");
    check(ParseFloatValue(L"Constants", L"tokenizer_regression", maximum, floating, false,
        &ini_namespace) && floating == static_cast<float>(UINT64_MAX) &&
        ParseIntValue(L"Constants", L"tokenizer_regression", maximum, integer, false, &ini_namespace) && integer == INT_MAX,
        "64-bit binary configuration keeps float precision and safe integer saturation");

    const std::vector<std::wstring> invalid = {L"0b", L"0b2", L"0b102", L"0b1junk", L"0b1 0b1",
        L"0b" + std::wstring(65, L'1'), L"0b" + std::wstring(65, L'0')};
    for (const auto& text : invalid) {
        check(!parse_expression(text, ini_namespace), "invalid complete binary expression rejected");
        check(!ParseFloatValue(L"Constants", L"tokenizer_regression", text, floating, false, &ini_namespace) &&
            !ParseIntValue(L"Constants", L"tokenizer_regression", text, integer, false, &ini_namespace) &&
            !ParseBoolValue(L"Constants", L"tokenizer_regression", text, boolean, false, &ini_namespace),
            "configuration parsers reject binary missing digits, overflow and trailing garbage");
    }
}

void test_resource_and_namespace_tokens(TokenizerFixture& fixture)
{
    const std::wstring empty_namespace;
    check(evaluate(fixture.variable + L"-1", empty_namespace) == 7,
        "unspaced variable subtraction separates identifier and minus");
    check(evaluate(fixture.variable + L"-1", fixture.ini_namespace) == 12,
        "implicit UTF namespace shadows global variable");
    const std::wstring explicit_name = get_namespaced_var_name_lower(fixture.variable, &fixture.ini_namespace);
    check(evaluate(explicit_name + L"-1", empty_namespace) == 12,
        "explicit UTF and path namespace survives hyphen, period and subtraction");
    check(evaluate(L"$pooltokenizerregression[$pooltokenizerregression[$pooltokenizerregression[0]]]+0b1",
        empty_namespace) == 4, "recursive pool indices remain one target token");
    check(evaluate(L"$pooltokenizerregression[-1]+1", empty_namespace) == 18,
        "unary negative ring index remains inside pool token");
    check(evaluate(fixture.resource_name + L"->stride+1", empty_namespace) == 13 &&
        evaluate(fixture.resource_name + L"->stride+1", fixture.ini_namespace) == 21,
        "resource member scanner resolves implicit resource namespace");
    std::wstring namespaced_resource;
    check(get_namespaced_section_name_lower(&fixture.resource_name, &fixture.ini_namespace, &namespaced_resource),
        "form explicit UTF resource identifier");
    check(evaluate(namespaced_resource + L"->size+1", empty_namespace) == 65,
        "explicit UTF resource namespace remains intact around member operator");
    check(evaluate(L"vs-cb0->stride+1", empty_namespace) == 17,
        "shader resource slot hyphen is preserved while arithmetic plus separates");

    CommandListScope scope;
    CommandListState state;
    ResourceCopyTarget nested;
    check(nested.ParseTarget(L"resourcetokenizerregression->hashregion(($tokenizer_regression_a+4)*2,(0b1000+0b1000))",
        true, &empty_namespace, &scope) && nested.member_args[0].GetValue(&state) == 24 &&
        nested.member_args[1].GetValue(&state) == 16, "real member parser retains nested arithmetic and binary arguments");
    check(parse_expression(L"resourcetokenizerregression->hashregion(($tokenizer_regression_a+4)*2,(0b1000+0b1000))+1",
        empty_namespace), "tokenizer consumes nested member parentheses before outer arithmetic");
    check(nested.ParseTarget(L"vb0", true, &empty_namespace, &scope) &&
        nested.evaluation_mode == ResourceCopyTargetEvaluationMode::RESOURCE &&
        !nested.member_args[0].expression && nested.member_args[0].constant_string.empty(),
        "target reparsing discards previous member expression metadata");
    ResourceCopyTarget shader_slot;
    check(shader_slot.ParseTarget(L"vs-cb0->stride", true, &empty_namespace, &scope) &&
        shader_slot.type == ResourceCopyTargetType::CONSTANT_BUFFER && shader_slot.slot == 0,
        "real resource target parser preserves shader slot metadata");

    // Member arguments are consumed before ParseTarget's outer whitespace check;
    // pool variables take an early path. Their internal spaces already work.
    check(parse_expression(L"resourcetokenizerregression->hashregion((4 + 4)*2,16)", empty_namespace),
        "member token scanner preserves whitespace inside nested arguments");
    check(evaluate(L"$pooltokenizerregression[1 + 1]", empty_namespace) == 3,
        "pool token scanner preserves arithmetic spaces inside index");
}

void run_tokenizer_regression()
{
    TokenizerFixture fixture;
    fixture.initialize();
    const std::wstring empty_namespace;
    test_binary_scanner();
    test_binary_expressions_and_config(empty_namespace);
    test_resource_and_namespace_tokens(fixture);
    check(evaluate(L".5+.25", empty_namespace) == .75f && evaluate(L"1e-2+1", empty_namespace) == 1.01f,
        "leading-dot decimal and signed exponent literal tokenize correctly");
    check(std::isinf(evaluate(L"inf", empty_namespace)) &&
        evaluate(L"-inf", empty_namespace) < 0 && std::isnan(evaluate(L"nan", empty_namespace)),
        "special float fallback remains supported");
    check(evaluate(L"1e-99", empty_namespace) == 0 && !std::signbit(evaluate(L"1e-99", empty_namespace)) &&
        std::signbit(evaluate(L"-1e-99", empty_namespace)), "tokenizer preserves underflow signed zero");
    for (const wchar_t* invalid : {L"", L" ", L"()", L"1+()", L"(1+2", L"1+2)", L"1+", L"1 2",
        L"unknown_tokenizer_regression", L"$tokenizer_regression_missing", L"$\\unterminated_namespace",
        L"$pooltokenizerregression[0", L"$pooltokenizerregression[(1]]", L"$pooltokenizerregression[1)]",
        L"resourcetokenizerregression->hashregion((1+2),)", L"resourcetokenizerregression->hashregion(1,2,3)",
        L"resourcetokenizerregression->hashregion((1+2],2)", L"infjunk", L"nan garbage"}) {
        check(!parse_expression(invalid, empty_namespace), "malformed expression rejects safely with complete consumption");
    }
}
} // namespace

void test_dx11_tokenizer_regression()
{
    try {
        run_tokenizer_regression();
        std::fputs("PASS: production tokenizer, binary literals, namespaces and nested targets\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: tokenizer regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
