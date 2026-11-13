#include "CommandList.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "ShaderBindings.h"
#include "globals.h"
#include "d3d11TokenizedProgramFormat.hpp"

#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

const char common_bindings[] =
    "cbuffer BindingsC0:register(b0){float4 immediate[4];}"
    "cbuffer BindingsC1:register(b1){float4 dynamicCB[16];}"
    "cbuffer BindingsLast:register(b13){float4 lastCB;}"
    "Texture2D<float4> image:register(t0);"
    "ByteAddressBuffer raw:register(t1);"
    "StructuredBuffer<float3> structured:register(t2);"
    "Texture1DArray<float4> oneArray:register(t3);"
    "Texture2DArray<float4> twoArray:register(t4);"
    "Texture3D<float4> volume:register(t5);"
    "TextureCube<float4> cube:register(t6);"
    "TextureCubeArray<float4> cubeArray:register(t7);"
    "Texture2DMS<float4,4> multisample:register(t8);"
    "Texture2DMSArray<float4,4> multisampleArray:register(t9);"
    "Texture1D<float4> one:register(t10);"
    "Buffer<float4> typedBuffer:register(t11);"
    "Texture2D<float4> lastImage:register(t127);"
    "SamplerState bindingsSampler:register(s0);"
    "float4 bindings_read(uint i){return immediate[3]+dynamicCB[i&15]+lastCB+"
    "image.Load(int3(0,0,0))+asfloat(raw.Load(0))+float4(structured[0],1)+"
    "oneArray.Load(int3(0,0,0))+twoArray.Load(int4(0,0,0,0))+volume.Load(int4(0,0,0,0))+"
    "cube.SampleLevel(bindingsSampler,float3(1,0,0),0)+cubeArray.SampleLevel(bindingsSampler,float4(1,0,0,0),0)+"
    "multisample.Load(int2(0,0),0)+multisampleArray.Load(int3(0,0,0),0)+one.Load(int2(0,0))+"
    "typedBuffer.Load(0)+lastImage.Load(int3(0,0,0));}";

struct StageSpec {
    const char* profile;
    const char* entry;
    wchar_t stage;
};

const StageSpec stage_specs[] = {
    {"vs_5_0", "float4 main(float4 p:POSITION):SV_Position{return p+bindings_read((uint)p.x)+0.125;}", L'v'},
    {"ps_5_0", "float4 main(float4 p:SV_Position):SV_Target{return bindings_read((uint)p.x)+0.25;}", L'p'},
    {"gs_5_0", "struct V{float4 p:SV_Position;};[maxvertexcount(1)]void main(point V p[1],inout PointStream<V> output)"
        "{V o;o.p=p[0].p+bindings_read((uint)p[0].p.x)+0.375;output.Append(o);}", L'g'},
    {"cs_5_0", "RWStructuredBuffer<float4> result:register(u0);[numthreads(1,1,1)]void main(uint3 i:SV_DispatchThreadID)"
        "{result[i.x]=bindings_read(i.x)+0.5;}", L'c'},
    {"hs_5_0", "struct V{float4 p:POSITION;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};"
        "C patch(InputPatch<V,3> p,uint id:SV_PrimitiveID){C o;o.e[0]=o.e[1]=o.e[2]=o.i=1;return o;}"
        "[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)]"
        "[patchconstantfunc(\"patch\")]V main(InputPatch<V,3> p,uint i:SV_OutputControlPointID)"
        "{V o;o.p=p[i].p+bindings_read((uint)p[i].p.x)+0.625;return o;}", L'h'},
    {"ds_5_0", "struct V{float4 p:POSITION;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};"
        "[domain(\"tri\")]float4 main(C c,const OutputPatch<V,3> p,float3 uv:SV_DomainLocation):SV_Position"
        "{return p[0].p*uv.x+p[1].p*uv.y+p[2].p*uv.z+bindings_read((uint)p[0].p.x)+0.75;}", L'd'},
};

ComPtr<ID3DBlob> compile(const std::string& source, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT result = D3DCompile(source.data(), source.size(), "shader_bindings_regression", nullptr, nullptr,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, code.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors)
        std::fprintf(stderr, "Shader fixture %s: %.*s\n", profile, static_cast<int>(errors->GetBufferSize()),
            static_cast<const char*>(errors->GetBufferPointer()));
    check(SUCCEEDED(result), "compile real declaration-oracle shader");
    return code;
}

bool empty_bindings(const ShaderBindings& value)
{
    for (const auto& cb : value.constant_buffers)
        if (cb.type != ShaderConstantBufferType::NONE || cb.size != 0)
            return false;
    for (const auto& srv : value.resources)
        if (srv.type != ShaderResourceType::NONE || srv.dimension != D3D_SRV_DIMENSION_UNKNOWN || srv.stride != 0)
            return false;
    return true;
}

ShaderBindings reflected_bindings(ID3DBlob* code)
{
    ShaderBindings parsed;
    check(get_shader_bindings_from_bytecode(code->GetBufferPointer(), code->GetBufferSize(), &parsed),
        "production parser accepts real compiled shader");
    ComPtr<ID3D11ShaderReflection> reflection;
    check(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf()))), "create independent D3DReflect oracle");
    D3D11_SHADER_DESC shader = {};
    check(SUCCEEDED(reflection->GetDesc(&shader)), "read oracle shader description");
    bool cb_slots[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    bool srv_slots[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    for (UINT i = 0; i < shader.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding = {};
        check(SUCCEEDED(reflection->GetResourceBindingDesc(i, &binding)), "read oracle resource declaration");
        if (binding.Type == D3D_SIT_CBUFFER) {
            D3D11_SHADER_BUFFER_DESC cb = {};
            check(binding.BindPoint < parsed.constant_buffers.size() &&
                SUCCEEDED(reflection->GetConstantBufferByName(binding.Name)->GetDesc(&cb)), "read oracle constant buffer size");
            cb_slots[binding.BindPoint] = true;
            check(parsed.constant_buffers[binding.BindPoint].size == cb.Size / 16,
                "bytecode CB size equals reflection allocation in 16-byte constants");
        } else if (binding.Type == D3D_SIT_TEXTURE || binding.Type == D3D_SIT_BYTEADDRESS || binding.Type == D3D_SIT_STRUCTURED) {
            check(binding.BindPoint < parsed.resources.size() && binding.BindCount == 1, "oracle SRV fits actual API slots");
            srv_slots[binding.BindPoint] = true;
            const auto& actual = parsed.resources[binding.BindPoint];
            const ShaderResourceType type = binding.Type == D3D_SIT_BYTEADDRESS ? ShaderResourceType::RAW :
                binding.Type == D3D_SIT_STRUCTURED ? ShaderResourceType::STRUCTURED : ShaderResourceType::TYPED;
            check(actual.type == type && actual.dimension == (type == ShaderResourceType::RAW ?
                D3D_SRV_DIMENSION_BUFFEREX : binding.Dimension), "SRV type/dimension agrees with oracle and documented RAW BUFFEREX convention");
        }
    }
    for (size_t i = 0; i < parsed.constant_buffers.size(); ++i)
        check(cb_slots[i] || parsed.constant_buffers[i].type == ShaderConstantBufferType::NONE,
            "undeclared CB slots stay NONE");
    for (size_t i = 0; i < parsed.resources.size(); ++i)
        check(srv_slots[i] || parsed.resources[i].type == ShaderResourceType::NONE,
            "undeclared SRV slots stay NONE");
    return parsed;
}

uint32_t word(const std::vector<uint8_t>& bytes, size_t offset)
{
    uint32_t value;
    check(offset <= bytes.size() && bytes.size() - offset >= sizeof(value), "fixture DXBC word bounds");
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void put_word(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    check(offset <= bytes.size() && bytes.size() - offset >= sizeof(value), "fixture mutation word bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void rejects_and_clears(const std::vector<uint8_t>& bytes)
{
    ShaderBindings output;
    output.constant_buffers[13] = {ShaderConstantBufferType::DYNAMIC_INDEXED, 123};
    output.resources[127] = {ShaderResourceType::RAW, D3D_SRV_DIMENSION_BUFFEREX, 16};
    check(!get_shader_bindings_from_bytecode(bytes.data(), bytes.size(), &output) && empty_bindings(output),
        "malformed bytecode clears all stale/partial output declarations");
}

void test_parser_failures(ID3DBlob* code)
{
    const size_t size = code->GetBufferSize();
    const auto* data = static_cast<const uint8_t*>(code->GetBufferPointer());
    ShaderBindings parsed;
    std::vector<uint8_t> unaligned(size + 1);
    std::memcpy(unaligned.data() + 1, data, size);
    check(get_shader_bindings_from_bytecode(unaligned.data() + 1, size, &parsed), "production parser supports unaligned DXBC input");
    for (size_t length = 0; length < size; ++length) {
        parsed.resources[127].type = ShaderResourceType::RAW;
        check(!get_shader_bindings_from_bytecode(data, length, &parsed) && empty_bindings(parsed),
            "every truncated real container rejects and clears reused output");
    }
    check(!get_shader_bindings_from_bytecode(nullptr, size, &parsed) && empty_bindings(parsed) &&
        !get_shader_bindings_from_bytecode(data, size, nullptr), "null parser input/output safely rejected");
    const std::vector<uint8_t> original(data, data + size);
    for (const size_t offset : {size_t(0), size_t(24), size_t(28), size_t(32)}) {
        auto bad = original;
        put_word(bad, offset, UINT32_MAX);
        rejects_and_clears(bad);
    }
    const uint32_t chunks = word(original, 28);
    check(chunks >= 2, "real DXBC fixture has a chunk table");
    auto overlapping = original;
    put_word(overlapping, 36, word(original, 32));
    rejects_and_clears(overlapping);
    bool exercised_program = false, exercised_cb = false, exercised_srv = false;
    for (uint32_t i = 0; i < chunks; ++i) {
        const size_t chunk = word(original, 32 + size_t(i) * 4);
        const uint32_t tag = word(original, chunk);
        if (tag != 0x52444853 && tag != 0x58454853)
            continue;
        exercised_program = true;
        const size_t program = chunk + 8;
        const uint32_t words = word(original, program + 4);
        auto late = original;
        put_word(late, program + size_t(words - 1) * 4, 0);
        rejects_and_clears(late);
        auto unsupported = original;
        put_word(unsupported, program, (word(original, program) & ~uint32_t(15)) | 1); // 5.1 declaration layout.
        rejects_and_clears(unsupported);
        for (size_t cursor = 2; cursor < words;) {
            const uint32_t token = word(original, program + cursor * 4);
            const auto opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
            const size_t length = opcode == D3D10_SB_OPCODE_CUSTOMDATA ? word(original, program + (cursor + 1) * 4) :
                DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
            check(length && cursor + length <= words, "compiled fixture instruction boundaries");
            if (!(token & 0x80000000u) && (opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER || opcode == D3D10_SB_OPCODE_DCL_RESOURCE)) {
                const uint32_t operand = word(original, program + (cursor + 1) * 4);
                check(!(operand & 0x80000000u), "compiler fixture uses nonextended declaration operands");
                auto invalid_slot = original;
                put_word(invalid_slot, program + (cursor + 2) * 4,
                    opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER ? 14 : 128);
                rejects_and_clears(invalid_slot);
                exercised_cb |= opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER;
                exercised_srv |= opcode == D3D10_SB_OPCODE_DCL_RESOURCE;
            }
            cursor += length;
        }
    }
    check(exercised_program && exercised_cb && exercised_srv, "real fixture exercised program and CB/SRV bounds mutations");
}

class BindingEnvironment {
    UINT hunting = G->hunting;
    ShaderHashType hash_type = G->shader_hash_type;
    bool export_shaders = G->EXPORT_SHADERS, export_fixed = G->EXPORT_FIXED,
        export_binary = G->EXPORT_BINARY, cache_shaders = G->CACHE_SHADERS;
    int export_hlsl = G->EXPORT_HLSL;
    std::wstring shader_path = G->SHADER_PATH, cache_path = G->SHADER_CACHE_PATH;
public:
    BindingEnvironment()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate shader bindings executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory)
            in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "shader bindings fixture must stay in build/");
        const auto absent = directory / (L"shader_bindings_no_fixes_" + std::to_wstring(GetCurrentProcessId()));
        check(!std::filesystem::exists(absent), "shader bindings fixture directory must not contain replacement files");
        check(absent.wstring().size() < MAX_PATH, "shader fixture path fits production buffers");
        wcscpy_s(G->SHADER_PATH, absent.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, absent.c_str());
        G->hunting = 0;
        G->shader_hash_type = ShaderHashType::FNV;
        G->EXPORT_SHADERS = G->EXPORT_FIXED = G->EXPORT_BINARY = G->CACHE_SHADERS = false;
        G->EXPORT_HLSL = 0;
    }
    ~BindingEnvironment()
    {
        G->hunting = hunting;
        G->shader_hash_type = hash_type;
        G->EXPORT_SHADERS = export_shaders;
        G->EXPORT_FIXED = export_fixed;
        G->EXPORT_BINARY = export_binary;
        G->CACHE_SHADERS = cache_shaders;
        G->EXPORT_HLSL = export_hlsl;
        wcscpy_s(G->SHADER_PATH, shader_path.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, cache_path.c_str());
    }
};

class BindingCacheScope {
    std::vector<UINT64> owned;
    std::unordered_map<UINT64, ShaderBindings> original;
public:
    void track(UINT64 hash)
    {
        if (std::find(owned.begin(), owned.end(), hash) != owned.end())
            return;
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        const auto previous = G->mShaderBindingsCache.find(hash);
        if (previous != G->mShaderBindingsCache.end()) {
            original.emplace(hash, previous->second);
            G->mShaderBindingsCache.erase(previous);
        }
        owned.push_back(hash);
    }
    ~BindingCacheScope()
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        for (UINT64 hash : owned) {
            const auto previous = original.find(hash);
            if (previous == original.end())
                G->mShaderBindingsCache.erase(hash);
            else
                G->mShaderBindingsCache[hash] = previous->second;
        }
    }
};

class BindingDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    std::vector<ComPtr<ID3D11DeviceChild>> shaders;
    CommandListState state;

    BindingDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate shader binding WARP runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load shader binding WARP runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve shader binding device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            base.GetAddressOf(), nullptr, immediate.GetAddressOf())) && SUCCEEDED(base.As(&native)) &&
            SUCCEEDED(immediate.As(&context)), "create shader binding real device/context");
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
    ~BindingDevice()
    {
        wrapper->ClearState();
        for (const auto& shader : shaders)
            CleanupShaderMaps(shader.Get());
        shaders.clear();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }

    template<typename T>
    void keep(T* shader)
    {
        ComPtr<ID3D11DeviceChild> child;
        check(SUCCEEDED(shader->QueryInterface(__uuidof(ID3D11DeviceChild), reinterpret_cast<void**>(child.GetAddressOf()))),
            "hold created shader until registry cleanup");
        shaders.push_back(child);
    }
};

float evaluate(const std::wstring& text, CommandListState& state)
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    check(expression.parse(&text, &ini_namespace, &scope), "parse production shader member expression");
    return expression.evaluate(&state);
}

std::wstring stage_name(wchar_t stage)
{
    return std::wstring(1, stage) + L"s";
}

void create_and_bind(BindingDevice& device, wchar_t stage, ID3DBlob* code)
{
    const void* data = code->GetBufferPointer();
    const size_t size = code->GetBufferSize();
    switch (stage) {
    case L'v': {
        ComPtr<ID3D11VertexShader> shader;
        check(SUCCEEDED(device.device->CreateVertexShader(data, size, nullptr, shader.GetAddressOf())), "real CreateVertexShader cache path");
        device.keep(shader.Get()); device.wrapper->VSSetShader(shader.Get(), nullptr, 0); break;
    }
    case L'p': {
        ComPtr<ID3D11PixelShader> shader;
        check(SUCCEEDED(device.device->CreatePixelShader(data, size, nullptr, shader.GetAddressOf())), "real CreatePixelShader cache path");
        device.keep(shader.Get()); device.wrapper->PSSetShader(shader.Get(), nullptr, 0); break;
    }
    case L'g': {
        ComPtr<ID3D11GeometryShader> shader;
        check(SUCCEEDED(device.device->CreateGeometryShader(data, size, nullptr, shader.GetAddressOf())), "real CreateGeometryShader cache path");
        device.keep(shader.Get()); device.wrapper->GSSetShader(shader.Get(), nullptr, 0); break;
    }
    case L'c': {
        ComPtr<ID3D11ComputeShader> shader;
        check(SUCCEEDED(device.device->CreateComputeShader(data, size, nullptr, shader.GetAddressOf())), "real CreateComputeShader cache path");
        device.keep(shader.Get()); device.wrapper->CSSetShader(shader.Get(), nullptr, 0); break;
    }
    case L'h': {
        ComPtr<ID3D11HullShader> shader;
        check(SUCCEEDED(device.device->CreateHullShader(data, size, nullptr, shader.GetAddressOf())), "real CreateHullShader cache path");
        device.keep(shader.Get()); device.wrapper->HSSetShader(shader.Get(), nullptr, 0); break;
    }
    case L'd': {
        ComPtr<ID3D11DomainShader> shader;
        check(SUCCEEDED(device.device->CreateDomainShader(data, size, nullptr, shader.GetAddressOf())), "real CreateDomainShader cache path");
        device.keep(shader.Get()); device.wrapper->DSSetShader(shader.Get(), nullptr, 0); break;
    }
    default: check(false, "unsupported shader fixture stage");
    }
}

void test_shader_target_parser()
{
    const std::wstring ini_namespace;
    CommandListScope scope;
    ShaderTarget target;
    for (const auto& spec : stage_specs)
        check(target.ParseTarget(stage_name(spec.stage).c_str(), true, &ini_namespace, &scope) &&
            target.shader_type == spec.stage, "ShaderTarget parses six exact pipeline stages");
    check(target.ParseTarget(L"vs->srv_type((1+1))", true, &ini_namespace, &scope) &&
        target.member_args[0].expression && !target.ParseTarget(nullptr, true, &ini_namespace, &scope) &&
        !target.member_args[0].expression, "null repeated shader parse safely clears member state");
    for (const wchar_t* invalid : {L"", L"xs", L"vsjunk", L"vs->srv_type()", L"vs->srv_type(1,2)",
        L"vs->srv_type((1+2]", L"vs->cb_size(,)", L"vs-cb0"})
        check(!target.ParseTarget(invalid, true, &ini_namespace, &scope), "ShaderTarget rejects malformed syntax and resource slots");
    ResourceCopyTarget resource;
    check(resource.ParseTarget(L"vs-cb0", true, &ini_namespace, &scope) &&
        resource.type == ResourceCopyTargetType::CONSTANT_BUFFER, "resource pipeline parser retains priority over shader stage prefix");
    const std::wstring dynamic_shader = L"vs->cb_type(0)";
    CommandListExpression no_scope;
    check(!no_scope.parse(&dynamic_shader, &ini_namespace, nullptr), "shader runtime queries are unavailable in static INI expression context");
}

class BindingFilterScope {
    UINT64 hash;
    ShaderOverrideMap::node_type previous;
public:
    explicit BindingFilterScope(UINT64 value) : hash(value)
    {
        CriticalSectionGuard(&G->mCriticalSection);
        previous = G->mShaderOverrideMap.extract(hash);
    }
    ~BindingFilterScope()
    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->mShaderOverrideMap.erase(hash);
        if (!previous.empty())
            G->mShaderOverrideMap.insert(std::move(previous));
    }
    void set(float value)
    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->mShaderOverrideMap[hash].filter_index = value;
    }
};

void test_plain_shader_filter(BindingDevice& device, wchar_t stage, UINT64 hash)
{
    BindingFilterScope fixture(hash);
    const auto name = stage_name(stage);
    const float missing = evaluate(name, device.state);
    check(missing == 0 && !std::signbit(missing), "plain bound shader without override returns positive zero");
    fixture.set(FLT_MAX);
    check(evaluate(name, device.state) == 1, "plain shader with default override retains one sentinel");
    fixture.set(7.25f);
    check(evaluate(name, device.state) == 7.25f, "plain shader still reads explicit override filter_index");
    fixture.set(-0.0f);
    const float zero = evaluate(name, device.state);
    check(zero == 0 && std::signbit(zero), "plain shader preserves explicit signed-zero filter_index");
}

void test_stage_members(BindingDevice& device, wchar_t stage)
{
    const std::wstring prefix = stage_name(stage);
    check(evaluate(prefix + L"->cb_mask", device.state) == 0 &&
        evaluate(prefix + L"->srv_mask", device.state) == 0, "recognized mask names retain upstream unimplemented zero behavior");
    check(evaluate(prefix + L"->cb_type(0)", device.state) == 1 && evaluate(prefix + L"->cb_size(0)", device.state) == 4 &&
        evaluate(prefix + L"->cb_type(1)", device.state) == 2 && evaluate(prefix + L"->cb_size(1)", device.state) == 16,
        "bound shader members expose immediate/dynamic CB declarations and constant-unit sizes");
    check(evaluate(prefix + L"->srv_type(0)", device.state) == 1 && evaluate(prefix + L"->srv_type(1)", device.state) == 3 &&
        evaluate(prefix + L"->srv_type(2)", device.state) == 2 && evaluate(prefix + L"->srv_stride(2)", device.state) == 12 &&
        evaluate(prefix + L"->srv_dimension(1)", device.state) == D3D_SRV_DIMENSION_BUFFEREX,
        "bound shader members expose typed/raw/structured resources with actual stride and dimension");
    check(evaluate(prefix + L"->cb_type(2)", device.state) == 0 && evaluate(prefix + L"->cb_size(2)", device.state) == -1 &&
        evaluate(prefix + L"->srv_type(126)", device.state) == 0 && evaluate(prefix + L"->srv_stride(126)", device.state) == -1 &&
        evaluate(prefix + L"->srv_dimension(126)", device.state) == -1, "undeclared slots distinguish NONE type from missing attributes");
    check(evaluate(prefix + L"->cb_size(13)", device.state) == 1 && evaluate(prefix + L"->srv_type(127)", device.state) == 1 &&
        evaluate(prefix + L"->cb_type(13.5)", device.state) == 1 && evaluate(prefix + L"->srv_type(127.5)", device.state) == 1,
        "last valid API slots and upstream positive fractional truncation remain valid");
    for (const wchar_t* index : {L"-1", L"nan", L"inf", L"1e30", L"14"})
        check(evaluate(prefix + L"->cb_size(" + index + L")", device.state) == -1, "CB slot bounds validate before integer conversion");
    for (const wchar_t* index : {L"-0.5", L"nan", L"inf", L"4294967295", L"128"})
        check(evaluate(prefix + L"->srv_dimension(" + index + L")", device.state) == -1, "SRV slot bounds validate before integer conversion");
}

class BindingVariable {
public:
    const std::wstring name = L"$shader_bindings_regression_slot";
    BindingVariable()
    {
        check(command_list_globals.count(name) == 0, "shader member variable fixture must be unique");
        command_list_globals.emplace(name, CommandListVariable(name, 0, VariableFlags::GLOBAL));
    }
    ~BindingVariable() { command_list_globals.erase(name); }
};

void test_dynamic_and_missing(BindingDevice& device, UINT64 vertex_hash)
{
    BindingVariable variable;
    const std::wstring text = L"vs->srv_stride(($shader_bindings_regression_slot+1)*2)";
    const std::wstring ini_namespace;
    CommandListScope scope;
    CommandListExpression expression;
    check(expression.parse(&text, &ini_namespace, &scope) && expression.evaluate(&device.state) == 12,
        "member slot preserves nested dynamic expression");
    command_list_globals.at(variable.name).fval = 62;
    check(expression.evaluate(&device.state) == -1, "same parsed member observes updated dynamic slot");
    ShaderBindings snapshot;
    check(device.device->GetShaderBindings(vertex_hash, &snapshot), "retrieve actual vertex binding snapshot");
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        G->mShaderBindingsCache.erase(vertex_hash);
    }
    const float missing = evaluate(L"vs->cb_type(0)", device.state);
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        G->mShaderBindingsCache[vertex_hash] = snapshot;
    }
    check(missing == 0 && !std::signbit(missing), "bound registered shader with missing cache safely returns positive zero");
    CommandListState missing_context;
    check(evaluate(L"vs->cb_type(0)", missing_context) == 0, "missing runtime HackerContext is a safe shader-query failure");
    CommandListExpression missing_state;
    const std::wstring shader_query = L"vs->srv_type(0)";
    check(missing_state.parse(&shader_query, &ini_namespace, &scope) &&
        missing_state.evaluate(nullptr, device.device) == 0, "null state with explicit device fails shader query safely");
}

void test_failed_cache_and_api(BindingDevice& device, BindingCacheScope& cache)
{
    const unsigned char invalid[] = {'n','o','t','d','x','b','c','!'};
    const UINT64 hash = fnv_64_buf(invalid, sizeof(invalid));
    cache.track(hash);
    ComPtr<ID3D11VertexShader> shader;
    check(FAILED(device.device->CreateVertexShader(invalid, sizeof(invalid), nullptr, shader.GetAddressOf())) && !shader,
        "native shader creation rejects malformed bytecode");
    ShaderBindings output;
    check(device.device->GetShaderBindings(hash, &output) && empty_bindings(output),
        "failed declaration parsing has an explicit empty cached snapshot");
    UINT64 absent = hash ^ UINT64{0x5d49a2c17b38e061};
    while (device.device->GetShaderBindings(absent, &output))
        ++absent;
    output.constant_buffers[13] = {ShaderConstantBufferType::DYNAMIC_INDEXED, 64};
    output.resources[127] = {ShaderResourceType::RAW, D3D_SRV_DIMENSION_BUFFEREX, 4};
    check(!device.device->GetShaderBindings(absent, &output) && empty_bindings(output) &&
        !device.device->GetShaderBindings(hash, nullptr), "missing cache lookup clears all output and null snapshot pointer is rejected");
}

void test_stream_output_boundary(BindingDevice& device, BindingCacheScope& cache)
{
    std::string body = stage_specs[2].entry;
    const size_t constant = body.find("0.375");
    check(constant != std::string::npos, "stream-output fixture has unique shader constant");
    body.replace(constant, 5, "0.875");
    auto code = compile(std::string(common_bindings) + body, "gs_5_0");
    const UINT64 hash = fnv_64_buf(code->GetBufferPointer(), code->GetBufferSize());
    cache.track(hash);
    D3D11_SO_DECLARATION_ENTRY declaration = {0, "SV_Position", 0, 0, 4, 0};
    const UINT stride = 16;
    ComPtr<ID3D11GeometryShader> shader;
    check(SUCCEEDED(device.device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(), code->GetBufferSize(),
        &declaration, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, shader.GetAddressOf())),
        "create actual geometry shader through upstream stream-output bypass path");
    device.keep(shader.Get());
    device.wrapper->GSSetShader(shader.Get(), nullptr, 0);
    ShaderBindings output;
    const float query = evaluate(L"gs->cb_type(0)", device.state);
    check(!device.device->GetShaderBindings(hash, &output) && empty_bindings(output) && query == 0 && !std::signbit(query),
        "current stream-output path remains uncached and shader getter handles missing original registration safely");
}

void test_concurrent_cache(BindingDevice& device, BindingCacheScope& cache, ID3DBlob* first, ID3DBlob* second)
{
    const UINT64 hashes[] = {fnv_64_buf(first->GetBufferPointer(), first->GetBufferSize()),
        fnv_64_buf(second->GetBufferPointer(), second->GetBufferSize())};
    cache.track(hashes[0]); cache.track(hashes[1]);
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        G->mShaderBindingsCache.erase(hashes[0]);
        G->mShaderBindingsCache.erase(hashes[1]);
    }
    ID3DBlob* programs[] = {first, second};
    std::atomic<bool> good{true};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker)
        workers.emplace_back([&, worker] {
            const unsigned index = worker % 2;
            for (unsigned i = 0; i < 8; ++i) {
                ComPtr<ID3D11VertexShader> shader;
                if (FAILED(device.device->CreateVertexShader(programs[index]->GetBufferPointer(), programs[index]->GetBufferSize(),
                    nullptr, shader.GetAddressOf()))) {
                    good = false;
                    continue;
                }
                ShaderBindings snapshot;
                if (!device.device->GetShaderBindings(hashes[index], &snapshot) ||
                    snapshot.constant_buffers[0].size != (index ? 8 : 4) || snapshot.resources[2].stride != 12 ||
                    snapshot.resources[127].type != ShaderResourceType::TYPED)
                    good = false;
                CleanupShaderMaps(shader.Get());
            }
        });
    for (auto& worker : workers)
        worker.join();
    check(good.load(), "concurrent real CreateShader publishes complete same/different hash cache snapshots");
}

void run_shader_bindings_regression()
{
    BindingEnvironment environment;
    BindingCacheScope cache;
    BindingDevice device;
    test_shader_target_parser();
    for (const auto& spec : stage_specs) {
        const float none = evaluate(stage_name(spec.stage) + L"->srv_type(0)", device.state);
        check(none == 0 && std::signbit(none), "no bound shader retains negative-zero member result");
        const float plain_none = evaluate(stage_name(spec.stage), device.state);
        check(plain_none == 0 && std::signbit(plain_none), "plain no-shader filter retains negative zero");
    }
    std::vector<ComPtr<ID3DBlob>> code;
    std::vector<UINT64> hashes;
    for (const auto& spec : stage_specs) {
        code.push_back(compile(std::string(common_bindings) + spec.entry, spec.profile));
        const ShaderBindings parsed = reflected_bindings(code.back().Get());
        check(parsed.constant_buffers[0].type == ShaderConstantBufferType::IMMEDIATE_INDEXED &&
            parsed.constant_buffers[1].type == ShaderConstantBufferType::DYNAMIC_INDEXED && parsed.resources[2].stride == 12,
            "real compiler declarations preserve CB indexing kinds and float3 structured stride");
        const UINT64 hash = fnv_64_buf(code.back()->GetBufferPointer(), code.back()->GetBufferSize());
        hashes.push_back(hash);
        cache.track(hash);
        create_and_bind(device, spec.stage, code.back().Get());
        ShaderBindings snapshot;
        check(device.device->GetShaderBindings(hash, &snapshot) && snapshot.constant_buffers[0].size == 4 &&
            snapshot.resources[2].stride == 12, "actual CreateShader caches original FNV-bytecode declarations");
        snapshot.resources[2].stride = 999;
        check(device.device->GetShaderBindings(hash, &snapshot) && snapshot.resources[2].stride == 12,
            "GetShaderBindings returns an owning snapshot rather than mutable cache storage");
        test_stage_members(device, spec.stage);
        test_plain_shader_filter(device, spec.stage, hash);
    }
    test_parser_failures(code[0].Get());
    test_dynamic_and_missing(device, hashes[0]);
    test_failed_cache_and_api(device, cache);
    test_stream_output_boundary(device, cache);
    for (const char* profile : {"vs_4_0", "vs_4_1"}) {
        auto sm4 = compile("cbuffer C:register(b0){float4 a[4];}Texture2D<float4> t:register(t3);"
            "float4 main(float4 p:POSITION):SV_Position{return p+a[3]+t.Load(int3(0,0,0));}", profile);
        const auto parsed = reflected_bindings(sm4.Get());
        check(parsed.constant_buffers[0].size == 4 && parsed.resources[3].dimension == D3D_SRV_DIMENSION_TEXTURE2D,
            "real SM4.0/4.1 bytecode agrees with reflection");
    }
    std::string other_declarations = common_bindings;
    const size_t declaration = other_declarations.find("immediate[4]");
    const size_t access = other_declarations.find("immediate[3]");
    check(declaration != std::string::npos && access != std::string::npos, "parallel fixture locates distinct CB allocation and used final element");
    other_declarations.replace(declaration, std::strlen("immediate[4]"), "immediate[8]");
    other_declarations.replace(access, std::strlen("immediate[3]"), "immediate[7]");
    auto other_vertex = compile(other_declarations +
        "float4 main(float4 p:POSITION):SV_Position{return p+bindings_read((uint)p.x)+19.125;}", "vs_5_0");
    test_concurrent_cache(device, cache, code[0].Get(), other_vertex.Get());
}
} // namespace

void test_dx11_shader_bindings_regression()
{
    try {
        run_shader_bindings_regression();
        std::fputs("PASS: real DXBC/reflection, six-stage CreateShader cache and declaration getters\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: shader bindings regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
