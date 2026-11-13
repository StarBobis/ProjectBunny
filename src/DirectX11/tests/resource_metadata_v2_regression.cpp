#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

extern std::vector<CommandList*> pending_post_command_lists;

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class MetadataDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    CommandListState state;

    MetadataDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(runtime != nullptr, "load actual metadata-test D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve native device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real metadata WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query D3D11.1 metadata interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
        state.mHackerDevice = device;
        state.mHackerContext = wrapper.get();
        state.mOrigDevice1 = native.Get();
        state.mOrigContext1 = context.Get();
    }

    ~MetadataDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class MetadataConfig {
    std::filesystem::path config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    unsigned saved_frame = G->frame_no;
    bool saved_disable = G->disable_input;
    bool saved_input_initialized = G->disable_input_initialized;
    InputDisableScope saved_scope = G->input_disable_scope;
    bool owns_config = false;
public:
    MetadataConfig()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate metadata-test executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "metadata fixtures must remain in build/");
        config = base / L"d3dx.ini";
        settings = base / (L"metadata_v2_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing metadata settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing metadata d3dx.ini");
        CloseHandle(file);
        owns_config = true;
        migoto_handle = nullptr;
    }

    void load()
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[ResourceMetadataDefault]\n"
            "[ResourceMetadataZero]\narray=0\nmips=0\n"
            "[ResourceMetadataConfigured]\narray=3\nmips=2\nbind_flags=shader_resource render_target\n"
            "[PoolMetadataV2]\npool_size=1\npool_element_type_switch_reset=0\n"
            "[Constants]\nglobal $metadata_v2_upper_bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET\n"
            "global $metadata_v2_upper_dxgi = DXGI_FORMAT_R8G8B8A8_UNORM\n"
            "global $metadata_v2_upper_unknown = DXGI_FORMAT_UNKNOWN\n";
        out.close();
        check(out.good(), "write actual resource metadata INI");
        ClearKeyBindings();
        LoadConfigFile();
        check(command_list_globals.at(L"$metadata_v2_upper_bind").fval ==
            (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
            command_list_globals.at(L"$metadata_v2_upper_dxgi").fval == DXGI_FORMAT_R8G8B8A8_UNORM &&
            command_list_globals.at(L"$metadata_v2_upper_unknown").fval == DXGI_FORMAT_UNKNOWN,
            "real INI loading normalizes uppercase D3D11_BIND/DXGI literals before expression parsing");
    }

    ~MetadataConfig()
    {
        ClearKeyBindings();
        pending_post_command_lists.clear();
        CurrentTransition.Stop();
        OverrideSave.Reset(nullptr);
        presetOverrides.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->frame_no = saved_frame;
        G->disable_input = saved_disable;
        G->disable_input_initialized = saved_input_initialized;
        G->input_disable_scope = saved_scope;
        if (owns_config) { DeleteFileW(config.c_str()); DeleteFileW(settings.c_str()); }
    }
};

std::wstring normalize_expression(std::wstring text)
{
    // ParseCommandList/GetIniValue normalize user INI text before invoking the
    // internal expression parser, whose identifier scanner expects lowercase.
    std::transform(text.begin(), text.end(), text.begin(), ::towlower);
    return text;
}

float evaluate(const wchar_t* text, MetadataDevice& device)
{
    const std::wstring expression_text = normalize_expression(text), ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    check(expression.parse(&expression_text, &ini_namespace, &scope), "parse actual metadata expression");
    return expression.evaluate(&device.state);
}

ResourceCopyTarget target(const wchar_t* text)
{
    ResourceCopyTarget result;
    const std::wstring ini_namespace;
    const std::wstring normalized = normalize_expression(text);
    CommandListScope scope;
    check(result.ParseTarget(normalized.c_str(), true, &ini_namespace, &scope), "parse actual metadata target");
    return result;
}

ULONG references(IUnknown* object)
{
    const ULONG count = object->AddRef();
    object->Release();
    return count;
}

void literal_tests()
{
    const std::wstring ini_namespace;
    for (const auto& entry : CustomResourceBindFlagNames) {
        if (!entry.name) break;
        const std::wstring literal = normalize_expression(L"D3D11_BIND_" + std::wstring(entry.name));
        CommandListExpression expression;
        check(expression.parse(&literal, &ini_namespace, nullptr), "parse each real D3D11_BIND enum literal without command scope");
        float value = -1;
        check(expression.static_evaluate(&value) && value == static_cast<float>(entry.val), "D3D11_BIND enum literal numeric/static value");
    }
    for (const auto& literal : {L"DXGI_FORMAT_UNKNOWN", L"DXGI_FORMAT_R8G8B8A8_UNORM", L"DXGI_FORMAT_R32G32B32A32_FLOAT"}) {
        const std::wstring text = normalize_expression(literal);
        CommandListExpression expression;
        float value = -1;
        check(expression.parse(&text, &ini_namespace, nullptr) && expression.static_evaluate(&value) &&
            value == static_cast<float>(ParseFormatString(literal, false)), "complete DXGI_FORMAT prefix and static value");
    }
    for (const wchar_t* invalid : {L"D3D11_BIND_", L"D3D11_BIND_NONE", L"D3D11_BIND_decoderjunk",
        L"D3D11_BINC_decoder", L"D3D11_BIND_not_a_flag", L"DXGI_FORMAT_", L"DXGI_FORMAT_not_a_format",
        L"DXGI_FURMAT_R8G8B8A8_UNORM", L"DXGI_FORMAT_R8G8B8A8_UNORMjunk"}) {
        const std::wstring text = normalize_expression(invalid);
        CommandListOperand operand(0);
        check(!operand.parse(&text, &ini_namespace, nullptr), "invalid enum prefixes/names/trailing junk fail without partial acceptance");
    }
}

void resource_tests(MetadataDevice& device)
{
    constexpr UINT flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE1D_DESC one = {};
    one.Width = 16; one.MipLevels = 4; one.ArraySize = 3;
    one.Format = DXGI_FORMAT_R8G8B8A8_UNORM; one.Usage = D3D11_USAGE_DEFAULT; one.BindFlags = flags;
    ComPtr<ID3D11Texture1D> texture1;
    check(SUCCEEDED(device.native->CreateTexture1D(&one, nullptr, &texture1)), "create actual 1D array/mips");
    D3D11_SHADER_RESOURCE_VIEW_DESC view1_desc = {};
    view1_desc.Format = one.Format; view1_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
    view1_desc.Texture1DArray.MostDetailedMip = 1; view1_desc.Texture1DArray.MipLevels = 1;
    view1_desc.Texture1DArray.FirstArraySlice = 1; view1_desc.Texture1DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> view1;
    check(SUCCEEDED(device.native->CreateShaderResourceView(texture1.Get(), &view1_desc, &view1)), "create restricted 1D array SRV");
    device.context->PSSetShaderResources(0, 1, view1.GetAddressOf());
    check(evaluate(L"ps-t0->Array", device) == 3 && evaluate(L"ps-t0->Mips", device) == 4 &&
        evaluate(L"ps-t0->Width", device) == 16 && evaluate(L"ps-t0->Height", device) == 1 &&
        evaluate(L"ps-t0->BindFlags", device) == flags, "1D properties use full resource rather than selected slice/mip");

    D3D11_TEXTURE2D_DESC two = {};
    two.Width = 32; two.Height = 16; two.MipLevels = 5; two.ArraySize = 4; two.SampleDesc.Count = 1;
    two.Format = one.Format; two.Usage = D3D11_USAGE_DEFAULT; two.BindFlags = flags;
    ComPtr<ID3D11Texture2D> texture2;
    check(SUCCEEDED(device.native->CreateTexture2D(&two, nullptr, &texture2)), "create actual 2D array/mips");
    D3D11_SHADER_RESOURCE_VIEW_DESC view2_desc = {};
    view2_desc.Format = two.Format; view2_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view2_desc.Texture2DArray.MostDetailedMip = 2; view2_desc.Texture2DArray.MipLevels = 1;
    view2_desc.Texture2DArray.FirstArraySlice = 2; view2_desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> view2;
    check(SUCCEEDED(device.native->CreateShaderResourceView(texture2.Get(), &view2_desc, &view2)), "create restricted 2D SRV");
    device.context->CSSetShaderResources(1, 1, view2.GetAddressOf());
    check(evaluate(L"cs-t1->array", device) == 4 && evaluate(L"cs-t1->mips", device) == 5 &&
        evaluate(L"cs-t1->BindFlags", device) == flags, "2D full allocation properties");

    D3D11_TEXTURE3D_DESC three = {};
    three.Width = 16; three.Height = 8; three.Depth = 4; three.MipLevels = 3;
    three.Format = one.Format; three.Usage = D3D11_USAGE_DEFAULT; three.BindFlags = flags;
    ComPtr<ID3D11Texture3D> texture3;
    check(SUCCEEDED(device.native->CreateTexture3D(&three, nullptr, &texture3)), "create actual 3D texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC view3_desc = {};
    view3_desc.Format = three.Format; view3_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    view3_desc.Texture3D.MostDetailedMip = 1; view3_desc.Texture3D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> view3;
    check(SUCCEEDED(device.native->CreateShaderResourceView(texture3.Get(), &view3_desc, &view3)), "create restricted 3D SRV");
    device.context->VSSetShaderResources(2, 1, view3.GetAddressOf());
    check(evaluate(L"vs-t2->Array", device) == -4 && evaluate(L"vs-t2->Mips", device) == 3 &&
        evaluate(L"vs-t2->BindFlags", device) == flags, "3D supports mip/flags but upstream Array getter has no 3D-array result");

    two.Width = two.Height = 8; two.MipLevels = 2; two.ArraySize = 12; two.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> cube;
    check(SUCCEEDED(device.native->CreateTexture2D(&two, nullptr, &cube)), "create two-cube resource");
    D3D11_SHADER_RESOURCE_VIEW_DESC cube_desc = {};
    cube_desc.Format = two.Format; cube_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    cube_desc.TextureCubeArray.MostDetailedMip = 1; cube_desc.TextureCubeArray.MipLevels = 1;
    cube_desc.TextureCubeArray.First2DArrayFace = 6; cube_desc.TextureCubeArray.NumCubes = 1;
    ComPtr<ID3D11ShaderResourceView> cube_view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(cube.Get(), &cube_desc, &cube_view)), "create single-cube restricted SRV");
    device.context->PSSetShaderResources(3, 1, cube_view.GetAddressOf());
    check(evaluate(L"ps-t3->Array", device) == 12 && evaluate(L"ps-t3->Mips", device) == 2,
        "cube Array counts native array faces, not selected view cubes");

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = 256; buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    check(SUCCEEDED(device.native->CreateBuffer(&buffer_desc, nullptr, &buffer)), "create typed SRV buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC buffer_view_desc = {};
    buffer_view_desc.Format = DXGI_FORMAT_R32_FLOAT; buffer_view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    buffer_view_desc.Buffer.FirstElement = 4; buffer_view_desc.Buffer.NumElements = 8;
    ComPtr<ID3D11ShaderResourceView> buffer_view;
    check(SUCCEEDED(device.native->CreateShaderResourceView(buffer.Get(), &buffer_view_desc, &buffer_view)), "create restricted typed buffer SRV");
    device.context->PSSetShaderResources(4, 1, buffer_view.GetAddressOf());
    check(evaluate(L"ps-t4->Array", device) == -4 && evaluate(L"ps-t4->Mips", device) == -4 &&
        evaluate(L"ps-t4->Size", device) == 256 && evaluate(L"ps-t4->BindFlags", device) == buffer_desc.BindFlags,
        "buffer rejection/physical ByteWidth/full native flags survive new attributes");

    buffer_desc.ByteWidth = 1024; buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constant;
    check(SUCCEEDED(device.native->CreateBuffer(&buffer_desc, nullptr, &constant)), "create physical constant buffer");
    const UINT first = 16, count = 16;
    device.context->VSSetConstantBuffers1(5, 1, constant.GetAddressOf(), &first, &count);
    check(evaluate(L"vs-cb5->Size", device) == 256 && evaluate(L"vs-cb5->Offset", device) == 256 &&
        evaluate(L"vs-cb5->BindFlags", device) == D3D11_BIND_CONSTANT_BUFFER && evaluate(L"vs-cb5->Array", device) == -4,
        "CB window size/absolute offset and native flags preserved");
    // Use an unbound IB to check shortest member parsing and failure values.
    device.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    check(evaluate(L"ib->Mips", device) == -2 && evaluate(L"ib->BindFlags", device) == 0,
        "shortest prefix resolves and missing buffer uses safe fallbacks");

    auto& custom = customResources.at(L"resourcemetadatadefault");
    check(custom.override_array == -1 && custom.override_mips == -1, "default custom overrides remain -1");
    custom.resource = texture2.Get(); custom.resource->AddRef(); custom.is_null = false; custom.substantiated = true;
    auto custom_target = target(L"ResourceMetadataDefault");
    check(custom_target.GetResourceArray(&device.state) == 4 && custom_target.GetResourceMips(&device.state) == 5,
        "default custom getters reach actual allocation");
    check(custom_target.GetResourceBindFlags(&device.state) == 0, "custom flags query uses stored metadata rather than native desc");
    custom.bind_flags = D3D11_BIND_UNORDERED_ACCESS;
    check(evaluate(L"ResourceMetadataDefault->BindFlags", device) == D3D11_BIND_UNORDERED_ACCESS,
        "custom stored bind flags have upstream precedence");
    custom.override_array = 0; custom.override_mips = 0;
    check(custom_target.GetResourceArray(nullptr) == 0 && custom_target.GetResourceMips(nullptr) == 0,
        "explicit zero overrides remain observable without a GPU state");
    custom.override_array = 7; custom.override_mips = 9;
    check(evaluate(L"ResourceMetadataDefault->Array", device) == 7 && evaluate(L"ResourceMetadataDefault->Mips", device) == 9,
        "configured custom overrides take priority over instantiated resource");
    auto& zero = customResources.at(L"resourcemetadatazero");
    check(zero.override_array == 0 && zero.override_mips == 0 && target(L"ResourceMetadataZero").GetResourceArray(nullptr) == 0,
        "actual INI parser accepts zero array/mips metadata");
    auto& configured = customResources.at(L"resourcemetadataconfigured");
    check(configured.override_array == 3 && configured.override_mips == 2 &&
        static_cast<UINT>(configured.override_bind_flags) == flags, "actual INI array/mips/bind_flags options retained");

    auto missing = target(L"ps-t17");
    check(missing.GetResourceArray(&device.state) == -2 && missing.GetResourceMips(&device.state) == -2 &&
        missing.GetResourceBindFlags(&device.state) == 0 && missing.GetResourceArray(nullptr) == -2 &&
        missing.GetResourceBindFlags(nullptr) == 0, "unbound targets and absent state fail safely");
    ResourceCopyTarget unassigned;
    unassigned.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
    check(unassigned.GetResourceArray(&device.state) == -2 && unassigned.GetResourceMips(&device.state) == -2 &&
        unassigned.GetResourceBindFlags(&device.state) == 0, "unassigned custom/pool pointer is never dereferenced");

    const ULONG resource_refs = references(texture1.Get()), view_refs = references(view1.Get());
    for (unsigned repeat = 0; repeat < 128; ++repeat) {
        check(evaluate(L"ps-t0->Array", device) == 3 && evaluate(L"ps-t0->Mips", device) == 4 &&
            evaluate(L"ps-t0->BindFlags", device) == flags, "repeat actual getters");
        check(missing.GetResourceMips(&device.state) == -2, "repeat failure path");
    }
    check(references(texture1.Get()) == resource_refs && references(view1.Get()) == view_refs,
        "successful/failing getters balance acquired COM resources and views");

    auto& pool = customResourcePools.at(L"poolmetadatav2");
    G->frame_no = 11;
    check(pool.GetVariable(0, false, false, true) != nullptr, "actual pool assignment records frame metadata");
    check(evaluate(L"PoolMetadataV2[0]->LastFrame", device) == 11, "existing 11-character LastFrame suffix is preserved");
    const std::wstring ini_namespace;
    CommandListScope scope;
    for (const wchar_t* invalid : {L"ps-t0->Mip", L"ps-t0->Mipsjunk", L"ps-t0->Arrays", L"ps-t0->bind_flags",
        L"ps-t0->BindFlagsjunk", L"ps-t0->Array(1)"}) {
        ResourceCopyTarget rejected;
        const std::wstring normalized = normalize_expression(invalid);
        check(!rejected.ParseTarget(normalized.c_str(), true, &ini_namespace, &scope), "invalid getter suffix/arguments rejected");
    }
    check(evaluate(L"(ps-t0->BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0", device) == 1,
        "BindFlags member and enum literals compose in real expressions");
    device.context->ClearState();
}
} // namespace

void test_dx11_resource_metadata_v2_regression()
{
    try {
        MetadataDevice device;
        MetadataConfig fixture;
        fixture.load();
        literal_tests();
        resource_tests(device);
        std::fputs("PASS: real Array/Mips/BindFlags allocation queries, overrides/literals/failures and COM balance\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: resource metadata v2 regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
