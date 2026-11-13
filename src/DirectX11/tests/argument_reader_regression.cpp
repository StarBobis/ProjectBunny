#include "CommandList.h"
#include "IniHandler.h"
#include "globals.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class ArgumentFixture {
public:
    const std::wstring ini_namespace;
    const std::wstring variable = L"$argument_reader_regression";
    std::vector<std::wstring> pools;
    std::vector<std::wstring> resources;

    ~ArgumentFixture()
    {
        for (const auto& name : pools)
            customResourcePools.erase(name);
        for (const auto& name : resources)
            customResources.erase(name);
        command_list_globals.erase(variable);
    }

    CustomResource& add_resource(const std::wstring& name)
    {
        check(customResources.count(name) == 0, "argument resource fixture must be unique");
        auto& resource = customResources[name];
        resource.name = name;
        resource.buf_size = 64;
        resource.override_stride = 16;
        resources.push_back(name);
        return resource;
    }

    CustomResourcePool& add_pool(const std::wstring& name, size_t size)
    {
        check(customResourcePools.count(name) == 0, "argument pool fixture must be unique");
        auto& resource_template = add_resource(name + L"_template");
        auto& pool = customResourcePools[name];
        pools.push_back(name);
        pool.name = name;
        pool.resource_template = &resource_template;
        resource_template.pool = &pool;
        resource_template.pool_index = -1;
        pool.variable_template = std::make_unique<CommandListVariable>(name + L"_template", 0, VariableFlags::NONE);
        pool.index_type = PoolIndexType::RING;
        pool.lazy_initialization = true;
        pool.Initialize(size);
        // GetResource will create these production registry entries lazily.
        for (size_t i = 0; i < size; ++i) {
            const std::wstring entry = name + L"_" + std::to_wstring(i);
            check(customResources.count(entry) == 0, "argument slot fixture must be unique");
            resources.push_back(entry);
        }
        return pool;
    }

    void initialize()
    {
        check(command_list_globals.count(variable) == 0, "argument variable fixture must be unique");
        command_list_globals.emplace(variable, CommandListVariable(variable, 1, VariableFlags::GLOBAL));
        add_resource(L"resourceargumentreadersource");
        add_resource(L"resource\\argument path.ini\\source");
        add_resource(L"resourceargumentreaderdestination");
        auto& base = add_pool(L"poolargumentreaderbase", 2);
        auto& proxy = add_pool(L"poolargumentreaderproxy", 1);
        proxy.SetSourcePool(&base);
        auto& chain = add_pool(L"poolargumentreaderchain", 1);
        chain.SetSourcePool(&proxy);
        add_pool(L"poolargumentreaderdestination", 1);
        add_pool(L"poolargumentreaderempty", 0);
    }
};

bool parse_copy(CommandList& commands, const wchar_t* lhs, const wchar_t* rhs)
{
    CommandListScope scope;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    const bool result = ParseCommandListResourceCopyTargetDirective(L"CommandListArgumentReaderRegression",
        lhs, &value, &commands, &ini_namespace);
    commands.scope = nullptr;
    return result;
}

float evaluate(const wchar_t* source)
{
    const std::wstring text = source;
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    check(expression.parse(&text, &ini_namespace, &scope), "parse actual pool member expression");
    CommandListState state;
    return expression.evaluate(&state);
}

void test_reader_api(ArgumentFixture& fixture)
{
    CommandListScope scope;
    const std::wstring command = L"alpha \t beta, (1 + (2 * 3))";
    CommandArgumentReader reader(L"regression", command, L"CommandListArgumentReaderRegression",
        &fixture.ini_namespace, &scope);
    std::wstring token;
    check(!reader.Finished() && reader.Error().empty(), "Finished probe does not create diagnostics");
    check(reader.PeekToken(&token) && token == L"alpha", "token peek stops at whitespace");
    check(reader.PeekToken(&token, CommandArgumentReader::PeekMode::Argument) && token == L"alpha \t beta",
        "argument mode rescans cached token to comma boundary");
    check(reader.ConsumeToken() && reader.ConsumeSeparator(SeparatorMode::Comma),
        "consume argument and its comma separator");
    std::unique_ptr<CommandListExpression> expression;
    CommandListState state;
    check(reader.GetExpression(&expression) && expression->evaluate(&state) == 7 && reader.Finished() &&
        reader.Error().empty(), "reader parses nested expression with whitespace and clean EOF");

    const std::wstring invalid_mode_input = L"alpha (]";
    CommandArgumentReader modes(L"regression", invalid_mode_input, L"CommandListArgumentReaderRegression",
        &fixture.ini_namespace, &scope);
    check(modes.PeekToken(&token) && token == L"alpha", "cache token before mode failure");
    check(!modes.PeekToken(&token, CommandArgumentReader::PeekMode::Argument) && !modes.Error().empty() &&
        !modes.PeekToken(&token, CommandArgumentReader::PeekMode::Argument),
        "repeated failed mode rescan cannot return stale cached token");
    check(modes.PeekToken(&token) && token == L"alpha" && modes.Error().empty(),
        "failed rescan leaves position unchanged and successful rescan clears trial error");

    const std::wstring source_input = L"resourceargumentreadersource";
    CommandArgumentReader trials(L"regression", source_input, L"CommandListArgumentReaderRegression",
        &fixture.ini_namespace, &scope);
    ResourceCopyOptions option = ResourceCopyOptions::INVALID;
    check(!trials.GetEnum(ResourceCopyOptionNames, ResourceCopyOptions::INVALID, &option),
        "non-option enum trial fails without consumption");
    ResourceCopyTarget source;
    check(trials.GetTarget(&source, true) && source.type == ResourceCopyTargetType::CUSTOM_RESOURCE &&
        trials.Finished() && trials.Error().empty(), "target read recovers after unsuccessful enum trial");

    const std::wstring scalar_input = fixture.variable + L", 0b101";
    CommandArgumentReader scalars(L"regression", scalar_input, L"CommandListArgumentReaderRegression",
        &fixture.ini_namespace, &scope);
    CommandListVariable* variable = nullptr;
    float value = 0;
    check(scalars.GetVariable(variable) && variable == &command_list_globals.at(fixture.variable) &&
        scalars.ConsumeSeparator(SeparatorMode::Comma) && scalars.GetFloat(&value) && value == 5 &&
        scalars.Finished(), "reader variable and binary float use production parsers");

    for (const wchar_t* input : {L"([)]", L"alpha)", L"[1+2", L"(1+2]", L"[1]]"}) {
        const std::wstring malformed = input;
        CommandArgumentReader invalid(L"regression", malformed, L"CommandListArgumentReaderRegression",
            &fixture.ini_namespace, &scope);
        check(!invalid.PeekToken(&token) && !invalid.Error().empty() &&
            invalid.ErrorPosition() <= malformed.size() && !invalid.ConsumeToken(),
            "reader rejects cross, extra and unterminated brackets without consumable peek");
    }
}

void test_source_options()
{
    for (const wchar_t* text : {L"copy resourceargumentreadersource", L"resourceargumentreadersource copy",
        L"\tcopy\tresourceargumentreadersource\tno_view_cache\t", L"no_view_cache resourceargumentreadersource copy"}) {
        CommandList commands;
        check(parse_copy(commands, L"resourceargumentreaderdestination", text) && commands.commands.size() == 1,
            "actual directive accepts prefix, suffix and mixed options with tabs");
        auto* operation = dynamic_cast<ResourceCopyOperation*>(commands.commands[0].get());
        check(operation && (operation->options & ResourceCopyOptions::COPY) &&
            operation->src.type == ResourceCopyTargetType::CUSTOM_RESOURCE,
            "parsed copy options and source reach real operation metadata");
        if (std::wstring(text).find(L"no_view_cache") != std::wstring::npos)
            check(!!(operation->options & ResourceCopyOptions::NO_VIEW_CACHE), "trailing modifier survives source parse");
    }
    for (const wchar_t* text : {L"ref resourceargumentreadersource", L"resourceargumentreadersource reference",
        L"unless_null resourceargumentreadersource ref no_view_cache"}) {
        CommandList commands;
        check(parse_copy(commands, L"resourceargumentreaderdestination", text), "actual directive accepts reference aliases and modifiers");
        auto* operation = dynamic_cast<ResourceCopyOperation*>(commands.commands[0].get());
        check(operation && (operation->options & ResourceCopyOptions::REFERENCE), "reference option survives actual parser");
    }

    CommandList dynamic_source;
    check(parse_copy(dynamic_source, L"resourceargumentreaderdestination",
        L"copy poolargumentreaderbase[$argument_reader_regression + (2 - 2)] no_view_cache"),
        "option parser consumes dynamic pool index with nested spaces");
    auto* dynamic_operation = dynamic_cast<ResourceCopyOperation*>(dynamic_source.commands[0].get());
    CommandListState state;
    check(dynamic_operation && dynamic_operation->src.GetCustomResource(&state)->pool_index == 1,
        "parsed dynamic source retains actual index evaluation");

    CommandList namespaced_source;
    check(parse_copy(namespaced_source, L"resourceargumentreaderdestination",
        L"copy resource\\argument path.ini\\source no_view_cache"),
        "source reader preserves whitespace inside an explicit namespace");
    auto* namespaced_operation = dynamic_cast<ResourceCopyOperation*>(namespaced_source.commands[0].get());
    check(namespaced_operation && namespaced_operation->src.GetCustomResource(&state) ==
        &customResources.at(L"resource\\argument path.ini\\source"),
        "namespaced source resolves exact registered resource");

    for (const wchar_t* invalid : {L"", L"copy", L"ref no_view_cache", L"unknown",
        L"unknown resourceargumentreadersource", L"copy unknown resourceargumentreadersource",
        L"resourceargumentreadersource unknown", L"resourceargumentreadersource resourceargumentreadersource",
        L"ref resourceargumentreadersource resourceargumentreaderdestination", L"resourceargumentreadersource,copy",
        L"copy poolargumentreaderbase[1", L"copy poolargumentreaderbase[(1]]",
        L"copy poolargumentreaderbase[1)]", L"copy poolargumentreaderbase[1]]",
        L"copy resourceargumentreadersource->hashregion((1+2],4)"}) {
        CommandList rejected;
        check(!parse_copy(rejected, L"resourceargumentreaderdestination", invalid) && rejected.commands.empty(),
            "invalid source/options must reject without publishing a partial operation");
    }
}

void test_member_reader()
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListState state;
    ResourceCopyTarget target;
    check(target.ParseTarget(L"resourceargumentreadersource->hashregion((($argument_reader_regression + 4)*2),(8+8))",
        true, &ini_namespace, &scope) && target.member_args[0].GetValue(&state) == 10 &&
        target.member_args[1].GetValue(&state) == 16, "member reader parses nested arithmetic as whole comma arguments");
    check(target.ParseTarget(L"vb0->elementformat(texcoord,($argument_reader_regression + 1))", false,
        &ini_namespace, &scope) && target.member_args[0].GetString() == L"texcoord" &&
        target.member_args[1].GetValue(&state) == 2, "member reader retains semantic string and expression argument types");
    check(target.ParseTarget(L"resourceargumentreadersource->size()", true, &ini_namespace, &scope) &&
        !target.member_args[0].expression && target.member_args[0].constant_string.empty(),
        "zero-argument member and repeated parse discard old argument metadata");
    for (const wchar_t* invalid : {L"resourceargumentreadersource->hashregion", L"resourceargumentreadersource->hashregion()",
        L"resourceargumentreadersource->hashregion(1)", L"resourceargumentreadersource->hashregion(1,2,3)",
        L"resourceargumentreadersource->hashregion(,2)", L"resourceargumentreadersource->hashregion(1,)",
        L"resourceargumentreadersource->size(1)", L"resourceargumentreadersource->hashregion((1+2],4)",
        L"resourceargumentreadersource->hashregion(1,2))", L"vb0->elementformat(,0)"}) {
        check(!target.ParseTarget(invalid, true, &ini_namespace, &scope), "member reader rejects missing, extra, empty and malformed arguments");
    }
    check(target.ParseTarget(L"vb1", true, &ini_namespace, &scope) && target.slot == 1 &&
        target.evaluation_mode == ResourceCopyTargetEvaluationMode::RESOURCE && !target.member_args[0].expression,
        "successful parse after failures has fresh target state");
}

void test_pool_members_and_flags()
{
    ResourceCopyTarget missing;
    const std::wstring missing_name = L"poolargumentreaderempty[0]", empty_namespace;
    CommandListScope scope;
    CommandListState state;
    check(missing.ParseTarget(missing_name.c_str(), true, &empty_namespace, &scope),
        "parse actual missing custom resource from an empty pool");
    ID3D11View* missing_view = reinterpret_cast<ID3D11View*>(1);
    check(!missing.GetResource(&state, &missing_view, nullptr, nullptr, nullptr, nullptr) && !missing_view &&
        missing.GetResourceStride(&state) == -2 && missing.GetResourceSize(&state) == -2,
        "missing custom resource rejects before expire/device access and preserves not-found properties");
    auto& base = customResourcePools.at(L"poolargumentreaderbase");
    auto& chain = customResourcePools.at(L"poolargumentreaderchain");
    auto* existing = base.GetResource(0, false, true, true);
    ResourceCopyTarget without_index;
    without_index.type = ResourceCopyTargetType::VERTEX_BUFFER;
    without_index.custom_resource_pool = &base;
    check(!without_index.GetCustomResource(nullptr) && !without_index.GetPoolVariable(nullptr),
        "non-pool target without static object or index cannot substitute a pool template");
    without_index.type = ResourceCopyTargetType::POOL;
    check(without_index.GetCustomResource(nullptr) == base.resource_template &&
        without_index.GetPoolVariable(nullptr) == base.variable_template.get(),
        "actual pool target still resolves its resource and variable templates");
    without_index.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
    without_index.SetCustomResource(existing);
    check(without_index.GetCustomResource(nullptr) == existing, "static object lookup remains valid without a dynamic index");
    check(evaluate(L"poolargumentreaderbase->size") == 2 && evaluate(L"#poolargumentreaderbase") == 2 &&
        evaluate(L"poolargumentreaderchain->size") == 2 && evaluate(L"poolargumentreaderempty->size") == 0,
        "pool Size aliases preserve physical size through proxies and empty pools");
    check(evaluate(L"poolargumentreaderbase[0]->index") == 0 && evaluate(L"#poolargumentreaderbase[0]") == 0 &&
        evaluate(L"poolargumentreaderchain[-2]->index") == 0,
        "pool Index aliases resolve actual indexed element through proxy chain");

    auto& destination = customResourcePools.at(L"poolargumentreaderdestination");
    destination.resource_template->bind_flags = static_cast<D3D11_BIND_FLAG>(D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
    destination.resource_template->misc_flags = static_cast<D3D11_RESOURCE_MISC_FLAG>(D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS |
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    check(customResources.count(L"poolargumentreaderbase_1") == 0, "second source element is genuinely uninitialized before reference parse");
    CommandList commands;
    check(parse_copy(commands, L"poolargumentreaderdestination", L"ref poolargumentreaderchain"),
        "actual pool reference parser accepts proxy-chain source");
    const UINT required_bind = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
    const UINT required_misc = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    check((base.resource_template->bind_flags & required_bind) == required_bind &&
        (base.resource_template->misc_flags & required_misc) == required_misc &&
        (existing->bind_flags & required_bind) == required_bind && (existing->misc_flags & required_misc) == required_misc,
        "pool reference parsing propagates destination flags to ultimate source template and existing resources");
    check(customResources.count(L"poolargumentreaderbase_1") == 0, "flag propagation preserves lazy resource initialization");
    auto* lazy = chain.GetResource(1, false, true, true);
    check(lazy && (lazy->bind_flags & required_bind) == required_bind && (lazy->misc_flags & required_misc) == required_misc,
        "later lazy resource receives propagated source-template flags");
}

void run_argument_reader_regression()
{
    ArgumentFixture fixture;
    fixture.initialize();
    test_reader_api(fixture);
    test_pool_members_and_flags();
    test_source_options();
    test_member_reader();
}
} // namespace

void test_dx11_argument_reader_regression()
{
    try {
        run_argument_reader_regression();
        std::fputs("PASS: production argument reader, source options, member arguments and pool flags\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: argument reader regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
