#include "CommandList.h"
#include "globals.h"
#include "ComScope.h"
#include "HackerContext.h"
#include "cache_policy_test_scope.h"
#include <ScreenGrab.h>
#include <wincodec.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <thread>

void test_dx11_persistence_regression();
void test_dx11_persist_preservation_regression();
void test_dx11_critical_section_guard_regression();
void test_dx11_store_structured_regression();
void test_dx11_legacy_source_regression();
void test_dx11_shader_bindings_regression();
void test_dx11_frame_timing_regression();
void test_dx11_callback_namespace_regression();
void test_dx11_png_color_space_regression();
void test_dx11_texture_properties_regression();
void test_dx11_color_detection_options_regression();
void test_dx11_log_level_regression();
void test_dx11_input_toggle_hold_regression();
void test_dx11_ini_expression_regression();
void test_dx11_layout_override_regression();
void test_dx11_tokenizer_regression();
void test_dx11_time_locked_regression();
void test_dx11_unary_function_regression();
void test_dx11_readback_pool_regression();
void test_dx11_readback_hash_regression(CommandListState&);
void test_dx11_argument_reader_regression();
void test_dx11_spatial_grid_regression(CommandListState&);
void test_dx11_enum_option_regression();
void test_dx11_command_list_callback_regression();
void test_dx11_draw_dispatch_regression();
void test_dx11_store_readback_regression();
void test_dx11_resource_region_regression();
void test_dx11_reader_variable_regression();
void test_dx11_pool_lifecycle_regression();
void test_dx11_utf8_tokenizer_regression();
void test_dx11_pool_missing_regression();
void test_dx11_proxy_recovery_regression();
void test_input_layout_followups(ID3D11Device1*, ID3D11DeviceContext1*);

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

static void test_parser()
{
    const std::wstring ini_namespace;
    ResourceCopyTarget empty;
    require(!empty.ParseTarget(nullptr, true, &ini_namespace, nullptr), "null target");
    require(!empty.ParseTarget(L"", true, &ini_namespace, nullptr), "empty target");

    ResourceCopyTarget vertex;
    require(vertex.ParseTarget(L"vb31->stride", true, &ini_namespace, nullptr), "vertex stride parse");
    require(vertex.slot == 31 && vertex.type == ResourceCopyTargetType::VERTEX_BUFFER &&
        vertex.evaluation_mode == ResourceCopyTargetEvaluationMode::RESOURCE_STRIDE, "vertex stride metadata");
    ResourceCopyTarget bad_slot;
    require(!bad_slot.ParseTarget(L"vb32", true, &ini_namespace, nullptr), "vertex slot upper bound");
    for (const wchar_t* invalid : {L"prefixib", L"prefixthis", L"vb 0",
        L"vb0->hashregion(0, 16, 32)", L"vb0->hashregion(0, 16,)", L"vb0->hashregion(,16)"}) {
        ResourceCopyTarget target;
        if (target.ParseTarget(invalid, true, &ini_namespace, nullptr)) {
            std::fwprintf(stderr, L"Unexpectedly accepted target: %ls\n", invalid);
            require(false, "reject malformed resource target");
        }
    }
    ResourceCopyTarget offset;
    require(offset.ParseTarget(L"vs-cb0->offset", true, &ini_namespace, nullptr) &&
        offset.evaluation_mode == ResourceCopyTargetEvaluationMode::RESOURCE_OFFSET,
        "constant buffer offset parse");
    ResourceCopyTarget bad_shader;
    require(!bad_shader.ParseTarget(L"xs-t0", true, &ini_namespace, nullptr), "invalid shader");
    ResourceCopyTarget bad_uav;
    require(!bad_uav.ParseTarget(L"vs-u0", true, &ini_namespace, nullptr), "vertex UAV rejected");
    ResourceCopyTarget source_only;
    require(!source_only.ParseTarget(L"null", false, &ini_namespace, nullptr), "source only destination rejected");
    ResourceCopyTarget backbuffer;
    require(backbuffer.ParseTarget(L"bb", true, &ini_namespace, nullptr) && backbuffer.forbid_view_cache,
        "backbuffer view caching forbidden");

    CustomResource& resource = customResources[L"resourcetest"];
    ResourceCopyTarget custom;
    require(custom.ParseTarget(L"resourcetest->size", true, &ini_namespace, nullptr), "custom size parse");
    require(custom.GetCustomResource(nullptr) == &resource, "actual custom resource lookup");
    resource.buf_size = 256;
    require(custom.GetResourceSize(nullptr) == 256, "actual custom size getter");
    resource.override_stride = 32;
    require(custom.GetResourceStride(nullptr) == 32, "actual custom stride getter");
    ResourceCopyTarget missing;
    require(!missing.ParseTarget(L"resourcemissing", true, &ini_namespace, nullptr), "missing custom rejected");

    CustomResourcePool& pool = customResourcePools[L"pooltest"];
    pool.name = L"pooltest";
    pool.resource_template = &customResources[L"pooltest_template"];
    pool.resource_template->pool = &pool;
    pool.variable_template = std::make_unique<CommandListVariable>(L"pooltest_template", 0, VariableFlags::NONE);
    pool.Initialize(2);
    pool.index_type = PoolIndexType::RING;
    pool.lazy_initialization = true;
    pool.resource_template->bind_flags = D3D11_BIND_SHADER_RESOURCE;
    pool.resource_template->misc_flags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    auto first = pool.GetResource(0, false, false, true);
    require(first && first->pool == &pool && first->pool_index == 0, "real lazy pool initialization");
    require(first->bind_flags == D3D11_BIND_SHADER_RESOURCE &&
        first->misc_flags == D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, "new pool instance inherits flags");
    require(pool.GetResource(2, false, false, true) == first, "ring slot reuse");
    require(pool.GetResource(1, true, false, false) == pool.resource_template, "static evaluation selects template");
    require(first->AddFlags(D3D11_BIND_VERTEX_BUFFER, static_cast<D3D11_RESOURCE_MISC_FLAG>(0)), "real flag propagation");
    auto second = pool.GetResource(1, false, false, true);
    require(second->bind_flags == first->bind_flags && second->pool == &pool, "late instance inherits updated flags");
    require(!first->AddFlags(D3D11_BIND_CONSTANT_BUFFER, static_cast<D3D11_RESOURCE_MISC_FLAG>(0)), "real CB conflict");

    CommandListState pool_state;
    ResourceCopyTarget pool_target;
    require(pool_target.ParseTarget(L"pooltest[0]->sourcestride", true, &ini_namespace, nullptr), "pool source stride parse");
    require(pool_target.GetCustomResource(&pool_state) == first &&
        pool_target.evaluation_mode == ResourceCopyTargetEvaluationMode::RESOURCE_SOURCE_STRIDE, "pool lookup metadata");
    first->source_stride = 64;
    const std::wstring source_stride = L"pooltest[0]->sourcestride";
    CommandListOperand operand(0);
    CommandListScope scope;
    CommandListState state;
    require(operand.parse(&source_stride, &ini_namespace, &scope), "actual source stride operand parse");
    require(operand.evaluate(&state) == 64, "actual source stride operand evaluation");
    // No COM allocation is performed in these pool resources; destruction still
    // exercises the production CustomResource destructor on all actual entries.
    customResourcePools.clear();
    customResources.clear();
}

static void test_batch_copy(CommandListState& state, ID3D11Buffer* source, const uint32_t* expected);
static void test_pool_variables(CommandListState& state);
static void test_spatial_pools();

static uint32_t spatial_key(uint32_t x, uint32_t y, uint32_t z)
{
    return ((x & 4095) << 20) | ((y & 255) << 12) | (z & 4095);
}

static void test_spatial_hash(CommandListState& state)
{
    float xyz[4] = {1.25f, -0.01f, 128.25f, 10.0f};
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(xyz);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = xyz;
    ID3D11Buffer* buffer = nullptr;
    require(SUCCEEDED(state.mOrigDevice1->CreateBuffer(&desc, &initial, &buffer)), "spatial coordinate buffer");
    G->mResources[buffer].type = D3D11_RESOURCE_DIMENSION_BUFFER;
    uint32_t hash = 0;
    require(TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 8, 0.125f, hash) &&
        hash == spatial_key(10, 255, 1026), "byte offsets, floor of negative coordinates and torus wrap");
    CustomResource unaligned;
    auto* bytes = static_cast<uint8_t*>(std::malloc(16));
    require(bytes != nullptr, "allocate unaligned coordinate snapshot");
    std::memset(bytes, 0, 16);
    std::memcpy(bytes + 1, xyz, 3 * sizeof(float));
    unaligned.InitializeHandleInfo(bytes, 16);
    require(TryGetSpatialHash(state.mHackerContext, buffer, 1, 5, 9, 0.125f, hash, &unaligned) &&
        hash == spatial_key(10, 255, 1026), "custom snapshot supports unaligned byte offsets");
    require(TryGetSpatialHash(state.mHackerContext, buffer, 0, 8, 4, 0.125f, hash) &&
        hash == spatial_key(10, 2, 4095), "distinct Y/Z offsets do not alias spatial cache");
    require(TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 8, 0.25f, hash) &&
        hash == spatial_key(5, 255, 513), "different cell size does not alias spatial cache");
    require(TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 12, 0.125f, hash) &&
        hash == spatial_key(10, 255, 80), "final float at buffer boundary allowed");
    require(!TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 13, 0.125f, hash) &&
        !TryGetSpatialHash(state.mHackerContext, buffer, UINT_MAX, 4, 8, 0.125f, hash), "spatial offsets bounded without overflow");
    require(!TryGetSpatialHash(state.mHackerContext, buffer, 0, UINT_MAX, 8, 0.125f, hash) &&
        !TryGetSpatialHash(state.mHackerContext, buffer, 0, 13, 8, 0.125f, hash),
        "spatial Y independently rejects oversized offsets including a partial final float");
    for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
        require(!TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 8, invalid, hash), "invalid cell sizes rejected");
    xyz[0] = xyz[1] = xyz[2] = 0;
    state.mOrigContext1->UpdateSubresource(buffer, 0, nullptr, xyz, 0, 0);
    ClearResourceRegionHashCache(buffer);
    require(TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 8, 0.125f, hash) && hash == 0, "origin has valid zero hash");
    state.mOrigContext1->VSSetConstantBuffers(0, 1, &buffer);
    const std::wstring text = L"vs-cb0->spatialhash(0, 1, 2, 0.125)";
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListOperand operand(0);
    require(operand.parse(&text, &ini_namespace, &scope) && BitCastToUint(operand.evaluate(&state)) == 0,
        "spatial expression preserves valid origin instead of unknown sentinel");
    ID3D11Buffer* none = nullptr;
    state.mOrigContext1->VSSetConstantBuffers(0, 1, &none);
    xyz[0] = std::numeric_limits<float>::quiet_NaN();
    state.mOrigContext1->UpdateSubresource(buffer, 0, nullptr, xyz, 0, 0);
    ClearResourceRegionHashCache(buffer);
    require(!TryGetSpatialHash(state.mHackerContext, buffer, 0, 4, 8, 0.125f, hash), "nonfinite coordinates rejected");
    ClearRegionHashesGlobalCache();
    G->mResources.erase(buffer);
    require(buffer->Release() == 0, "spatial buffer reference balance");
    require(SpatialDistanceChebyshev(UnpackCellCoords(spatial_key(0, 0, 0)),
        UnpackCellCoords(spatial_key(4095, 1, 4095))) == 1, "wrapped diagonal Chebyshev distance");
}

static void test_resource_runtime_reset(CommandListState& state, ID3D11Buffer* source, const uint32_t* expected)
{
    CustomResource resource;
    ResourceCopyTarget target;
    target.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
    target.SetCustomResource(&resource);
    target.SetResource(&state, source, nullptr, 16, 32, DXGI_FORMAT_R32_UINT, 4096);
    resource.source_stride = 64;
    resource.substantiated = true;
    resource.copies_this_frame = 3;
    resource.frame_no = 12;
    void* snapshot = std::malloc(32);
    require(snapshot != nullptr, "allocate reset snapshot");
    resource.InitializeHandleInfo(snapshot, 32);
    resource.ResetRuntimeState();
    target.SetResource(&state, source, nullptr, 16, 32, DXGI_FORMAT_R32_UINT, 4096);
    resource.source_stride = 64;
    resource.substantiated = true;
    HMODULE runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(runtime != nullptr, "load second-device runtime");
    const auto create_device = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
    require(create_device != nullptr, "resolve second device creation");
    ID3D11Device* second = nullptr;
    ID3D11DeviceContext* second_context = nullptr;
    require(SUCCEEDED(create_device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &second, nullptr, &second_context)), "create second actual WARP device");
    ID3D11Device1* second1 = nullptr;
    ID3D11DeviceContext1* second_context1 = nullptr;
    require(SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&second1))) &&
        SUCCEEDED(second_context->QueryInterface(IID_PPV_ARGS(&second_context1))), "second D3D11.1 interfaces");
    resource.expire(second1, second_context1);
    require(resource.resource && resource.resource != source && resource.device == second1 && resource.substantiated &&
        resource.stride == 16 && resource.offset == 32 && resource.buf_size == 4096 && resource.source_stride == 64,
        "device transfer preserves content metadata after reset");
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 4096;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* staging = nullptr;
    require(SUCCEEDED(second->CreateBuffer(&desc, nullptr, &staging)), "second-device readback buffer");
    second_context->CopyResource(staging, resource.resource);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require(SUCCEEDED(second_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "read transferred payload");
    require(std::memcmp(mapped.pData, expected, 4096) == 0, "cross-device payload remains exact");
    second_context->Unmap(staging, 0);
    staging->Release();
    resource.ResetRuntimeState();
    second_context1->Release(); second1->Release();
    second_context->Release(); second->Release();
    FreeLibrary(runtime);
    require(resource.resource == nullptr && resource.view == nullptr && resource.device == nullptr &&
        resource.GetHandleInfo() == nullptr && !resource.substantiated && resource.copies_this_frame == 0 &&
        resource.frame_no == 0 && resource.buf_size == 0 && resource.source_stride == 0,
        "runtime reset clears allocation, snapshot, metadata and copy counters");
    resource.ResetRuntimeState();
}

static void test_input_layout(CommandListState& state)
{
    const char shader[] = "float4 main(float3 p:POSITION):SV_Position{return float4(p,1);}";
    ID3DBlob* bytecode = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT compile = D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &bytecode, &errors);
    if (errors)
        errors->Release();
    require(SUCCEEDED(compile), "compile actual input-layout shader signature");
    std::string semantic = "POSITION";
    D3D11_INPUT_ELEMENT_DESC element = {semantic.c_str(), 0, DXGI_FORMAT_R32G32B32_FLOAT,
        0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ID3D11InputLayout* original = nullptr;
    require(SUCCEEDED(state.mOrigDevice1->CreateInputLayout(&element, 1, bytecode->GetBufferPointer(),
        bytecode->GetBufferSize(), &original)), "create native WARP input layout");
    original->AddRef(); // Observe ownership after the wrapper and pipeline release.
    auto* wrapped = new HackerInputLayout(original, &element, 1);
    const uint32_t hash = wrapped->GetLayoutHash();
    semantic[0] = 'X';
    require(std::strcmp(wrapped->GetElements()[0].SemanticName, "POSITION") == 0 &&
        wrapped->GetElementCount() == 1 && wrapped->GetLayoutHash() == hash, "layout owns semantic metadata");
    require(wrapped->QueryInterface(__uuidof(IUnknown), nullptr) == E_POINTER, "null interface output rejected");
    IUnknown* identity = nullptr;
    require(SUCCEEDED(wrapped->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&identity))) &&
        identity == static_cast<IUnknown*>(wrapped), "stable wrapper IUnknown identity");
    identity->Release();
    void* unsupported = reinterpret_cast<void*>(1);
    require(wrapped->QueryInterface(__uuidof(ID3D11Texture2D), &unsupported) == E_NOINTERFACE &&
        unsupported == nullptr, "unsupported layout interface clears output");
    {
        HackerContext context(state.mOrigDevice1, state.mOrigContext1);
        context.IASetInputLayout(wrapped);
        context.IASetInputLayout(wrapped);
        ID3D11InputLayout* actual = nullptr;
        context.IAGetInputLayout(&actual);
        require(actual == wrapped, "wrapped layout survives repeated bind and getter");
        actual->Release();
        require(wrapped->Release() == 1, "context retains layout after caller release");
        context.ClearState();
        context.IAGetInputLayout(&actual);
        require(actual == nullptr, "ClearState clears tracked input layout");
        require(original->Release() == 0, "ClearState releases wrapper and native ownership");

        semantic = "POSITION";
        element.SemanticName = semantic.c_str();
        ID3D11InputLayout* native = nullptr;
        require(SUCCEEDED(state.mOrigDevice1->CreateInputLayout(&element, 1, bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(), &native)), "create bypass native layout");
        context.IASetInputLayout(native);
        context.IAGetInputLayout(&actual);
        require(actual == native, "unwrapped native layout is not cast to wrapper");
        actual->Release();
        state.mOrigContext1->ClearState();
        context.IAGetInputLayout(&actual);
        require(actual == nullptr, "getter observes native state reset");
        require(native->Release() == 0, "native fallback reference balance");
    }
    bytecode->Release();
}

static void test_constant_buffer_windows()
{
    DataCacheTestScope cache_policy;
    HMODULE runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(runtime != nullptr, "load system D3D11 runtime");
    auto create_device = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
    require(create_device != nullptr, "resolve system device creation");
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    require(SUCCEEDED(create_device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)), "create real WARP device");
    CommandListState state;
    require(SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&state.mOrigDevice1))),
        "query D3D11.1 device");
    require(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&state.mOrigContext1))),
        "query D3D11.1 context");
    auto hash_context = std::make_unique<HackerContext>(state.mOrigDevice1, state.mOrigContext1);
    state.mHackerContext = hash_context.get();
    test_input_layout(state);
    test_input_layout_followups(state.mOrigDevice1, state.mOrigContext1);
    test_spatial_hash(state);
    test_dx11_readback_hash_regression(state);
    test_dx11_spatial_grid_regression(state);
    // Load through the actual custom-resource path on separate fresh threads.
    // Repetition also checks DirectXTK's shared WIC factory after COM scopes end.
    for (int i = 0; i < 3; ++i) {
        std::thread loader([device]() {
            CustomResource image;
            image.name = L"wic-regression";
            image.filename = L"wic-regression.png";
            image.bind_flags = D3D11_BIND_SHADER_RESOURCE;
            image.Substantiate(device, D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
            require(image.resource != nullptr && !image.is_null, "actual WIC custom texture load");
            ULONG_PTR com_token = 0;
            require(CoGetContextToken(&com_token) == CO_E_NOTINITIALIZED,
                "WIC load balances owned COM initialization");
            CustomResource missing;
            missing.filename = L"missing-wic-regression.png";
            missing.Substantiate(device, D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
            require(missing.resource == nullptr && CoGetContextToken(&com_token) == CO_E_NOTINITIALIZED,
                "failed WIC load leaves no COM initialization behind");
            ID3D11Texture2D* texture = nullptr;
            require(SUCCEEDED(image.resource->QueryInterface(__uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture))), "loaded texture interface");
            D3D11_TEXTURE2D_DESC image_desc = {};
            texture->GetDesc(&image_desc);
            std::fprintf(stderr, "WIC texture: %u x %u, format=%u\n", image_desc.Width,
                image_desc.Height, static_cast<unsigned>(image_desc.Format));
            require(image_desc.Width == 1 && image_desc.Height == 1 &&
                (image_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                 image_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM), "WIC default format for untagged PNG");
            D3D11_TEXTURE2D_DESC staging_desc = image_desc;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.BindFlags = staging_desc.MiscFlags = 0;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* staging = nullptr;
            require(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)), "WIC readback texture");
            ID3D11DeviceContext* read_context = nullptr;
            device->GetImmediateContext(&read_context);
            CommandListState image_state;
            require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&image_state.mOrigDevice1))) &&
                SUCCEEDED(read_context->QueryInterface(IID_PPV_ARGS(&image_state.mOrigContext1))),
                "real custom texture getter context");
            ResourceCopyTarget image_target;
            image_target.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
            image_target.SetCustomResource(&image);
            require(image_target.GetResourceSize(&image_state) == -3 &&
                image_target.GetResourceStride(&image_state) == -3,
                "existing real custom texture retains non-buffer property errors");
            image_state.mOrigContext1->Release();
            image_state.mOrigDevice1->Release();
            read_context->CopyResource(staging, texture);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            require(SUCCEEDED(read_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "WIC pixel readback");
            const auto* pixel = static_cast<const uint8_t*>(mapped.pData);
            const bool bgra = image_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
            require(pixel[bgra ? 2 : 0] == 0x80 && pixel[1] == 0x40 &&
                pixel[bgra ? 0 : 2] == 0x20 && pixel[3] == 0xff, "WIC decoded pixel channels");
            read_context->Unmap(staging, 0);
            {
                ComScope screenshot_com;
                require(static_cast<bool>(screenshot_com), "screenshot COM initialization");
                require(SUCCEEDED(DirectX::SaveWICTextureToFile(read_context, texture,
                    GUID_ContainerFormatPng, L"wic-roundtrip.png")), "actual WIC screenshot save");
            }
            staging->Release();
            read_context->Release();
            texture->Release();
        });
        loader.join();
    }
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 4096;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* buffer = nullptr;
    uint32_t data[1024];
    for (uint32_t i = 0; i < 1024; ++i)
        data[i] = i * 17;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    require(SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer)), "create constant buffer");
    for (wchar_t shader : {L'v', L'h', L'd', L'g', L'p', L'c'}) {
        ResourceCopyTarget target;
        target.type = ResourceCopyTargetType::CONSTANT_BUFFER;
        target.shader_type = shader;
        target.slot = 0;
        target.SetResource(&state, buffer, nullptr, 16, 256, DXGI_FORMAT_UNKNOWN, 512);
        UINT offset = 0, size = 0;
        ID3D11View* view = nullptr;
        ID3D11Resource* fetched = target.GetResource(&state, &view, nullptr, &offset, nullptr, &size);
        require(fetched == buffer && offset == 256 && size == 512, "real shader window round trip");
        fetched->Release();
        require(target.GetResourceStride(&state) == 16, "constant stride is 16 bytes");
        require(target.GetResourceOffset(&state) == 256, "window offset getter");
        require(target.GetResourceSize(&state) == 512, "window size getter");
        target.SetResource(&state, buffer, nullptr, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        fetched = target.GetResource(&state, &view, nullptr, &offset, nullptr, nullptr);
        require(fetched == buffer && offset == 0, "whole buffer fallback");
        fetched->Release();
        target.SetResource(&state, nullptr, nullptr, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        require(target.GetResource(&state, &view, nullptr, nullptr, nullptr, nullptr) == nullptr, "unbind constant buffer");
    }
    G->mResources[buffer].type = D3D11_RESOURCE_DIMENSION_BUFFER;
    ResourceCopyTarget bound_cb;
    bound_cb.type = ResourceCopyTargetType::CONSTANT_BUFFER;
    bound_cb.shader_type = L'v';
    bound_cb.SetResource(&state, buffer, nullptr, 16, 256, DXGI_FORMAT_UNKNOWN, 512);
    CommandListScope expression_scope;
    expression_scope.emplace_front();
    CommandListVariable variable(L"$offset", 16, VariableFlags::NONE);
    expression_scope.front().emplace(variable.name, &variable);
    const std::wstring ini_namespace;
    const std::wstring hash_text = L"vs-cb0->hashregion($offset, 32)";
    std::fputs("Testing HashRegion expressions\n", stderr);
    CommandListExpression expression;
    require(expression.parse(&hash_text, &ini_namespace, &expression_scope), "HashRegion expression with dynamic argument");
    uint32_t mixed = crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 272, 32);
    mixed ^= mixed >> 16;
    mixed *= 0x85ebca6b;
    mixed ^= mixed >> 13;
    const uint32_t payload = mixed & 0x3fffffff;
    uint32_t bits = ((1 + (payload >> 23)) << 23) | (payload & 0x7fffff);
    float expected_expression;
    std::memcpy(&expected_expression, &bits, sizeof(bits));
    require(expression.evaluate(&state) == expected_expression, "HashRegion includes CB window and member offsets");
    const std::wstring nested_hash_text = L"vs-cb0->hashregion(($offset + 4) * 2, (8 + 8))";
    CommandListExpression nested_hash;
    require(nested_hash.parse(&nested_hash_text, &ini_namespace, &expression_scope), "nested member argument expressions");
    mixed = crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 296, 16);
    mixed ^= mixed >> 16; mixed *= 0x85ebca6b; mixed ^= mixed >> 13;
    const uint32_t nested_payload = mixed & 0x3fffffff;
    bits = ((1 + (nested_payload >> 23)) << 23) | (nested_payload & 0x7fffff);
    std::memcpy(&expected_expression, &bits, sizeof(bits));
    require(nested_hash.evaluate(&state) == expected_expression, "nested offset and size expressions select exact byte region");
    for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), 4294967296.0f}) {
        variable.fval = invalid;
        require(expression.evaluate(&state) == -1, "invalid dynamic hash range rejected");
    }
    const std::wstring grouped_text = L"(vs-cb0->size()) + 1";
    std::fputs("Testing grouped member expressions\n", stderr);
    CommandListExpression grouped;
    require(grouped.parse(&grouped_text, &ini_namespace, &expression_scope) &&
        grouped.evaluate(&state) == 513, "grouped no-argument member expression");
    const std::wstring missing_close = L"vs-cb0->hashregion(0, 32";
    std::fputs("Testing malformed member expressions\n", stderr);
    CommandListExpression malformed;
    require(!malformed.parse(&missing_close, &ini_namespace, &expression_scope), "missing member parenthesis rejected");
    std::fputs("Testing region hash snapshots\n", stderr);
    bound_cb.SetResource(&state, nullptr, nullptr, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    const uint32_t expected = crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 4080, 16);
    require(GetRegionHash(hash_context.get(), buffer, 4080, 128) == expected, "oversized hash region clamps to buffer end");
    require(GetRegionHash(hash_context.get(), buffer, 4080, 128) == expected, "repeat clamped hash cache hit");
    require(GetRegionHash(hash_context.get(), buffer, 4096, 16) == 0, "hash offset at end rejected");
    require(GetRegionHash(hash_context.get(), buffer, UINT_MAX, 16) == 0, "large hash offset rejected");
    require(GetRegionHash(hash_context.get(), buffer, 0, 0) == 0, "empty hash region rejected");
    ClearRegionHashesGlobalCache();
    G->mResources[buffer].ClearDataCache();
    data[1023] ^= 0x12345678;
    context->UpdateSubresource(buffer, 0, nullptr, data, 0, 0);
    require(GetRegionHash(hash_context.get(), buffer, 4080, 128) ==
        crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 4080, 16), "hash snapshot refresh after invalidation");
    CustomResource first_view, second_view;
    std::fputs("Testing shared view hashes\n", stderr);
    first_view.SetHandleInfo(buffer, 16, 32);
    second_view.SetHandleInfo(buffer, 128, 32);
    require(first_view.GetHandleInfo() && first_view.GetHandleInfo()->cached_data_size == 32,
        "shared cache view metadata");
    const uint32_t first_hash = crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 16, 32);
    const uint32_t second_hash = crc32c_hw(0, reinterpret_cast<uint8_t*>(data) + 128, 32);
    require(GetRegionHash(hash_context.get(), buffer, 0, 32, &first_view) == first_hash, "first view hash");
    require(GetRegionHash(hash_context.get(), buffer, 0, 32, &second_view) == second_hash,
        "same GPU resource distinct views do not collide");
    const uint32_t replacement = 0x11223344;
    G->mResources[buffer].SetDataCacheRegion(&replacement, sizeof(replacement), 128);
    std::fputs("Testing partial source update\n", stderr);
    require(GetRegionHash(hash_context.get(), buffer, 0, 32, &second_view) == second_hash,
        "captured view snapshot survives partial source update");
    uint8_t updated[32];
    std::memcpy(updated, reinterpret_cast<uint8_t*>(data) + 128, sizeof(updated));
    std::memcpy(updated, &replacement, sizeof(replacement));
    require(GetRegionHash(hash_context.get(), buffer, 128, 32) == crc32c_hw(0, updated, sizeof(updated)),
        "source partial update invalidates hash caches");
    // Restore the source snapshot before checking view rebind below.
    G->mResources[buffer].SetDataCacheRegion(data + 32, sizeof(replacement), 128);
    require(GetRegionHash(hash_context.get(), buffer, 0, 512) == crc32c_hw(0, data, 512), "cache multi-page region");
    G->mResources[buffer].SetDataCacheRegion(&replacement, sizeof(replacement), 300);
    uint32_t updated_pages[128];
    std::memcpy(updated_pages, data, sizeof(updated_pages));
    updated_pages[75] = replacement;
    require(GetRegionHash(hash_context.get(), buffer, 0, 512) == crc32c_hw(0, updated_pages, 512),
        "later-page write invalidates a region starting on an earlier page");
    G->mResources[buffer].SetDataCacheRegion(data + 75, sizeof(replacement), 300);
    first_view.SetHandleInfo(buffer, 128, 32);
    require(GetRegionHash(hash_context.get(), buffer, 0, 32, &first_view) == second_hash, "view rebind clears hash cache");
    first_view.SetHandleInfo(buffer, 4080, 0);
    require(first_view.GetHandleInfo()->cached_data_size == 16, "default view size excludes offset");
    first_view.SetHandleInfo(buffer, 4080, SIZE_MAX);
    require(first_view.GetHandleInfo()->cached_data_size == 16, "view size clamps without overflow");
    first_view.SetHandleInfo(buffer, SIZE_MAX, 1);
    require(first_view.GetHandleInfo() == nullptr, "invalid view replacement clears stale state");
    ClearRegionHashesGlobalCache();
    G->mResources.erase(buffer);
    require(GetRegionHash(hash_context.get(), buffer, 0, 32, &second_view) == second_hash,
        "shared view survives source cache destruction");
    second_view.SetHandleInfo(nullptr, 0, 0);
    require(second_view.GetHandleInfo() == nullptr, "null source invalidates old view");
    test_batch_copy(state, buffer, data);
    test_pool_variables(state);
    test_resource_runtime_reset(state, buffer, data);
    context->ClearState();
    require(buffer->Release() == 0, "no leaked constant buffer references");
    hash_context.reset();
    state.mHackerContext = nullptr;
    state.mOrigContext1->Release();
    state.mOrigDevice1->Release();
    context->Release();
    device->Release();
    FreeLibrary(runtime);
}

struct ClusterHasher
{
    size_t operator()(uint32_t) const noexcept { return 15; }
};

static void test_hash_map_erase()
{
    FlatHashMap<uint32_t, uint32_t, ClusterHasher> actual(16);
    std::unordered_map<uint32_t, uint32_t> expected;
    uint32_t random = 17;
    for (unsigned step = 0; step < 2000; ++step) {
        random = random * 1664525u + 1013904223u;
        const uint32_t key = (random >> 12) % 32;
        if (step % 127 == 0) {
            actual.clear();
            expected.clear();
        } else if (random & 1) {
            actual.insert(key, random);
            expected[key] = random;
        } else {
            require(actual.erase(key) == (expected.erase(key) != 0), "erase result matches reference map");
        }
        require(actual.size() == expected.size(), "erase preserves map size");
        for (uint32_t candidate = 0; candidate < 32; ++candidate) {
            const auto* value = actual.find_ptr(candidate);
            const auto found = expected.find(candidate);
            require((value == nullptr) == (found == expected.end()), "wrapped collision chain lookup");
            if (value)
                require(*value == found->second, "collision chain value after erase");
        }
    }
}

static void test_cache_resize()
{
    FlatHashMap<uint32_t, uint32_t> tiny(0);
    tiny.insert(1, 7);
    require(tiny.find_ptr(2) == nullptr, "small hash map miss terminates after first insert");
    ResourceHandleInfo cache;
    auto* bytes = static_cast<uint8_t*>(std::malloc(64));
    require(bytes != nullptr, "allocate owned snapshot");
    std::memset(bytes, 0x42, 64);
    cache.SetDataCache(bytes, 64);
    const uint32_t value = 0x12345678;
    cache.SetDataCacheRegion(&value, sizeof(value), 60);
    require(std::memcmp(cache.GetCachedData() + 60, &value, sizeof(value)) == 0 &&
        cache.GetCachedData()[0] == 0x42, "partial update preserves unaffected bytes");
    cache.InitializeDataCache(128);
    require(cache.GetCachedData() == nullptr && cache.cached_data_size == 128, "resize resets old allocation");
    cache.SetDataCacheRegion(&value, sizeof(value), 124);
    require(std::memcmp(cache.GetCachedData() + 124, &value, sizeof(value)) == 0, "partial cache allocation after resize");
    cache.ClearDataCache();
    require(cache.GetCachedData() == nullptr && cache.cached_data_size == 0, "clear resets snapshot metadata");
}

static void test_com_apartments()
{
    std::thread fresh([]() {
        ULONG_PTR context_token = 0;
        require(CoGetContextToken(&context_token) == CO_E_NOTINITIALIZED, "fresh thread starts without COM");
        {
            ComScope outer;
            require(static_cast<bool>(outer), "lazy COM initialization");
            ComScope nested;
            require(static_cast<bool>(nested), "nested S_FALSE initialization");
        }
        require(CoGetContextToken(&context_token) == CO_E_NOTINITIALIZED, "owned COM references balanced");
    });
    fresh.join();
    for (DWORD model : {COINIT_APARTMENTTHREADED, COINIT_MULTITHREADED}) {
        std::thread initialized([model]() {
            require(SUCCEEDED(CoInitializeEx(nullptr, model)), "host COM initialization");
            {
                ComScope scope;
                require(static_cast<bool>(scope), "existing STA/MTA supported");
            }
            ULONG_PTR token = 0;
            require(SUCCEEDED(CoGetContextToken(&token)), "host apartment retained");
            CoUninitialize();
            require(CoGetContextToken(&token) == CO_E_NOTINITIALIZED, "no leaked S_FALSE references");
        });
        initialized.join();
    }
}

static CustomResourcePool& make_pool(const wchar_t* name, PoolIndexType type,
    size_t size, unsigned keep_alive, bool lazy = true)
{
    CustomResourcePool& pool = customResourcePools[name];
    pool.name = name;
    pool.resource_template = &customResources[std::wstring(name) + L"_template"];
    pool.resource_template->pool = &pool;
    pool.resource_template->pool_index = -1;
    pool.index_type = type;
    pool.keep_alive_frames = keep_alive;
    pool.reset_expired_elements = true;
    pool.lazy_initialization = lazy;
    pool.variable_template = std::make_unique<CommandListVariable>(std::wstring(name) + L"_template", 0, VariableFlags::NONE);
    pool.Initialize(size);
    return pool;
}

static void test_spatial_pools()
{
    const unsigned saved_frame = G->frame_no;
    const float saved_frame_time = G->gFrameTime;
    G->gFrameTime = 1.0f / 120.0f;
    G->frame_no = 100;
    auto& pool = make_pool(L"poolspatial", PoolIndexType::SPATIAL, 2, 1);
    pool.spatial_radius = 1;
    auto* first = pool.GetVariable(BitCastToFloat(spatial_key(10, 20, 30)), false, false, true);
    first->fval = 101;
    require(pool.GetVariable(BitCastToFloat(spatial_key(10, 20, 30)), false, false, false) == first,
        "spatial pool exact match");
    require(pool.GetVariable(BitCastToFloat(spatial_key(11, 21, 31)), false, false, true) == first &&
        first->fval == 101, "nearby diagonal spatial reuse preserves data");
    auto* second = pool.GetVariable(BitCastToFloat(spatial_key(100, 100, 100)), false, false, true);
    require(second != first, "distant spatial cell takes unused slot");
    require(pool.GetVariable(BitCastToFloat(spatial_key(200, 200, 200)), false, false, true) == first,
        "full spatial pool uses FIFO replacement");
    first->fval = 303;
    G->frame_no = 102;
    pool.GetElementIndex(0, true, false);
    require(first->fval == 0, "spatial expiry resets variable contents");
    pool.ResetPool();
    require(pool.GetVariable(BitCastToFloat(spatial_key(0, 0, 0)), false, false, true) ==
        pool.GetVariable(BitCastToFloat(spatial_key(4095, 255, 4095)), false, false, true),
        "spatial pool finds nearest cell across torus boundary");
    customResourcePools.clear(); customResources.clear();
    persistent_variables.clear(); command_list_globals.clear();
    G->frame_no = saved_frame;
    G->gFrameTime = saved_frame_time;
}

static void execute_pool_directive(CommandListState& state, const wchar_t* lhs, const wchar_t* rhs)
{
    CommandListScope scope;
    CommandList list;
    list.scope = &scope;
    const std::wstring ini_namespace;
    std::wstring value = rhs;
    require(ParseCommandListResourceCopyTargetDirective(L"CommandListRegression", lhs, &value, &list,
        &ini_namespace), "parse real pool directive");
    for (const auto& command : list.commands)
        command->run(&state);
}

static void test_pool_variables(CommandListState& state)
{
    HackerContext context(state.mOrigDevice1, state.mOrigContext1);
    state.mHackerContext = &context;
    const bool saved_dirty = G->user_config_dirty;
    auto& values = make_pool(L"poolvalues", PoolIndexType::RING, 3, UINT32_MAX);
    values.variable_template->flags = VariableFlags::PERSIST;
    make_pool(L"poolindices", PoolIndexType::RING, 2, UINT32_MAX);
    execute_pool_directive(state, L"$poolvalues[0]", L"1.25");
    execute_pool_directive(state, L"$poolindices[0]", L"0");
    execute_pool_directive(state, L"$poolindices[1]", L"1");
    execute_pool_directive(state, L"$poolvalues[$poolindices[$poolindices[1]] + 1]", L"$poolvalues[0] + 2");
    require(values.GetVariable(2, false, true, false)->fval == 3.25f, "recursive pool-index expression");
    G->user_config_dirty = false;
    execute_pool_directive(state, L"$poolvalues[*]", L"$poolvalues[0] + 1");
    for (size_t i = 0; i < values.GetPoolSize(); ++i)
        require(values.GetVariable(static_cast<float>(i), false, true, false)->fval == 2.25f,
            "full-range assignment evaluates source once");
    require(G->user_config_dirty && persistent_variables.size() == 3, "persistent pool variables registered and marked dirty");
    G->user_config_dirty = false;
    execute_pool_directive(state, L"$poolvalues[*]", L"null");
    require(values.GetVariable(0, false, true, false)->fval == 0 && G->user_config_dirty,
        "persistent range reset returns to default and marks dirty");
    auto& proxy = make_pool(L"poolproxy", PoolIndexType::RING, 1, UINT32_MAX);
    execute_pool_directive(state, L"poolproxy", L"ref poolvalues");
    require(proxy.GetPoolSize() == 3 && proxy.ResolvePool() == &values, "pool reference resolves descriptor");
    execute_pool_directive(state, L"$poolproxy[0]", L"9");
    require(values.GetVariable(0, false, true, false)->fval == 9, "pool proxy writes target variable");
    auto& clone = make_pool(L"poolclone", PoolIndexType::FIFO, 1, UINT32_MAX);
    execute_pool_directive(state, L"poolclone", L"copy_desc poolproxy");
    {
        CommandList rejected;
        CommandListScope scope;
        rejected.scope = &scope;
        const std::wstring ini_namespace;
        std::wstring invalid = L"copy poolvalues";
        require(!ParseCommandListResourceCopyTargetDirective(L"CommandListRegression", L"poolclone",
            &invalid, &rejected, &ini_namespace), "unsupported pool deep copy rejected");
    }
    require(clone.GetPoolSize() == 3 && clone.index_type == PoolIndexType::RING &&
        clone.variable_template->flags == VariableFlags::PERSIST, "copy descriptor follows proxy source");
    values.SetSourcePool(&proxy);
    require(values.ResolvePool() == &values, "proxy cycle refused");
    auto& chain = make_pool(L"poolchain", PoolIndexType::RING, 1, UINT32_MAX);
    chain.SetSourcePool(&proxy);
    require(chain.GetPoolSize() == 3, "multi-hop proxy resolution");
    execute_pool_directive(state, L"poolproxy", L"null");
    require(proxy.GetPoolSize() == 1 && values.GetVariable(0, false, true, false)->fval == 9,
        "pool reset detaches proxy without clearing target");
    values.GetResource(0, false, true, true);
    require(values.GetVariable(0, false, true, false)->fval == 0, "resource assignment resets prior variable type");
    values.element_type_switch_reset = false;
    values.GetVariable(0, false, true, true)->fval = 11;
    values.GetResource(0, false, true, true);
    require(values.GetVariable(0, false, true, false)->fval == 11, "mixed element preserves variable and resource state");
    customResourcePools.clear(); customResources.clear();
    persistent_variables.clear(); command_list_globals.clear();
    G->user_config_dirty = saved_dirty;
    state.mHackerContext = nullptr;
}

static void test_batch_copy(CommandListState& state, ID3D11Buffer* source, const uint32_t* expected)
{
    HackerContext hacker_context(state.mOrigDevice1, state.mOrigContext1);
    state.mHackerContext = &hacker_context;
    auto& pool = make_pool(L"poolbatch", PoolIndexType::FIFO, 3, 10);
    pool.GetResource(10, false, false, true);
    pool.GetResource(20, false, false, true);
    const size_t original_slot = pool.GetElementIndex(20, false, false);
    auto& resource = customResources[L"resourcebatchsource"];
    ResourceCopyOperation operation;
    operation.src.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
    operation.src.SetCustomResource(&resource);
    operation.src.SetResource(&state, source, nullptr, 16, 0, DXGI_FORMAT_UNKNOWN, 4096);
    const std::wstring ini_namespace;
    require(operation.dst.ParseTarget(L"poolbatch", false, &ini_namespace, nullptr), "batch destination parser");
    operation.options = ResourceCopyOptions::REFERENCE;
    operation.run(&state);
    for (size_t i = 0; i < pool.GetPoolSize(); ++i) {
        auto* slot = pool.GetResource(static_cast<float>(i), false, true, false);
        require(slot && slot->resource == source && !slot->is_null, "batch reference reaches every physical slot");
    }
    require(pool.GetElementIndex(20, false, false) == original_slot && operation.dst.type == ResourceCopyTargetType::POOL &&
        operation.dst.custom_resource_pool == &pool, "batch preserves indexing and command destination");
    operation.options = ResourceCopyOptions::COPY;
    operation.run(&state);
    D3D11_BUFFER_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* staging = nullptr;
    require(SUCCEEDED(state.mOrigDevice1->CreateBuffer(&desc, nullptr, &staging)), "batch readback buffer");
    for (size_t i = 0; i < pool.GetPoolSize(); ++i) {
        auto* slot = pool.GetResource(static_cast<float>(i), false, true, false);
        require(slot->resource != source && !slot->is_null, "batch full copy owns destination buffer");
        state.mOrigContext1->CopyResource(staging, slot->resource);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        require(SUCCEEDED(state.mOrigContext1->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "batch result readback");
        require(std::memcmp(mapped.pData, expected, 4096) == 0, "batch copy preserves full payload");
        state.mOrigContext1->Unmap(staging, 0);
    }
    staging->Release();
    execute_pool_directive(state, L"poolbatch", L"null");
    require(pool.GetElementIndex(77, false, true) == 0, "batch null resets pool indexing");
    execute_pool_directive(state, L"poolbatch[*]", L"null");
    for (size_t i = 0; i < pool.GetPoolSize(); ++i)
        require(pool.GetResource(static_cast<float>(i), false, true, false)->is_null && pool.GetResource(static_cast<float>(i), false, true, false)->buf_size == 0 && pool.GetResource(static_cast<float>(i), false, true, false)->source_stride == 0, "batch null resets all slots");
    customResourcePools.clear();
    customResources.clear();
    state.mHackerContext = nullptr;
}

static void test_pool_expiration()
{
    const unsigned saved_frame = G->frame_no;
    G->frame_no = 10;
    auto& fifo = make_pool(L"poolexpiry", PoolIndexType::FIFO, 2, 1);
    auto* first = fifo.GetResource(10, false, false, true);
    require(first && first->pool_index == 0, "FIFO starts in first physical slot");
    first->is_null = false; first->source_stride = 32; first->buf_size = 64;
    G->frame_no = 11;
    fifo.GetElementIndex(0, true, false);
    require(!first->is_null, "keep-alive inclusive boundary");
    G->frame_no = 12;
    fifo.GetElementIndex(0, true, false);
    require(first->is_null && first->source_stride == 0 && first->buf_size == 0,
        "expiration clears query metadata");
    require(fifo.GetResource(20, false, false, true)->pool_index == 1, "FIFO continues after expiry");
    fifo.GetResource(0, false, true, false);
    require(fifo.GetElementIndex(20, false, false) == 1, "physical iteration preserves keyed indexing");
    fifo.ResetPool();
    require(fifo.GetResource(30, false, false, true)->pool_index == 0, "reset restarts FIFO");
    auto& eviction = make_pool(L"poolevict", PoolIndexType::FIFO, 2, UINT32_MAX);
    auto* one = eviction.GetResource(1, false, false, true);
    auto* two = eviction.GetResource(2, false, false, true);
    require(one != two && eviction.GetResource(3, false, false, true) == one &&
        eviction.GetResource(2, false, false, false) == two, "FIFO evicts oldest slot mapping");
    const float nan_key = BitCastToFloat(UINT32_MAX);
    auto* nan_resource = eviction.GetResource(nan_key, false, false, true);
    require(nan_resource == eviction.GetResource(nan_key, false, false, false),
        "FIFO supports bitwise NaN keys after an explicit write");
    auto& ring = make_pool(L"poolring", PoolIndexType::RING, 3, 0);
    require(ring.GetResource(-1, false, false, true)->pool_index == 2 &&
        ring.GetResource(-4, false, false, true)->pool_index == 2, "negative ring indices wrap");
    require(ring.GetResource(std::numeric_limits<float>::infinity(), false, false, false) == nullptr,
        "nonfinite ring index rejected");
    auto* zero = ring.GetResource(0, false, false, true);
    zero->is_null = false;
    ++G->frame_no; ring.GetElementIndex(0, true, false);
    require(zero->is_null, "zero keep-alive expires next frame");
    auto& wrapped = make_pool(L"poolwrap", PoolIndexType::FIFO, 1, 1);
    G->frame_no = UINT_MAX;
    auto* last = wrapped.GetResource(7, false, false, true); last->is_null = false;
    G->frame_no = 1; wrapped.GetElementIndex(0, true, false);
    require(last->is_null, "frame wrap and UINT_MAX update timestamp");
    auto& eager = make_pool(L"pooleager", PoolIndexType::RING, 3, UINT32_MAX, false);
    require(customResources.count(L"pooleager_0") && customResources.count(L"pooleager_2") &&
        command_list_globals.count(L"$pooleager[2]"), "eager resources and variables registered");
    auto& empty = make_pool(L"poolempty", PoolIndexType::FIFO, 0, 0);
    require(empty.GetResource(1, false, false, false) == nullptr, "empty pool lookup");
    empty.ResetPool();
    customResourcePools.clear(); customResources.clear();
    persistent_variables.clear(); command_list_globals.clear();
    G->frame_no = saved_frame;
}

void test_dx11_initialization_gate_regression();
void test_dx11_parser_optimization_regression();
void test_dx11_input_scope_regression();
void test_dx11_persist_unknown_pool_regression();

void test_dx11_file_append_regression();
void test_dx11_resource_metadata_v2_regression();
void test_dx11_ini_parser_v2_regression();
void test_dx11_persist_settings_v2_regression();

void test_dx11_slot_batch_regression();
void test_dx11_pool_range_flags_regression();
void test_dx11_texture_override_cache_regression();
void test_dx11_profiling_generation_regression();
void test_dx11_version_regression();
void test_dx11_dynamic_slot_regression();
void test_dx11_dynamic_pipeline_slot_regression();
void test_dx11_slot_range_regression();
void test_dx11_slot_range_cb_regression();
void test_dx11_slot_range_exception_regression();
void test_dx11_range_capture_example_regression();
void test_dx11_cache_resource_data_regression();
void test_dx11_store_cache_regression();
void test_dx11_check_to_range_regression();
void test_dx11_view_reference_regression();
void test_dx11_conditional_batch_regression();
void test_dx11_conditional_batch_followup_regression();
void test_dx11_region_reference_followup_regression();
void test_dx11_region_followup_regression();
void test_dx11_stream_output_regression();
void test_dx11_shader_summary_regression();
void test_dx11_shader_dump_filter_regression();
void test_dx11_stream_output_append_regression();
void test_dx11_texture_upload_state_regression();
void test_dx11_original_resource_metadata_regression();
void test_dx11_dhash_gpu_regression();
void test_dx11_member_argument_regression();
void test_dx11_dhash_cache_regression();
void test_dx11_hamming_distance_regression();
void test_dx11_adversarial_region_regression();
void test_dx11_adversarial_dhash_regression();
void test_dx11_adversarial_store_regression();
void test_dx11_round2_spatial_boundary_regression();
void test_dx11_round2_handle_info_exception_regression();
void test_dx11_round2_reload_snapshot_regression();
void test_dx11_round2_region_clamp_regression();
void test_dx11_round2_snapshot_stride_regression();
void test_dx11_round3_nondraw_metadata_regression();
void test_dx11_round3_composition_regression();
void test_dx11_round3_lifetime_regression();
void test_dx11_round3_oracle_regression();
void test_dx11_round4_alias_regression();
void test_dx11_round4_gpu_regression();
void test_dx11_round4_config_regression();
void test_dx11_round5_assignment_regression();
void test_dx11_round5_config_regression();
void test_dx11_round7_descriptor_regression();
void test_dx11_round8_pool_regression();
void test_dx11_dump_range_regression();
void test_dx11_slot_range_numeric_regression();

int main()
{
    InitializeCriticalSection(&G->mCriticalSection);
    InitializeCriticalSection(&G->mResourcesLock);
    InitializeCriticalSection(&resource_creation_mode_lock);
    test_dx11_version_regression();
    try {
        test_dx11_slot_range_numeric_regression();
        test_dx11_dynamic_slot_regression();
        test_dx11_dynamic_pipeline_slot_regression();
        test_dx11_slot_range_regression();
        test_dx11_slot_range_cb_regression();
        test_dx11_slot_range_exception_regression();
        test_dx11_range_capture_example_regression();
        test_dx11_cache_resource_data_regression();
        test_dx11_store_cache_regression();
        test_dx11_check_to_range_regression();
        test_dx11_view_reference_regression();
        test_dx11_conditional_batch_regression();
        test_dx11_conditional_batch_followup_regression();
        test_dx11_region_reference_followup_regression();
        test_dx11_region_followup_regression();
        test_dx11_stream_output_regression();
        test_dx11_shader_summary_regression();
        test_dx11_shader_dump_filter_regression();
        test_dx11_stream_output_append_regression();
        test_dx11_original_resource_metadata_regression();
        test_dx11_dhash_gpu_regression();
        test_dx11_member_argument_regression();
        test_dx11_dhash_cache_regression();
        test_dx11_hamming_distance_regression();
        test_dx11_adversarial_region_regression();
        test_dx11_adversarial_dhash_regression();
        test_dx11_adversarial_store_regression();
        test_dx11_round2_spatial_boundary_regression();
        test_dx11_round2_handle_info_exception_regression();
        test_dx11_round2_reload_snapshot_regression();
        test_dx11_round2_region_clamp_regression();
        test_dx11_round2_snapshot_stride_regression();
        test_dx11_round3_nondraw_metadata_regression();
        bool round3_passed = true;
        for (auto test : {test_dx11_round3_composition_regression,
                test_dx11_round3_lifetime_regression, test_dx11_round3_oracle_regression}) {
            try { test(); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL independent round3 scenario: %s\n", error.what());
                round3_passed = false;
            }
        }
        require(round3_passed, "independent round3 scenarios");
        bool round4_passed = true;
        for (auto test : {test_dx11_round4_alias_regression, test_dx11_round4_gpu_regression,
                test_dx11_round4_config_regression}) {
            try { test(); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL independent round4 scenario: %s\n", error.what());
                round4_passed = false;
            }
        }
        require(round4_passed, "independent round4 scenarios");
        bool round5_passed = true;
        for (auto test : {test_dx11_round5_assignment_regression, test_dx11_round5_config_regression}) {
            try { test(); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL independent round5 scenario: %s\n", error.what());
                round5_passed = false;
            }
        }
        require(round5_passed, "independent round5 scenarios");
        test_dx11_round7_descriptor_regression();
        test_dx11_round8_pool_regression();
        test_dx11_texture_upload_state_regression();
        test_dx11_dump_range_regression();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL DX11 upstream feature integration: %s\n", error.what());
        return EXIT_FAILURE;
    }
    test_dx11_slot_batch_regression();
    test_dx11_pool_range_flags_regression();
    test_dx11_texture_override_cache_regression();
    test_dx11_profiling_generation_regression();
    test_dx11_file_append_regression();
    test_dx11_resource_metadata_v2_regression();
    test_dx11_ini_parser_v2_regression();
    test_dx11_persist_settings_v2_regression();
    test_dx11_initialization_gate_regression();
    test_dx11_parser_optimization_regression();
    test_dx11_input_scope_regression();
    test_dx11_persist_unknown_pool_regression();
    test_parser();
    test_pool_expiration();
    test_spatial_pools();
    test_com_apartments();
    std::fputs("Testing WARP windows and cache views\n", stderr);
    test_constant_buffer_windows();
    test_cache_resize();
    test_hash_map_erase();
    test_dx11_layout_override_regression();
    test_dx11_ini_expression_regression();
    test_dx11_tokenizer_regression();
    test_dx11_time_locked_regression();
    test_dx11_frame_timing_regression();
    test_dx11_unary_function_regression();
    test_dx11_readback_pool_regression();
    test_dx11_argument_reader_regression();
    test_dx11_command_list_callback_regression();
    test_dx11_draw_dispatch_regression();
    test_dx11_store_readback_regression();
    test_dx11_resource_region_regression();
    test_dx11_reader_variable_regression();
    test_dx11_pool_lifecycle_regression();
    test_dx11_utf8_tokenizer_regression();
    test_dx11_pool_missing_regression();
    test_dx11_proxy_recovery_regression();
    test_dx11_texture_properties_regression();
    test_dx11_shader_bindings_regression();
    test_dx11_enum_option_regression();
    test_dx11_persistence_regression();
    test_dx11_persist_preservation_regression();
    test_dx11_png_color_space_regression();
    test_dx11_callback_namespace_regression();
    test_dx11_color_detection_options_regression();
    test_dx11_log_level_regression();
    test_dx11_input_toggle_hold_regression();
    test_dx11_critical_section_guard_regression();
    test_dx11_store_structured_regression();
    test_dx11_legacy_source_regression();
    DeleteCriticalSection(&resource_creation_mode_lock);
    DeleteCriticalSection(&G->mResourcesLock);
    DeleteCriticalSection(&G->mCriticalSection);
    std::puts("PASS: real DX11 parser, custom lookup, pool creation/reuse/flags/destruction");
    return EXIT_SUCCESS;
}
