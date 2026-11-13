#include "CommandList.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

static void require_utf8_token(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL UTF namespace/tokenizer: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class Utf8NativeContext
{
public:
    HMODULE runtime = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device1> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> native;
    std::unique_ptr<HackerContext> context;

    Utf8NativeContext()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_utf8_token(runtime != nullptr, "load actual runtime for command execution");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_utf8_token(create != nullptr, "resolve native device creation");
        Microsoft::WRL::ComPtr<ID3D11Device> base;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        require_utf8_token(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create actual command context");
        require_utf8_token(SUCCEEDED(base.As(&device)) && SUCCEEDED(immediate.As(&native)), "query D3D11.1 context");
        context = std::make_unique<HackerContext>(device.Get(), native.Get());
    }

    ~Utf8NativeContext()
    {
        context.reset();
        native.Reset();
        device.Reset();
        FreeLibrary(runtime);
    }
};

class Utf8TokenFixture
{
public:
    const std::wstring unicode_resource = L"resourceutf8_regression_\u6a21\u578b-test";
    const std::wstring trailing_resource = L"resourceutf8_regression_tail-";
    const std::wstring ini_namespace = L"\u547d\u540d \u7a7a\u95f4.ini";
    const std::wstring local_name = L"$utf8_regression_value";
    std::wstring full_name;

    Utf8TokenFixture()
    {
        full_name = get_namespaced_var_name_lower(local_name, &ini_namespace);
        require_utf8_token(customResources.count(unicode_resource) == 0 &&
            customResources.count(trailing_resource) == 0 && command_list_globals.count(full_name) == 0,
            "fixture owns unique registry names");
        customResources[unicode_resource].name = unicode_resource;
        customResources[unicode_resource].buf_size = 128;
        customResources[trailing_resource].name = trailing_resource;
        customResources[trailing_resource].buf_size = 64;
        command_list_globals.emplace(full_name, CommandListVariable(full_name, 3.0f, VariableFlags::GLOBAL));
    }

    ~Utf8TokenFixture()
    {
        customResources.erase(unicode_resource);
        customResources.erase(trailing_resource);
        command_list_globals.erase(full_name);
    }
};

void test_dx11_utf8_tokenizer_regression()
{
    std::fputs("Testing Unicode resource tokens and namespace assignments\n", stderr);
    Utf8TokenFixture fixture;
    Utf8NativeContext execution;
    const std::wstring empty_namespace;
    CommandListScope scope;
    scope.emplace_front();
    CommandListState state;
    state.mHackerContext = execution.context.get();

    const std::wstring unicode_getter = fixture.unicode_resource + L"->size + 2";
    CommandListExpression expression;
    require_utf8_token(expression.parse(&unicode_getter, &empty_namespace, &scope) &&
        expression.evaluate(&state) == 130.0f, "Unicode and hyphen resource identifier keeps member getter and arithmetic");
    const std::wstring trailing_getter = fixture.trailing_resource + L"->size - 1";
    CommandListExpression trailing;
    require_utf8_token(trailing.parse(&trailing_getter, &empty_namespace, &scope) &&
        trailing.evaluate(&state) == 63.0f, "resource trailing hyphen remains part of its registered name");

    for (const wchar_t* valid : {L"ib-1", L"vb0->stride", L"vs-cb0->size", L"ib->offset"}) {
        const std::wstring text(valid);
        CommandListExpression parsed;
        require_utf8_token(parsed.parse(&text, &empty_namespace, &scope), "slot subtraction and member getter precedence");
    }
    const std::wstring spaced_tail = fixture.trailing_resource + L" -1";
    CommandListExpression tail_subtract;
    require_utf8_token(tail_subtract.parse(&spaced_tail, &empty_namespace, &scope),
        "registered trailing-hyphen resource allows whitespace-disambiguated subtraction");
    for (const std::wstring& invalid : {std::wstring(L"vs-cb0-1"), fixture.trailing_resource + L"-1",
        fixture.unicode_resource + L"->size)", fixture.unicode_resource + L"->size([)]",
        std::wstring(L"vb0->hashregion((0,16)"), std::wstring(L"$utf8_regression_missing")}) {
        CommandListExpression parsed;
        require_utf8_token(!parsed.parse(&invalid, &empty_namespace, &scope),
            "historically ambiguous slot/resource tokens, malformed brackets and unknown variable fail safely");
    }

    ResourceCopyTarget target;
    require_utf8_token(target.ParseTarget((fixture.unicode_resource + L"->size").c_str(), true,
        &empty_namespace, &scope) && target.GetResourceSize(&state) == 128.0f,
        "direct real resource target parser accepts Unicode getter");
    require_utf8_token(!target.ParseTarget(L"vb0->hashregion([)]", true, &empty_namespace, &scope) &&
        target.ParseTarget(L"vb0->stride", true, &empty_namespace, &scope) && target.slot == 0,
        "malformed target does not leave stale member state on reuse");

    CommandList commands, post;
    commands.scope = &scope;
    post.scope = &scope;
    std::wstring assigned = L"7 + 2";
    require_utf8_token(ParseCommandListVariableAssignment(L"CommandListUtf8Regression", fixture.full_name.c_str(),
        &assigned, nullptr, &commands, &commands, &post, &empty_namespace) && commands.commands.size() == 1,
        "LHS consumes complete Unicode namespace including spaces");
    commands.commands.back()->run(&state);
    require_utf8_token(command_list_globals.at(fixture.full_name).fval == 9.0f,
        "actual assignment writes exactly the registered namespaced variable");
    const std::wstring rhs = fixture.full_name + L" + 1";
    CommandListExpression namespaced_rhs;
    require_utf8_token(namespaced_rhs.parse(&rhs, &empty_namespace, &scope) && namespaced_rhs.evaluate(&state) == 10.0f,
        "explicit Unicode namespace with spaces can be read as expression operand");

    CommandListVariable* found = nullptr;
    CommandArgumentReader reader(L"utf8_assignment", fixture.full_name, L"CommandListUtf8Regression",
        &empty_namespace, nullptr);
    require_utf8_token(reader.GetVariable(found, false, CommandArgumentReader::PeekMode::Argument) && reader.Finished() &&
        found == &command_list_globals.at(fixture.full_name), "Argument reader without local scope resolves exact full global name");
    const std::wstring resource_text = fixture.unicode_resource + L"->size";
    CommandArgumentReader resource_reader(L"utf8_source", resource_text, L"CommandListUtf8Regression",
        &empty_namespace, &scope);
    ResourceCopyTarget read_target;
    require_utf8_token(resource_reader.GetTarget(&read_target, true, CommandArgumentReader::PeekMode::Argument) &&
        resource_reader.Finished(), "reader source validation uses Unicode-capable target scanner");

    const size_t before = commands.commands.size();
    const std::wstring unknown = L"$\\\u4e0d\u5b58\u5728 \u547d\u540d.ini\\utf8_regression_missing";
    require_utf8_token(!ParseCommandListVariableAssignment(L"CommandListUtf8Regression", unknown.c_str(), &assigned,
        nullptr, &commands, &commands, &post, &empty_namespace) && commands.commands.size() == before,
        "unknown namespaced assignment fails without null dereference or queued command");
    const size_t local_scope_size = scope.front().size();
    const auto local_count = std::distance(commands.static_vars.begin(), commands.static_vars.end());
    std::wstring bad_initializer = L"1 +";
    require_utf8_token(!ParseCommandListVariableAssignment(L"CommandListUtf8Regression", L"local $utf8_regression_temporary",
        &bad_initializer, nullptr, &commands, &commands, &post, &empty_namespace) &&
        scope.front().size() == local_scope_size &&
        std::distance(commands.static_vars.begin(), commands.static_vars.end()) == local_count &&
        commands.commands.size() == before, "failed local initializer rolls back declaration and queued commands");
    std::wstring good_initializer = L"11";
    require_utf8_token(ParseCommandListVariableAssignment(L"CommandListUtf8Regression", L"local $utf8_regression_temporary",
        &good_initializer, nullptr, &commands, &commands, &post, &empty_namespace),
        "same local variable can be declared after failed initialization");
    commands.commands.back()->run(&state);
    require_utf8_token(scope.front().at(L"$utf8_regression_temporary")->fval == 11.0f,
        "successful local declaration evaluates after rollback");
    commands.scope = nullptr;
    post.scope = nullptr;
}
