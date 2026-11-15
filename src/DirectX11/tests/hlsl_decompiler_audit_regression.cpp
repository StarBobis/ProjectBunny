// Keep BinaryDecompiler's private token definitions separate from the SDK
// headers in the WARP fixtures. This TU compiles the production decompiler once.
#include "../../HLSLDecompiler/DecompileHLSL.cpp"
#include <cstring>
#include <stdexcept>

void test_dx11_decompiler_public_literal_regression();
void test_dx11_decompiler_public_ibfe_regression();

namespace {
void check_literal_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
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
