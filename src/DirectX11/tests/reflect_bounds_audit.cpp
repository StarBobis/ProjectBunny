// F-128: reflection chunk bounds, counts and recursive type graphs.
//
// R1   string offset at the declared chunk end, payload backed by a guard page
// R2   NumResourceBindings = 0xFFFFFFFF (and a count that passes the coarse
//      F-104 gate but not the per-table clamp)
// R3   a struct member whose type offset points back at its own type record
// R4   RDEF declares size=24 while the binding data lives in undeclared padding
// G1   a real D3DCompile ps_5_0 shader still decodes and matches D3DReflect

// Keep BinaryDecompiler's private token headers ahead of the Windows SDK, as
// the other regression TUs do. windows.h defines `interface` as a macro, which
// would corrupt structs.h's `interface` member, and _HAS_STD_BYTE keeps
// std::byte from colliding with the SDK's global byte typedef.
#define _HAS_STD_BYTE 0
#include "BinaryDecompiler/internal_includes/decode.h"
#include "BinaryDecompiler/internal_includes/reflect.h"

#include <windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <malloc.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef DX11_AUDIT_ASAN_LOCKS
#include <sanitizer/allocator_interface.h>
#endif

namespace {

void check_reflect(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("F128: " + message);
}

size_t reflect_live_bytes()
{
#ifdef DX11_AUDIT_ASAN_LOCKS
    return __sanitizer_get_current_allocated_bytes();
#else
    size_t bytes = 0;
    _HEAPINFO entry{};
    int status = 0;
    while ((status = _heapwalk(&entry)) == _HEAPOK)
    {
        if (entry._useflag == _USEDENTRY)
            bytes += entry._size;
    }
    check_reflect(status == _HEAPEND, "R2: walk the complete CRT heap");
    return bytes;
#endif
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset)
{
    check_reflect(offset <= bytes.size() && bytes.size() - offset >= sizeof(uint32_t),
        "fixture DWORD is inside the container");
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void write_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    check_reflect(offset <= bytes.size() && bytes.size() - offset >= sizeof(value),
        "fixture DWORD is writable");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    check_reflect(offset <= bytes.size() && bytes.size() - offset >= sizeof(value),
        "fixture WORD is writable");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> compile_pixel_shader(const char* source, Microsoft::WRL::ComPtr<ID3DBlob>* blob)
{
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, blob->ReleaseAndGetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr))
    {
        std::string message = "G1: compile the reflection fixture";
        if (errors)
            message += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error("F128: " + message);
    }

    const auto* data = static_cast<const uint8_t*>((*blob)->GetBufferPointer());
    return std::vector<uint8_t>(data, data + (*blob)->GetBufferSize());
}

std::vector<uint8_t> extract_code_chunk(const std::vector<uint8_t>& container)
{
    check_reflect(container.size() >= 32 && std::memcmp(container.data(), "DXBC", 4) == 0,
        "fixture has a complete DXBC header");
    const uint32_t totalSize = read_u32(container, 24);
    check_reflect(totalSize <= container.size(), "fixture totalSize is inside its bytes");
    const uint32_t chunkCount = read_u32(container, 28);
    check_reflect(size_t(chunkCount) * sizeof(uint32_t) <= container.size() - 32,
        "fixture chunk table is complete");

    for (uint32_t i = 0; i < chunkCount; ++i)
    {
        const uint32_t offset = read_u32(container, 32 + size_t(i) * sizeof(uint32_t));
        check_reflect(offset <= totalSize && totalSize - offset >= 8, "fixture chunk header is in range");
        const uint32_t payloadBytes = read_u32(container, size_t(offset) + 4);
        check_reflect(payloadBytes <= totalSize - size_t(offset) - 8, "fixture chunk payload is in range");
        if (std::memcmp(container.data() + offset, "SHDR", 4) == 0 ||
            std::memcmp(container.data() + offset, "SHEX", 4) == 0)
        {
            const auto begin = container.begin() + size_t(offset);
            return std::vector<uint8_t>(begin, begin + 8 + payloadBytes);
        }
    }

    throw std::runtime_error("F128: fixture has no SHDR/SHEX code chunk");
}

//F-128 fixture container: DXBC header, two offset slots, a real shader chunk,
//then an RDEF chunk whose header records declaredRdefSize independently of the
//bytes actually present in the vector.
std::vector<uint8_t> make_container(const std::vector<uint8_t>& shaderChunk,
                                    const std::vector<uint8_t>& rdefPayload,
                                    uint32_t declaredRdefSize)
{
    check_reflect(shaderChunk.size() >= 8 && shaderChunk.size() % 4 == 0,
        "fixture shader chunk is complete and DWORD aligned");
    check_reflect(rdefPayload.size() % 4 == 0, "fixture RDEF payload is DWORD aligned");

    const size_t rdefOffset = 32 + 2 * sizeof(uint32_t) + shaderChunk.size();
    std::vector<uint8_t> container(rdefOffset + 8 + rdefPayload.size(), 0);

    write_u32(container, 0, 0x43425844u); // 'DXBC'
    write_u32(container, 20, 1);
    write_u32(container, 24, static_cast<uint32_t>(container.size()));
    write_u32(container, 28, 2);
    write_u32(container, 32, static_cast<uint32_t>(32 + 2 * sizeof(uint32_t)));
    write_u32(container, 36, static_cast<uint32_t>(rdefOffset));

    std::memcpy(container.data() + 32 + 2 * sizeof(uint32_t), shaderChunk.data(), shaderChunk.size());
    write_u32(container, rdefOffset, 0x46454452u); // 'RDEF'
    write_u32(container, rdefOffset + 4, declaredRdefSize);
    std::memcpy(container.data() + rdefOffset + 8, rdefPayload.data(), rdefPayload.size());

    return container;
}

struct VirtualAllocation
{
    void* address = nullptr;
    ~VirtualAllocation()
    {
        if (address)
            VirtualFree(address, 0, MEM_RELEASE);
    }
};

void compare_type(const ShaderVarType& parsed, ID3D11ShaderReflectionType* reflected,
                  const std::string& path)
{
    D3D11_SHADER_TYPE_DESC desc{};
    check_reflect(reflected && SUCCEEDED(reflected->GetDesc(&desc)), path + ": type description");
    check_reflect(static_cast<uint32_t>(parsed.Class) == static_cast<uint32_t>(desc.Class),
        path + ": class");
    check_reflect(static_cast<uint32_t>(parsed.Type) == static_cast<uint32_t>(desc.Type),
        path + ": type");
    check_reflect(parsed.Rows == desc.Rows, path + ": rows");
    check_reflect(parsed.Columns == desc.Columns, path + ": columns");
    check_reflect(parsed.Elements == desc.Elements, path + ": elements");
    check_reflect(parsed.MemberCount == desc.Members, path + ": member count");

    if (!desc.Members)
        return;

    check_reflect(parsed.Members != nullptr, path + ": member storage");
    for (UINT i = 0; i < desc.Members; ++i)
    {
        ID3D11ShaderReflectionType* member = reflected->GetMemberTypeByIndex(i);
        const char* memberName = reflected->GetMemberTypeName(i);
        check_reflect(member != nullptr && memberName != nullptr, path + ": member description");
        check_reflect(parsed.Members[i].Name == memberName,
            path + ": member name " + std::string(memberName));
        compare_type(parsed.Members[i], member, path + "." + memberName);
    }
}

void compare_with_d3d_reflect(const ShaderInfo& parsed, ID3D11ShaderReflection* reflected)
{
    D3D11_SHADER_DESC shaderDesc{};
    check_reflect(SUCCEEDED(reflected->GetDesc(&shaderDesc)), "G1: shader description");
    check_reflect(parsed.ui32NumConstantBuffers == shaderDesc.ConstantBuffers, "G1: cbuffer count");
    check_reflect(parsed.ui32NumResourceBindings == shaderDesc.BoundResources, "G1: binding count");
    check_reflect(parsed.ui32NumInputSignatures == shaderDesc.InputParameters, "G1: input signature count");
    check_reflect(parsed.ui32NumOutputSignatures == shaderDesc.OutputParameters, "G1: output signature count");

    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* cbuffer = reflected->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cbufferDesc{};
        check_reflect(cbuffer && SUCCEEDED(cbuffer->GetDesc(&cbufferDesc)), "G1: cbuffer description");

        const ConstantBuffer& parsedBuffer = parsed.psConstantBuffers[i];
        check_reflect(parsedBuffer.Name == cbufferDesc.Name,
            "G1: cbuffer name " + std::string(cbufferDesc.Name));
        check_reflect(parsedBuffer.asVars.size() == cbufferDesc.Variables,
            "G1: variable count in " + std::string(cbufferDesc.Name));
        check_reflect(parsedBuffer.ui32TotalSizeInBytes == cbufferDesc.Size,
            "G1: cbuffer size " + std::string(cbufferDesc.Name));

        for (UINT j = 0; j < cbufferDesc.Variables; ++j)
        {
            ID3D11ShaderReflectionVariable* variable = cbuffer->GetVariableByIndex(j);
            D3D11_SHADER_VARIABLE_DESC variableDesc{};
            check_reflect(variable && SUCCEEDED(variable->GetDesc(&variableDesc)),
                "G1: variable description");
            const ShaderVar& parsedVariable = parsedBuffer.asVars[j];
            const std::string path = std::string(cbufferDesc.Name) + "." + variableDesc.Name;

            check_reflect(parsedVariable.Name == variableDesc.Name, "G1: variable name " + path);
            check_reflect(parsedVariable.ui32StartOffset == variableDesc.StartOffset,
                "G1: variable start offset " + path);
            check_reflect(parsedVariable.ui32Size == variableDesc.Size, "G1: variable size " + path);
            check_reflect(parsedVariable.haveDefaultValue == (variableDesc.DefaultValue != nullptr),
                "G1: default-value presence " + path);
            if (variableDesc.DefaultValue)
            {
                check_reflect(parsedVariable.pui32DefaultValues.size() * sizeof(uint32_t) == variableDesc.Size,
                    "G1: default-value size " + path);
                check_reflect(std::memcmp(parsedVariable.pui32DefaultValues.data(),
                    variableDesc.DefaultValue, variableDesc.Size) == 0,
                    "G1: default-value bytes " + path);
            }

            compare_type(parsedVariable.sType, variable->GetType(), path);
        }
    }

    for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC bindingDesc{};
        check_reflect(SUCCEEDED(reflected->GetResourceBindingDesc(i, &bindingDesc)),
            "G1: resource binding description");
        const ResourceBinding& parsedBinding = parsed.psResourceBindings[i];
        check_reflect(parsedBinding.Name == bindingDesc.Name,
            "G1: binding name " + std::string(bindingDesc.Name));
        check_reflect(static_cast<uint32_t>(parsedBinding.eType) == static_cast<uint32_t>(bindingDesc.Type),
            "G1: binding type " + std::string(bindingDesc.Name));
        check_reflect(parsedBinding.ui32BindPoint == bindingDesc.BindPoint,
            "G1: binding point " + std::string(bindingDesc.Name));
        check_reflect(parsedBinding.ui32BindCount == bindingDesc.BindCount,
            "G1: binding count " + std::string(bindingDesc.Name));
    }

    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameterDesc{};
        check_reflect(SUCCEEDED(reflected->GetInputParameterDesc(i, &parameterDesc)),
            "G1: input parameter description");
        check_reflect(parsed.psInputSignatures[i].SemanticName == parameterDesc.SemanticName,
            "G1: input semantic name");
        check_reflect(parsed.psInputSignatures[i].ui32SemanticIndex == parameterDesc.SemanticIndex,
            "G1: input semantic index");
        check_reflect(parsed.psInputSignatures[i].ui32Register == parameterDesc.Register,
            "G1: input register");
    }

    for (UINT i = 0; i < shaderDesc.OutputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameterDesc{};
        check_reflect(SUCCEEDED(reflected->GetOutputParameterDesc(i, &parameterDesc)),
            "G1: output parameter description");
        check_reflect(parsed.psOutputSignatures[i].SemanticName == parameterDesc.SemanticName,
            "G1: output semantic name");
        check_reflect(parsed.psOutputSignatures[i].ui32SemanticIndex == parameterDesc.SemanticIndex,
            "G1: output semantic index");
        check_reflect(parsed.psOutputSignatures[i].ui32Register == parameterDesc.Register,
            "G1: output register");
    }
}

std::string make_repeated_struct_shader(unsigned memberCount, unsigned variableCount)
{
    std::string source = "struct Shared {";
    for (unsigned member = 0; member < memberCount; ++member)
        source += " float member_" + std::to_string(member) + ";";
    source += " };\n";
    for (unsigned variable = 0; variable < variableCount; ++variable)
        source += "Shared variable_" + std::to_string(variable) + ";\n";
    source += "float4 main() : SV_Target\n{\n    float sum = 0;\n";
    for (unsigned variable = 0; variable < variableCount; ++variable)
    {
        for (unsigned member = 0; member < memberCount; ++member)
            source += "    sum += variable_" + std::to_string(variable) +
                ".member_" + std::to_string(member) + ";\n";
    }
    source += "    return sum;\n}\n";
    return source;
}

} // namespace

void test_dx11_reflect_bounds_regression()
{
    const char shaderSource[] =
        "struct Inner { float a; float3 b; };\n"
        "struct Outer { Inner inner; float4 arr[2]; };\n"
        "float4 tint = float4(1, 2, 3, 4);\n"
        "Outer data;\n"
        "float scale = 0.5;\n"
        "Texture2D<float4> tex : register(t0);\n"
        "SamplerState samp : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : SV_Target\n"
        "{\n"
        "    return tint + data.arr[0] + float4(data.inner.b, 1) * scale + tex.Sample(samp, uv);\n"
        "}\n";

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    const std::vector<uint8_t> valid = compile_pixel_shader(shaderSource, &blob);
    const std::vector<uint8_t> shaderChunk = extract_code_chunk(valid);

    std::fprintf(stderr, "F128 G1: legal ps_5_0 (%zu B) must match D3DReflect\n", valid.size());
    std::fflush(stderr);
    {
        ShaderPtr shader(DecodeDXBC(valid.data(), valid.size()));
        check_reflect(shader != nullptr, "G1: legal shader still decodes");
        check_reflect(shader->sInfo != nullptr, "G1: legal shader keeps reflection data");

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflected;
        check_reflect(SUCCEEDED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(reflected.GetAddressOf()))), "G1: create D3DReflect");
        compare_with_d3d_reflect(*shader->sInfo, reflected.Get());
    }

    //G2: the same shared 100-member struct behind 20 variables. Legal RDEF
    //data re-reads every member name once per variable, so a chunk-wide string
    //budget rejects this shader even though the chunk itself is small.
    {
        const std::string repeatedSource = make_repeated_struct_shader(100, 20);
        Microsoft::WRL::ComPtr<ID3DBlob> repeatedBlob;
        const std::vector<uint8_t> repeated = compile_pixel_shader(repeatedSource.c_str(), &repeatedBlob);
        ShaderPtr shader(DecodeDXBC(repeated.data(), repeated.size()));
        check_reflect(shader != nullptr, "G2: 100-member x 20-variable cbuffer still decodes");
        check_reflect(shader->sInfo != nullptr, "G2: repeated struct keeps reflection data");
        check_reflect(shader->sInfo->ui32NumConstantBuffers == 1,
            "G2: repeated struct produces one $Globals cbuffer");
        check_reflect(shader->sInfo->psConstantBuffers[0].asVars.size() == 20,
            "G2: all 20 repeated-struct variables are retained");
        check_reflect(shader->sInfo->psConstantBuffers[0].asVars[0].sType.MemberCount == 100,
            "G2: the shared struct keeps all 100 members");

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflected;
        check_reflect(SUCCEEDED(D3DReflect(repeatedBlob->GetBufferPointer(), repeatedBlob->GetBufferSize(),
            IID_PPV_ARGS(reflected.GetAddressOf()))), "G2: create D3DReflect");
        compare_with_d3d_reflect(*shader->sInfo, reflected.Get());
    }

    //R1: the 56-byte payload ends exactly at a PAGE_NOACCESS boundary and the
    //binding name offset is 56. The reader must reject the offset, not touch
    //the guard page in ReadString.
    {
        std::vector<uint8_t> rdef(56, 0);
        write_u32(rdef, 0, 0);
        write_u32(rdef, 4, 0);
        write_u32(rdef, 8, 1);
        write_u32(rdef, 12, 24);
        write_u32(rdef, 16, 0x50050);
        write_u32(rdef, 20, 0);
        write_u32(rdef, 24, 56);

        const std::vector<uint8_t> container = make_container(shaderChunk, rdef, 56);
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        const size_t pageSize = systemInfo.dwPageSize;
        check_reflect(container.size() <= pageSize, "R1: container fits one page");

        VirtualAllocation pages;
        pages.address = VirtualAlloc(nullptr, pageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        check_reflect(pages.address != nullptr, "R1: reserve two pages");
        DWORD previousProtect = 0;
        auto* firstPage = static_cast<uint8_t*>(pages.address);
        check_reflect(VirtualProtect(firstPage + pageSize, pageSize, PAGE_NOACCESS, &previousProtect) != FALSE,
            "R1: guard the second page");

        auto* data = firstPage + (pageSize - container.size());
        std::memcpy(data, container.data(), container.size());
        std::fprintf(stderr, "F128 R1: chunk-end string offset against PAGE_NOACCESS\n");
        std::fflush(stderr);
        ShaderPtr shader(DecodeDXBC(data, container.size()));
        check_reflect(shader == nullptr, "R1: out-of-chunk string offset is rejected without an AV");
    }

    //R2: 0xFFFFFFFF bindings and a count that fits the coarse payload/4 gate but
    //not the per-record clamp. Both must be rejected before any array allocation.
    {
        std::vector<uint8_t> exact(24, 0);
        write_u32(exact, 0, 0);
        write_u32(exact, 4, 0);
        write_u32(exact, 8, 0xFFFFFFFFu);
        write_u32(exact, 12, 24);
        write_u32(exact, 16, 0x50050);
        write_u32(exact, 20, 0);

        std::vector<uint8_t> nearMiss(24, 0);
        write_u32(nearMiss, 0, 0);
        write_u32(nearMiss, 4, 0);
        write_u32(nearMiss, 8, 6);
        write_u32(nearMiss, 12, 24);
        write_u32(nearMiss, 16, 0x50050);
        write_u32(nearMiss, 20, 0);

        const std::vector<uint8_t> exactContainer = make_container(shaderChunk, exact, 24);
        const std::vector<uint8_t> nearMissContainer = make_container(shaderChunk, nearMiss, 24);

        std::fprintf(stderr, "F128 R2: NumResourceBindings 0xFFFFFFFF and offset-clamped count\n");
        std::fflush(stderr);
        {
            ShaderPtr warmup(DecodeDXBC(exactContainer.data(), exactContainer.size()));
            check_reflect(warmup == nullptr, "R2: 0xFFFFFFFF bindings rejected");
            ShaderPtr warmupNear(DecodeDXBC(nearMissContainer.data(), nearMissContainer.size()));
            check_reflect(warmupNear == nullptr, "R2: offset-clamped bindings rejected");
        }

        const size_t before = reflect_live_bytes();
        for (unsigned iteration = 0; iteration < 64; ++iteration)
        {
            ShaderPtr shader(DecodeDXBC(exactContainer.data(), exactContainer.size()));
            check_reflect(shader == nullptr, "R2: 64 rejected decodes with 0xFFFFFFFF bindings");
            ShaderPtr nearMissShader(DecodeDXBC(nearMissContainer.data(), nearMissContainer.size()));
            check_reflect(nearMissShader == nullptr, "R2: 64 rejected decodes with a non-fitting binding table");
        }
        const size_t after = reflect_live_bytes();
        check_reflect(before == after, "R2: rejected binding counts leave no allocation behind");
    }

    //R3: one struct member points its type offset back at the enclosing type.
    {
        std::vector<uint8_t> rdef(124, 0);
        write_u32(rdef, 0, 1);
        write_u32(rdef, 4, 24);
        write_u32(rdef, 8, 0);
        write_u32(rdef, 12, 0);
        write_u32(rdef, 16, 0x50050);
        write_u32(rdef, 20, 0);

        write_u32(rdef, 24, 116); // cbuffer name "cb"
        write_u32(rdef, 28, 1);
        write_u32(rdef, 32, 48);
        write_u32(rdef, 36, 16);
        write_u32(rdef, 40, 0);
        write_u32(rdef, 44, 0);

        write_u32(rdef, 48, 119); // variable name "v"
        write_u32(rdef, 52, 0);
        write_u32(rdef, 56, 16);
        write_u32(rdef, 60, 0);
        write_u32(rdef, 64, 88);
        write_u32(rdef, 68, 0);

        write_u16(rdef, 88, 5); // SVC_STRUCT
        write_u16(rdef, 90, 0);
        write_u16(rdef, 92, 1);
        write_u16(rdef, 94, 1);
        write_u16(rdef, 96, 0);
        write_u16(rdef, 98, 1);
        write_u32(rdef, 100, 104);

        write_u32(rdef, 104, 121); // member name "m"
        write_u32(rdef, 108, 88);  // points back at the record at 88
        write_u32(rdef, 112, 0);

        rdef[116] = 'c'; rdef[117] = 'b'; rdef[118] = 0;
        rdef[119] = 'v'; rdef[120] = 0;
        rdef[121] = 'm'; rdef[122] = 0;

        const std::vector<uint8_t> container = make_container(shaderChunk, rdef, 124);
        std::fprintf(stderr, "F128 R3: self-referencing member type offset\n");
        std::fflush(stderr);
        ShaderPtr shader(DecodeDXBC(container.data(), container.size()));
        check_reflect(shader == nullptr, "R3: type cycle rejected without recursing to stack overflow");
    }

    //R4: the RDEF chunk header says 24 bytes, but the binding data sits in the
    //undeclared 32-byte tail. The old parser ignored chunk->size and accepted it.
    {
        std::vector<uint8_t> rdef(56, 0);
        write_u32(rdef, 0, 0);
        write_u32(rdef, 4, 0);
        write_u32(rdef, 8, 1);
        write_u32(rdef, 12, 24);
        write_u32(rdef, 16, 0x50050);
        write_u32(rdef, 20, 0);
        write_u32(rdef, 24, 0); // NameOffset 0 -> empty string, legal binding fields

        const std::vector<uint8_t> container = make_container(shaderChunk, rdef, 24);
        std::fprintf(stderr, "F128 R4: RDEF size=24 with binding data in trailing padding\n");
        std::fflush(stderr);
        ShaderPtr shader(DecodeDXBC(container.data(), container.size()));
        check_reflect(shader == nullptr, "R4: data outside the declared chunk size is rejected");
    }
}
