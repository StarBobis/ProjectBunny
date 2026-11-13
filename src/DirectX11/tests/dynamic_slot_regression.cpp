#include "slot_range_test_support.h"
#include <limits>
using namespace dx11_slot_test;

void test_dx11_dynamic_slot_regression()
{
    BatchDevice device;
    Registry registry;
    auto native = texture(device, 0x77442211u);
    registry.resource(L"resourceslot_dynamic_texture", device, native.Get());
    auto& index = registry.variable(L"$slot_dynamic_index", 0);
    for (wchar_t stage : std::wstring(L"vhdgpc")) {
        const auto prefix = std::wstring(1, stage) + L"s-t";
        CommandList command;
        append(command, prefix + L"[$slot_dynamic_index]", L"ref resourceslot_dynamic_texture");
        index.fval = 0; execute(command, device);
        index.fval = 127.9f; execute(command, device);
        require_batch(bound_resource(device, stage, 0).Get() == native.Get() &&
            bound_resource(device, stage, 127).Get() == native.Get(), "dynamic: actual slot 0/127 bindings reevaluate/truncate");
        index.fval = std::numeric_limits<float>::quiet_NaN();
        execute(command, device);
        require_batch(bound_resource(device, stage, 127).Get() == native.Get(), "dynamic: invalid destination leaves actual binding intact");
    }
    CommandList barrier;
    append(barrier, L"ps-t0", L"ref resourceslot_dynamic_texture");
    append(barrier, L"ps-t1", L"ref resourceslot_dynamic_texture");
    append(barrier, L"ps-t[2]", L"null");
    append(barrier, L"ps-t3", L"ref resourceslot_dynamic_texture");
    append(barrier, L"ps-t4", L"ref resourceslot_dynamic_texture");
    merge_shader_resource_batches(&barrier);
    require_batch(barrier.commands.size() == 3, "dynamic: even constant bracket slot remains an old-batch barrier");
    const auto stats = execute(barrier, device);
    require_batch(stats.get_calls == 0 && stats.set_calls == 2 && !get_slot(device, L'p', 2),
        "dynamic: both safe static runs still issue actual batched calls around dynamic operation");
    CommandList bad;
    require_batch(!parse(bad, L"vs-u[0]", L"ref resourceslot_dynamic_texture"), "dynamic: UAV stage restriction");
    require_batch(!parse(bad, L"vb[0]->elementformat(\"position\",0)", L"r32_float"), "dynamic: layout override requires fixed VB");
}
