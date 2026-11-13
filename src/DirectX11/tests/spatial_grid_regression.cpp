#include "CommandList.h"
#include "HackerContext.h"
#include "globals.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern uint32_t PackCellCoords(float, float, float, float);

static void require_grid(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL spatial grid: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void test_dx11_spatial_grid_regression(CommandListState& state)
{
    require_grid(PackCellCoords(1023, 0, 0, 1) == 0x3ff00000u &&
        PackCellCoords(1024, 0, 0, 1) == 0x40000000u &&
        PackCellCoords(4095, 0, 0, 1) == 0xfff00000u &&
        PackCellCoords(4096, 0, 0, 1) == 0, "X uses all twelve bits and wraps at 4096");
    require_grid(PackCellCoords(0, 255, 0, 1) == 0x000ff000u &&
        PackCellCoords(0, 256, 0, 1) == 0 &&
        PackCellCoords(0, 0, 1024, 1) == 1024 &&
        PackCellCoords(0, 0, 4095, 1) == 4095 &&
        PackCellCoords(0, 0, 4096, 1) == 0, "Y uses eight bits and Z twelve bits");
    require_grid(PackCellCoords(-1, -1, -1, 1) == UINT32_MAX, "negative coordinates wrap on each axis");
    require_grid(PackCellCoords(2147483904.0f, 0, -2147483904.0f, 1) == 0x10000f00u,
        "large-coordinate safe narrowing uses each new axis modulus");
    const GridPos all = UnpackCellCoords(UINT32_MAX);
    require_grid(all.x == 4095 && all.y == 255 && all.z == 4095 &&
        SpatialDistanceChebyshev(all, GridPos{0, 0, 0}) == 1 &&
        SpatialDistanceChebyshev(GridPos{2048, 128, 2048}, GridPos{0, 0, 0}) == 2048,
        "unpack and toroidal distances use independent axis sizes");
    require_grid(PackCellCoords(std::numeric_limits<float>::infinity(), 0, 0, 1) == 0 &&
        PackCellCoords(0, 0, 0, 0) == 0, "public packing rejects undefined numeric conversions");

    uint32_t data[256] = {};
    const float position[3] = {128, 31.875f, 511.875f};
    std::memcpy(data + 64, position, sizeof(position));
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(data);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    ID3D11Buffer* buffer = nullptr;
    require_grid(SUCCEEDED(state.mOrigDevice1->CreateBuffer(&desc, &initial, &buffer)), "spatial CB window buffer");
    G->mResources[buffer];
    ResourceCopyTarget target;
    target.type = ResourceCopyTargetType::CONSTANT_BUFFER;
    target.shader_type = L'v';
    target.slot = 0;
    target.SetResource(&state, buffer, nullptr, 0, 256, DXGI_FORMAT_UNKNOWN, 256);
    const std::wstring text = L"vs-cb0->spatialhash(0, 1, 2, 0.125)";
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    require_grid(expression.parse(&text, &ini_namespace, &scope) &&
        BitCastToUint(expression.evaluate(&state)) == 0x400fffffu,
        "nonzero CB byte base is combined with public float element indices");
    const float negative[3] = {-0.125f, -0.125f, -0.125f};
    std::memcpy(data + 64, negative, sizeof(negative));
    state.mOrigContext1->UpdateSubresource(buffer, 0, nullptr, data, 0, 0);
    ClearResourceRegionHashCache(buffer);
    require_grid(BitCastToUint(expression.evaluate(&state)) == UINT32_MAX,
        "all-one packed spatial value survives its IEEE NaN representation");

    const std::wstring name = L"poolspatialgridregression";
    require_grid(!customResourcePools.count(name) && !customResources.count(name + L"_template"), "unique spatial pool fixture");
    auto& pool = customResourcePools[name];
    pool.name = name;
    pool.resource_template = &customResources[name + L"_template"];
    pool.resource_template->pool = &pool;
    pool.variable_template = std::make_unique<CommandListVariable>(name, 0, VariableFlags::NONE);
    pool.index_type = PoolIndexType::SPATIAL;
    pool.Initialize(2);
    const float key = BitCastToFloat(UINT32_MAX);
    auto* variable = pool.GetVariable(key, false, false, true);
    require_grid(variable != nullptr, "spatial pool allocates NaN bit key");
    variable->fval = 17;
    require_grid(pool.GetVariable(key, false, false, false) == variable && variable->fval == 17,
        "NaN key occupancy is distinct from an unused slot");
    customResourcePools.erase(name);
    customResources.erase(name + L"_template");
    command_list_globals.erase(L"$" + name + L"[0]");
    ID3D11Buffer* none = nullptr;
    state.mOrigContext1->VSSetConstantBuffers(0, 1, &none);
    G->mResources.erase(buffer);
    ClearRegionHashesGlobalCache();
    require_grid(buffer->Release() == 0, "spatial CB references released");
}
