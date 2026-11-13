#include "slot_range_test_support.h"

extern std::vector<std::shared_ptr<CommandList>> dynamically_allocated_command_lists;

namespace {
using namespace dx11_slot_test;

class CaptureParserRegistration {
    size_t registered = registered_command_lists.size();
    size_t allocated = dynamically_allocated_command_lists.size();
public:
    ~CaptureParserRegistration()
    {
        registered_command_lists.resize(registered);
        dynamically_allocated_command_lists.resize(allocated);
    }
};

void flow(CommandList& list, const wchar_t* source)
{
    const std::wstring text = source, ns;
    require_batch(ParseCommandListFlowControl(L"CommandListRangeCaptureExample", &text, &list, nullptr, &ns),
        "range example: parse actual if/endif flow syntax");
}

void assignment(CommandList& list, const wchar_t* key, const wchar_t* value)
{
    const std::wstring ns;
    std::wstring rhs = value, raw = std::wstring(key) + L" = " + value;
    require_batch(ParseCommandListVariableAssignment(L"CommandListRangeCaptureExample", key, &rhs, &raw,
        &list, &list, nullptr, &ns), "range example: parse actual capture guard assignment");
}

void compile_capture(CommandList& list)
{
    CommandListScope scope;
    scope.emplace_front();
    list.scope = &scope;
    flow(list, L"if !$range_capture_example_guard");
    const std::wstring ns;
    std::wstring source = L"ref ps-t[3:5]";
    require_batch(ParseCommandListResourceCopyTargetDirective(L"CommandListRangeCaptureExample",
        L"poolrange_capture_example[0:2]", &source, &list, &ns), "range example: parse explicit mismatched-origin capture bounds");
    assignment(list, L"$range_capture_example_guard", L"1");
    flow(list, L"endif");
    list.scope = nullptr;
}

void compile_release(CommandList& list)
{
    CommandListScope scope;
    scope.emplace_front();
    list.scope = &scope;
    flow(list, L"if $range_capture_example_guard");
    const std::wstring ns;
    std::wstring source = L"ref poolrange_capture_example[0:2]";
    require_batch(ParseCommandListResourceCopyTargetDirective(L"CommandListRangeCaptureExample",
        L"ps-t[3:5]", &source, &list, &ns), "range example: parse actual null-preserving restoration");
    source = L"null";
    require_batch(ParseCommandListResourceCopyTargetDirective(L"CommandListRangeCaptureExample",
        L"poolrange_capture_example[*]", &source, &list, &ns), "range example: parse actual whole-pool clear");
    assignment(list, L"$range_capture_example_guard", L"0");
    flow(list, L"endif");
    list.scope = nullptr;
}

void range_parser_rollback(BatchDevice& device, Registry& registry)
{
    registry.pool(L"poolrange_parser_rollback", device, PoolIndexType::RING, 4);
    const std::wstring ns;
    CommandListScope scope;
    scope.emplace_front();
    ResourceCopyTarget target;
    const wchar_t* malformed[] = {
        L"ps-t[:3]", L"ps-t[1:]", L"ps-t[1+:3]", L"ps-t[1:3+]", L"ps-t[0:1:2]",
        L"poolrange_parser_rollback[:3]", L"poolrange_parser_rollback[1:]",
        L"poolrange_parser_rollback[1+:3]", L"poolrange_parser_rollback[1:3+]"
    };
    for (const auto* text : malformed) {
        require_batch(target.ParseTarget(L"ps-t[(1+1):(2+3)]", true, &ns, &scope, true, true),
            "range parser: a reused target starts with valid nested bounds");
        unsigned first = 0, count = 0;
        require_batch(target.ResolveRange(&device.state, &first, &count) && first == 2 && count == 4,
            "range parser: actual nested bounds resolve inclusively");
        require_batch(!target.ParseTarget(text, true, &ns, &scope, true, true),
            "range parser: malformed first or last expression is rejected");
        first = 99; count = 88;
        require_batch(!target.ResolveRange(&device.state, &first, &count) && first == 99 && count == 88,
            "range parser: failed parse cannot resolve a stale or half-parsed range");
        require_batch(target.ParseTarget(L"poolrange_parser_rollback[-1:1]", true, &ns, &scope, true, true) &&
            target.ResolveRange(&device.state, &first, &count) && first == 3 && count == 3,
            "range parser: same target recovers with a valid wrapped pool range");
    }
}
}

void test_dx11_range_capture_example_regression()
{
    BatchDevice device;
    Registry registry;
    CaptureParserRegistration registrations;
    range_parser_rollback(device, registry);
    auto& pool = registry.pool(L"poolrange_capture_example", device, PoolIndexType::RING, 3);
    auto& guard = registry.variable(L"$range_capture_example_guard", 0);
    auto a = texture(device, 0x11223344u), b = texture(device, 0x55667788u), mod = texture(device, 0x99aabbccu);
    auto a_view = srv(device, a.Get()), b_view = srv(device, b.Get()), mod_view = srv(device, mod.Get());
    CommandList capture, release;
    compile_capture(capture);
    compile_release(release);
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        const auto& first = cycle ? b_view : a_view;
        const auto& last = cycle ? a_view : b_view;
        set_slot(device, L'p', 3, first.Get());
        set_slot(device, L'p', 4, nullptr);
        set_slot(device, L'p', 5, last.Get());
        execute(capture, device);
        require_batch(guard.fval == 1 && pool.GetResource(1, false, true, false)->is_null,
            "range example: first capture records originally empty slot and raises guard");
        for (UINT slot = 3; slot <= 5; ++slot) set_slot(device, L'p', slot, mod_view.Get());
        execute(capture, device);
        require_batch(pool.GetResource(0, false, true, false)->view == first.Get() &&
            pool.GetResource(2, false, true, false)->view == last.Get(),
            "range example: second draw does not capture mod views over original bindings");
        execute(release, device);
        require_batch(guard.fval == 0 && get_slot(device, L'p', 3).Get() == first.Get() &&
            !get_slot(device, L'p', 4) && get_slot(device, L'p', 5).Get() == last.Get(),
            "range example: release restores native views and unbinds originally empty slot");
        for (unsigned i = 0; i < 3; ++i)
            require_batch(pool.GetResource(static_cast<float>(i), false, true, false)->is_null,
                "range example: release resets every logical pool resource");
        execute(release, device);
        require_batch(get_slot(device, L'p', 3).Get() == first.Get() && !get_slot(device, L'p', 4),
            "range example: already-released cycle is a no-op");
    }
    device.context->ClearState();
    std::fputs("PASS: actual parsed guarded range capture/restore example and repeated cycles\n", stderr);
}
