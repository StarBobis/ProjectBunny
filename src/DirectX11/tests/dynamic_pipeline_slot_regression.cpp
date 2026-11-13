#include "slot_range_test_support.h"
#include <limits>

namespace {
using namespace dx11_slot_test;

enum class DynamicKind { ConstantBuffer, VertexBuffer, RenderTarget, StreamOutput, UnorderedAccess };

ComPtr<ID3D11Resource> observe_dynamic(BatchDevice& device, DynamicKind kind, wchar_t stage, UINT slot)
{
    ComPtr<ID3D11Resource> resource;
    if (kind == DynamicKind::ConstantBuffer) {
        UINT offset = 0, count = 0;
        auto value = get_cb(device, stage, slot, offset, count);
        resource = value;
    } else if (kind == DynamicKind::VertexBuffer) {
        ComPtr<ID3D11Buffer> value;
        UINT stride = 0, offset = 0;
        device.context->IAGetVertexBuffers(slot, 1, &value, &stride, &offset);
        resource = value;
    } else if (kind == DynamicKind::StreamOutput) {
        ID3D11Buffer* values[D3D11_SO_STREAM_COUNT] = {};
        device.context->SOGetTargets(D3D11_SO_STREAM_COUNT, values);
        for (UINT i = 0; i < D3D11_SO_STREAM_COUNT; ++i) {
            ComPtr<ID3D11Buffer> value;
            value.Attach(values[i]);
            if (i == slot) resource = value;
        }
    } else {
        ComPtr<ID3D11View> view;
        if (kind == DynamicKind::RenderTarget) {
            ID3D11RenderTargetView* values[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            device.context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, values, nullptr);
            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
                ComPtr<ID3D11RenderTargetView> value;
                value.Attach(values[i]);
                if (i == slot) view = value;
            }
        } else {
            ComPtr<ID3D11UnorderedAccessView> value;
            if (stage == L'c') device.context->CSGetUnorderedAccessViews(slot, 1, &value);
            else device.context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, slot, 1, &value);
            view = value;
        }
        if (view) view->GetResource(&resource);
    }
    return resource;
}

ComPtr<ID3D11Resource> make_dynamic_resource(BatchDevice& device, DynamicKind kind)
{
    ComPtr<ID3D11Resource> result;
    if (kind == DynamicKind::RenderTarget) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 4;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture;
        require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &texture)),
            "dynamic pipeline: create actual RT texture");
        result = texture;
    } else {
        const UINT flags = kind == DynamicKind::ConstantBuffer ? D3D11_BIND_CONSTANT_BUFFER :
            kind == DynamicKind::VertexBuffer ? D3D11_BIND_VERTEX_BUFFER :
            kind == DynamicKind::StreamOutput ? D3D11_BIND_STREAM_OUTPUT : D3D11_BIND_UNORDERED_ACCESS;
        result = buffer(device, 256, flags);
    }
    return result;
}

void exercise_dynamic_kind(BatchDevice& device, Registry& registry, DynamicKind kind,
    wchar_t stage, const std::wstring& prefix, UINT limit, unsigned fixture_id)
{
    device.context->ClearState();
    const auto suffix = std::to_wstring(fixture_id);
    const auto index_name = L"$dynamic_pipeline_index_" + suffix;
    auto& index = registry.variable(index_name, 0);
    const auto resource_name = L"resourcedynamic_pipeline_" + suffix;
    auto native = make_dynamic_resource(device, kind);
    registry.resource(resource_name, device, native.Get(),
        kind == DynamicKind::UnorderedAccess ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN,
        kind == DynamicKind::VertexBuffer ? 16 : 0,
        kind == DynamicKind::RenderTarget ? 0 : 256);
    CommandList bind;
    append(bind, prefix + L"[" + index_name + L"]", L"ref " + resource_name);
    require_batch(bind.commands.size() == 1, "dynamic pipeline: one real parsed directive is reused");
    ResourceCopyTarget dynamic_source;
    CommandListScope scope;
    scope.emplace_front();
    const std::wstring ini_namespace;
    const auto expression = prefix + L"[" + index_name + L"]";
    require_batch(dynamic_source.ParseTarget(expression.c_str(), true, &ini_namespace, &scope),
        "dynamic pipeline: parse actual dynamic GetResource target");

    for (float selected : {0.0f, static_cast<float>(limit - 1), static_cast<float>(limit - 1) + 0.25f}) {
        // SO/UAV/RT aliases can conflict across slots: every iteration starts
        // from genuine unbound API state, then reuses the same parsed command.
        device.context->ClearState();
        index.fval = selected;
        execute(bind, device);
        const UINT slot = static_cast<UINT>(selected);
        require_batch(observe_dynamic(device, kind, stage, slot).Get() == native.Get(),
            "dynamic pipeline: first/last/fractional valid index binds actual resource");
        ID3D11View* raw = nullptr;
        UINT stride = 0, offset = 0, size = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        ComPtr<ID3D11Resource> fetched;
        fetched.Attach(dynamic_source.GetResource(&device.state, &raw, &stride, &offset, &format, &size));
        ComPtr<ID3D11View> view;
        view.Attach(raw);
        require_batch(fetched.Get() == native.Get(),
            "dynamic pipeline: actual dynamic source getter selects changed command index");
    }

    // Each invalid destination must leave the genuinely bound final slot intact.
    index.fval = static_cast<float>(limit - 1);
    for (float invalid : {-1.0f, static_cast<float>(limit), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        index.fval = invalid;
        execute(bind, device);
        require_batch(observe_dynamic(device, kind, stage, limit - 1).Get() == native.Get(),
            "dynamic pipeline: invalid destination preserves actual prior binding");
        require_batch(!observe_dynamic(device, kind, stage, 0),
            "dynamic pipeline: invalid destination does not alias the first slot");
    }

    // Invalid source resolves to NULL, so the ordinary command clears a valid
    // destination; unless_null keeps it. This tests actual bindings, not just
    // a null return from the source target parser/getter.
    CommandList clear, preserve;
    const auto destination = prefix + std::to_wstring(limit - 1);
    append(clear, destination, L"ref " + expression);
    append(preserve, destination, L"ref unless_null " + expression);
    for (float invalid : {-1.0f, static_cast<float>(limit), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
        device.context->ClearState();
        index.fval = static_cast<float>(limit - 1);
        execute(bind, device);
        index.fval = invalid;
        execute(preserve, device);
        require_batch(observe_dynamic(device, kind, stage, limit - 1).Get() == native.Get(),
            "dynamic pipeline: invalid source with unless_null preserves actual destination");
        execute(clear, device);
        require_batch(!observe_dynamic(device, kind, stage, limit - 1),
            "dynamic pipeline: invalid source without unless_null actually clears destination");
    }
    device.context->ClearState();
}
} // namespace

void test_dx11_dynamic_pipeline_slot_regression()
{
    BatchDevice device;
    Registry registry;
    unsigned fixture = 0;
    for (wchar_t stage : std::wstring(L"vhdgpc"))
        exercise_dynamic_kind(device, registry, DynamicKind::ConstantBuffer, stage,
            std::wstring(1, stage) + L"s-cb", D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, fixture++);
    exercise_dynamic_kind(device, registry, DynamicKind::VertexBuffer, L'v', L"vb",
        D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, fixture++);
    exercise_dynamic_kind(device, registry, DynamicKind::RenderTarget, L'p', L"o",
        D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, fixture++);
    exercise_dynamic_kind(device, registry, DynamicKind::StreamOutput, L'g', L"so",
        D3D11_SO_STREAM_COUNT, fixture++);
    for (wchar_t stage : std::wstring(L"pc"))
        exercise_dynamic_kind(device, registry, DynamicKind::UnorderedAccess, stage,
            std::wstring(1, stage) + L"s-u", D3D11_1_UAV_SLOT_COUNT, fixture++);
}
