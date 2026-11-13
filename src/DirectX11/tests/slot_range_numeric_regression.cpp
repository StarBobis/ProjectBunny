#include "CommandList.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL dynamic slot/range numeric: %s\n", message);
        throw std::runtime_error(message);
    }
}

class NumericFixture {
public:
    const std::wstring low_name = L"$slot_range_numeric_low";
    const std::wstring high_name = L"$slot_range_numeric_high";
    CommandListState state;
    CustomResourcePool pool;
    ResourceCopyTarget slots, ring;

    NumericFixture()
    {
        check(!command_list_globals.count(low_name) && !command_list_globals.count(high_name),
            "numeric regression globals must be unique");
        command_list_globals.emplace(low_name, CommandListVariable(low_name, 0, VariableFlags::GLOBAL));
        command_list_globals.emplace(high_name, CommandListVariable(high_name, 0, VariableFlags::GLOBAL));
        slots.type = ResourceCopyTargetType::SHADER_RESOURCE;
        slots.max_slot = 128;
        slots.slot_expression = expression(low_name);
        slots.range_start = expression(low_name);
        slots.range_end = expression(high_name);
        pool.lazy_initialization = true;
        pool.index_type = PoolIndexType::RING;
        pool.Initialize(4);
        ring.type = ResourceCopyTargetType::POOL;
        ring.custom_resource_pool = &pool;
        ring.range_start = slots.range_start;
        ring.range_end = slots.range_end;
    }

    ~NumericFixture()
    {
        command_list_globals.erase(low_name);
        command_list_globals.erase(high_name);
    }

    std::shared_ptr<CommandListExpression> expression(const std::wstring& source)
    {
        auto result = std::make_shared<CommandListExpression>();
        const std::wstring ini_namespace;
        CommandListScope scope;
        check(result->parse(&source, &ini_namespace, &scope), "parse real numeric variable expression");
        return result;
    }

    void values(float low, float high)
    {
        command_list_globals.at(low_name).fval = low;
        command_list_globals.at(high_name).fval = high;
    }

    void range(ResourceCopyTarget& target, float low, float high, unsigned first, unsigned count)
    {
        values(low, high);
        unsigned actual_first = UINT_MAX, actual_count = UINT_MAX;
        check(target.ResolveRange(&state, &actual_first, &actual_count), "valid numeric range resolves");
        check(actual_first == first && actual_count == count, "numeric range inclusive endpoints and wrap");
    }

    void invalid(ResourceCopyTarget& target, float low, float high)
    {
        values(low, high);
        unsigned first = 12345, count = 23456;
        check(!target.ResolveRange(&state, &first, &count), "invalid numeric range rejected");
        check(first == 12345 && count == 23456, "invalid range leaves output untouched");
    }
};
}

void test_dx11_slot_range_numeric_regression()
{
    NumericFixture fixture;
    auto& slots = fixture.slots;
    auto& ring = fixture.ring;
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float largest = (std::numeric_limits<float>::max)();
    const float tiny = std::numeric_limits<float>::denorm_min();

    for (float bad : {nan, infinity, -infinity, largest, -largest, -tiny, -0.1f, 128.0f}) {
        fixture.values(bad, 0);
        check(slots.ResolveSlot(&fixture.state) == UINT_MAX, "dynamic slot rejects nonfinite or outside index");
    }
    for (float valid : {0.0f, -0.0f, tiny, 0.9f, 1.9f, 127.0f, std::nextafter(128.0f, 0.0f)}) {
        fixture.values(valid, 0);
        check(slots.ResolveSlot(&fixture.state) == static_cast<unsigned>(std::trunc(valid)),
            "dynamic slot reevaluates variables and truncates valid fractions");
    }
    fixture.range(slots, 0, 127, 0, 128);
    fixture.range(slots, 0.9f, 1.9f, 0, 2);
    fixture.range(slots, 127.9f, 127.9f, 127, 1);
    fixture.range(slots, -0.0f, 0.0f, 0, 1);
    fixture.invalid(slots, -tiny, 0);
    fixture.invalid(slots, 0, 128);
    fixture.invalid(slots, 0.9f, 0.1f);
    fixture.range(ring, -1, 1, 3, 3);
    fixture.range(ring, -3.9f, -0.1f, 1, 4);
    fixture.range(ring, 3, 6, 3, 4);
    fixture.range(ring, -tiny, tiny, 0, 1);
    fixture.range(ring, 1e20f, 1e20f, 0, 1);
    fixture.range(ring, -1e20f, -1e20f, 0, 1);
    fixture.range(ring, largest, largest, 0, 1);
    fixture.range(ring, -largest, -largest, 0, 1);
    fixture.invalid(ring, 100.9f, 104.1f);
    fixture.invalid(ring, 0, 4);
    fixture.invalid(ring, -largest, largest);
    fixture.invalid(ring, 1, 0);
    for (float bad : {nan, infinity, -infinity}) {
        fixture.invalid(slots, bad, 1);
        fixture.invalid(slots, 0, bad);
        fixture.invalid(ring, bad, 1);
        fixture.invalid(ring, 0, bad);
    }
    fixture.pool.Initialize(0);
    fixture.invalid(ring, 0, 0);
    fixture.pool.Initialize(1);
    fixture.range(ring, largest, largest, 0, 1);
    fixture.invalid(ring, 0.9f, 1.1f);
    fixture.pool.Initialize(7);
    // 2^100 modulo 7 is 2. An odd capacity catches implementations that
    // accidentally turn every enormous finite index into zero.
    const float enormous = std::ldexp(1.0f, 100);
    fixture.range(ring, enormous, enormous, 2, 1);
    fixture.range(ring, -enormous, -enormous, 5, 1);
    std::fputs("PASS: DX11 dynamic slot/range numeric boundaries\n", stderr);
}
