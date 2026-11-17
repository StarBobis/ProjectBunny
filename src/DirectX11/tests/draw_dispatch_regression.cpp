#include "HackerContext.h"
#include "HackerDevice.h"
#include "globals.h"
#include "ShaderRegex.h"

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

class CountShaderRuleCommand final : public CommandListCommand {
    unsigned& count;
public:
    explicit CountShaderRuleCommand(unsigned& count) : count(count) {}
    void run(CommandListState*) override { ++count; }
};

static void test_cached_shader_checks(CallCounterDevice& fixture)
{
    const auto previous_mode = Profiling::mode.load();
    const bool previous_shader_cache = G->CACHE_SHADERS;
    G->CACHE_SHADERS = false;
    auto& group = shader_regex_groups[L"stage cache regression"];
    G->mShaderOverrideMap.try_emplace(1);

    const char source[] = "float4 main(uint i:SV_VertexID):SV_Position{return float4(i,0,0,1);}";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3D11VertexShader> vs;
    require_call_counter(SUCCEEDED(D3DCompile(source, sizeof(source), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &code, nullptr)), "compile stage cache test shader");
    require_call_counter(SUCCEEDED(fixture.wrapper->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
        nullptr, &vs)), "register stage cache test shader through the wrapper");
    const auto hash = G->mShaders.at(vs.Get());
    require_call_counter(hash != 1 && !G->mShaderOverrideMap.count(hash), "bound shader initially misses every override");
    fixture.context->VSSetShader(vs.Get(), nullptr, 0);

    Profiling::clear();
    Profiling::mode = Profiling::Mode::SUMMARY;
    fixture.context->Draw(3, 0);
    const unsigned regex_lookups = Profiling::shader_reload_lookup_overhead.count;
    const unsigned override_lookups = Profiling::shaderoverride_lookup_overhead.count;
    require_call_counter(regex_lookups > 0 && override_lookups > 0, "first draw checks both registries");
    for (unsigned draw = 0; draw < 8; ++draw)
        fixture.context->Draw(3, 0);
    require_call_counter(Profiling::shader_reload_lookup_overhead.count == regex_lookups &&
        Profiling::shaderoverride_lookup_overhead.count == override_lookups,
        "stable shader skips both the processed regex lookup and the override miss");

    unsigned pre = 0, post = 0;
    group.command_list.commands.push_back(std::make_shared<CountShaderRuleCommand>(pre));
    group.post_command_list.commands.push_back(std::make_shared<CountShaderRuleCommand>(post));
    group.link_command_lists_and_filter_index(hash);
    fixture.context->Draw(3, 0);
    require_call_counter(pre == 1 && post == 1, "dynamic link replaces a cached miss without rebinding");

    CleanupShaderMaps(vs.Get());
    const unsigned after_cleanup = Profiling::shader_reload_lookup_overhead.count;
    fixture.context->Draw(3, 0);
    require_call_counter(Profiling::shader_reload_lookup_overhead.count == after_cleanup + 1,
        "retired shader metadata invalidates the processed-stage cache");

    Profiling::clear();
    Profiling::mode = previous_mode;
    fixture.context->VSSetShader(nullptr, nullptr, 0);
    G->mShaderOverrideMap.clear();
    shader_regex_groups.clear();
    G->CACHE_SHADERS = previous_shader_cache;
}

static void test_draw_indexed_literal_args(CallCounterDevice& fixture)
{
    const char vs_source[] = "struct V{float4 p:SV_Position;float v:VALUE;};"
        "V main(float value:VALUE){V o;o.p=float4(0,0,0,1);o.v=value;return o;}";
    const char gs_source[] = "struct V{float4 p:SV_Position;float v:VALUE;};"
        "[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> output){output.Append(input[0]);}";
    ComPtr<ID3DBlob> vs_code, gs_code;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11InputLayout> layout;
    const D3D11_INPUT_ELEMENT_DESC element{"VALUE", 0, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    const D3D11_SO_DECLARATION_ENTRY declaration{0, "VALUE", 0, 0, 1, 0};
    const UINT stride = sizeof(float), zero = 0;
    require_call_counter(SUCCEEDED(D3DCompile(vs_source, sizeof(vs_source), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vs_code, nullptr)) && SUCCEEDED(fixture.device->CreateVertexShader(
        vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs)) && SUCCEEDED(fixture.device->CreateInputLayout(
        &element, 1, vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &layout)), "indexed literals: real VS and input layout");
    require_call_counter(SUCCEEDED(D3DCompile(gs_source, sizeof(gs_source), nullptr, nullptr, nullptr,
        "main", "gs_5_0", 0, 0, &gs_code, nullptr)) && SUCCEEDED(fixture.device->CreateGeometryShaderWithStreamOutput(
        gs_code->GetBufferPointer(), gs_code->GetBufferSize(), &declaration, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
        nullptr, &gs)), "indexed literals: real stream output shader");

    const float vertices[] = {10, 20, 30, 40, 50, 60, 70, 80};
    const uint16_t indices[] = {7, 4, 2, 6, 3, 5, 7};
    std::array<float, 12> initial;
    initial.fill(-1.0f);
    const auto make_buffer = [&](UINT bytes, UINT flags, const void* data) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = bytes;
        desc.BindFlags = flags;
        D3D11_SUBRESOURCE_DATA source{};
        source.pSysMem = data;
        ComPtr<ID3D11Buffer> buffer;
        require_call_counter(SUCCEEDED(fixture.device->CreateBuffer(&desc, &source, &buffer)), "indexed literals: native buffer");
        return buffer;
    };
    auto vb = make_buffer(sizeof(vertices), D3D11_BIND_VERTEX_BUFFER, vertices);
    auto ib = make_buffer(sizeof(indices), D3D11_BIND_INDEX_BUFFER, indices);
    auto so = make_buffer(sizeof(initial), D3D11_BIND_STREAM_OUTPUT, initial.data());
    auto* context = fixture.native;
    context->ClearState();
    context->IASetInputLayout(layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &zero);
    context->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->GSSetShader(gs.Get(), nullptr, 0);
    context->SOSetTargets(1, so.GetAddressOf(), &zero);

    CommandListScope scope;
    scope.emplace_front();
    CommandListVariable start(L"$draw_index_start", 1, VariableFlags::NONE);
    scope.front().emplace(start.name, &start);
    const std::wstring ini_namespace;
    const auto parse_draw = [&](std::wstring args) {
        CommandList list;
        list.scope = &scope;
        require_call_counter(ParseCommandListGeneralCommands(L"CommandListIndexedLiterals", L"drawindexed", &args,
            &list, &list, nullptr, &ini_namespace) && list.commands.size() == 1, "indexed literals: real INI parser");
        auto command = std::dynamic_pointer_cast<DrawCommand>(list.commands.front());
        require_call_counter(command != nullptr, "indexed literals: parsed actual DrawCommand");
        return command;
    };
    auto negative = parse_draw(L"3, 2, -2");
    auto positive = parse_draw(L"2, +3, 1");
    auto dynamic = parse_draw(L"1, $draw_index_start, -1");
    require_call_counter(negative->optimise(fixture.wrapper) && positive->optimise(fixture.wrapper) &&
        !dynamic->optimise(fixture.wrapper) && !parse_draw(L"auto")->optimise(fixture.wrapper) &&
        !parse_draw(L"3, 2, 2147483648")->optimise(fixture.wrapper),
        "indexed literals: fold signed literals only within the native integer range");

    ShaderResourceBatchStats stats;
    CommandListState state;
    state.mHackerContext = fixture.context.get();
    state.mHackerDevice = fixture.wrapper;
    state.mOrigContext1 = context;
    state.mOrigDevice1 = fixture.device;
    state.slot_batch_stats = &stats;
    const unsigned before = Profiling::injected_draw_calls;
    negative->run(&state);
    dynamic->run(&state);
    positive->run(&state);
    start.fval = 4;
    dynamic->run(&state);
    negative->run(&state);
    require_call_counter(stats.literal_indexed_draws == 3 && Profiling::injected_draw_calls == before + 5,
        "indexed literals: submissions are preserved and only the literal executions hit the fast path");
    Profiling::injected_draw_calls = before;

    context->SOSetTargets(0, nullptr, nullptr);
    D3D11_BUFFER_DESC staging_desc{};
    staging_desc.ByteWidth = sizeof(initial);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_call_counter(SUCCEEDED(fixture.device->CreateBuffer(&staging_desc, nullptr, &staging)), "indexed literals: staging buffer");
    context->CopyResource(staging.Get(), so.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_call_counter(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "indexed literals: actual GPU output map");
    std::array<float, 12> result;
    std::memcpy(result.data(), mapped.pData, sizeof(result));
    context->Unmap(staging.Get(), 0);
    require_call_counter(result == std::array<float, 12>{10, 50, 20, 40, 80, 50, 30, 10, 50, 20, -1, -1},
        "indexed literals: real draw order, count, first index, signed base vertex and dynamic values are preserved");
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
        test_cached_shader_checks(fixture);
        test_draw_indexed_literal_args(fixture);
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
