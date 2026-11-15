// Keep BinaryDecompiler's private token definitions separate from the SDK
// headers in the WARP fixtures. This TU compiles the production decompiler once.
#include "../../HLSLDecompiler/DecompileHLSL.cpp"
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
