#include "ResourceHash.h"
#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include <stdexcept>

// Link the production implementation; it is intentionally not a public header API.
uint32_t PackCellCoords(float x, float y, float z, float cell_size);

void test_dx11_round2_spatial_boundary_regression()
{
    // Upstream divides float coordinates by a float cell size before floor.
    // These valid inputs round to exactly five in float arithmetic.
    if (PackCellCoords(0.5f, 0, 0, 0.1f) != 0x00500000u ||
        PackCellCoords(0, 0.5f, 0, 0.1f) != 0x00005000u ||
        PackCellCoords(0, 0, 0.5f, 0.1f) != 0x00000005u)
        throw std::runtime_error("round2 spatial: preserve upstream float division at valid decimal cell boundaries");

    using namespace dx11_slot_test;
    DataCacheTestScope policy;
    BatchDevice device;
    float coordinates[128]{};
    coordinates[64] = 0.5f;
    coordinates[65] = 1.0f;
    coordinates[66] = 1.5f;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(coordinates);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = coordinates;
    ComPtr<ID3D11Buffer> source;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &data, &source)),
        "round2 spatial: create tracked actual CB with position in nonzero window");
    ID3D11Buffer* native = source.Get();
    const UINT first = 16, count = 16;
    device.context->VSSetConstantBuffers1(0, 1, &native, &first, &count);
    CommandListScope scope;
    scope.emplace_front();
    const std::wstring ns;
    ResourceCopyTarget target;
    require_batch(target.ParseTarget(L"vs-cb0->spatialhash(0,1,2,0.1)", true, &ns, &scope),
        "round2 spatial: parse upstream public float-element offsets");
    const uint32_t result = BitCastToUint(target.GetResourceSpatialHash(&device.state));
    device.context->ClearState();
    require_batch(result == 0x0050a00fu,
        "round2 spatial: public element indices select consecutive floats relative to the CB window");
}
