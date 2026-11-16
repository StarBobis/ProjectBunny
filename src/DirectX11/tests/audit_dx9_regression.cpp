// Keep the decoder's private DX9 tokens separate from Windows SDK DX9 enums,
// just as the production BinaryDecompiler translation units do.
#include "BinaryDecompiler/internal_includes/tokens.h"
#include "BinaryDecompiler/internal_includes/decode.h"
#include "BinaryDecompiler/internal_includes/reflect.h"
#include "HLSLDecompiler/DecompileHLSL.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#ifndef F126_FIXTURE_FILE
#error "F126_FIXTURE_FILE must be provided by dx11_upstream_regression"
#endif

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
    std::unique_ptr<Shader, ShaderDeleter> shader(DecodeDX9BC(tokens.data(), tokens.size() * sizeof(uint32_t)));
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

const uint32_t *DecodeDeclaration(Shader *, const uint32_t *, Declaration *);

void test_dx11_customdata_length_regression()
{
    Shader shader{};

    // COMMENT is CUSTOMDATA class 0. Five DWORDs total means a three-DWORD
    // payload, which is not a whole number of vec4s; it must still decode.
    uint32_t comment[] = {
        static_cast<uint32_t>(OPCODE_CUSTOMDATA), 5, 0x11223344, 0x55667788, 0x99aabbcc
    };
    Declaration comment_decl{};
    const uint32_t *after_comment = DecodeDeclaration(&shader, comment, &comment_decl);
    std::fprintf(stderr, "F124: odd-length COMMENT returned offset=%td\n", after_comment - comment);
    check(after_comment == comment + 5, "F124: non-vec4-aligned COMMENT payload decodes with the full length");

    // The same length on an immediate constant buffer is invalid.
    uint32_t bad_icb[] = {
        static_cast<uint32_t>(OPCODE_CUSTOMDATA) | (CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER << 11),
        5, 0, 0, 0
    };
    bool rejected = false;
    try { Declaration decl{}; DecodeDeclaration(&shader, bad_icb, &decl); } catch (...) { rejected = true; }
    std::fprintf(stderr, "F124: misaligned ICB rejected=%d\n", rejected ? 1 : 0);
    check(rejected, "F124: misaligned immediate constant buffer length is rejected");

    // 1024 vec4s (4098 DWORDs) remains the valid upper bound.
    std::vector<uint32_t> big(4098, 0);
    big[0] = static_cast<uint32_t>(OPCODE_CUSTOMDATA) | (CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER << 11);
    big[1] = 4098;
    Declaration big_decl{};
    const uint32_t *after_big = DecodeDeclaration(&shader, big.data(), &big_decl);
    std::fprintf(stderr, "F124: 1024-vec4 ICB returned offset=%td operands=%u\n",
        after_big - big.data(), big_decl.ui32NumOperands);
    check(after_big == big.data() + 4098 && big_decl.ui32NumOperands == 1024,
        "F124: 1024-vec4 immediate constant buffer remains valid");
}

void test_dx11_dx9_register_bounds_regression()
{
    struct RegisterCase { uint32_t token; const char *name; };
    const RegisterCase invalid[] = {
        {0x00e40000u | 32u,  "r32"},
        {0x10e40000u | 16u,  "v16"},
        {0xa0e40000u | 256u, "c256"},
        {0x70e40000u | 16u,  "i16"},
    };
    for (const auto &item : invalid) {
        std::vector<uint32_t> tokens{0xfffe0300, 0x02000001, 0x800f0000, item.token, 0x0000ffff};
        bool rejected = false;
        try {
            std::unique_ptr<Shader, ShaderDeleter> shader(DecodeDX9BC(tokens.data(), tokens.size() * sizeof(uint32_t)));
            rejected = !shader;
        } catch (...) {
            rejected = true;
        }
        std::fprintf(stderr, "F125: invalid %s rejected=%d\n", item.name, rejected ? 1 : 0);
        std::fflush(stderr);
        check(rejected, "F125: out-of-range DX9 register is rejected instead of decoded");
    }

    // Boundary that must stay legal: c255 is the last constant register.
    std::vector<uint32_t> legal{0xfffe0300, 0x02000001, 0x800f0000, 0xa0e40000u | 255u, 0x0000ffff};
    std::unique_ptr<Shader, ShaderDeleter> shader(DecodeDX9BC(legal.data(), legal.size() * sizeof(uint32_t)));
    std::fprintf(stderr, "F125: legal c255 decoded=%d\n", shader ? 1 : 0);
    check(shader && shader->dx9Shader, "F125: c255 remains a legal DX9 constant register");
}

void check_dx11_dx9_sampler_dimension(const void* bytecode, size_t byteSize, unsigned expected_dimension)
{
    std::unique_ptr<Shader,ShaderDeleter> shader(DecodeDXBC(bytecode, byteSize));
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

namespace {
std::vector<uint32_t> read_f126_tokens()
{
    std::ifstream input(std::filesystem::u8path(F126_FIXTURE_FILE));
    check(input.good(), "F126: open the reviewed five-DWORD DX9 token file");
    std::vector<uint32_t> tokens;
    std::string word;
    while (input >> word) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(word.c_str(), &end, 16);
        check(end != word.c_str() && *end == '\0' && word.size() <= 8 && value <= 0xfffffffful,
            "F126: the token file contains hexadecimal DWORDs only");
        tokens.push_back(static_cast<uint32_t>(value));
    }
    check(tokens.size() == 5, "F126: the reviewed input has exactly five DWORDs");
    return tokens;
}

class F126LogCapture
{
public:
    F126LogCapture() : mSavedLog(LogFile), mSavedVerbosity(gLogVerbosity), mFile(std::tmpfile())
    {
        check(mFile != nullptr, "F126: create the diagnostic log capture");
        LogFile = mFile;
        gLogVerbosity = LogVerbosity::INFO;
    }

    ~F126LogCapture()
    {
        restore();
    }

    void restore()
    {
        if (!mFile) return;
        LogFile = mSavedLog;
        gLogVerbosity = mSavedVerbosity;
        std::fclose(mFile);
        mFile = nullptr;
    }

    std::string read()
    {
        check(mFile != nullptr, "F126: the diagnostic log capture is still open");
        check(std::fflush(mFile) == 0 && std::fseek(mFile, 0, SEEK_SET) == 0, "F126: rewind the diagnostic log capture");
        std::string text;
        char buffer[512];
        size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), mFile)) != 0)
            text.append(buffer, count);
        check(std::fseek(mFile, 0, SEEK_END) == 0, "F126: restore the diagnostic log capture append position");
        return text;
    }

private:
    FILE* mSavedLog;
    LogVerbosity mSavedVerbosity;
    FILE* mFile;
};
}

void check_dx11_dx9_dcl_const_without_ctab_lifetime(const uint32_t* tokens, size_t tokenCount);

void test_dx11_dx9_dcl_const_without_ctab_regression()
{
    const auto tokens = read_f126_tokens();
    F126LogCapture capture;

    bool threw = false;
    std::string exception_type;
    try {
        std::unique_ptr<Shader, ShaderDeleter> shader(
            DecodeDX9BC(tokens.data(), tokens.size() * sizeof(uint32_t)));
        if (shader)
            std::fprintf(stderr, "F126: decode unexpectedly returned a shader\n");
    } catch (const std::exception& e) {
        threw = true;
        exception_type = typeid(e).name();
        std::fprintf(stderr, "F126: direct decode exception=%s what=%s\n", typeid(e).name(), e.what());
    } catch (...) {
        threw = true;
        exception_type = "non-std::exception";
        std::fprintf(stderr, "F126: direct decode threw a non-std::exception\n");
    }

    const std::string direct_log = capture.read();
    std::fprintf(stderr, "F126: direct decode log: %s", direct_log.c_str());
    check(threw, "F126: DCL constant buffer without CTAB must throw instead of dereferencing psConstantBuffers[0]");
    check(exception_type.find("DecompileErrorDX9") != std::string::npos,
        "F126: the malformed stream is rejected with decompileErrorDX9");
    check(direct_log.find("error parsing shader> DCL constant buffer without CTAB (c0)\n") != std::string::npos,
        "F126: the exact no-CTAB diagnostic from the decodeDX9 branch is logged");
    check(direct_log.find("out of range") == std::string::npos,
        "F126: the fixture must reach the no-CTAB branch, not an earlier register-range diagnostic");

    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = tokens.data();
    params.bytecodeSize = tokens.size() * sizeof(uint32_t);
    params.decompiled = "";
    params.decompiledSize = 0;
    params.ZeroOutput = false;
    params.G = &settings;
    bool patched = false, failed = false;
    std::string model;
    const std::string hlsl = DecompileBinaryHLSL(params, patched, model, failed);
    const std::string wrapper_log = capture.read();
    std::fprintf(stderr, "F126: production wrapper failed=%d hlsl=%zu model=%s patched=%d\n",
        failed ? 1 : 0, hlsl.size(), model.c_str(), patched ? 1 : 0);
    std::fprintf(stderr, "F126: production wrapper log: %s", wrapper_log.c_str());
    check(hlsl.empty() && failed,
        "F126: DecompileBinaryHLSL returns no HLSL and marks the malformed DX9 shader as failed");
    check(wrapper_log.find("error parsing shader> DCL constant buffer without CTAB (c0)\n") != std::string::npos,
        "F126: the production wrapper sees the exact no-CTAB diagnostic from the decode branch");
    check(wrapper_log.find("out of range") == std::string::npos,
        "F126: the production wrapper must not report an earlier register-range failure");
    check(wrapper_log.find("Exception caught while decompiling shader") != std::string::npos,
        "F126: the decompileErrorDX9 reaches the DecompileBinaryHLSL catch");

    capture.restore();
    check_dx11_dx9_dcl_const_without_ctab_lifetime(tokens.data(), tokens.size());
}
