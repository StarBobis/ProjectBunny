#include "HackerContext.h"
#include "globals.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static void require_layout(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL input layout: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

static void require_bound(HackerContext& context, ID3D11InputLayout* expected, const char* message)
{
    ID3D11InputLayout* actual = nullptr;
    context.IAGetInputLayout(&actual);
    require_layout(actual == expected, message);
    if (actual) actual->Release();
}

void test_input_layout_followups(ID3D11Device1* device, ID3D11DeviceContext1* native_context)
{
    const char shader[] = "float4 main(float3 p:POSITION):SV_Position{return float4(p,1);}";
    ID3DBlob* bytecode = nullptr;
    require_layout(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &bytecode, nullptr)), "compile WARP signature");
    const void* signature = bytecode->GetBufferPointer();
    const SIZE_T size = bytecode->GetBufferSize();
    D3D11_INPUT_ELEMENT_DESC element = {"position", 0, DXGI_FORMAT_R32G32B32_FLOAT,
        0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    auto cache = std::make_shared<HackerInputLayoutCache>();
    ID3D11InputLayout* first = nullptr;
    require_layout(SUCCEEDED(cache->Create(device, &element, 1, signature, size, &first)), "create cached layout");
    auto* wrapped = static_cast<HackerInputLayout*>(first);
    require_layout(std::strcmp(wrapped->GetElements()[0].SemanticName, "POSITION") == 0,
        "semantic names normalized");
    require_layout(wrapped->GetShaderSignatureSize() == size &&
        std::memcmp(wrapped->GetShaderSignature(), signature, size) == 0,
        "signature owned independently");
    element.SemanticName = "POSITION";
    ID3D11InputLayout* same = nullptr;
    require_layout(SUCCEEDED(cache->Create(device, &element, 1, signature, size, &same)) && same == first,
        "case-insensitive full-key cache hit");
    same->Release();
    const HRESULT validation = device->CreateInputLayout(&element, 1, signature, size, nullptr);
    require_layout(cache->Create(device, &element, 1, signature, size, nullptr) == validation,
        "validation-only HRESULT preserved");
    ID3D11InputLayout* invalid = reinterpret_cast<ID3D11InputLayout*>(1);
    require_layout(FAILED(cache->Create(device, nullptr, 1, signature, size, &invalid)) && !invalid,
        "invalid descriptor array delegated to runtime");
    D3D11_INPUT_ELEMENT_DESC changed = element;
    changed.AlignedByteOffset = 4;
    ID3D11InputLayout* second = nullptr;
    require_layout(SUCCEEDED(cache->Create(device, &changed, 1, signature, size, &second)) && second != first,
        "different layout remains distinct");
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) {
        workers.emplace_back([&]() {
            for (unsigned j = 0; j < 100; ++j) {
                ID3D11InputLayout* layout = nullptr;
                require_layout(SUCCEEDED(cache->Create(device, &element, 1, signature, size, &layout)) &&
                    layout == first, "concurrent cache deduplication");
                layout->Release();
            }
        });
    }
    for (auto& worker : workers) worker.join();

    HackerContext context(device, native_context);
    context.IASetInputLayout(first);
    // DeferInputLayoutOverride consumes this newly acquired reference.
    second->AddRef();
    context.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
    require_bound(context, first, "deferred override does not bind early");
    context.OverrideInputLayout();
    require_bound(context, second, "override binds modified layout");
    context.OverrideInputLayout();
    context.RestoreInputLayout();
    require_bound(context, first, "repeated override preserves original wrapper");
    context.RestoreInputLayout();
    require_bound(context, first, "restore without override is safe");

    context.IASetInputLayout(nullptr);
    second->AddRef();
    context.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
    context.OverrideInputLayout();
    context.RestoreInputLayout();
    require_bound(context, nullptr, "null original layout restored");

    ID3D11InputLayout* raw = nullptr;
    require_layout(SUCCEEDED(device->CreateInputLayout(&element, 1, signature, size, &raw)), "create raw layout");
    context.IASetInputLayout(raw);
    second->AddRef();
    context.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
    context.OverrideInputLayout();
    context.RestoreInputLayout();
    require_bound(context, raw, "raw original layout restored");
    raw->Release();

    second->AddRef();
    context.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
    second->AddRef();
    context.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
    context.OverrideInputLayout();
    context.ClearState();
    context.RestoreInputLayout();
    require_bound(context, nullptr, "ClearState discards pending and saved override state");
    ID3D11DeviceContext1* deferred_native = nullptr;
    require_layout(SUCCEEDED(device->CreateDeferredContext1(0, &deferred_native)), "create deferred context");
    {
        HackerContext deferred(device, deferred_native);
        deferred.IASetInputLayout(first);
        second->AddRef();
        deferred.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
        deferred.OverrideInputLayout();
        ID3D11CommandList* commands = nullptr;
        require_layout(SUCCEEDED(deferred.FinishCommandList(TRUE, &commands)), "finish preserving deferred state");
        require_bound(deferred, second, "FinishCommandList true retains active override");
        deferred.RestoreInputLayout();
        require_bound(deferred, first, "restore after deferred finish preserves original wrapper");
        context.IASetInputLayout(first);
        const bool previous_enabled = G->deferred_contexts_enabled;
        G->deferred_contexts_enabled = true;
        context.ExecuteCommandList(commands, TRUE);
        require_bound(context, first, "ExecuteCommandList true preserves original IA state");
        context.ExecuteCommandList(commands, FALSE);
        require_bound(context, nullptr, "ExecuteCommandList false clears tracked state");
        G->deferred_contexts_enabled = previous_enabled;
        commands->Release();
        second->AddRef();
        deferred.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
        deferred.OverrideInputLayout();
        require_layout(SUCCEEDED(deferred.FinishCommandList(FALSE, &commands)), "finish resetting deferred state");
        deferred.RestoreInputLayout();
        require_bound(deferred, nullptr, "FinishCommandList false discards saved overrides");
        commands->Release();
    }
    deferred_native->Release();
    {
        HackerContext dying(device, native_context);
        dying.IASetInputLayout(first);
        second->AddRef();
        dying.DeferInputLayoutOverride(static_cast<HackerInputLayout*>(second));
        dying.OverrideInputLayout();
    }
    native_context->ClearState();
    second->Release();
    ID3D11InputLayout* native = wrapped->GetOrigInputLayout();
    native->AddRef();
    require_layout(first->Release() == 0, "weak cache does not retain wrapper");
    require_layout(native->Release() == 0, "weak cache does not retain native/device ownership");
    require_layout(SUCCEEDED(cache->Create(device, &element, 1, signature, size, &same)),
        "cache recreates a released entry");
    cache.reset();
    require_layout(same->Release() == 0, "layout survives cache owner teardown");
    bytecode->Release();
}
