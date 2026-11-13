#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class StoreDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    StoreDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate Store system D3D runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load Store system D3D11");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve Store device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, base.GetAddressOf(), nullptr, immediate.GetAddressOf())), "create Store WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query Store DX11 interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~StoreDevice()
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
    const std::wstring output_name = L"$store_readback_regression_output";
    const std::wstring index_name = L"$store_readback_regression_index";
    const std::wstring locked_name = L"$store_readback_regression_locked";
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

void run_store(StoreDevice& device, CommandList& commands)
{
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
}

void execute(StoreDevice& device, const std::wstring& arguments)
{
    CommandList commands;
    check(parse_store(commands, arguments) && commands.commands.size() == 1, "parse actual Store command");
    run_store(device, commands);
}

ComPtr<ID3D11Buffer> make_buffer(StoreDevice& device, const std::vector<float>& values, UINT flags)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = flags;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = values.data();
    ComPtr<ID3D11Buffer> result;
    check(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, result.GetAddressOf())), "create actual Store default buffer");
    return result;
}

float read_staging(StoreDevice& device, ID3D11Buffer* staging)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    check(SUCCEEDED(device.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "map actual shared Store staging");
    float value;
    std::memcpy(&value, mapped.pData, sizeof(value));
    device.context->Unmap(staging, 0);
    return value;
}

ULONG references(IUnknown* object)
{
    object->AddRef();
    return object->Release();
}

void test_store_success(StoreDevice& device, StoreVariables& variables)
{
    std::vector<float> values(128);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<float>(i) + .25f;
    auto buffer = make_buffer(device, values, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VERTEX_BUFFER);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    view_desc.Format = DXGI_FORMAT_R32_FLOAT;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view_desc.Buffer.FirstElement = 16;
    view_desc.Buffer.NumElements = 8;
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(buffer.Get(), &view_desc, view.GetAddressOf())),
        "create offset buffer SRV for Store absolute-index regression");
    ID3D11ShaderResourceView* bound_view = view.Get();
    device.context->VSSetShaderResources(0, 1, &bound_view);

    CommandList dynamic;
    check(parse_store(dynamic, variables.output_name + L", vs-t0, " + variables.index_name + L" + 1"),
        "Store parser accepts dynamic offset expression");
    G->user_config_dirty = 0;
    run_store(device, dynamic);
    check(variables.output().fval == values[1] && G->user_config_dirty,
        "Store offset remains backing-buffer float index despite nonzero SRV view offset");
    ID3D11Buffer* shared = device.wrapper->GetReadbackBuffer(sizeof(float));
    check(shared != nullptr, "Store exposes the shared four-byte cache entry");
    D3D11_BUFFER_DESC staging_desc = {};
    shared->GetDesc(&staging_desc);
    check(staging_desc.ByteWidth == sizeof(float) && staging_desc.Usage == D3D11_USAGE_STAGING &&
        staging_desc.CPUAccessFlags == D3D11_CPU_ACCESS_READ && read_staging(device, shared) == values[1],
        "Store copies the selected float to the real four-byte staging buffer");
    G->user_config_dirty = 0;
    run_store(device, dynamic);
    check(!G->user_config_dirty && variables.output().fval == values[1], "unchanged Store value does not dirty persistence");
    G->user_config_dirty = 2;
    run_store(device, dynamic);
    check(G->user_config_dirty == 2, "unchanged Store preserves unknown-setting cleanup bit without adding dirty bit");
    G->user_config_dirty = 3;
    run_store(device, dynamic);
    check(G->user_config_dirty == 3, "unchanged Store preserves existing combined dirty flags");
    variables.index().fval = 63;
    G->user_config_dirty = 2;
    run_store(device, dynamic);
    check(variables.output().fval == values[64] && G->user_config_dirty == 3 &&
        device.wrapper->GetReadbackBuffer(sizeof(float)) == shared && read_staging(device, shared) == values[64],
        "same parsed Store re-evaluates offset and reuses shared staging");
    execute(device, variables.output_name + L", vs-t0, 127");
    check(variables.output().fval == values.back(), "Store reads last complete float beyond current SRV window");
    execute(device, variables.output_name + L", vs-t0->hashregion((1+1)*4,(2*4)), 3");
    check(variables.output().fval == values[3], "Store source supports balanced member argument syntax");
    execute(device, variables.output_name + L", vs-t0, 1.9");
    check(variables.output().fval == values[1], "fractional Store index preserves upstream truncation to float element");

    const ULONG buffer_refs = references(buffer.Get());
    const ULONG view_refs = references(view.Get());
    for (int i = 0; i < 24; ++i)
        execute(device, variables.output_name + L", vs-t0, 2");
    check(references(buffer.Get()) == buffer_refs && references(view.Get()) == view_refs,
        "repeated successful Store releases both acquired resource and source-view references");

    for (const wchar_t* offset : {L"-1", L"-0.5", L"nan", L"inf", L"-inf", L"1e30", L"4294967295", L"128"}) {
        const float previous = variables.output().fval;
        G->user_config_dirty = 0;
        execute(device, variables.output_name + L", vs-t0, " + offset);
        check(variables.output().fval == previous && !G->user_config_dirty,
            "Store rejects nonfinite, negative, overflow and physical out-of-range indices without publishing value");
    }
    check(references(buffer.Get()) == buffer_refs && references(view.Get()) == view_refs,
        "invalid Store offsets release any acquired resource/view references");

    std::vector<float> constants(512);
    for (size_t i = 0; i < constants.size(); ++i)
        constants[i] = 1000.5f + static_cast<float>(i);
    auto cb = make_buffer(device, constants, D3D11_BIND_CONSTANT_BUFFER);
    ID3D11Buffer* bound_cb = cb.Get();
    UINT first = 16, count = 16;
    device.context->VSSetConstantBuffers1(0, 1, &bound_cb, &first, &count);
    execute(device, variables.output_name + L", vs-cb0, 0");
    check(variables.output().fval == constants[0], "nonzero constant-buffer window does not shift absolute Store index zero");
    execute(device, variables.output_name + L", vs-cb0, 64");
    check(variables.output().fval == constants[64], "Store CB offset is float index rather than byte offset");
    execute(device, variables.output_name + L", vs-cb0, 511");
    check(variables.output().fval == constants.back(), "Store physical CB bound is independent of visible NumConstants");

    // A default-constructed command must be a safe no-op, including with no state.
    StoreCommand empty;
    const float previous = variables.output().fval;
    empty.run(nullptr);
    check(variables.output().fval == previous, "default Store cannot publish an uninitialized target");

    CommandList runtime_locked;
    check(parse_store(runtime_locked, variables.output_name + L", vs-t0, 4"), "parse unlocked target before runtime guard");
    variables.output().flags |= VariableFlags::LOCKED;
    G->user_config_dirty = 0;
    run_store(device, runtime_locked);
    check(variables.output().fval == previous && !G->user_config_dirty, "runtime LOCKED guard prevents readback assignment and dirty flag");
    variables.output().flags &= static_cast<VariableFlags>(~VariableFlags::LOCKED);
}

void test_store_failures(StoreDevice& device, StoreVariables& variables)
{
    for (const std::wstring& arguments : {variables.locked_name + L", vs-t0, 0", variables.output_name + L", vs-t0",
        variables.output_name + L", vs-t0,", variables.output_name + L", vs-t0, ()", variables.output_name + L", vs-t0, 1, 2",
        variables.output_name + L", copy vs-t0, 0", variables.output_name + L", vs-t0->hashregion(1,), 0"}) {
        CommandList rejected;
        check(!parse_store(rejected, arguments) && rejected.commands.empty(), "Store parser rejects locked, incomplete and malformed argument lists");
    }
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = texture_desc.Height = texture_desc.MipLevels = texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32_FLOAT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    float pixel = 88;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = &pixel;
    initial.SysMemPitch = sizeof(float);
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateTexture2D(&texture_desc, &initial, texture.GetAddressOf())) &&
        SUCCEEDED(device.native->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())), "create actual non-buffer Store source");
    ID3D11ShaderResourceView* bound = view.Get();
    device.context->VSSetShaderResources(1, 1, &bound);
    const float previous = variables.output().fval;
    const ULONG texture_refs = references(texture.Get());
    const ULONG view_refs = references(view.Get());
    G->user_config_dirty = 0;
    execute(device, variables.output_name + L", vs-t1, 0");
    execute(device, variables.output_name + L", vs-t7, 0");
    check(variables.output().fval == previous && !G->user_config_dirty &&
        references(texture.Get()) == texture_refs && references(view.Get()) == view_refs,
        "texture and unbound source failures preserve variable and release acquired COM references");
}

void test_store_deferred(StoreDevice& device, StoreVariables& variables)
{
    std::vector<float> values = {301.5f, 302.5f, 303.5f, 304.5f};
    auto buffer = make_buffer(device, values, D3D11_BIND_SHADER_RESOURCE);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    view_desc.Format = DXGI_FORMAT_R32_FLOAT;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view_desc.Buffer.NumElements = static_cast<UINT>(values.size());
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(buffer.Get(), &view_desc, view.GetAddressOf())), "create deferred Store buffer view");
    ComPtr<ID3D11DeviceContext1> deferred;
    check(SUCCEEDED(device.native->CreateDeferredContext1(0, deferred.GetAddressOf())), "create actual deferred Store context");
    HackerContext wrapper(device.native.Get(), deferred.Get());
    wrapper.SetHackerDevice(device.device);
    ID3D11Buffer* staging = wrapper.GetReadbackBuffer(sizeof(float));
    check(staging != nullptr, "allocate deferred-context-owned readback cache for no-copy observation");
    D3D11_BOX box = {};
    box.left = 2 * sizeof(float);
    box.right = box.left + sizeof(float);
    box.bottom = box.back = 1;
    device.context->CopySubresourceRegion(staging, 0, 0, 0, 0, buffer.Get(), 0, &box);
    check(read_staging(device, staging) == values[2], "prime deferred cache through actual immediate GPU copy");
    ID3D11ShaderResourceView* bound = view.Get();
    deferred->VSSetShaderResources(0, 1, &bound);
    CommandList command;
    check(parse_store(command, variables.output_name + L", vs-t0, 0"), "parse deferred Store command");
    const float previous = variables.output().fval;
    G->user_config_dirty = 0;
    RunCommandList(device.device, &wrapper, &command, nullptr, false);
    ComPtr<ID3D11CommandList> recorded;
    check(SUCCEEDED(deferred->FinishCommandList(FALSE, recorded.GetAddressOf())), "finish real deferred commands after Store rejection");
    device.context->ExecuteCommandList(recorded.Get(), TRUE);
    check(variables.output().fval == previous && !G->user_config_dirty && read_staging(device, staging) == values[2],
        "deferred Store rejection records no GPU copy and preserves output even after replay");
}

void test_store_device_mismatch(StoreDevice& device, StoreVariables& variables)
{
    StoreDevice other;
    CommandList commands;
    check(parse_store(commands, variables.output_name + L", vs-t0, 0"), "parse Store for real-device mismatch guards");
    CommandListState state;
    state.mHackerDevice = other.device;
    state.mHackerContext = other.wrapper.get();
    state.mOrigDevice1 = other.native.Get();
    state.mOrigContext1 = device.context.Get();
    const float previous = variables.output().fval;
    G->user_config_dirty = 0;
    // All objects are real WARP objects; deliberately cross their state fields
    // to exercise validation before a void GPU copy could silently fail.
    commands.commands[0]->run(&state);
    check(variables.output().fval == previous && !G->user_config_dirty,
        "Store source/context cannot be copied using another state's device");
    state.mHackerDevice = device.device;
    state.mOrigDevice1 = device.native.Get();
    commands.commands[0]->run(&state);
    check(variables.output().fval == previous && !G->user_config_dirty,
        "Store cannot publish stale data from another context's staging device");
}

void run_store_readback_regression()
{
    StoreDevice device;
    StoreVariables variables;
    variables.initialize();
    test_store_success(device, variables);
    test_store_failures(device, variables);
    test_store_device_mismatch(device, variables);
    test_store_deferred(device, variables);
}
} // namespace

void test_dx11_store_readback_regression()
{
    try {
        run_store_readback_regression();
        std::fputs("PASS: real Store parser, four-byte shared readback, bounds and failures\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: Store regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
