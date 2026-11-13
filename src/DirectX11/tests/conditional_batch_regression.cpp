#include "slot_range_test_support.h"
using namespace dx11_slot_test;
extern std::vector<std::shared_ptr<CommandList>> dynamically_allocated_command_lists;

namespace {
class ConditionalGlobals {
    size_t registered = registered_command_lists.size();
    size_t allocated = dynamically_allocated_command_lists.size();
public:
    ~ConditionalGlobals()
    {
        registered_command_lists.resize(registered);
        dynamically_allocated_command_lists.resize(allocated);
    }
};

std::shared_ptr<IfCommand> chain(const wchar_t* condition)
{
    auto result = std::make_shared<IfCommand>(L"CommandListConditionalBatch");
    result->pre_finalised = result->post_finalised = true;
    const std::wstring expression(condition), ns;
    CommandListScope scope; scope.emplace_front();
    require_batch(result->expression.parse(&expression, &ns, &scope), "conditional: parse actual runtime expression");
    result->ini_line = L"if " + expression;
    return result;
}

void bind_cases(BatchDevice& device, Registry& registry, ID3D11Resource* a, ID3D11Resource* b)
{
    auto& choose = registry.variable(L"$conditional_basic_choose", 1);
    auto condition = chain(L"$conditional_basic_choose");
    append(*condition->true_commands_pre, L"ps-t3", L"ref resourceconditional_a");
    append(*condition->false_commands_pre, L"ps-t3", L"ref resourceconditional_b");
    CommandList command;
    command.commands.push_back(condition);
    append(command, L"ps-t4", L"ref resourceconditional_a");
    merge_shader_resource_batches(&command);
    auto batch = std::dynamic_pointer_cast<ShaderResourceBindBatch>(command.commands.front());
    require_batch(command.commands.size() == 1 && batch && batch->first_slot == 3 && batch->count == 2,
        "conditional: nonzero conditional and ordinary copy form actual bind batch");
    const auto first = execute(command, device);
    require_batch(first.get_calls == 0 && first.set_calls == 1 && bound_resource(device, L'p', 3).Get() == a &&
        bound_resource(device, L'p', 4).Get() == a, "conditional: true branch binds actual nonzero slot without old binding read");
    choose.fval = 0;
    const auto second = execute(command, device);
    require_batch(second.set_calls == 1 && bound_resource(device, L'p', 3).Get() == b,
        "conditional: same optimised command reevaluates false branch");

    auto no_else = chain(L"$conditional_basic_choose");
    append(*no_else->true_commands_pre, L"ps-t3", L"null");
    CommandList keep;
    keep.commands.push_back(no_else);
    append(keep, L"ps-t4", L"ref resourceconditional_b");
    merge_shader_resource_batches(&keep);
    const auto kept = execute(keep, device);
    require_batch(kept.get_calls == 1 && kept.set_calls == 1 && bound_resource(device, L'p', 3).Get() == b &&
        bound_resource(device, L'p', 4).Get() == b, "conditional: missing else preserves original slot and publishes neighbour");
    choose.fval = 1;
    const auto cleared = execute(keep, device);
    require_batch(cleared.get_calls == 0 && cleared.set_calls == 1 && !get_slot(device, L'p', 3),
        "conditional: selected null is an assignment, not a skipped branch");
}

void fetch_cases(BatchDevice& device, Registry& registry, ID3D11Resource* a, ID3D11Resource* b)
{
    registry.resource(L"resourceconditional_outa", device);
    registry.resource(L"resourceconditional_outb", device);
    registry.resource(L"resourceconditional_outnext", device);
    auto av = srv(device, a), bv = srv(device, b);
    set_slot(device, L'c', 7, av.Get()); set_slot(device, L'c', 8, bv.Get());
    auto& choose = registry.variable(L"$conditional_fetch_choose", 1);
    auto condition = chain(L"$conditional_fetch_choose");
    append(*condition->true_commands_pre, L"resourceconditional_outa", L"ref cs-t7");
    append(*condition->false_commands_pre, L"resourceconditional_outb", L"ref cs-t7");
    CommandList command;
    command.commands.push_back(condition);
    append(command, L"resourceconditional_outnext", L"ref cs-t8");
    merge_shader_resource_batches(&command);
    require_batch(command.commands.size() == 1 && std::dynamic_pointer_cast<ShaderResourceFetchBatch>(command.commands.front()),
        "conditional: conditional fetch uses actual fetch batch");
    auto first = execute(command, device);
    require_batch(first.get_calls == 1 && first.set_calls == 0 && customResources.at(L"resourceconditional_outa").resource == a &&
        customResources.at(L"resourceconditional_outnext").resource == b,
        "conditional: virtual source dispatch uses nonzero branch source slot");
    choose.fval = 0; set_slot(device, L'c', 7, bv.Get());
    auto second = execute(command, device);
    require_batch(second.get_calls == 1 && customResources.at(L"resourceconditional_outa").resource == a &&
        customResources.at(L"resourceconditional_outb").resource == b,
        "conditional: repeated fetch routes current native source to selected destination only");
}

void barriers(BatchDevice& device)
{
    auto solitary = chain(L"1");
    append(*solitary->true_commands_pre, L"vs-t3", L"ref resourceconditional_a");
    CommandList single; single.commands.push_back(solitary);
    merge_shader_resource_batches(&single);
    require_batch(single.commands.size() == 1 && single.commands.front() == solitary,
        "conditional: chain outside batch retains original If object");

    auto multiple = chain(L"1");
    append(*multiple->true_commands_pre, L"vs-t3", L"ref resourceconditional_a");
    append(*multiple->true_commands_pre, L"vs-t4", L"ref resourceconditional_a");
    CommandList ordered; ordered.commands.push_back(multiple);
    append(ordered, L"vs-t5", L"ref resourceconditional_b");
    merge_shader_resource_batches(&ordered);
    require_batch(ordered.commands.size() == 2 && ordered.commands.front() == multiple,
        "conditional: multi-command branch retains ordinary control flow");
    execute(ordered, device);

    auto dynamic = chain(L"1");
    append(*dynamic->true_commands_pre, L"vs-t[3]", L"ref resourceconditional_b");
    CommandList bracket; bracket.commands.push_back(dynamic);
    append(bracket, L"vs-t4", L"ref resourceconditional_a");
    merge_shader_resource_batches(&bracket);
    require_batch(bracket.commands.size() == 2 && bracket.commands.front() == dynamic,
        "conditional: bracket destination remains dynamic-slot barrier even constant");
}
} // namespace

void test_dx11_conditional_batch_regression()
{
    BatchDevice device;
    Registry registry;
    ConditionalGlobals globals;
    auto a = texture(device, 0x14253647), b = texture(device, 0x58697a8b);
    registry.resource(L"resourceconditional_a", device, a.Get());
    registry.resource(L"resourceconditional_b", device, b.Get());
    bind_cases(device, registry, a.Get(), b.Get());
    fetch_cases(device, registry, a.Get(), b.Get());
    barriers(device);
}
