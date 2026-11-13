#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "HackerInputLayout.h"
#include "globals.h"

#include <D3DCompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class LayoutDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    LayoutDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate Windows runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load layout system D3D11");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve layout native device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, base.GetAddressOf(), nullptr, immediate.GetAddressOf())), "create layout WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query layout DX11 interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef(); // The wrapper owns the matching reference.
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~LayoutDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

bool parse(CommandList& commands, const wchar_t* lhs, const wchar_t* rhs)
{
    CommandListScope scope;
    commands.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    const bool result = ParseCommandListResourceCopyTargetDirective(L"CommandListLayoutRegression", lhs,
        &value, &commands, &ini_namespace);
    commands.scope = nullptr;
    return result;
}

void add(CommandList& commands, const wchar_t* lhs, const wchar_t* rhs)
{
    check(parse(commands, lhs, rhs), "parse production layout override");
}

ComPtr<HackerInputLayout> bound_layout(HackerContext& context)
{
    ComPtr<ID3D11InputLayout> bound;
    context.IAGetInputLayout(bound.GetAddressOf());
    ComPtr<HackerInputLayout> wrapped;
    check(bound && SUCCEEDED(bound.As(&wrapped)), "bound layout has production metadata");
    return wrapped;
}

void run_layout_override_regression()
{
    LayoutDevice device;
    const char shader[] = "float4 main(float4 a:POSITION0,float4 b:POSITION1,float4 c:TEXCOORD0,"
        "float4 d:TEXCOORD1,float4 e:COLOR0):SV_POSITION{return a+b+c+d+e;}";
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    check(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
        signature.GetAddressOf(), errors.GetAddressOf())), "compile actual layout signature");
    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"POSITION", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ComPtr<ID3D11InputLayout> original;
    check(SUCCEEDED(device.device->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
        signature->GetBufferSize(), original.GetAddressOf())), "create production wrapped input layout");
    {
        ComPtr<ID3D11InputLayout> game_second, custom_first, custom_second, game_after_custom;
        check(SUCCEEDED(device.device->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), game_second.GetAddressOf())) && game_second.Get() != original.Get(),
            "game layout creation does not reuse a wrapper for the same descriptor");
        check(SUCCEEDED(device.device->CreateCustomInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), custom_first.GetAddressOf())) &&
            SUCCEEDED(device.device->CreateCustomInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), custom_second.GetAddressOf())) && custom_first.Get() == custom_second.Get() &&
            custom_first.Get() != original.Get(), "custom layouts retain full-key cache independently of game layouts");
        check(SUCCEEDED(device.device->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), game_after_custom.GetAddressOf())) &&
            game_after_custom.Get() != custom_first.Get() && game_after_custom.Get() != game_second.Get(),
            "game creation bypasses an existing custom cache entry");
        const HRESULT validation = device.native->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), nullptr);
        check(device.device->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), nullptr) == validation &&
            device.device->CreateCustomInputLayout(elements, 5, signature->GetBufferPointer(),
            signature->GetBufferSize(), nullptr) == E_INVALIDARG,
            "game validation-only HRESULT and internal custom output requirement are preserved");
        device.wrapper->IASetInputLayout(original.Get());
        check(bound_layout(*device.wrapper).Get() == original.Get(), "first game wrapper binding tracked");
        device.wrapper->IASetInputLayout(game_second.Get());
        check(bound_layout(*device.wrapper).Get() == game_second.Get(), "second game wrapper binding tracked independently");
        device.wrapper->IASetInputLayout(nullptr);
    }
    device.wrapper->IASetInputLayout(original.Get());

    CommandList shrink;
    add(shrink, L"vb0->elementformat(position,0)", L"R32G32_FLOAT");
    RunCommandList(device.device, device.wrapper.get(), &shrink, nullptr, false);
    check(bound_layout(*device.wrapper).Get() == original.Get(), "native pre-list defers layout until draw");
    device.wrapper->OverrideInputLayout();
    {
        auto modified = bound_layout(*device.wrapper);
        const auto* actual = modified->GetElements();
        check(actual[0].Format == DXGI_FORMAT_R32G32_FLOAT && actual[1].AlignedByteOffset == 8 &&
            actual[3].AlignedByteOffset == 16, "format shrink realigns from original byte offset boundary");
        check(actual[2].AlignedByteOffset == 16 && actual[4].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT,
            "realignment preserves other input slots and APPEND descriptors");
    }
    device.wrapper->RestoreInputLayout();
    check(bound_layout(*device.wrapper).Get() == original.Get(), "restore original binding after override");

    CommandList grow;
    add(grow, L"vb0->elementformat(position,1)", L"R32G32B32A32_FLOAT");
    RunCommandList(device.device, device.wrapper.get(), &grow, nullptr, false);
    device.wrapper->OverrideInputLayout();
    {
        auto actual = bound_layout(*device.wrapper);
        check(actual->GetElements()[0].Format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
            actual->GetElements()[1].Format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
            actual->GetElements()[3].AlignedByteOffset == 32, "semantic index isolates matching element and growth shifts successors");
    }
    device.wrapper->RestoreInputLayout();

    CommandList offset;
    add(offset, L"vb1->elementoffset(texcoord,0)", L"40");
    CommandList nested;
    nested.commands.push_back(std::make_shared<RunLinkedCommandList>(&offset));
    RunCommandList(device.device, device.wrapper.get(), &nested, nullptr, false);
    device.wrapper->OverrideInputLayout();
    check(bound_layout(*device.wrapper)->GetElements()[2].AlignedByteOffset == 40 &&
        bound_layout(*device.wrapper)->GetElements()[3].AlignedByteOffset == 24,
        "nested command list offset override selects input slot and semantic index");
    device.wrapper->RestoreInputLayout();

    CommandList invalid_format;
    add(invalid_format, L"vb0->elementformat(position,0)", L"BC1_UNORM");
    RunCommandList(device.device, device.wrapper.get(), &invalid_format, nullptr, false);
    device.wrapper->OverrideInputLayout();
    check(bound_layout(*device.wrapper).Get() == original.Get(), "failed native layout creation preserves original binding");
    device.wrapper->RestoreInputLayout();

    for (const wchar_t* invalid : {L"", L"nan", L"inf", L"-1", L"4294967295", L"4294967296", L"1.5", L"12junk"}) {
        CommandList rejected;
        check(!parse(rejected, L"vb0->elementoffset(position,0)", invalid), "reject unsafe layout offset before conversion");
    }
    for (const wchar_t* invalid : {L"ib->elementoffset(position,0)", L"vb32->elementoffset(position,0)",
        L"vb0->elementoffset(bad-name,0)", L"vb0->elementoffset(position)", L"vb0->elementoffset(position,0,1)"}) {
        CommandList rejected;
        check(!parse(rejected, invalid, L"8"), "reject unsupported/malformed layout targets");
    }

    CommandList invalid_index;
    add(invalid_index, L"vb0->elementformat(position,-1)", L"R32G32_FLOAT");
    RunCommandList(device.device, device.wrapper.get(), &invalid_index, nullptr, false);
    device.wrapper->OverrideInputLayout();
    check(bound_layout(*device.wrapper).Get() == original.Get(), "negative evaluated semantic index cannot become wildcard");
    device.wrapper->RestoreInputLayout();

    ComPtr<ID3D11InputLayout> raw;
    check(SUCCEEDED(device.native->CreateInputLayout(elements, 5, signature->GetBufferPointer(),
        signature->GetBufferSize(), raw.GetAddressOf())), "create raw native layout for interop");
    device.wrapper->IASetInputLayout(raw.Get());
    RunCommandList(device.device, device.wrapper.get(), &shrink, nullptr, false);
    device.wrapper->OverrideInputLayout();
    ComPtr<ID3D11InputLayout> raw_bound;
    device.wrapper->IAGetInputLayout(raw_bound.GetAddressOf());
    check(raw_bound.Get() == raw.Get(), "native layout safely bypasses metadata-only override");
    device.wrapper->RestoreInputLayout();
    device.wrapper->IASetInputLayout(nullptr);
    RunCommandList(device.device, device.wrapper.get(), &shrink, nullptr, false);
    device.wrapper->OverrideInputLayout();
    raw_bound.Reset();
    device.wrapper->IAGetInputLayout(raw_bound.GetAddressOf());
    check(!raw_bound, "null layout override is consumed safely");

    device.wrapper->IASetInputLayout(original.Get());
    RunCommandList(device.device, device.wrapper.get(), &offset, nullptr, true);
    device.wrapper->OverrideInputLayout();
    check(bound_layout(*device.wrapper).Get() == original.Get(), "post list does not leak deferred changes into next draw");
    device.wrapper->RestoreInputLayout();

    CommandList custom_draw;
    add(custom_draw, L"vb0->elementformat(position,0)", L"R32G32_FLOAT");
    std::wstring draw_args = L"0,0";
    const std::wstring ini_namespace;
    check(ParseCommandListGeneralCommands(L"CommandListLayoutRegression", L"draw", &draw_args,
        &custom_draw, &custom_draw, nullptr, &ini_namespace), "parse actual custom draw command");
    RunCommandList(device.device, device.wrapper.get(), &custom_draw, nullptr, false);
    check(bound_layout(*device.wrapper)->GetElements()[0].Format == DXGI_FORMAT_R32G32_FLOAT,
        "custom Draw command applies layout within execution before outer-list flush");
    device.wrapper->RestoreInputLayout();
}
} // namespace

void test_dx11_layout_override_regression()
{
    try {
        run_layout_override_regression();
        std::fputs("PASS: actual layout command parsing, deferred replacement and restore\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: layout override regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
