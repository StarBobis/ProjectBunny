#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"
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
    for (float invalid : {float(values.size()), -1.0f, std::numeric_limits<float>::infinity(),
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
} // namespace

void test_dx11_store_structured_regression()
{
    try {
        StructuredStoreDevice device;
        StoreVariables variables; variables.initialize();
        test_structured_stride(device, variables, 12);
        test_structured_stride(device, variables, 16);
        test_raw_window(device, variables);
        std::fputs("PASS: real structured stride12/16 Store alignment, windows, bounds and RAW fallback\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL structured Store: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
