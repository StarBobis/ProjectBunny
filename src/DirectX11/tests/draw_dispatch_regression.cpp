#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"
#include "ShaderRegex.h"

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using Microsoft::WRL::ComPtr;

static void require_call_counter(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL draw/dispatch counter: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class CallCounterDevice
{
public:
    HMODULE runtime = nullptr;
    ID3D11Device1* device = nullptr;
    ID3D11DeviceContext1* native = nullptr;
    HackerDevice* wrapper = nullptr;
    std::unique_ptr<HackerContext> context;

    CallCounterDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require_call_counter(runtime != nullptr, "load actual system runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        require_call_counter(create != nullptr, "resolve native device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        require_call_counter(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real WARP device");
        require_call_counter(SUCCEEDED(base->QueryInterface(IID_PPV_ARGS(&device))) &&
            SUCCEEDED(immediate->QueryInterface(IID_PPV_ARGS(&native))), "query native D3D11.1 interfaces");
        wrapper = new HackerDevice(device, native);
        context = std::make_unique<HackerContext>(device, native);
        context->SetHackerDevice(wrapper);
        wrapper->SetHackerContext(context.get());
    }

    ~CallCounterDevice()
    {
        context.reset();
        if (native)
            native->Release();
        if (wrapper)
            wrapper->Release();
        if (runtime)
            FreeLibrary(runtime);
    }
};

static void install_counter_shaders(ID3D11Device1* device, ID3D11DeviceContext1* context)
{
    const char vertex[] = "float4 main(uint i:SV_VertexID):SV_Position{return float4(0,0,0,1);}";
    const char compute[] = "[numthreads(1,1,1)] void main(uint3 i:SV_DispatchThreadID){}";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3D11VertexShader> vs;
    require_call_counter(SUCCEEDED(D3DCompile(vertex, sizeof(vertex), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &code, nullptr)), "compile actual vertex shader");
    require_call_counter(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
        nullptr, &vs)), "create actual vertex shader");
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    code.Reset();
    ComPtr<ID3D11ComputeShader> cs;
    require_call_counter(SUCCEEDED(D3DCompile(compute, sizeof(compute), nullptr, nullptr, nullptr,
        "main", "cs_5_0", 0, 0, &code, nullptr)), "compile actual compute shader");
    require_call_counter(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
        nullptr, &cs)), "create actual compute shader");
    context->CSSetShader(cs.Get(), nullptr, 0);
}

void test_dx11_draw_dispatch_regression()
{
    std::fputs("Testing real WARP draw and dispatch counters\n", stderr);
    require_call_counter(G->mShaderOverrideMap.empty() && shader_regex_groups.empty(),
        "counter fixture starts without unrelated shader overrides");
    const auto saved_hunting = G->hunting;
    const auto saved_marking = G->marking_mode;
    const auto saved_selected_compute = G->mSelectedComputeShader;
    const bool saved_fix = G->fix_enabled;
    const bool saved_dump = G->DumpUsage;
    G->hunting = HUNTING_MODE_DISABLED;
    G->fix_enabled = true;
    {
        CallCounterDevice fixture;
        auto& context = *fixture.context;
        require_call_counter(context.GetDrawNumber() == 0 && context.GetDispatchNumber() == 0,
            "new context is zero before its first Present");
        install_counter_shaders(fixture.device, fixture.native);
        ComPtr<ID3D11DeviceContext1> deferred;
        require_call_counter(SUCCEEDED(fixture.device->CreateDeferredContext1(0, &deferred)),
            "create independent real deferred context");
        HackerContext second(fixture.device, deferred.Get());
        second.SetHackerDevice(fixture.wrapper);
        install_counter_shaders(fixture.device, deferred.Get());
        require_call_counter(second.GetDrawNumber() == 0 && second.GetDispatchNumber() == 0,
            "second context starts independently at zero");

        const std::wstring ini_namespace;
        CommandListScope scope;
        const std::wstring draw_text = L"draw_number";
        const std::wstring dispatch_text = L"dispatch_number";
        const std::wstring sum_text = L"draw_number + dispatch_number";
        CommandListExpression draws, dispatches, sum;
        require_call_counter(draws.parse(&draw_text, &ini_namespace, &scope) &&
            dispatches.parse(&dispatch_text, &ini_namespace, &scope) &&
            sum.parse(&sum_text, &ini_namespace, &scope), "parse runtime counter expressions");
        for (auto* expression : {&draws, &dispatches, &sum}) {
            require_call_counter(!expression->static_evaluate(nullptr) &&
                !expression->static_evaluate(nullptr, nullptr, true), "context counters cannot be statically snapshotted");
            expression->optimise(nullptr);
            require_call_counter(!expression->static_evaluate(nullptr), "optimisation keeps counters dynamic");
        }
        CommandListState state;
        state.mHackerContext = &context;
        require_call_counter(sum.evaluate(&state) == 0.0f, "initial runtime expression is zero");
        context.Draw(3, 0);
        context.DrawInstanced(3, 1, 0, 0);
        context.Dispatch(1, 1, 1);
        require_call_counter(context.GetDrawNumber() == 2 && context.GetDispatchNumber() == 1 &&
            draws.evaluate(&state) == 2.0f && dispatches.evaluate(&state) == 1.0f && sum.evaluate(&state) == 3.0f,
            "real wrapped draw and dispatch calls independently increment runtime counters");
        require_call_counter(second.GetDrawNumber() == 0 && second.GetDispatchNumber() == 0,
            "first context calls do not modify second context");
        second.Dispatch(1, 1, 1);
        require_call_counter(second.GetDispatchNumber() == 1 && context.GetDispatchNumber() == 1,
            "deferred context has its own call counter");

        // The hunting skip is a real production branch, before native Dispatch.
        G->hunting = HUNTING_MODE_ENABLED;
        G->DumpUsage = false;
        G->marking_mode = MarkingMode::SKIP;
        G->mSelectedComputeShader = 0;
        context.Dispatch(1, 1, 1);
        require_call_counter(context.GetDispatchNumber() == 2, "skipped dispatches still count as processed calls");
        G->hunting = HUNTING_MODE_DISABLED;

        // Present uses this production reset method. This checks its behavior,
        // without claiming that a full swapchain Present has been exercised.
        context.ResetCallCounters();
        require_call_counter(context.GetDrawNumber() == 0 && context.GetDispatchNumber() == 0 &&
            sum.evaluate(&state) == 0.0f && second.GetDispatchNumber() == 1, "reset affects only the selected context");
        context.Draw(3, 0);
        require_call_counter(draws.evaluate(&state) == 1.0f && dispatches.evaluate(&state) == 0.0f,
            "counting resumes after frame reset");
        context.ResetCallCounters();
        context.ResetCallCounters();
        require_call_counter(sum.evaluate(&state) == 0.0f, "repeated reset is safe");
        require_call_counter(std::isnan(draws.evaluate(nullptr)), "missing runtime state fails safely");
        state.mHackerContext = nullptr;
        require_call_counter(std::isnan(dispatches.evaluate(&state)), "missing context fails safely");
        CommandListExpression invalid_draw, invalid_dispatch;
        require_call_counter(!invalid_draw.parse(&draw_text, &ini_namespace, nullptr) &&
            !invalid_dispatch.parse(&dispatch_text, &ini_namespace, nullptr), "configuration without command scope rejects counters");
        deferred->ClearState();
        fixture.native->ClearState();
        fixture.native->Flush();
    }
    G->hunting = saved_hunting;
    G->marking_mode = saved_marking;
    G->mSelectedComputeShader = saved_selected_compute;
    G->fix_enabled = saved_fix;
    G->DumpUsage = saved_dump;
}
