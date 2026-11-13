#include "CommandList.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void legacy_source_check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL legacy source: %s\n", message); std::exit(EXIT_FAILURE); }
}

class LegacySourceFixture {
public:
    std::vector<std::wstring> names;
    ~LegacySourceFixture() { for (const auto& name : names) customResources.erase(name); }
    CustomResource& add(const std::wstring& name)
    {
        legacy_source_check(!customResources.count(name), "unique legacy resource fixture");
        names.push_back(name);
        return customResources[name];
    }
};

static bool parse_legacy_source(CommandList& commands, const std::wstring& value)
{
    const std::wstring ns;
    std::wstring source = value;
    return ParseCommandListResourceCopyTargetDirective(L"LegacySourceRegression",
        L"resourcelegacy_source_destination", &source, &commands, &ns);
}

void test_dx11_legacy_source_regression()
{
    LegacySourceFixture fixture;
    fixture.add(L"resourcelegacy_source_destination");
    const std::wstring ns;
    CommandListScope scope;
    for (const wchar_t* spelling : {L"resourcelegacy+name", L"resourcelegacy*name", L"resourcelegacy|name",
        L"resourcelegacy>name", L"resourcelegacy=name"}) {
        const std::wstring name = spelling;
        auto& resource = fixture.add(name);
        CommandArgumentReader reader(L"legacy", name, L"LegacySourceRegression", &ns, &scope);
        ResourceCopyTarget target;
        legacy_source_check(!reader.GetTarget(&target, true) && !reader.Error().empty(),
            "source validation stays strict by default");
        legacy_source_check(reader.GetTarget(&target, true, CommandArgumentReader::PeekMode::Token, false) &&
            reader.Finished() && reader.Error().empty() && target.GetCustomResource(nullptr) == &resource,
            "explicit legacy mode performs exact resource lookup and clears recovered trial error");
        for (const std::wstring& text : {L"copy " + name, name + L" copy", L"ref " + name + L" no_view_cache"}) {
            CommandList commands;
            legacy_source_check(parse_legacy_source(commands, text) && commands.commands.size() == 1,
                "real copy parser accepts legacy operator-containing names with prefix/suffix options");
            auto* operation = dynamic_cast<ResourceCopyOperation*>(commands.commands.front().get());
            legacy_source_check(operation && operation->src.GetCustomResource(nullptr) == &resource,
                "legacy source reaches exact production copy target");
        }
        CommandListExpression arithmetic;
        legacy_source_check(!arithmetic.parse(&name, &ns, &scope),
            "legacy copy-name compatibility does not bypass expression operator tokenization");
        for (const std::wstring& invalid : {L"unknown " + name, name + L" garbage", name + L" " + name,
            name + L",copy", L"copy " + name + L" (]"}) {
            CommandList rejected;
            legacy_source_check(!parse_legacy_source(rejected, invalid) && rejected.commands.empty(),
                "legacy mode retains unknown-option, duplicate-source and bracket rejection");
        }
    }
    CommandList unknown;
    legacy_source_check(!parse_legacy_source(unknown, L"copy resourcelegacy+missing") && unknown.commands.empty(),
        "legacy mode never invents unknown resources");
    const std::wstring decimal = L".5 + .25";
    CommandListExpression leading_decimal;
    float value = 0;
    legacy_source_check(leading_decimal.parse(&decimal, &ns, &scope) &&
        leading_decimal.static_evaluate(&value) && value == .75f,
        "leading-dot decimal remains supported by real parser");
    const std::wstring unsupported = L"noise(1)";
    CommandListExpression noise;
    legacy_source_check(!noise.parse(&unsupported, &ns, &scope), "removed unimplemented noise token remains rejected");
    const std::wstring random_text = L"random(1)";
    CommandListExpression random;
    legacy_source_check(random.parse(&random_text, &ns, &scope) && !random.static_evaluate(nullptr),
        "removing noise leaves dynamic random behavior intact");
    std::fputs("PASS: legacy resource-copy names with strict expression and option boundaries\n", stderr);
}
