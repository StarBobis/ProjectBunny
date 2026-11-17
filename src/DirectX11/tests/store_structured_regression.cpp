#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"
#include "cache_policy_test_scope.h"
#include <d3d11_4.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>
#include <limits>

namespace {
using Microsoft::WRL::ComPtr;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

class StructuredStoreDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    StructuredStoreDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate Store system D3D runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load Store system D3D11");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve Store device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        HRESULT result = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
            D3D11_SDK_VERSION, base.GetAddressOf(), nullptr, immediate.GetAddressOf());
        if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
            result = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, base.GetAddressOf(), nullptr, immediate.GetAddressOf());
        check(SUCCEEDED(result), "create structured Store WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query Store DX11 interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~StructuredStoreDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class StoreVariables {
public:
    const std::wstring output_name = L"$store_structured_regression_output";
    const std::wstring index_name = L"$store_structured_regression_index";
    const std::wstring locked_name = L"$store_structured_regression_locked";
    std::vector<std::wstring> names;
    std::vector<CommandListVariable*> owned_persistent;
    int saved_dirty = G->user_config_dirty;

    ~StoreVariables()
    {
        for (auto* owned : owned_persistent)
            persistent_variables.erase(std::remove(persistent_variables.begin(), persistent_variables.end(), owned), persistent_variables.end());
        for (const auto& name : names)
            command_list_globals.erase(name);
        G->user_config_dirty = saved_dirty;
    }

    void initialize()
    {
        for (const auto& name : {output_name, index_name, locked_name})
            check(command_list_globals.count(name) == 0, "Store global fixture must be unique");
        std::wstring output = output_name;
        float initial = -100;
        auto* registered = RegisterGlobalVariable(output, &initial,
            static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::PERSIST));
        check(registered != nullptr, "register real persistent Store output");
        names.push_back(output);
        owned_persistent.push_back(registered);
        command_list_globals.emplace(index_name, CommandListVariable(index_name, 0, VariableFlags::GLOBAL));
        names.push_back(index_name);
        command_list_globals.emplace(locked_name, CommandListVariable(locked_name, 17,
            static_cast<VariableFlags>(VariableFlags::GLOBAL | VariableFlags::LOCKED)));
        names.push_back(locked_name);
    }

    CommandListVariable& output() { return command_list_globals.at(output_name); }
    CommandListVariable& index() { return command_list_globals.at(index_name); }
};

bool parse_store(CommandList& commands, const std::wstring& arguments)
{
    CommandListScope scope;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = arguments;
    const bool result = ParseCommandListGeneralCommands(L"CommandListStoreRegression", L"store", &value,
        &commands, &commands, nullptr, &ini_namespace);
    commands.scope = nullptr;
    return result;
}

void run_store(StructuredStoreDevice& device, CommandList& commands)
{
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

void execute(StructuredStoreDevice& device, const std::wstring& arguments)
{
    CommandList commands;
    check(parse_store(commands, arguments) && commands.commands.size() == 1, "parse actual Store command");
    run_store(device, commands);
}


void verify_debug_errors(ID3D11InfoQueue* queue)
{
    if (!queue) return;
    const UINT64 count = queue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T bytes = 0;
        check(SUCCEEDED(queue->GetMessage(i, nullptr, &bytes)), "query structured-copy diagnostic size");
        auto storage = std::make_unique<unsigned char[]>(bytes);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.get());
        check(SUCCEEDED(queue->GetMessage(i, message, &bytes)), "query structured-copy diagnostic");
        if (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR || message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION) {
            std::fprintf(stderr, "D3D11 structured Store error: %s\n", message->pDescription);
            check(false, "real debug layer must accept aligned structured-buffer copy");
        }
    }
}

void test_structured_stride(StructuredStoreDevice& device, StoreVariables& variables, UINT stride)
{
    std::vector<float> values((stride / sizeof(float)) * 3);
    for (size_t i = 0; i < values.size(); ++i) values[i] = float(stride * 100 + i) + 0.25f;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = values.data();
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, &buffer)), "create actual structured buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 1; srv.Buffer.NumElements = 2;
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(buffer.Get(), &srv, &view)), "create structured view window");
    auto* bound = view.Get(); device.context->VSSetShaderResources(0, 1, &bound);
    CommandList command;
    check(parse_store(command, variables.output_name + L", vs-t0, " + variables.index_name), "parse dynamic structured Store");
    ID3D11Buffer* staging = device.wrapper->GetReadbackBuffer(stride);
    check(staging != nullptr, "acquire actual full-structure staging bucket");
    ComPtr<ID3D11InfoQueue> queue;
    device.native.As(&queue);
    if (queue) queue->ClearStoredMessages();
    for (size_t i = 0; i < values.size(); ++i) {
        variables.index().fval = static_cast<float>(i);
        variables.output().fval = -100;
        G->user_config_dirty = 0;
        run_store(device, command);
        check(variables.output().fval == values[i] && G->user_config_dirty,
            "structured Store reads first/middle/final float by absolute index irrespective of SRV window");
        G->user_config_dirty = 0;
        run_store(device, command);
        check(!G->user_config_dirty, "identical structured read does not dirty persistence");
        check(device.wrapper->GetReadbackBuffer(stride) == staging, "complete-structure staging buffer reused");
    }
    variables.index().fval = static_cast<float>(values.size());
    variables.output().fval = -713;
    G->user_config_dirty = 0;
    run_store(device, command);
    check(variables.output().fval == 0 && G->user_config_dirty,
        "structured Store: valid UINT physical out-of-bounds publishes upstream zero and marks persistence");
    for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), (std::numeric_limits<float>::max)()}) {
        variables.index().fval = invalid;
        const float before = variables.output().fval;
        G->user_config_dirty = 0;
        run_store(device, command);
        check(variables.output().fval == before && !G->user_config_dirty,
            "invalid structured read preserves prior variable and dirty state");
    }
    for (float& value : values) value += 1000;
    device.context->UpdateSubresource(buffer.Get(), 0, nullptr, values.data(), 0, 0);
    variables.index().fval = static_cast<float>(values.size() - 1);
    run_store(device, command);
    check(variables.output().fval == values.back(), "reused structured staging sees updated buffer content");
    verify_debug_errors(queue.Get());
    std::fprintf(stderr, "Structured Store stride%u debug layer: %s\n", stride, queue ? "checked" : "unavailable");
}

void test_raw_window(StructuredStoreDevice& device, StoreVariables& variables)
{
    float values[] = {11.25f, 22.25f, 33.25f, 44.25f, 55.25f, 66.25f, 77.25f, 88.25f};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values); desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = values;
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, &buffer)), "create actual RAW buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW; srv.BufferEx.FirstElement = 4; srv.BufferEx.NumElements = 4;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11InfoQueue> queue;
    device.native.As(&queue);
    if (queue) queue->ClearStoredMessages();
    const HRESULT view_result = device.native->CreateShaderResourceView(buffer.Get(), &srv, &view);
    if (FAILED(view_result)) {
        std::fprintf(stderr, "RAW SRV creation HRESULT=0x%08lx\n", static_cast<unsigned long>(view_result));
        verify_debug_errors(queue.Get());
    }
    check(SUCCEEDED(view_result), "create RAW view window");
    auto* bound = view.Get(); device.context->VSSetShaderResources(0, 1, &bound);
    execute(device, variables.output_name + L", vs-t0, 0");
    check(variables.output().fval == values[0], "RAW Store retains original absolute backing-buffer index");
    execute(device, variables.output_name + L", vs-t0, 7");
    check(variables.output().fval == values[7], "RAW last complete float remains valid");
    bound = nullptr; device.context->VSSetShaderResources(0, 1, &bound);
    G->user_config_dirty = 0;
    execute(device, variables.output_name + L", vs-t0, 0");
    check(variables.output().fval == values[7] && !G->user_config_dirty,
        "source unbind cannot reuse stale structured/RAW staging data");
    verify_debug_errors(queue.Get());
}

class RewriteStoreBuffer final : public CommandListCommand {
    ID3D11Buffer* buffer;
    const float* values;
public:
    RewriteStoreBuffer(ID3D11Buffer* buffer, const float* values) : buffer(buffer), values(values) {}
    void run(CommandListState* state) override
    {
        state->mOrigContext1->UpdateSubresource(buffer, 0, nullptr, values, 0, 0);
    }
};

void test_paired_store(StructuredStoreDevice& device, StoreVariables& variables)
{
    DataCacheTestScope policy(DataCacheBindFlags::INVALID);
    const auto saved_mode = Profiling::mode.exchange(Profiling::Mode::NONE);
    const bool saved_analysis = G->analyse_frame;
    G->analyse_frame = false;
    static constexpr wchar_t name[] = L"resourcestore_pair_regression";
    const auto erase_resource = [](CustomResource*) { customResources.erase(name); };
    auto [it, inserted] = customResources.try_emplace(name);
    check(inserted, "paired Store resource fixture name is unique");
    std::unique_ptr<CustomResource, decltype(erase_resource)> resource(&it->second, erase_resource);

    float values[24]{};
    values[7] = static_cast<float>((23 << 14) | (45 << 7) | 67);
    values[15] = static_cast<float>((98 << 16) | (76 << 8) | 54);
    values[23] = 807.25f;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(values);
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = 32;
    D3D11_SUBRESOURCE_DATA input{};
    input.pSysMem = values;
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &input, &buffer)), "create actual health-bar-shaped structured buffer");
    resource->name = name;
    resource->resource = buffer.Get();
    buffer->AddRef();
    resource->device = device.native.Get();
    resource->is_null = false;
    resource->substantiated = true;

    ComPtr<ID3D11InfoQueue> queue;
    device.native.As(&queue);
    if (queue) queue->ClearStoredMessages();

    const auto build = [&](CommandList& commands, UINT second_index, const std::shared_ptr<CommandListCommand>& barrier = {}) {
        const std::wstring source = variables.output_name + L", " + name + L", ";
        check(parse_store(commands, source + L"7"), "parse actual first store");
        std::wstring rhs = L"(" + variables.output_name + L" >> 14) & 127";
        const std::wstring ns, raw = variables.index_name + L" = " + rhs;
        CommandListScope scope;
        commands.scope = &scope;
        check(ParseCommandListVariableAssignment(L"CommandListPairedStore", variables.index_name.c_str(), &rhs, &raw,
            &commands, &commands, nullptr, &ns), "parse a scalar reader of the first store's value");
        if (barrier)
            commands.commands.push_back(barrier);
        check(parse_store(commands, source + std::to_wstring(second_index)), "parse actual second store");
        merge_store_readbacks(&commands);
    };
    const auto execute_batch = [&](CommandList& commands) {
        ShaderResourceBatchStats stats;
        CommandListState state;
        state.mHackerDevice = device.device;
        state.mHackerContext = device.wrapper.get();
        state.mOrigDevice1 = device.native.Get();
        state.mOrigContext1 = device.context.Get();
        state.slot_batch_stats = &stats;
        for (const auto& command : commands.commands)
            command->run(&state);
        return stats;
    };

    CommandList paired;
    build(paired, 15);
    check(paired.commands.size() == 1, "scalar consumer between fixed stores is eligible for the real merge pass");
    auto stats = execute_batch(paired);
    check(stats.store_readback_maps == 1 && stats.store_readback_bytes == 64 &&
        variables.index().fval == 23 && variables.output().fval == values[15],
        "one native map publishes the same two values in their original order");

    G->analyse_frame = true;
    stats = execute_batch(paired);
    G->analyse_frame = false;
    check(stats.store_readback_maps == 2 && variables.index().fval == 23 && variables.output().fval == values[15],
        "frame analysis keeps the two original reads and results");

    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(device.context.As(&multithread))) {
        const BOOL was_protected = multithread->SetMultithreadProtected(TRUE);
        stats = execute_batch(paired);
        multithread->SetMultithreadProtected(was_protected);
        check(stats.store_readback_maps == 2 && variables.index().fval == 23 && variables.output().fval == values[15],
            "a multithread-protected context keeps the reads separate");
    }

    CommandList distant, invalid;
    build(distant, 23);
    stats = execute_batch(distant);
    check(stats.store_readback_maps == 2 && stats.store_readback_bytes == 64 && variables.output().fval == values[23],
        "distant structures retain separate narrow reads");
    build(invalid, 24);
    stats = execute_batch(invalid);
    check(stats.store_readback_maps == 1 && stats.store_readback_bytes == 32 && variables.output().fval == 0.0f,
        "out-of-range second store leaves the first read narrow and still publishes zero");

    float changed[24]{};
    changed[15] = 255.25f;
    CommandList writes_between;
    build(writes_between, 15, std::make_shared<RewriteStoreBuffer>(buffer.Get(), changed));
    check(writes_between.commands.size() == 4, "a GPU write separates the stores during optimization");
    stats = execute_batch(writes_between);
    check(stats.store_readback_maps == 2 && variables.index().fval == 23 && variables.output().fval == changed[15],
        "separated stores see the old and new GPU contents at their own positions");
    verify_debug_errors(queue.Get());
    G->analyse_frame = saved_analysis;
    Profiling::mode = saved_mode;
    std::fputs("PASS: paired health-bar Store uses one 64-byte read, preserves ordering and falls back across writes or gaps\n", stderr);
}
} // namespace

void test_dx11_store_structured_regression()
{
    try {
        StructuredStoreDevice device;
        StoreVariables variables; variables.initialize();
        test_structured_stride(device, variables, 12);
        test_structured_stride(device, variables, 16);
        test_raw_window(device, variables);
        test_paired_store(device, variables);
        std::fputs("PASS: real structured stride12/16 Store alignment, windows, bounds and RAW fallback\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL structured Store: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
