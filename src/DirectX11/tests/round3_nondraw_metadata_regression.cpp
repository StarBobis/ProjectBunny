#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"

namespace {
using namespace dx11_slot_test;

float metadata(BatchDevice& device, const wchar_t* source)
{
    const std::wstring text(source), ns;
    CommandListScope scope;
    scope.emplace_front();
    CommandListExpression expression;
    require_batch(expression.parse(&text, &ns, &scope), "non-draw metadata: parse production expression");
    return expression.evaluate(&device.state);
}

void check_metadata(BatchDevice& device, const wchar_t* slot, UINT base, const float* values)
{
    const std::wstring prefix(slot);
    require_batch(metadata(device, (prefix + L"->offset").c_str()) == base,
        "non-draw metadata: absent draw contributes zero to native binding offset");
    const uint32_t hash = crc32c_hw(0, values + base / sizeof(float), 12);
    require_batch(metadata(device, (prefix + L"->hashregion(0,12)").c_str()) == EncodeFloat30(HashUnsigned32(hash)),
        "non-draw metadata: hash reads bytes relative to binding or active draw");
    const UINT first = base / sizeof(float) + 1;
    const uint32_t packed = (first << 20) | ((first + 1) << 12) | (first + 2);
    require_batch(BitCastToUint(metadata(device, (prefix + L"->spatialhash(0,1,2,1)").c_str())) == packed,
        "non-draw metadata: spatial indices use binding or active draw base");
}
}

void test_dx11_round3_nondraw_metadata_regression()
{
    using namespace dx11_slot_test;
    DataCacheTestScope policy;
    BatchDevice device;
    float values[64];
    for (UINT i = 0; i < 64; ++i) values[i] = static_cast<float>(i + 1);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = values;
    ComPtr<ID3D11Buffer> source;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, &initial, &source)),
        "non-draw metadata: create actual tracked WARP buffer");
    ID3D11Buffer* bound = source.Get();
    const UINT stride = 16, offset = 32;
    device.context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
    device.context->IASetIndexBuffer(bound, DXGI_FORMAT_R32_UINT, offset);
    require_batch(device.state.call_info == nullptr, "non-draw metadata: use actual default no-draw state");
    check_metadata(device, L"vb0", offset, values);
    check_metadata(device, L"ib", offset, values);

    DrawCallInfo draw;
    draw.FirstVertex = 2;
    draw.FirstIndex = 4;
    device.state.call_info = &draw;
    check_metadata(device, L"vb0", offset + stride * 2, values);
    check_metadata(device, L"ib", offset + 4 * 4, values);
    device.state.call_info = nullptr;
    require_batch(GetVertexBufferRegionSize(stride, nullptr) == 0 &&
        GetIndexBufferRegionSize(DXGI_FORMAT_R32_UINT, nullptr) == 0,
        "non-draw metadata: unavailable draw extent falls back instead of inventing a region");
    device.context->ClearState();
    require_batch(metadata(device, L"vb0->offset") == -2 && metadata(device, L"ib->offset") == -2,
        "non-draw metadata: absent buffers retain resource-not-found result");
    std::fputs("PASS: non-draw VB/IB offsets and hashes, draw contribution and missing bindings\n", stderr);
}
