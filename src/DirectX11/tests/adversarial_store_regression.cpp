#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;
void append_store(CommandList& commands, const std::wstring& arguments)
{
    CommandListScope scope;
    scope.emplace_front();
    commands.scope = &scope;
    std::wstring text = arguments;
    const std::wstring ns;
    const bool parsed = ParseCommandListGeneralCommands(L"AdversarialStore", L"store", &text,
        &commands, &commands, nullptr, &ns);
    commands.scope = nullptr;
    require_batch(parsed && commands.commands.size() == 1, "adversarial Store: real valid directive parsed");
}
}

void test_dx11_adversarial_store_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    Registry registry;
    const float values[] = {11,22,33,44};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values); desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = values;
    ComPtr<ID3D11Buffer> source;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, &data, &source)), "adversarial Store: real source buffer");
    registry.resource(L"resourceadversarialstore", device, source.Get(), DXGI_FORMAT_R32_FLOAT, 4, sizeof(values));
    registry.resource(L"resourceadversarialstorenull", device);
    auto& output = registry.variable(L"$adversarial_store_output", 91);
    auto& index = registry.variable(L"$adversarial_store_index", 2);
    CommandList command;
    append_store(command, L"$adversarial_store_output, resourceadversarialstore, $adversarial_store_index");
    execute(command, device);
    require_batch(output.fval == 33, "adversarial Store: normal actual GPU read retains float-index semantics");
    index.fval = 4;
    output.fval = 91;
    execute(command, device);
    require_batch(output.fval == 0, "adversarial Store: upstream physical read failure publishes zero instead of stale output");
    index.fval = 1073741824.0f; // Exact UINT index; byte addressing is UINT64 upstream.
    output.fval = 91;
    execute(command, device);
    require_batch(output.fval == 0, "adversarial Store: valid large UINT index reaches physical failure and publishes zero");
    output.fval = 73;
    output.flags |= VariableFlags::LOCKED;
    execute(command, device);
    require_batch(output.fval == 73, "adversarial Store: runtime locked output remains protected even on failed read");
    output.flags &= static_cast<VariableFlags>(~VariableFlags::LOCKED);
    CommandList null_source;
    append_store(null_source, L"$adversarial_store_output, resourceadversarialstorenull, 0");
    execute(null_source, device);
    require_batch(output.fval == 73, "adversarial Store: upstream missing source preserves prior output");
    device.context->ClearState();
}
