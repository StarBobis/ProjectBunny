// Keep the decoder's private DX9 tokens separate from Windows SDK DX9 enums,
// just as the production BinaryDecompiler translation units do.
#include "BinaryDecompiler/internal_includes/tokens.h"
#include "BinaryDecompiler/internal_includes/decode.h"
#include "BinaryDecompiler/internal_includes/reflect.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct ShaderDeleter {
    void operator()(Shader* shader) const
    {
        if (!shader) return;
        FreeShaderInfo(shader->sInfo);
        delete shader;
    }
};
OPERAND_TYPE constant_kind(unsigned reg, bool define)
{
    // SM3: optional DEF cN, then MOV r0,cN and END. CTAB is optional.
    std::vector<uint32_t> tokens{0xfffe0300};
    if (define) tokens.insert(tokens.end(), {0x05000051, 0xa00f0000 | reg,
        0x3f800000, 0x40000000, 0x40400000, 0x40800000});
    tokens.insert(tokens.end(), {0x02000001, 0x800f0000, 0xa0e40000 | reg, 0x0000ffff});
    std::unique_ptr<Shader, ShaderDeleter> shader(DecodeDX9BC(tokens.data()));
    check(shader && shader->dx9Shader && shader->ui32MajorVersion == 3, "F127: decode real DX9 shader stream");
    const auto& instructions = shader->asPhase[MAIN_PHASE].ppsInst[0];
    check(!instructions.empty() && instructions[0].eOpcode == OPCODE_MOV && instructions[0].ui32NumOperands == 2,
        "F127: inspect actual decoded MOV source");
    return instructions[0].asOperands[1].eType;
}
}
void test_dx11_dx9_constant_reset_regression()
{
    for (unsigned reg : {0u, 63u, 64u, 127u, 200u, 255u}) {
        check(constant_kind(reg, true) == OPERAND_TYPE_SPECIAL_IMMCONST, "F127: current DEF remains immediate");
        std::fprintf(stderr, "F127: clear prior shader constant c%u\n", reg); std::fflush(stderr);
        check(constant_kind(reg, false) == OPERAND_TYPE_CONSTANT_BUFFER,
            "F127: next shader without DEF must not inherit prior immediate-constant metadata");
        check(constant_kind(reg, false) == OPERAND_TYPE_CONSTANT_BUFFER, "F127: repeated non-DEF decodes stay independent");
    }
}

void check_dx11_dx9_sampler_dimension(const void* bytecode, unsigned expected_dimension)
{
    std::unique_ptr<Shader,ShaderDeleter> shader(DecodeDXBC(static_cast<uint32_t*>(const_cast<void*>(bytecode))));
    check(shader && shader->dx9Shader && shader->ui32MajorVersion==3 && shader->eShaderType==PIXEL_SHADER,
        "F285: production public decoder recognizes the native DX9 pixel shader");
    const auto* info=shader->sInfo;
    check(info && info->ui32NumConstantBuffers==1 && info->psConstantBuffers && info->psConstantBuffers[0].asVars.empty() &&
        info->ui32NumResourceBindings==1 && info->psResourceBindings,
        "F285: actual compiler CTAB contains a sampler resource and no struct variables");
    ResourceBinding* resource=nullptr;
    check(GetResourceFromBindingPoint(RGROUP_TEXTURE,3,info,&resource)==1 && resource==info->psResourceBindings &&
        resource->Name=="Surface" && resource->ui32BindPoint==3 && resource->ui32BindCount==1 &&
        resource->eType==RTYPE_TEXTURE && resource->ui32NumSamples==1,
        "F285: downstream binding lookup preserves sampler identity and bind metadata");
    std::fprintf(stderr,"F285: native CTAB dimension=%u expected=%u\n",static_cast<unsigned>(resource->eDimension),expected_dimension);
    check(static_cast<unsigned>(resource->eDimension)==expected_dimension,
        "F285: native sampler CTAB reflects its declared texture dimension");
    ResourceBinding* missing=nullptr;
    check(GetResourceFromBindingPoint(RGROUP_TEXTURE,2,info,&missing)==0 && missing==nullptr &&
        GetResourceFromBindingPoint(RGROUP_SAMPLER,3,info,&missing)==0 && missing==nullptr,
        "F285: unbound slots and separate sampler group remain absent for DX9 combined resources");
    if (expected_dimension==REFLECT_RESOURCE_DIMENSION_TEXTURE2D || expected_dimension==REFLECT_RESOURCE_DIMENSION_TEXTURE3D ||
            expected_dimension==REFLECT_RESOURCE_DIMENSION_TEXTURECUBE) {
        const auto expected_declaration=expected_dimension==REFLECT_RESOURCE_DIMENSION_TEXTURE2D ? RESOURCE_DIMENSION_TEXTURE2D :
            expected_dimension==REFLECT_RESOURCE_DIMENSION_TEXTURE3D ? RESOURCE_DIMENSION_TEXTURE3D : RESOURCE_DIMENSION_TEXTURECUBE;
        bool found=false;
        for (const auto& declaration : shader->asPhase[MAIN_PHASE].ppsDecl[0]) {
            if (declaration.eOpcode==OPCODE_DCL_RESOURCE && declaration.asOperands[0].ui32RegisterNumber==3) {
                check(declaration.value.eResourceDimension==expected_declaration,
                    "F285: independent bytecode declaration agrees with reflected native sampler dimension");
                found=true;
            }
        }
        check(found,"F285: native bytecode exposes the real sampler resource declaration");
    }
}
