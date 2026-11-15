// Keep BinaryDecompiler's private token definitions separate from the SDK
// headers in the WARP fixtures. This TU compiles the production decompiler once.
#include "BinaryDecompiler/internal_includes/decode.h"
#include "BinaryDecompiler/internal_includes/reflect.h"
#include <cstdio>
#include "log.h"
namespace {
Shader* decode_lifetime_audit(uint32_t*);
void free_lifetime_audit(ShaderInfo*);
std::string header_time_lifetime_audit();
}
#define DecodeDXBC decode_lifetime_audit
#define FreeShaderInfo free_lifetime_audit
#define LogTime header_time_lifetime_audit
#include "../../HLSLDecompiler/DecompileHLSL.cpp"
#undef LogTime
#undef FreeShaderInfo
#undef DecodeDXBC
#include <cstring>
#include <stdexcept>
#include <malloc.h>
#ifdef DX11_AUDIT_ASAN_LOCKS
#include <sanitizer/allocator_interface.h>
#endif

void test_dx11_decompiler_public_literal_regression();
void test_dx11_decompiler_public_ibfe_regression();
void test_dx11_decompiler_public_signature_regression();
std::string dx11_audit_dx9_binding_disassembly();
void test_dx11_decompiler_public_dx9_binding_regression();
void test_dx11_decompiler_public_suffix_regression();
void test_dx11_decompiler_public_texture_literal_regression();
void test_dx11_decompiler_public_immediate_regression();

namespace {
void check_literal_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

namespace {
size_t binding_live_bytes()
{
#ifdef DX11_AUDIT_ASAN_LOCKS
    // ASan's allocations are not reported by the CRT's native heap walker.
    return __sanitizer_get_current_allocated_bytes();
#else
    size_t bytes = 0;
    _HEAPINFO entry{};
    int status;
    while ((status = _heapwalk(&entry)) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) bytes += entry._size;
    }
    check_literal_audit(status == _HEAPEND, "F276: inspect the actual CRT heap completely");
    return bytes;
#endif
}

void check_binding_allocation_counter()
{
    const auto before = binding_live_bytes();
    void* probe = ::operator new[](8192);
    const auto during = binding_live_bytes();
    ::operator delete[](probe);
    const auto after = binding_live_bytes();
    check_literal_audit(during >= before + 8192 && after == before,
        "F276: the live-byte counter must observe both allocation and release of real array storage");
}

void check_dx9_binding_lifetime(const std::string& assembly, bool has_bindings, const char* label)
{
    auto parse = [&] {
        Decompiler decoder;
        decoder.ReadResourceBindingsDX9(assembly.data(), assembly.size());
        if (has_bindings) {
            check_literal_audit(decoder.mTextureNames.size() == 1 && decoder.mTextureNames.at(2) == "diffuse" &&
                decoder.mTextureNamesArraySize.at(2) == 1 && decoder.mTextureType.at(2) == "Texture2D<float4>" &&
                decoder.mUniformNames.size() == 1 && decoder.mUniformNames.at(3) == "tint" &&
                decoder.mBoolUniformNames.size() == 1 && decoder.mBoolUniformNames.at(1) == "b1",
                "F276: preserve sampler, float and bool register metadata from native SM3 disassembly");
        } else {
            check_literal_audit(decoder.mTextureNames.empty() && decoder.mUniformNames.empty() && decoder.mBoolUniformNames.empty(),
                "F276: rejected/header rows cannot publish register metadata");
        }
    };
    parse(); // Exclude one-time runtime initialization from the live-allocation comparison.
    const auto before = binding_live_bytes();
    for (unsigned iteration = 0; iteration < 64; ++iteration) parse();
    const auto after = binding_live_bytes();
    std::fprintf(stderr, "F276 %s: live allocation bytes %zu -> %zu (64 parser lifetimes)\n", label, before, after);
    check_literal_audit(before == after,
        "F276: register-row storage must be released after successful and rejected parses");
}
}

void test_dx11_dx9_binding_lifetime_regression()
{
    const auto assembly = dx11_audit_dx9_binding_disassembly();
    check_binding_allocation_counter();
    check_dx9_binding_lifetime("// Registers:\n//\n// Name Reg Size\n// ---- ---- ----\n// invalid\n// extra c0 1 field\nps_3_0\n",
        false, "rejected rows");
    check_dx9_binding_lifetime("// Registers:\n//\nps_3_0\n", false, "empty table");
    check_dx9_binding_lifetime(assembly, true, "native SM3 rows");
    std::string crlf;
    crlf.reserve(assembly.size() * 2);
    for (char ch : assembly) {
        if (ch == '\n' && (crlf.empty() || crlf.back() != '\r')) crlf += '\r';
        crlf += ch;
    }
    check_dx9_binding_lifetime(crlf, true, "CRLF rows");
    test_dx11_decompiler_public_dx9_binding_regression();
    test_dx11_decompiler_public_literal_regression();
}
namespace {

struct DecompilerLifetimeState {
    bool enabled=false, throw_at_header=false, bad_release=false;
    unsigned decoded=0, freed=0, faults=0, member_arrays=0;
    ShaderInfo* current_info=nullptr;
};
DecompilerLifetimeState lifetime_state;

unsigned member_array_count(const ShaderVarType& type) noexcept
{
    if (!type.Members) return 0;
    unsigned count=1;
    for (unsigned i=0; i<type.MemberCount; ++i) count+=member_array_count(type.Members[i]);
    return count;
}

Shader* decode_lifetime_audit(uint32_t* data)
{
    Shader* shader=::DecodeDXBC(data);
    if (lifetime_state.enabled && shader) {
        ++lifetime_state.decoded;
        lifetime_state.current_info=shader->sInfo;
        for (unsigned i=0;i<shader->sInfo->ui32NumConstantBuffers;++i)
            for (const auto& variable:shader->sInfo->psConstantBuffers[i].asVars)
                lifetime_state.member_arrays+=member_array_count(variable.sType);
    }
    return shader;
}

void free_lifetime_audit(ShaderInfo* info)
{
    if (lifetime_state.enabled) {
        ++lifetime_state.freed;
        lifetime_state.bad_release|=info!=lifetime_state.current_info;
        lifetime_state.current_info=nullptr;
    }
    ::FreeShaderInfo(info);
    if (lifetime_state.enabled)
        lifetime_state.bad_release|=info->psInputSignatures || info->psOutputSignatures || info->psPatchConstantSignatures ||
            info->psResourceBindings || info->psConstantBuffers || info->psClassTypes || info->psClassInstances;
}

std::string header_time_lifetime_audit()
{
    if (lifetime_state.enabled && lifetime_state.throw_at_header) {
        ++lifetime_state.faults;
        throw std::runtime_error("F283 injected header failure after successful real Decode");
    }
    return ::LogTime();
}

size_t decompiler_live_bytes()
{
#ifdef DX11_AUDIT_ASAN_LOCKS
    return __sanitizer_get_current_allocated_bytes();
#else
    size_t total=0;
    _HEAPINFO entry{};
    int status;
    while ((status=_heapwalk(&entry))==_HEAPOK)
        if (entry._useflag==_USEDENTRY) total+=entry._size;
    check_literal_audit(status==_HEAPEND,"F283: inspect the complete CRT allocation set");
    return total;
#endif
}

class DecompilerLifetimeScope {
public:
    explicit DecompilerLifetimeScope(bool throw_at_header)
    {
        lifetime_state={}; lifetime_state.enabled=true; lifetime_state.throw_at_header=throw_at_header;
    }
    ~DecompilerLifetimeScope() { lifetime_state.enabled=false; }
};

void check_decompiler_lifetime(const void* bytecode,const std::string& assembly,bool throw_at_header,bool reject_signature)
{
    DecompilerSettings settings;
    ParseParameters params{}; params.bytecode=bytecode; params.decompiled=assembly.c_str(); params.decompiledSize=assembly.size(); params.G=&settings;
    DecompilerLifetimeScope scope(throw_at_header);
    auto decompile=[&] {
        bool patched=false,failed=false; std::string model;
        const auto result=DecompileBinaryHLSL(params,patched,model,failed);
        check_literal_audit(throw_at_header || reject_signature ? failed && result.empty() : !failed && !result.empty() && model=="ps_5_0",
            "F283: the real public return and error contract survives normal and injected failures");
    };
    decompile(); // Exclude one-time runtime/logging initialization.
    lifetime_state.decoded=lifetime_state.freed=lifetime_state.faults=lifetime_state.member_arrays=0;
    const auto before=decompiler_live_bytes();
    for (unsigned i=0;i<64;++i) decompile();
    const auto after=decompiler_live_bytes();
    lifetime_state.enabled=false;
    std::fprintf(stderr,"F283 %s: live %zu -> %zu, decoded=%u freed=%u faults=%u nested-arrays=%u\n",
        throw_at_header ? "post-decode exception" : reject_signature ? "signature early return" : "normal",
        before,after,lifetime_state.decoded,lifetime_state.freed,lifetime_state.faults,lifetime_state.member_arrays);
    check_literal_audit(before==after,"F283: successful Decode state and nested reflection storage must not survive a public call");
    check_literal_audit(lifetime_state.decoded==64 && lifetime_state.freed==64 && lifetime_state.member_arrays>=128 &&
        lifetime_state.faults==(throw_at_header ? 64u : 0u) && !lifetime_state.bad_release,
        "F283: every successful real Decode with nested members is released once, including exception and early-return paths");
}

std::string swizzle_literal_audit(const char* left, const char* literal, bool use_int = false)
{
    Decompiler decoder;
    decoder.mErrorOccurred = false; // Same initial error state as DecompileBinaryHLSL.
    char right[opcodeSize]; strcpy_s(right, literal);
    decoder.applySwizzle(left, right, use_int);
    check_literal_audit(!decoder.mErrorOccurred, "F100: valid literal selection cannot be rejected");
    return right;
}
}

void test_dx11_decompiler_literal_regression()
{
    std::fputs("F100: valid two-component literal through the actual swizzle caller\n", stderr); std::fflush(stderr);
    check_literal_audit(swizzle_literal_audit("r0.xy", "l(1,2)") == "float2(1,2)",
        "F100: parse the actual provided components without looking for a nonexistent fourth comma");
    check_literal_audit(swizzle_literal_audit("r0.xyz", "l(1.25,2.5,3.75)") == "float3(1.25,2.5,3.75)", "F100: legitimate three-component literal");
    check_literal_audit(swizzle_literal_audit("r0.wzyx", "l(1,2,3,4)") == "float4(4,3,2,1)", "F100: four-component permutation");
    check_literal_audit(swizzle_literal_audit("r0.z", "l(1,2,3)") == "3", "F100: select an available scalar component");
    check_literal_audit(swizzle_literal_audit("r0.xyzw", "l(1.25)") == "1.25", "F100: scalar literal still broadcasts without requiring four input components");
    check_literal_audit(swizzle_literal_audit("r0", "l(2)") == "2", "F100: default scalar selection remains valid");
    check_literal_audit(swizzle_literal_audit("r0.xy", "-|l(1.25,2.5)|") == "-abs(float2(1.25,2.5))", "F100: literal modifiers survive valid formatting");
    check_literal_audit(swizzle_literal_audit("r0.xy", "l(2147483647,-2147483648)", true) == "int2(2147483647,-2147483648)",
        "F100: exact signed integer components cannot be rounded through float before formatting");
    check_literal_audit(swizzle_literal_audit("r0.xy", "l(4294967295,2147483648)", true) == "int2(0xffffffff,0x80000000)",
        "F100: unsigned 32-bit components keep their exact bit patterns");
    check_literal_audit(swizzle_literal_audit("r0.xy", "l(0X80000000,0x1)", true) == "int2(0x80000000,0x1)", "F100: bounded hexadecimal components");
    for (const auto* literal : {"l()", "l(", "l(1,)", "l(,2)", "l(1,,2)", "l(1,2,3,4,5)", "l(1,bad)",
            "l(1junk,2)", "l(1,2))", "l(0x100000000,2)", "l(1,2)"}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        char input[opcodeSize]; strcpy_s(input, literal);
        decoder.applySwizzle("r0.xyzw", input);
        check_literal_audit(decoder.mErrorOccurred && !std::strcmp(input, literal),
            "F100: malformed or unavailable components set the real error flag without publishing a partially split literal");
    }
    for (const auto* left : {"r0.a", "r0.", "r0.xyzwx"}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        char input[opcodeSize] = "l(1,2,3,4)";
        decoder.applySwizzle(left, input);
        check_literal_audit(decoder.mErrorOccurred, "F100: invalid swizzles are diagnosed before indexing their character map");
    }
    char output[opcodeSize] = "unchanged";
    const char invalid_index[4] = {4,-1,-1,-1};
    check_literal_audit(!Decompiler::applySwizzleLiteral("l(1,2,3,4)", output, false, 1, invalid_index) && !std::strcmp(output,"unchanged"),
        "F100: direct component selection cannot index past the four slots");
    test_dx11_decompiler_public_literal_regression();
}

void test_dx11_decompiler_swizzle_count_regression()
{
    // Enter through legal native DXBC and the actual public decompiler before
    // checking private helper boundaries, so the missing-dot case is reachable.
    test_dx11_decompiler_public_ibfe_regression();
    const std::pair<const char*,const char*> cases[]={{"r0.x",""},{"r0.xy","2"},{"r0.xyz","3"},
        {"r0.xyzw","4"},{"r0.wzyx","4"},{"r[cb0[0].x].yz","2"},{"scalar",""},{"",""}};
    for(const auto& test:cases) {
        char operand[opcodeSize]{}; strcpy_s(operand,test.first);
        check_literal_audit(swizCount(operand)==test.second,"F272: destination masks and scalar/no-mask operands have a defined cast suffix");
    }
    check_literal_audit(swizCount(nullptr).empty(),"F272: the existing null operand scalar result is preserved");
}

void test_dx11_decompiler_output_signature_regression()
{
    const std::string header = "// Output signature:\n//\n"
        "// Name Index Mask Register SysValue Format Used\n"
        "// ---- ----- ---- -------- -------- ------ ----\n";
    const std::string input = "\nvoid main(\n  float v0 : TEXCOORD0,\n";
    for (const auto& prefix : {std::string("\nvoid main(\n"), input}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        decoder.mOutput.assign(prefix.begin(), prefix.end());
        const auto text = header + "// no Output\n//\n";
        decoder.ParseOutputSignature(text.c_str(), text.size());
        const std::string expected = prefix == input ? "\nvoid main(\n  float v0 : TEXCOORD0)\n{\n" : "\nvoid main(\n)\n{\n";
        check_literal_audit(!decoder.mErrorOccurred && std::string(decoder.mOutput.begin(),decoder.mOutput.end()) == expected,
            "F274: zero outputs close the function without removing its opening parenthesis or retaining an input separator");
    }
    const std::string regular = "// SV_Target 0 xyzw 0 TARGET float xyzw\n";
    for (const auto& rows : {std::string("// unrecognized\n"), regular + "// unrecognized\n",
            regular + "// SV_Position 0 xyzw 1 POS float xyzw\n// unrecognized\n",
            std::string("// SV_Target 0 xy 0 TARGET float xy\n// SV_Target 1 zw 0 TARGET float zw\n// unrecognized\n"),
            std::string("// SV_Depth 0 N/A oDepth\n"), std::string("// SV_Target 0 xyzw 0 TARGET unrecognized xyzw\n"),
            std::string("// SV_Depth 0 N/A oDepth DEPTH unrecognized YES\n")}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        decoder.mOutput.assign(input.begin(),input.end());
        const auto text = header + rows + "//\n" + std::string(100,' ');
        decoder.ParseOutputSignature(text.c_str(),text.size());
        check_literal_audit(decoder.mErrorOccurred && std::string(decoder.mOutput.begin(),decoder.mOutput.end()) == input &&
            decoder.mOutputRegisterType.empty() && decoder.mRemappedOutputRegisters.empty() && decoder.mSV_Position.empty(),
            "F274: failed output parsing rolls back its text and register metadata before publishing any signature");
    }
    for (const auto& text : {std::string(), std::string("// unknown output section\n"),
            std::string("// Output signature:\n"), header}) {
        Decompiler decoder; decoder.mErrorOccurred=false;
        decoder.mOutput.assign(input.begin(),input.end());
        decoder.ParseOutputSignature(text.c_str(),text.size());
        check_literal_audit(decoder.mErrorOccurred && std::string(decoder.mOutput.begin(),decoder.mOutput.end())==input,
            "F274: missing or truncated output metadata is diagnosed without silently leaving a half signature");
    }
    test_dx11_decompiler_public_signature_regression();
}

void check_dx11_special_output_rows(const std::string& text,
    const std::vector<std::string>& registers)
{
    Decompiler signature;
    signature.mErrorOccurred = false;
    const std::string header = "\nvoid main(\n";
    signature.mOutput.assign(header.begin(), header.end());
    signature.ParseOutputSignature(text.c_str(), text.size());
    check_literal_audit(!signature.mErrorOccurred, "F275: native special output declarations parse successfully");
    const std::string declarations(signature.mOutput.begin(), signature.mOutput.end());
    for (const auto& reg : registers)
        check_literal_audit(declarations.find(" " + reg + " : ") != std::string::npos,
            "F275: retain every native special and ordinary output declaration");

    Decompiler zero;
    zero.mErrorOccurred = false;
    zero.WriteZeroOutputSignature(text.c_str(), text.size());
    check_literal_audit(!zero.mErrorOccurred, "F275: special zero-output scan must update the caller's conversion result");
    const std::string assignments(zero.mOutput.begin(), zero.mOutput.end());
    for (const auto& reg : registers)
        check_literal_audit(assignments.find("  " + reg + " = 0;\n") != std::string::npos,
            "F275: continue through every special output and the following ordinary row");
}

void check_dx11_special_output_failure(const std::string& text)
{
    Decompiler signature;
    signature.mErrorOccurred = false;
    const std::string header = "\nvoid main(\n";
    signature.mOutput.assign(header.begin(), header.end());
    signature.ParseOutputSignature(text.c_str(), text.size());
    const std::string declarations(signature.mOutput.begin(), signature.mOutput.end());
    check_literal_audit(signature.mErrorOccurred && declarations.find("oMask") == std::string::npos,
        "F275: incomplete special declaration fails without publishing unread fields");
    Decompiler zero;
    zero.mErrorOccurred = false;
    zero.WriteZeroOutputSignature(text.c_str(), text.size());
    const std::string assignments(zero.mOutput.begin(), zero.mOutput.end());
    check_literal_audit(zero.mErrorOccurred && assignments == "  o0 = 0;\n",
        "F275: incomplete special row fails before publishing it or consuming the next valid row");
}

void test_dx11_decompiler_suffix_regression()
{
    Decompiler reused; reused.mErrorOccurred = false;
    // ReadStatement only resets the first byte of each fixed-size operand array.
    // Reuse it exactly as ParseCode does; this is a logical-tail read, not a heap overread.
    reused.ReadStatement("mov r0.xyzw, r1.wwww, r2.xyzw");
    reused.ReadStatement("ubfe r0.xyzw, r1.x, r2.x, r3.x");
    reused.applySwizzle("r0.xyzw", reused.op2);
    const auto second = reused.GetSuffix(reused.op2, 1);
    const auto fourth = reused.GetSuffix(reused.op2, 3);
    std::fprintf(stderr, "F279: reused operand '%s', second size=%zu byte=%u, fourth='%s'\n",
        reused.op2, second.size(), second.size() > 3 ? static_cast<unsigned char>(second[3]) : 0, fourth.c_str());
    test_dx11_decompiler_public_suffix_regression();
    check_literal_audit(second == "r1.x" && fourth == "r1.x" && !reused.mErrorOccurred,
        "F279: selected scalar broadcasts without reading the NUL or old bytes in the operand array");

    for (const auto* operand : {"r1.x", "-r1.x", "abs(r1.x)", "-abs(r1.x)",
            "Width", "Globals.Value", "Globals.xyzValue", "Globals[r0.x]", "9", "1.25"}) {
        for (int index = 0; index < 4; ++index) {
            Decompiler decoder; decoder.mErrorOccurred = false;
            char input[opcodeSize]; strcpy_s(input, operand);
            check_literal_audit(decoder.GetSuffix(input, index) == operand && !decoder.mErrorOccurred,
                "F279: scalar selection and already typed operands retain their modifiers and no-swizzle policy");
        }
    }
    for (const auto* suffix : {"yw", "zxy", "wzyx"}) {
        for (const auto* prefix : {"", "-", "abs(", "-abs("}) {
            const bool absolute = std::strchr(prefix, '(') != nullptr;
            const std::string input = std::string(prefix) + "r1." + suffix + (absolute ? ")" : "");
            for (int index = 0; index < static_cast<int>(std::strlen(suffix)); ++index) {
                Decompiler decoder; decoder.mErrorOccurred = false;
                char operand[opcodeSize]; strcpy_s(operand, input.c_str());
                const auto expected = std::string(prefix) + "r1." + suffix[index] + (absolute ? ")" : "");
                check_literal_audit(decoder.GetSuffix(operand, index) == expected && !decoder.mErrorOccurred,
                    "F279: two/three/four-component permutations keep all source modifiers");
            }
        }
    }
    for (const auto type : {DT_float, DT_uint, DT_int, DT_bool}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        decoder.mCBufferData[0] = BufferEntry{type, 0, false, "Width"};
        char scalar[opcodeSize] = "cb0[0].x";
        decoder.applySwizzle("r0.xyzw", scalar);
        check_literal_audit(std::string(scalar) == "Width" && decoder.GetSuffix(scalar, 3) == "Width" && !decoder.mErrorOccurred,
            "F279: applySwizzle owns the typed constant-buffer scalar decision");
    }
    Decompiler vector; vector.mErrorOccurred = false;
    vector.mCBufferData[0] = BufferEntry{DT_float4, 0, false, "Values"};
    char values[opcodeSize] = "cb0[0].wzyx";
    vector.applySwizzle("r0.xyz", values);
    check_literal_audit(std::string(values) == "Values.wzy" && vector.GetSuffix(values, 2) == "Values.y" && !vector.mErrorOccurred,
        "F279: typed vectors keep the existing selected component mapping");
    Decompiler comma; comma.mErrorOccurred = false;
    char comma_input[opcodeSize] = "r1.yw,";
    check_literal_audit(comma.GetSuffix(comma_input, 1) == "r1.w" && !comma.mErrorOccurred,
        "F279: trailing operand punctuation remains supported");
    for (const auto* operand : {"r1.xy", "r1.xyz", "r1.xyzwx"}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        char input[opcodeSize]; strcpy_s(input, operand);
        check_literal_audit(decoder.GetSuffix(input, 3) == operand && decoder.mErrorOccurred,
            "F279: unavailable or oversized swizzles report failure without publishing a fabricated component");
    }
    for (const int index : {-1, 4}) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        char input[opcodeSize] = "r1.xyzw";
        check_literal_audit(decoder.GetSuffix(input, index) == input && decoder.mErrorOccurred,
            "F279: component index is bounded before selecting a source byte");
    }
    Decompiler empty; empty.mErrorOccurred = false;
    char input[opcodeSize] = {};
    check_literal_audit(empty.GetSuffix(input, 0).empty() && empty.mErrorOccurred,
        "F279: an absent operand reports the existing error state before testing its last character");
}

void test_dx11_decompiler_texture_literal_regression()
{
    std::fputs("F280: complete texture-coordinate constructors through the production truncator\n", stderr);
    std::fflush(stderr);
    for (const auto* type : {"Texture2D<float4>", "Texture3D<float4>"}) {
        for (const auto* input : {"float4(1,2)", "float4(1)", "float4(1,2,3)",
                "float4(1,2,3,)", "float4(1,2,,4)", "float4(1,2,3,4,5)",
                "float4(1,2,3,4", "float4(1,2,3,4))", "float4(1,2,3,bad)",
                "float4(1,2,3,4junk)", "float4(1 2,3,4)", "float4(1,2 3,4)",
                "float4(1,2,3 4)", "float3(1,2)", "float2(1)",
                "float5(1,2,3,4,5)", "float(1)", "float", "float1", "float2", "float3", "float4", "float4()"}) {
            Decompiler decoder; decoder.mErrorOccurred = false;
            char operand[opcodeSize]; strcpy_s(operand, input);
            decoder.truncateTexturePos(operand, type);
            check_literal_audit(decoder.mErrorOccurred && !std::strcmp(operand, input),
                "F280: reject every malformed constructor without publishing a truncated prefix");
        }
    }
    struct TextureLiteralCase {
        const char* input;
        const char* type;
        const char* expected;
    };
    const TextureLiteralCase valid[] = {
        {"float2(1,2)", "Texture2D<float4>", "float2(1,2)"},
        {"float3(1,2,3)", "Texture2D<float4>", "float2(1,2)"},
        {"float3(1,2,3)", "Texture3D<float4>", "float3(1,2,3)"},
        {"float4(1,2,3,4)", "Texture2D<float4>", "float2(1,2)"},
        {"float4(1,2,3,4)", "Texture3D<float4>", "float3(1,2,3)"},
        {"float4(1,2,3,4)", "TextureCubeArray<float4>", "float4(1,2,3,4)"},
        {"floatCoords.xyzx", "TextureCube<float4>", "floatCoords.xyzx"},
        {"float2Indices.xyzx", "TextureCubeArray<float4>", "float2Indices.xyzx"},
        {"float123.xyzx", "TextureCube<float4>", "float123.xyzx"},
        {"floatCoords", "Texture1D<float4>", "floatCoords"},
        {"float123", "Texture1D<float4>", "float123"},
        {"float4(1.23456789e-19,-0.00000000,inf,nan)", "Texture2D<float4>",
            "float2(1.23456789e-19,-0.00000000)"},
        {"float4(inf,-inf,nan,-0)", "Texture3D<float4>", "float3(inf,-inf,nan)"},
        {"float4(3.40282347e+38,1.40129846e-45,-0,1)", "Texture3D<float4>",
            "float3(3.40282347e+38,1.40129846e-45,-0)"},
        {"float3( 1.23456789e-19 , -0 , nan )", "Texture3D<float4>",
            "float3( 1.23456789e-19 , -0 , nan )"},
    };
    for (const auto& test : valid) {
        Decompiler decoder; decoder.mErrorOccurred = false;
        char operand[opcodeSize]; strcpy_s(operand, test.input);
        decoder.truncateTexturePos(operand, test.type);
        check_literal_audit(!decoder.mErrorOccurred && !std::strcmp(operand, test.expected),
            "F280: valid coordinates preserve component spelling, precision and special values");
        decoder.truncateTexturePos(operand, test.type);
        check_literal_audit(!decoder.mErrorOccurred && !std::strcmp(operand, test.expected),
            "F280: truncation is safe when the operand already has the requested dimension");
    }
    Decompiler decoder; decoder.mErrorOccurred = false;
    char short_operand[opcodeSize] = "float2(1,2)";
    decoder.truncateTexturePos(short_operand, "Texture3D<float4>");
    check_literal_audit(decoder.mErrorOccurred && !std::strcmp(short_operand, "float2(1,2)"),
        "F280: an insufficient coordinate dimension is diagnosed without mutation");
    test_dx11_decompiler_public_texture_literal_regression();
}

bool dx11_immediate_bytecode_precision_matches(const void* original, const void* rebuilt)
{
    std::vector<std::vector<unsigned>> constants;
    for (const auto* bytecode : {original,rebuilt}) {
        Shader* shader=DecodeDXBC(static_cast<uint32_t*>(const_cast<void*>(bytecode)));
        check_literal_audit(shader!=nullptr,"F282: decode actual native shader immediates");
        std::vector<unsigned> values;
        for (const auto& instruction : shader->asPhase[MAIN_PHASE].ppsInst[0])
            for (unsigned i=0;i<instruction.ui32NumOperands;++i) {
                const auto& operand=instruction.asOperands[i];
                if (operand.eType!=OPERAND_TYPE_IMMEDIATE32) continue;
                for (int j=0;j<operand.iNumComponents;++j) {
                    unsigned bits;
                    std::memcpy(&bits,&operand.afImmediates[j],sizeof(bits));
                    values.push_back(bits);
                }
            }
        FreeShaderInfo(shader->sInfo); delete shader;
        constants.push_back(std::move(values));
    }
    return !constants[0].empty() && constants[0]==constants[1];
}

void check_dx11_immediate_bytecode_precision(const void* original, const void* rebuilt)
{
    check_literal_audit(dx11_immediate_bytecode_precision_matches(original,rebuilt),
        "F282: public decompile/native recompile preserves the exact binary32 literal bits");
}

void test_dx11_decompiler_immediate_regression()
{
    test_dx11_decompiler_public_immediate_regression();
    const float values[]={0.123456789f,-1.23456789f,16777216.0f,1e20f};
    for (int size=1;size<=4;++size) {
        Operand operand{}; operand.eType=OPERAND_TYPE_IMMEDIATE32; operand.iNumComponents=size;
        std::memcpy(operand.afImmediates,values,sizeof(values));
        char text[opcodeSize]="l(0.123457";
        for (int i=1;i<size;++i) strcat_s(text,",0");
        strcat_s(text,")");
        if (size%2==0) strcat_s(text," , \t");
        Decompiler decoder; decoder.mErrorOccurred=false;
        check_literal_audit(decoder.fixImm(text,operand)==text && !decoder.mErrorOccurred,
            "F282: valid immediate retains the pointer and restores decoded precision");
        const char* next=text+2;
        for (int i=0;i<size;++i) {
            float number=0; int consumed=0;
            check_literal_audit(sscanf_s(next,"%f%n",&number,&consumed)==1 &&
                std::memcmp(&number,&values[i],sizeof(number))==0,
                "F282: each emitted decimal round-trips to the decoded binary32 value");
            next+=consumed;
            check_literal_audit(*next==(i+1==size ? ')' : ','),"F282: emitted component count matches the decoded operand");
            ++next;
        }
        check_literal_audit(*next==0,"F282: emitted immediate has no trailing garbage");
    }
    Operand operand{}; operand.eType=OPERAND_TYPE_IMMEDIATE32; operand.iNumComponents=1; operand.afImmediates[0]=0.25f;
    for (const auto* pass : {"l(4)","l(4),","l(2147483647)","l(0x80000000)"}) {
        Decompiler decoder; decoder.mErrorOccurred=false;
        char text[opcodeSize]; strcpy_s(text,pass);
        decoder.fixImm(text,operand);
        check_literal_audit(!decoder.mErrorOccurred && !std::strcmp(text,pass),
            "F282: values outside the existing float match threshold preserve integer and bit-pattern text");
    }
    Decompiler legacy_inf; legacy_inf.mErrorOccurred=false;
    char infinity[opcodeSize]="l(1.#INF00)";
    legacy_inf.fixImm(infinity,operand);
    check_literal_audit(!legacy_inf.mErrorOccurred && !std::strcmp(infinity,"l(0.25)"),
        "F282: retain the existing positive legacy-INF special rewrite");
    operand.eType=OPERAND_TYPE_TEMP;
    char reg[opcodeSize]="r0.x"; legacy_inf.fixImm(reg,operand);
    check_literal_audit(!legacy_inf.mErrorOccurred && !std::strcmp(reg,"r0.x"),"F282: non-immediates remain untouched");
    operand.eType=OPERAND_TYPE_IMMEDIATE32; operand.afImmediates[0]=0.123456789f;
    for (const auto* invalid : {"", "l()", "l(bad)", "l(0.123457junk)", "l(0.123457,bad)",
            "l(0.123457,)", "l(1.#INF00junk)"}) {
        Decompiler decoder; decoder.mErrorOccurred=false;
        char text[opcodeSize]; strcpy_s(text,invalid);
        decoder.fixImm(text,operand);
        check_literal_audit(decoder.mErrorOccurred && !std::strcmp(text,invalid),
            "F282: failed or partial immediate scan reports an error without publishing decoded replacements");
    }
}

void check_dx11_decompiler_lifetime(const void* bytecode,const char* assembly,size_t size)
{
    auto decode=[&] { return ::DecodeDXBC(static_cast<uint32_t*>(const_cast<void*>(bytecode))); };
    auto destroy=[](Shader* shader) { ::FreeShaderInfo(shader->sInfo); delete shader; };
    destroy(decode());
    const auto before=decompiler_live_bytes();
    Shader* shader=decode();
    unsigned nested=0;
    for (unsigned i=0;i<shader->sInfo->ui32NumConstantBuffers;++i)
        for (const auto& variable:shader->sInfo->psConstantBuffers[i].asVars) nested+=member_array_count(variable.sType);
    const auto during=decompiler_live_bytes();
    destroy(shader);
    const auto after=decompiler_live_bytes();
    check_literal_audit(nested>=2 && during>before+sizeof(Shader)+sizeof(ShaderInfo) && after==before,
        "F283: allocation observation sees real Shader, ShaderInfo and nested native reflection allocations and release");
    const std::string text(assembly,size);
    check_decompiler_lifetime(bytecode,text,false,false);
    check_decompiler_lifetime(bytecode,text,true,false);
    auto invalid=text;
    const auto row=invalid.find("// SV_Target");
    check_literal_audit(row!=std::string::npos,"F283: real native output row is available for early-return injection");
    const auto end=invalid.find('\n',row);
    check_literal_audit(end!=std::string::npos,"F283: native output row has its terminator");
    invalid.replace(row,end-row,"// unrecognized output");
    check_decompiler_lifetime(bytecode,invalid,false,true);
}
