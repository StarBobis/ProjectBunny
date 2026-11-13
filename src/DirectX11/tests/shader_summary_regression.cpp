#include "slot_range_test_support.h"
#include "ShaderBindings.h"
#include "d3d11TokenizedProgramFormat.hpp"
#include "util.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <cmath>
#include <optional>
#include <mutex>
#include <atomic>
#include <thread>
#include <iterator>
#include <exception>

namespace {
using namespace dx11_slot_test;

struct SummaryStage {
    const char* profile;
    const char* entry;
    wchar_t stage;
};
const char declarations[] =
    "cbuffer SummaryFirst:register(b4){float4 first;}"
    "cbuffer SummaryLast:register(b13){float4 last;}"
    "ByteAddressBuffer raw:register(t9);"
    "StructuredBuffer<float4> structured:register(t63);"
    "Texture2D<float4> typed:register(t127);"
    "float4 summary_read(){return first+last+asfloat(raw.Load4(0))+structured[0]+typed.Load(int3(0,0,0));}";
const SummaryStage stages[] = {
    {"vs_5_0", "float4 main(float4 p:POSITION):SV_Position{return p+summary_read()+0.03125;}", L'v'},
    {"ps_5_0", "float4 main(float4 p:SV_Position):SV_Target{return summary_read()+0.0625;}", L'p'},
    {"gs_5_0", "struct V{float4 p:SV_Position;};[maxvertexcount(1)]void main(point V p[1],inout PointStream<V> output)"
        "{V o;o.p=p[0].p+summary_read()+0.09375;output.Append(o);}", L'g'},
    {"cs_5_0", "RWStructuredBuffer<float4> result:register(u0);[numthreads(1,1,1)]void main(uint3 i:SV_DispatchThreadID)"
        "{result[i.x]=summary_read()+0.125;}", L'c'},
    {"hs_5_0", "struct V{float4 p:POSITION;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};"
        "C patch(InputPatch<V,3> p,uint id:SV_PrimitiveID){C o;o.e[0]=o.e[1]=o.e[2]=o.i=1;return o;}"
        "[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)]"
        "[patchconstantfunc(\"patch\")]V main(InputPatch<V,3> p,uint i:SV_OutputControlPointID)"
        "{V o;o.p=p[i].p+summary_read()+0.15625;return o;}", L'h'},
    {"ds_5_0", "struct V{float4 p:POSITION;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};"
        "[domain(\"tri\")]float4 main(C c,const OutputPatch<V,3> p,float3 uv:SV_DomainLocation):SV_Position"
        "{return p[0].p*uv.x+p[1].p*uv.y+p[2].p*uv.z+summary_read()+0.1875;}", L'd'}
};

ComPtr<ID3DBlob> compile_summary(const std::string& source, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "ShaderSummaryRegression", nullptr, nullptr,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &code, &errors);
    if (FAILED(hr) && errors) std::fprintf(stderr, "shader summary %s: %.*s\n", profile,
        static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
    require_batch(SUCCEEDED(hr), "shader summary: compile actual oracle shader");
    return code;
}

class SummaryEnvironment {
    UINT hunting = G->hunting;
    ShaderHashType hash_type = G->shader_hash_type;
    std::wstring shader_path = G->SHADER_PATH, cache_path = G->SHADER_CACHE_PATH;
    std::unordered_map<UINT64, std::optional<ShaderBindings>> saved;
public:
    SummaryEnvironment()
    {
        G->hunting = 0;
        G->shader_hash_type = ShaderHashType::FNV;
        G->SHADER_PATH[0] = G->SHADER_CACHE_PATH[0] = 0;
    }
    void track(UINT64 hash)
    {
        if (saved.count(hash)) return;
        std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
        auto it = G->mShaderBindingsCache.find(hash);
        saved.emplace(hash, it == G->mShaderBindingsCache.end() ? std::optional<ShaderBindings>{} : it->second);
    }
    ~SummaryEnvironment()
    {
        {
            std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
            for (const auto& item : saved) {
                if (item.second) G->mShaderBindingsCache[item.first] = *item.second;
                else G->mShaderBindingsCache.erase(item.first);
            }
        }
        G->hunting = hunting;
        G->shader_hash_type = hash_type;
        wcscpy_s(G->SHADER_PATH, shader_path.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, cache_path.c_str());
    }
};

class SummaryDevice : public BatchDevice {
    std::vector<ComPtr<ID3D11DeviceChild>> shaders;
public:
    ~SummaryDevice()
    {
        wrapper->ClearState();
        for (const auto& shader : shaders) CleanupShaderMaps(shader.Get());
        shaders.clear();
    }
    void create_and_bind(wchar_t stage, ID3DBlob* code)
    {
        const auto* bytes = code->GetBufferPointer();
        const auto size = code->GetBufferSize();
        ComPtr<ID3D11DeviceChild> child;
        switch (stage) {
        case L'v': {
            ComPtr<ID3D11VertexShader> shader;
            require_batch(SUCCEEDED(device->CreateVertexShader(bytes, size, nullptr, &shader)), "shader summary: real VS creation");
            wrapper->VSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        case L'p': {
            ComPtr<ID3D11PixelShader> shader;
            require_batch(SUCCEEDED(device->CreatePixelShader(bytes, size, nullptr, &shader)), "shader summary: real PS creation");
            wrapper->PSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        case L'g': {
            ComPtr<ID3D11GeometryShader> shader;
            require_batch(SUCCEEDED(device->CreateGeometryShader(bytes, size, nullptr, &shader)), "shader summary: real GS creation");
            wrapper->GSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        case L'c': {
            ComPtr<ID3D11ComputeShader> shader;
            require_batch(SUCCEEDED(device->CreateComputeShader(bytes, size, nullptr, &shader)), "shader summary: real CS creation");
            wrapper->CSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        case L'h': {
            ComPtr<ID3D11HullShader> shader;
            require_batch(SUCCEEDED(device->CreateHullShader(bytes, size, nullptr, &shader)), "shader summary: real HS creation");
            wrapper->HSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        case L'd': {
            ComPtr<ID3D11DomainShader> shader;
            require_batch(SUCCEEDED(device->CreateDomainShader(bytes, size, nullptr, &shader)), "shader summary: real DS creation");
            wrapper->DSSetShader(shader.Get(), nullptr, 0); child = shader; break;
        }
        }
        shaders.push_back(std::move(child));
    }
    void unbind(wchar_t stage)
    {
        switch (stage) {
        case L'v': wrapper->VSSetShader(nullptr, nullptr, 0); break;
        case L'p': wrapper->PSSetShader(nullptr, nullptr, 0); break;
        case L'g': wrapper->GSSetShader(nullptr, nullptr, 0); break;
        case L'c': wrapper->CSSetShader(nullptr, nullptr, 0); break;
        case L'h': wrapper->HSSetShader(nullptr, nullptr, 0); break;
        case L'd': wrapper->DSSetShader(nullptr, nullptr, 0); break;
        }
    }
};

float summary_value(SummaryDevice& device, const std::wstring& text)
{
    const std::wstring ns;
    CommandListScope scope;
    scope.emplace_front();
    CommandListExpression expression;
    require_batch(expression.parse(&text, &ns, &scope), "shader summary: parse actual zero-argument public member");
    return expression.evaluate(&device.state);
}

const wchar_t* members[] = {L"->cb_slot_count", L"->cb_first_slot", L"->cb_last_slot",
    L"->srv_slot_count", L"->srv_first_slot", L"->srv_last_slot"};

void require_sparse(const ShaderBindings& bindings)
{
    require_batch(bindings.constant_buffer_slots.count == 2 && bindings.constant_buffer_slots.first == 4 &&
        bindings.constant_buffer_slots.last == 13 && bindings.resource_slots.count == 3 &&
        bindings.resource_slots.first == 9 && bindings.resource_slots.last == 127,
        "shader summary: sparse unique counts differ from their nonzero-first spans");
    require_batch(bindings.resources[9].type == ShaderResourceType::RAW &&
        bindings.resources[63].type == ShaderResourceType::STRUCTURED && bindings.resources[127].type == ShaderResourceType::TYPED,
        "shader summary: raw/structured/typed declarations all contribute exactly one slot");
}

bool summary_consistent(const ShaderBindings& bindings)
{
    ShaderSlotRange cb, srv;
    for (UINT i = 0; i < bindings.constant_buffers.size(); ++i)
        if (bindings.constant_buffers[i].type != ShaderConstantBufferType::NONE) cb.include(i);
    for (UINT i = 0; i < bindings.resources.size(); ++i)
        if (bindings.resources[i].type != ShaderResourceType::NONE) srv.include(i);
    return cb.count == bindings.constant_buffer_slots.count && cb.first == bindings.constant_buffer_slots.first &&
        cb.last == bindings.constant_buffer_slots.last && srv.count == bindings.resource_slots.count &&
        srv.first == bindings.resource_slots.first && srv.last == bindings.resource_slots.last;
}

void reflection_oracle(ID3DBlob* code, const ShaderBindings& bindings)
{
    ComPtr<ID3D11ShaderReflection> reflection;
    require_batch(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf()))), "shader summary: actual D3DReflect oracle");
    D3D11_SHADER_DESC desc = {};
    require_batch(SUCCEEDED(reflection->GetDesc(&desc)), "shader summary: reflect shader descriptor");
    std::array<bool, 14> cb = {};
    std::array<bool, 128> srv = {};
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC resource = {};
        require_batch(SUCCEEDED(reflection->GetResourceBindingDesc(i, &resource)), "shader summary: reflect bound resource");
        for (UINT n = 0; n < resource.BindCount; ++n) {
            const UINT slot = resource.BindPoint + n;
            if (resource.Type == D3D_SIT_CBUFFER) {
                require_batch(slot < cb.size(), "shader summary: reflected CB slot within physical limit"); cb[slot] = true;
            } else if (resource.Type == D3D_SIT_TEXTURE || resource.Type == D3D_SIT_TBUFFER ||
                resource.Type == D3D_SIT_STRUCTURED || resource.Type == D3D_SIT_BYTEADDRESS) {
                require_batch(slot < srv.size(), "shader summary: reflected SRV slot within physical limit"); srv[slot] = true;
            }
        }
    }
    require_batch(std::count(cb.begin(), cb.end(), true) == bindings.constant_buffer_slots.count &&
        std::count(srv.begin(), srv.end(), true) == bindings.resource_slots.count,
        "shader summary: unique declaration counts match expanded reflection binding sets");
    for (UINT i = 0; i < cb.size(); ++i)
        require_batch(cb[i] == (bindings.constant_buffers[i].type != ShaderConstantBufferType::NONE),
            "shader summary: every reflected CB declaration matches parser");
    for (UINT i = 0; i < srv.size(); ++i)
        require_batch(srv[i] == (bindings.resources[i].type != ShaderResourceType::NONE),
            "shader summary: every reflected SRV declaration matches parser");
}

uint32_t word_at(const unsigned char* bytes, size_t offset)
{
    uint32_t value;
    memcpy(&value, bytes + offset, sizeof(value));
    return value;
}
void write_word(std::vector<unsigned char>& bytes, size_t offset, uint32_t value)
{
    memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<unsigned char> duplicate_declaration(ID3DBlob* original, bool constant_buffer)
{
    const auto* bytes = static_cast<const unsigned char*>(original->GetBufferPointer());
    const size_t length = original->GetBufferSize();
    require_batch(length >= 36, "shader summary: real compiled DXBC container exists");
    const uint32_t count = word_at(bytes, 28);
    for (UINT chunk = 0; chunk < count; ++chunk) {
        const UINT offset = word_at(bytes, 32 + chunk * 4);
        const UINT tag = word_at(bytes, offset), payload = word_at(bytes, offset + 4);
        if (tag != 0x52444853u && tag != 0x58454853u) continue; // SHDR/SHEX
        require_batch(offset + 8ull + payload <= length && payload % 4 == 0,
            "shader summary: actual program payload bounded");
        std::vector<uint32_t> program(payload / 4);
        memcpy(program.data(), bytes + offset + 8, payload);
        for (size_t cursor = 2; cursor < program.size();) {
            const auto opcode = DECODE_D3D10_SB_OPCODE_TYPE(program[cursor]);
            size_t size = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(program[cursor]);
            if (opcode == D3D10_SB_OPCODE_CUSTOMDATA) size = program.at(cursor + 1);
            require_batch(size && size <= program.size() - cursor, "shader summary: real instruction boundary valid");
            if (opcode == (constant_buffer ? D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER : D3D11_SB_OPCODE_DCL_RESOURCE_RAW)) {
                const std::vector<uint32_t> declaration(program.begin() + cursor, program.begin() + cursor + size);
                program.insert(program.begin() + cursor + size, declaration.begin(), declaration.end());
                program[1] = static_cast<uint32_t>(program.size());
                // A parser-only container mutation of actual compiler tokens.
                // It is never passed to the native shader creator.
                std::vector<unsigned char> result(44 + program.size() * 4);
                memcpy(result.data(), bytes, 32);
                write_word(result, 24, static_cast<uint32_t>(result.size()));
                write_word(result, 28, 1);
                write_word(result, 32, 36);
                write_word(result, 36, tag);
                write_word(result, 40, static_cast<uint32_t>(program.size() * 4));
                memcpy(result.data() + 44, program.data(), program.size() * 4);
                return result;
            }
            cursor += size;
        }
    }
    throw std::runtime_error("shader summary: real fixture contains requested declaration token");
}

void test_duplicate_and_failure_reset(ID3DBlob* code, const ShaderBindings& populated)
{
    for (bool cb : {false, true}) {
        auto modified = duplicate_declaration(code, cb);
        ShaderBindings result;
        require_batch(get_shader_bindings_from_bytecode(modified.data(), modified.size(), &result),
            "shader summary: duplicate same-slot compiler declaration remains parseable");
        require_sparse(result);
        result = populated;
        require_batch(!get_shader_bindings_from_bytecode(modified.data(), modified.size() - 1, &result) &&
            result.constant_buffer_slots.count == 0 && result.resource_slots.count == 0 && summary_consistent(result),
            "shader summary: malformed container resets declaration arrays and both summary records together");
    }
    ShaderBindings invalid = populated;
    const unsigned char bad[] = {'D', 'X', 'B', 'C'};
    require_batch(!get_shader_bindings_from_bytecode(bad, sizeof(bad), &invalid) &&
        invalid.constant_buffer_slots.count == 0 && invalid.resource_slots.count == 0 && summary_consistent(invalid),
        "shader summary: short bytecode does not retain earlier sparse counts");
}

void test_concurrent_snapshot(SummaryEnvironment& environment, SummaryDevice& device)
{
    auto code = compile_summary(std::string(declarations) +
        "float4 main(float4 p:SV_Position):SV_Target{return summary_read()+0.28125;}", "ps_5_0");
    const auto hash = fnv_64_buf(code->GetBufferPointer(), code->GetBufferSize());
    environment.track(hash);
    {
        std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
        G->mShaderBindingsCache.erase(hash);
    }
    std::atomic<bool> done{false};
    std::exception_ptr failure;
    std::thread writer([&]() {
        try { device.create_and_bind(L'p', code.Get()); }
        catch (...) { failure = std::current_exception(); }
        done.store(true, std::memory_order_release);
    });
    bool consistent = true;
    while (!done.load(std::memory_order_acquire)) {
        ShaderBindings snapshot;
        const bool found = device.device->GetShaderBindings(hash, &snapshot);
        consistent &= summary_consistent(snapshot);
        if (found) consistent &= snapshot.constant_buffer_slots.count == 2 && snapshot.resource_slots.count == 3;
    }
    writer.join();
    if (failure) std::rethrow_exception(failure);
    require_batch(consistent, "shader summary: concurrent actual CreateShader publication exposes only complete snapshots");
    ShaderBindings snapshot;
    require_batch(device.device->GetShaderBindings(hash, &snapshot), "shader summary: real writer published cache entry");
    require_sparse(snapshot);
    {
        std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
        G->mShaderBindingsCache.erase(hash);
    }
    require_sparse(snapshot); // Owned copy survives unordered-map erase/rehash.
}
} // namespace

void test_dx11_shader_summary_regression()
{
    SummaryEnvironment environment;
    SummaryDevice device;
    ComPtr<ID3DBlob> pixel_code;
    ShaderBindings pixel_bindings;
    const float expected[] = {2, 4, 13, 3, 9, 127};
    for (const auto& stage : stages) {
        auto code = compile_summary(std::string(declarations) + stage.entry, stage.profile);
        ShaderBindings parsed;
        require_batch(get_shader_bindings_from_bytecode(code->GetBufferPointer(), code->GetBufferSize(), &parsed),
            "shader summary: production parser accepts actual six-stage sparse bytecode");
        require_sparse(parsed);
        reflection_oracle(code.Get(), parsed);
        const auto hash = fnv_64_buf(code->GetBufferPointer(), code->GetBufferSize());
        environment.track(hash);
        device.create_and_bind(stage.stage, code.Get());
        ShaderBindings snapshot;
        require_batch(device.device->GetShaderBindings(hash, &snapshot), "shader summary: actual wrapped creation caches summary");
        require_sparse(snapshot);
        const auto name = std::wstring(1, stage.stage) + L"s";
        for (size_t i = 0; i < std::size(members); ++i)
            require_batch(summary_value(device, name + members[i]) == expected[i],
                "shader summary: six-stage public members read exact sparse count/first/last");
        {
            std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
            G->mShaderBindingsCache.erase(hash);
        }
        snapshot = parsed;
        require_batch(!device.device->GetShaderBindings(hash, &snapshot) && snapshot.constant_buffer_slots.count == 0 &&
            snapshot.resource_slots.count == 0 && summary_consistent(snapshot),
            "shader summary: missing snapshot zeroes all array and summary output state");
        for (const auto* member : members) {
            const float value = summary_value(device, name + member);
            require_batch(value == 0 && !std::signbit(value), "shader summary: actual cache miss returns positive zero");
        }
        {
            std::lock_guard<std::mutex> lock(G->mShaderBindingsMutex);
            G->mShaderBindingsCache[hash] = parsed;
        }
        device.unbind(stage.stage);
        for (const auto* member : members) {
            const float value = summary_value(device, name + member);
            require_batch(value == 0 && std::signbit(value), "shader summary: actual missing shader preserves negative-zero convention");
        }
        if (stage.stage == L'p') { pixel_code = code; pixel_bindings = parsed; }
    }
    test_duplicate_and_failure_reset(pixel_code.Get(), pixel_bindings);

    auto empty = compile_summary("float4 main():SV_Target{return float4(0.34375,0.375,0.40625,1);}", "ps_5_0");
    const auto empty_hash = fnv_64_buf(empty->GetBufferPointer(), empty->GetBufferSize());
    environment.track(empty_hash);
    device.create_and_bind(L'p', empty.Get());
    ShaderBindings empty_snapshot;
    require_batch(device.device->GetShaderBindings(empty_hash, &empty_snapshot) && summary_consistent(empty_snapshot),
        "shader summary: declaration-free compiled shader has an actual cached empty summary");
    for (size_t i = 0; i < std::size(members); ++i)
        require_batch(summary_value(device, std::wstring(L"ps") + members[i]) == ((i == 0 || i == 3) ? 0 : -1),
            "shader summary: cached no-declaration endpoints are -1 while count is zero");

    const std::wstring ns;
    CommandListScope scope;
    scope.emplace_front();
    for (const wchar_t* invalid : {L"ps->cb_first_slot(0)", L"ps->srv_slot_count(1)", L"ps->cb_last_slot(1,2)",
        L"ps->srv_first_slot[0]", L"ps->srv_last_slot(1+)"}) {
        ShaderTarget target;
        require_batch(!target.ParseTarget(invalid, true, &ns, &scope), "shader summary: zero-argument members reject stray parameters");
    }
    test_concurrent_snapshot(environment, device);
}
