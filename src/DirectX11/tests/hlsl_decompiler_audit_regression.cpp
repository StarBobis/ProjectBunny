// This TU includes DecompileHLSL.cpp, which does "using namespace std".
// Newer MSVC introduces std::byte via <cstddef>, which clashes with the
// Windows SDK's global "byte" typedef (C2872) when <windows.h> follows.
#define _HAS_STD_BYTE 0
// Keep BinaryDecompiler's private token definitions separate from the SDK
// headers in the WARP fixtures. This TU compiles the production decompiler once.
#include "BinaryDecompiler/internal_includes/decode.h"
#include "BinaryDecompiler/internal_includes/reflect.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <vector>
#include <cstring>
#include "log.h"

// F-103 RED: the ObjectPos target array (op1) is a Decompiler member, so
// AddressSanitizer cannot place a redzone between op1 and op2.  The bytes
// immediately after op1 are op2; paint op2's first byte with 0xCC and check it
// at the first strcpy whose destination is op2 (the production code's next use
// of that member).  Production strcpy behaviour is otherwise unchanged: the
// wrapper forwards to the CRT.
namespace f103_red {
struct CanaryHit {};
inline thread_local unsigned char* canary = nullptr;
inline thread_local bool canary_hit = false;
inline char* checked_strcpy(char* dest, const char* src)
{
    if (canary && reinterpret_cast<unsigned char*>(dest) == canary) {
        if (*canary != 0xCC) {
            canary_hit = true;
            canary = nullptr;
            throw CanaryHit{};
        }
        canary = nullptr; // The canary slot is about to be written by production code.
    }
    return std::strcpy(dest, src);
}
}
#define strcpy f103_red::checked_strcpy
namespace {
Shader* decode_lifetime_audit(const void*, size_t);
void free_lifetime_audit(ShaderInfo*);
std::string header_time_lifetime_audit();
}
#define DecodeDXBC decode_lifetime_audit
#define FreeShaderInfo free_lifetime_audit
#define LogTime header_time_lifetime_audit
#include "../../HLSLDecompiler/DecompileHLSL.cpp"
#undef strcpy
#undef LogTime
#undef FreeShaderInfo
#undef DecodeDXBC
#include <cstring>
#include <stdexcept>
#include <malloc.h>
#include <windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <wrl/client.h>
#include "../../D3D_Shaders/stdafx.h"
#ifndef F281_FIXTURE_DIR
#error "F281_FIXTURE_DIR must be provided by dx11_upstream_regression"
#endif
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

// F-278b: place the line immediately before an inaccessible page, so a scan that
// reads the byte at `size` faults instead of silently reading a spare NUL.
class GuardedLineEnd {
    void* allocation = nullptr;
public:
    char* data = nullptr;
    explicit GuardedLineEnd(size_t bytes)
    {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        check_literal_audit(bytes > 0 && bytes <= system.dwPageSize,
            "F278B: guarded line fits before the inaccessible page");
        allocation = VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check_literal_audit(allocation != nullptr, "F278B: allocate guarded line");
        DWORD previous = 0;
        if (!VirtualProtect(static_cast<char*>(allocation) + system.dwPageSize,
                system.dwPageSize, PAGE_NOACCESS, &previous)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("F278B: protect the page after the line");
        }
        data = static_cast<char*>(allocation) + system.dwPageSize - bytes;
        std::memset(data, 'x', bytes);
    }
    ~GuardedLineEnd() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};

// F-273: text + readable NUL sentinel + a small slack for the parser's bounded
// lookahead, placed immediately before an inaccessible page. The removed fixed
// 200-byte SkipPacking scan runs past the sentinel and the slack into the guard.
class GuardedSignatureText {
    void* allocation = nullptr;
public:
    char* data = nullptr;
    size_t size = 0;
    explicit GuardedSignatureText(const std::string& text, size_t slack = 32)
    {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        size = text.size();
        const size_t bytes = size + 1 + slack;
        check_literal_audit(bytes > 0 && bytes <= system.dwPageSize,
            "F273: guarded signature text fits before the inaccessible page");
        allocation = VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check_literal_audit(allocation != nullptr, "F273: allocate guarded signature text");
        DWORD previous = 0;
        if (!VirtualProtect(static_cast<char*>(allocation) + system.dwPageSize,
                system.dwPageSize, PAGE_NOACCESS, &previous)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("F273: protect the page after the signature text");
        }
        data = static_cast<char*>(allocation) + system.dwPageSize - bytes;
        std::memcpy(data, text.data(), size);
        data[size] = '\0';
        std::memset(data + size + 1, 'x', slack);
    }
    ~GuardedSignatureText() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};

// F-277: keep the text, its c_str NUL and a 32-byte readable pad before the
// inaccessible page. strncmp/sscanf may read the NUL; only the old fixed
// 80-byte diagnostic read crosses the pad and faults.
class GuardedShortTail {
    void* allocation = nullptr;
public:
    char* data = nullptr;
    GuardedShortTail(const char* text, size_t size)
    {
        const size_t padding = 32;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        check_literal_audit(size + 1 + padding <= system.dwPageSize,
            "F277: the guarded tail and padding fit in one page");
        allocation = VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check_literal_audit(allocation != nullptr, "F277: allocate the guarded pages");
        DWORD previous = 0;
        if (!VirtualProtect(static_cast<char*>(allocation) + system.dwPageSize,
                system.dwPageSize, PAGE_NOACCESS, &previous)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("F277: protect the page after the tail");
        }
        data = static_cast<char*>(allocation) + system.dwPageSize - (size + 1 + padding);
        // Copy the view plus its caller-provided readable sentinel at text[size].
        std::memcpy(data, text, size + 1);
    }
    ~GuardedShortTail() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};

// F-277: kept free of C++ objects requiring unwinding so /EHsc permits __try.
DWORD f277_parse_short_tail(Decompiler& decoder, const char* text, size_t size, bool& warned)
{
    DWORD code = 0;
    decoder.mErrorOccurred = false;
    __try {
        decoder.ParseStructureDefinitions(nullptr, text, size);
        warned = decoder.mErrorOccurred;
    }
    __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    }
    return code;
}

// F-278b: drive the real ParseCode/DecompileBinaryHLSL path with an assembled
// multi-line ICB followed by a mov. The ICB branch must leave the newline for
// ParseCode's unconditional NextLine; otherwise the next physical line is lost.
void check_f278b_icb_progress(const std::string& text, bool expect_continuation)
{
    const char seed_source[] = "float4 main():SV_Target{return float4(0,0,0,1);}";
    Microsoft::WRL::ComPtr<ID3DBlob> seed, compile_errors;
    const HRESULT seed_hr = D3DCompile(seed_source, sizeof(seed_source) - 1, nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, seed.GetAddressOf(), compile_errors.GetAddressOf());
    if (FAILED(seed_hr) && compile_errors)
        std::fprintf(stderr, "F278B seed compile: %s\n", static_cast<const char*>(compile_errors->GetBufferPointer()));
    check_literal_audit(SUCCEEDED(seed_hr), "F278B: compile the ps_5_0 container seed");

    const auto* seed_bytes = static_cast<const unsigned char*>(seed->GetBufferPointer());
    std::vector<unsigned char> container(seed_bytes, seed_bytes + seed->GetBufferSize());
    std::vector<char> assembly(text.begin(), text.end());
    const auto bytecode = assembler(&assembly, container);
    check_literal_audit(!bytecode.empty(), "F278B: assemble the ICB fixture");

    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = bytecode.data();
    params.bytecodeSize = bytecode.size();
    params.decompiled = text.c_str();
    params.decompiledSize = text.size();
    params.G = &settings;
    bool patched = false, failed = false;
    std::string model;
    const auto hlsl = DecompileBinaryHLSL(params, patched, model, failed);
    if (failed || hlsl.empty())
        std::fprintf(stderr, "F278B ICB integration HLSL:\n%s\n", hlsl.c_str());
    check_literal_audit(!failed && !hlsl.empty(),
        "F278B: the multi-line ICB fixture decompiles without the error flag");
    check_literal_audit(hlsl.find("const float4 icb[] =") != std::string::npos,
        "F278B: the ICB declaration is emitted");
    check_literal_audit(hlsl.find("r0.y") != std::string::npos && hlsl.find("321") != std::string::npos,
        "F278B: the mov after the ICB is decompiled and not swallowed by the line scan");
    if (expect_continuation) {
        check_literal_audit(hlsl.find("{ 1.0, 2.0, 3.0, 4.0}") != std::string::npos,
            "F278B: the first ICB row is emitted");
        check_literal_audit(hlsl.find("{ 5.0, 6.0, 7.0, 8.0}") != std::string::npos,
            "F278B: the first continuation row of a multi-line ICB is not skipped");
    }
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

Shader* decode_lifetime_audit(const void* data, size_t byteSize)
{
    Shader* shader=::DecodeDXBC(data, byteSize);
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

void check_decompiler_lifetime(const void* bytecode,size_t bytecodeSize,const std::string& assembly,bool throw_at_header,bool reject_signature)
{
    DecompilerSettings settings;
    ParseParameters params{}; params.bytecode=bytecode; params.bytecodeSize=bytecodeSize; params.decompiled=assembly.c_str(); params.decompiledSize=assembly.size(); params.G=&settings;
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

void test_dx11_decompiler_next_line_regression()
{
    // F-278: production NextLine must test the bound before reading c[pos] and
    // must never leave pos past max. Before the fix, pos == max produced max + 1
    // (after reading c[max]); a line without a newline also produced max + 1.
    const char unterminated[] = "mov r0, r1";
    const size_t size = sizeof(unterminated) - 1;
    size_t pos = size;
    Decompiler::NextLine(unterminated, pos, size);
    std::fprintf(stderr, "F278 NextLine eof: pos=%zu max=%zu\n", pos, size);
    check_literal_audit(pos <= size, "F278: NextLine at pos==max must not advance past max");

    pos = 0;
    Decompiler::NextLine(unterminated, pos, size);
    std::fprintf(stderr, "F278 NextLine unterminated: pos=%zu max=%zu\n", pos, size);
    check_literal_audit(pos == size, "F278: a line without a newline ends exactly at max");

    const char terminated[] = "abc\ndef";
    const size_t terminated_size = sizeof(terminated) - 1;
    pos = 0;
    Decompiler::NextLine(terminated, pos, terminated_size);
    check_literal_audit(pos == 4, "F278: a legal newline still advances exactly one byte past it");

    const char trailing_newline[] = "abc\n";
    pos = 0;
    Decompiler::NextLine(trailing_newline, pos, sizeof(trailing_newline) - 1);
    check_literal_audit(pos == sizeof(trailing_newline) - 1,
        "F278: a trailing newline still lands exactly at max");
}

void test_dx11_decompiler_next_line_bounded_regression()
{
    // F-278b: every former open-coded scan now calls one helper. Each case starts
    // on the last byte (which is not a newline) of a line ending at a guard page;
    // reading c[size] would fault, so returning exactly size is required.
    // F-273 removed the former SkipPacking fixed-200 site; its bounded
    // replacement is covered by test_dx11_decompiler_skip_packing_regression below.
    const std::pair<const char*, size_t> sites[] = {
        {"DecompileHLSL.cpp:4377 dcl_immediateConstantBuffer", 64},
        {"DecompileHLSL.cpp:4717 immediate constant body", 96},
        {"DecompileHLSL.cpp:5270 LOG lookahead", 128},
        {"DecompileHLSL.cpp:5293 LOG lookahead tail", 160},
        {"DecompileHLSL.cpp:5576 MAX lookahead", 192},
        {"DecompileHLSL.cpp:5591 MAX lookahead tail", 224},
        {"DecompileHLSL.cpp:5704 DP3 lookahead", 256},
        {"DecompileHLSL.cpp:5723 DP3 lookahead tail", 320},
    };
    for (const auto& site : sites) {
        GuardedLineEnd line(site.second);
        const size_t pos = site.second - 1;
        const size_t next = next_line_bounded(line.data, pos, site.second);
        std::fprintf(stderr, "F278B %s: pos=%zu size=%zu next=%zu\n",
            site.first, pos, site.second, next);
        check_literal_audit(next == site.second,
            "F278B: a non-newline byte at size-1 stops at size without reading the guard page");
    }

    // :1675 compares "// }" at c[pos] after NextLine, where pos can equal size.
    // All real callers pass std::string::c_str(), so c[size] is a readable NUL
    // sentinel; IsBufferClose must compare it and report "not the marker".
    const std::string sentinel = "// }";
    const size_t sentinel_pos = sentinel.size();
    std::fprintf(stderr, "F278B DecompileHLSL.cpp:1693 buffer close: pos=%zu size=%zu\n",
        sentinel_pos, sentinel.size());
    check_literal_audit(sentinel.c_str()[sentinel_pos] == '\0',
        "F278B: std::string::c_str exposes c[size] as the readable NUL sentinel");
    check_literal_audit(!Decompiler::IsBufferClose(sentinel.c_str(), sentinel_pos),
        "F278B: :1675 at pos==size reads the sentinel and leaves the do/while");
    check_literal_audit(Decompiler::IsBufferClose(sentinel.c_str(), 0),
        "F278B: :1675 still recognises the close marker at a legal position");

    // Integration: a real multi-line ICB followed by mov. The ICB branch must
    // leave the newline for ParseCode's NextLine; the single-line control catches
    // the case where that NextLine eats the following instruction instead.
    const std::string signature =
        "// Output signature:\n"
        "//\n"
        "// Name                 Index   Mask Register SysValue  Format   Used\n"
        "// -------------------- ----- ------ -------- -------- ------- ------\n"
        "// SV_Target                0   xyzw        0   TARGET   float   xyzw\n"
        "//\n";
    check_f278b_icb_progress(signature +
        "ps_5_0\n"
        "dcl_output o0.xyzw\n"
        "dcl_temps 1\n"
        "dcl_immediateConstantBuffer { { 1.0, 2.0, 3.0, 4.0},\n"
        "                              { 5.0, 6.0, 7.0, 8.0} }\n"
        "mov r0.y, l(321.0)\n"
        "mov o0.xyzw, r0.xxxx\n"
        "ret\n", true);
    check_f278b_icb_progress(signature +
        "ps_5_0\n"
        "dcl_output o0.xyzw\n"
        "dcl_temps 1\n"
        "dcl_immediateConstantBuffer {  { 1.0, 2.0, 3.0, 4.0}  }\n"
        "mov r0.y, l(321.0)\n"
        "mov o0.xyzw, r0.xxxx\n"
        "ret\n", false);
}

void test_dx11_decompiler_skip_packing_regression()
{
    // F-273: a signature table can legitimately end with a TEXCOORD row without
    // a trailing newline (truncated/synthesised decompiled text). The old
    // SkipPacking scanned a fixed 200 bytes from the row start; the real tail is
    // only ~70 bytes, so it crossed the NUL and the slack into the guard page.
    const std::string outputText =
        "// Output signature:\n"
        "//\n"
        "// Name                 Index   Mask Register SysValue  Format   Used\n"
        "// -------------------- ----- ------ -------- -------- ------- ------\n"
        "// TEXCOORD                 0   xy          0     NONE   float   xy";
    const std::string inputText =
        "// Input signature:\n"
        "//\n"
        "// Name                 Index   Mask Register SysValue  Format   Used\n"
        "// -------------------- ----- ------ -------- -------- ------- ------\n"
        "// TEXCOORD                 0   xy          0     NONE   float   xy";

    {
        GuardedSignatureText guarded(outputText);
        Decompiler decoder;
        decoder.mErrorOccurred = false;
        const bool parsed = decoder.ParseOutputSignature(guarded.data, guarded.size, false);
        const std::string out(decoder.mOutput.begin(), decoder.mOutput.end());
        std::fprintf(stderr, "F273 output truncated: parsed=%d error=%d bytes=%zu\n",
            parsed ? 1 : 0, decoder.mErrorOccurred ? 1 : 0, out.size());
        check_literal_audit(parsed && !decoder.mErrorOccurred,
            "F273: truncated output signature tail is rejected without crossing the real end");
        check_literal_audit(out.find("out float2 o0 : TEXCOORD0") != std::string::npos,
            "F273: the single TEXCOORD row still decompiles with its real mask");
    }

    {
        GuardedSignatureText guarded(inputText);
        Shader shader{};
        Decompiler decoder;
        decoder.mErrorOccurred = false;
        decoder.ParseInputSignature(&shader, guarded.data, guarded.size);
        const std::string out(decoder.mOutput.begin(), decoder.mOutput.end());
        std::fprintf(stderr, "F273 input truncated: error=%d bytes=%zu\n",
            decoder.mErrorOccurred ? 1 : 0, out.size());
        check_literal_audit(!decoder.mErrorOccurred,
            "F273: truncated input signature tail does not fault");
        check_literal_audit(out.find("float2 v0 : TEXCOORD0") != std::string::npos,
            "F273: the input row survives with its real mask");
    }

    // The two-row packing pattern must still be recognised: the first row looks
    // ahead to a TEXCOORD with a different register and is forced to float4; the
    // last row has no third line and keeps its own mask.
    const std::string pairText =
        "// Output signature:\n//\n"
        "// Name                 Index   Mask Register SysValue  Format   Used\n"
        "// -------------------- ----- ------ -------- -------- ------- ------\n"
        "// TEXCOORD                 0   xy          0     NONE   float   xy\n"
        "// TEXCOORD                 1   xy          1     NONE   float   xy\n";
    {
        Decompiler decoder;
        decoder.mErrorOccurred = false;
        const bool parsed = decoder.ParseOutputSignature(pairText.data(), pairText.size(), false);
        const std::string out(decoder.mOutput.begin(), decoder.mOutput.end());
        check_literal_audit(parsed && !decoder.mErrorOccurred,
            "F273: the two-row TEXCOORD packing pattern is still recognized");
        check_literal_audit(out.find("out float4 o0 : TEXCOORD0") != std::string::npos,
            "F273: the first row is forced to float4 when the next TEXCOORD uses a different register");
        check_literal_audit(out.find("out float2 o1 : TEXCOORD1") != std::string::npos,
            "F273: the second row keeps its real float2");
    }

    const char pair[] =
        "// TEXCOORD 0 xy 0 NONE float xy\n"
        "// TEXCOORD 1 xy 1 NONE float xy";
    const char single[] = "// TEXCOORD 0 xy 0 NONE float xy";
    Decompiler direct;
    std::map<std::string, DataType> inUse;
    check_literal_audit(direct.SkipPacking(pair, sizeof(pair) - 1, inUse),
        "F273: a legal second TEXCOORD row still returns true");
    check_literal_audit(!direct.SkipPacking(single, sizeof(single) - 1, inUse),
        "F273: a row without a following newline returns false");
    check_literal_audit(!direct.SkipPacking(single, 0, inUse),
        "F273: an empty remaining range is rejected without reading");
}

void test_dx11_decompiler_f277_diagnostic_bounds_regression()
{
    // F-277: the snippet is bounded by the caller's size, not by strlen and not
    // by a fixed 80 bytes.
    const char sample[] = "abcdef";
    check_literal_audit(Decompiler::diagnosticSnippet(sample, 0, 6) == "abcdef",
        "F277: a short legal tail is copied in full");
    check_literal_audit(Decompiler::diagnosticSnippet(sample, 4, 6) == "ef",
        "F277: the snippet stops at size - pos");
    check_literal_audit(Decompiler::diagnosticSnippet(sample, 6, 6).empty() &&
        Decompiler::diagnosticSnippet(sample, 7, 6).empty(),
        "F277: pos >= size produces an empty snippet without unsigned underflow");
    check_literal_audit(Decompiler::diagnosticSnippet(nullptr, 0, 6).empty(),
        "F277: a null input produces an empty snippet");
    const std::string long_tail(100, 'x');
    check_literal_audit(Decompiler::diagnosticSnippet(long_tail.data(), 5, long_tail.size()).size() == 80,
        "F277: a long tail still caps the diagnostic at 80 bytes");

    // The 31-byte view leaves only 2 bytes on the // line. The old string(c,80)
    // read 77 bytes past the logical end; the readable sentinel and 32-byte pad
    // below keep strncmp/sscanf inside the page so only that diagnostic read
    // faults. The trailing 'x' is outside size and only keeps the existing
    // c[pos] probes at pos==size readable without relying on uninitialized stack
    // data.
    const char short_tail[] = "\n// Resource bind info for X\n//x";
    constexpr size_t short_tail_size = 31;
    static_assert(sizeof(short_tail) - 1 == short_tail_size + 1,
        "F277: the short tail view is 31 bytes plus one readable sentinel");
    {
        GuardedShortTail guarded(short_tail, short_tail_size);
        Decompiler decoder;
        bool warned = false;
        const DWORD code = f277_parse_short_tail(decoder, guarded.data, short_tail_size, warned);
        std::fprintf(stderr, "F277 31-byte tail: exception=0x%08lx warned=%d\n", code, warned);
        check_literal_audit(code == 0,
            "F277: a 2-byte tail must be diagnosed from size, not by reading 80 bytes");
        check_literal_audit(warned, "F277: the truncated // line is still diagnosed");
    }

    // The final line is "//   struct " with no type name. This is the :1755
    // direct diagnostic; fixing the helper alone does not cover it.
    const char struct_tail[] = "\n// Resource bind info for X\n// {\n//\n//   struct ";
    static_assert(sizeof(struct_tail) - 1 == 49, "F277: the struct tail is 49 bytes");
    {
        GuardedShortTail guarded(struct_tail, sizeof(struct_tail) - 1);
        Decompiler decoder;
        bool warned = false;
        const DWORD code = f277_parse_short_tail(decoder, guarded.data, sizeof(struct_tail) - 1, warned);
        std::fprintf(stderr, "F277 struct tail: exception=0x%08lx warned=%d\n", code, warned);
        check_literal_audit(code == 0,
            "F277: a short struct type name must be diagnosed from size, not by reading 80 bytes");
        check_literal_audit(warned, "F277: the truncated struct line is still diagnosed");
    }
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

// F-326: applySwizzle fills idx/idx1 (both char[4]) with up to four components
// and leaves no -1 sentinel for a four-component swizzle. The pre-fix loop
// condition read idx[i]/idx1[i] before testing i < 4, so the terminating
// iteration read one byte past the array. The loop body never ran at i == 4,
// so legal output must stay byte-for-byte identical.
static void check_f326_swizzle(const char* left, const char* operand, const char* expected)
{
    Decompiler decoder;
    decoder.mErrorOccurred = false;
    char right[opcodeSize];
    strcpy_s(right, operand);
    decoder.applySwizzle(left, right);
    std::fprintf(stderr, "F326 swizzle '%s' <- '%s': '%s'\n", operand, left, right);
    check_literal_audit(!decoder.mErrorOccurred && std::strcmp(right, expected) == 0,
        "F326: every legal component count stays inside idx[4] and keeps its existing output");
}

void test_dx11_decompiler_swizzle_slot_bound_regression()
{
    // Four components fill all of idx[0..3]; three/two/one stop on the -1
    // sentinel. The first case reaches the pre-fix idx[4] read in the generic
    // branch, so ASan aborts here before any later assertion runs.
    check_f326_swizzle("r0.xyzw", "r1", "r1.xyzw");
    check_f326_swizzle("r0.wzyx", "r1.xyzw", "r1.wzyx");
    check_f326_swizzle("r0.xyzw", "r1.wzyx", "r1.wzyx");
    check_f326_swizzle("r0.xyz", "r1", "r1.xyz");
    check_f326_swizzle("r0.w", "r1", "r1.w");
    check_f326_swizzle("r0", "r1", "r1.x");

    // dx9 constant-register branch: the first loop rewrites c0 with idx while
    // the second formats the constant through idx1. Both are char[4] too.
    for (const bool permuted : {false, true}) {
        Decompiler decoder;
        decoder.mErrorOccurred = false;
        decoder.mConstantValues[0] = ConstantValue{"c0", 1.0f, 2.0f, 3.0f, 4.0f};
        char right[opcodeSize];
        if (permuted) strcpy_s(right, "c0.xyzw"); else strcpy_s(right, "c0");
        decoder.applySwizzle(permuted ? "r0.wzyx" : "r0.xyzw", right);
        std::fprintf(stderr, "F326 dx9 constant %s: '%s'\n", permuted ? "permuted" : "four", right);
        const char* expected = permuted
            ? "c0.wzyx 4.000000 3.000000 2.000000 1.000000"
            : "c0.xyzw 1.000000 2.000000 3.000000 4.000000";
        check_literal_audit(!decoder.mErrorOccurred && std::strcmp(right, expected) == 0,
            "F326: dx9 constant registers exercise the same four-slot bound without reordering constants");
    }
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

bool dx11_immediate_bytecode_precision_matches(const void* original, size_t originalSize, const void* rebuilt, size_t rebuiltSize)
{
    std::vector<std::vector<unsigned>> constants;
    const void* bytecodes[] = {original, rebuilt};
    const size_t sizes[] = {originalSize, rebuiltSize};
    for (int i = 0; i < 2; ++i) {
        Shader* shader=DecodeDXBC(bytecodes[i], sizes[i]);
        check_literal_audit(shader!=nullptr,"F282: decode actual native shader immediates");
        std::vector<unsigned> values;
        for (const auto& instruction : shader->asPhase[MAIN_PHASE].ppsInst[0])
            for (unsigned operandIndex=0;operandIndex<instruction.ui32NumOperands;++operandIndex) {
                const auto& operand=instruction.asOperands[operandIndex];
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

void check_dx11_immediate_bytecode_precision(const void* original, size_t originalSize, const void* rebuilt, size_t rebuiltSize)
{
    check_literal_audit(dx11_immediate_bytecode_precision_matches(original,originalSize,rebuilt,rebuiltSize),
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

void check_dx11_decompiler_lifetime(const void* bytecode,size_t bytecodeSize,const char* assembly,size_t size)
{
    auto decode=[&] { return ::DecodeDXBC(bytecode, bytecodeSize); };
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
    check_decompiler_lifetime(bytecode,bytecodeSize,text,false,false);
    check_decompiler_lifetime(bytecode,bytecodeSize,text,true,false);
    auto invalid=text;
    const auto row=invalid.find("// SV_Target");
    check_literal_audit(row!=std::string::npos,"F283: real native output row is available for early-return injection");
    const auto end=invalid.find('\n',row);
    check_literal_audit(end!=std::string::npos,"F283: native output row has its terminator");
    invalid.replace(row,end-row,"// unrecognized output");
    check_decompiler_lifetime(bytecode,bytecodeSize,invalid,false,true);
}

namespace {
std::string read_f104_fixture(const char* name)
{
    std::ifstream input(std::string(F281_FIXTURE_DIR) + "/" + name, std::ios::binary);
    check_literal_audit(input.good(), "F104: open the checked-in native fixture");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

uint32_t read_f104_u32(const std::string& bytes, size_t offset)
{
    uint32_t value = 0;
    check_literal_audit(offset + sizeof(value) <= bytes.size(), "F104: fixture field is inside the container");
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void write_f104_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    check_literal_audit(offset + sizeof(value) <= bytes.size(), "F104: patched field is inside the container");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

uint32_t find_f104_chunk(const std::string& bytes, uint32_t fourcc)
{
    const uint32_t chunkCount = read_f104_u32(bytes, 28);
    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint32_t offset = read_f104_u32(bytes, 32 + size_t(i) * 4);
        if (read_f104_u32(bytes, offset) == fourcc) return offset;
    }
    return 0;
}

// Rebuild the container with chunkCount+1 offset slots and a four-byte hole
// after the table. The extra slot is zero, so old code would read the DXBC
// header as a chunk and still find the real SHDR payload.
std::vector<uint8_t> add_f104_extra_chunk_slot(const std::string& original)
{
    check_literal_audit(original.size() >= 32, "F104: fixture has a full DXBC header");
    const uint32_t totalSize = read_f104_u32(original, 24);
    const uint32_t chunkCount = read_f104_u32(original, 28);
    check_literal_audit(totalSize <= original.size(), "F104: fixture totalSize matches its bytes");

    std::vector<std::vector<uint8_t>> chunks;
    size_t chunkBytes = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint32_t offset = read_f104_u32(original, 32 + size_t(i) * 4);
        check_literal_audit(size_t(offset) + 8 <= totalSize, "F104: fixture chunk header is inside totalSize");
        const uint32_t payload = read_f104_u32(original, size_t(offset) + 4);
        check_literal_audit(size_t(offset) + 8 + payload <= totalSize, "F104: fixture chunk payload is inside totalSize");
        chunks.emplace_back(original.begin() + size_t(offset), original.begin() + size_t(offset) + 8 + payload);
        chunkBytes += chunks.back().size();
    }

    const size_t tableBytes = (size_t(chunkCount) + 1) * sizeof(uint32_t);
    const size_t holeBytes = sizeof(uint32_t);
    std::vector<uint8_t> rebuilt(32 + tableBytes + holeBytes + chunkBytes, 0);
    std::copy(original.begin(), original.begin() + 32, rebuilt.begin());
    write_f104_u32(rebuilt, 24, static_cast<uint32_t>(rebuilt.size()));
    write_f104_u32(rebuilt, 28, chunkCount + 1);

    size_t offset = 32 + tableBytes + holeBytes;
    write_f104_u32(rebuilt, 32 + size_t(chunkCount) * sizeof(uint32_t), 0);
    for (uint32_t i = 0; i < chunkCount; ++i) {
        write_f104_u32(rebuilt, 32 + size_t(i) * sizeof(uint32_t), static_cast<uint32_t>(offset));
        std::copy(chunks[i].begin(), chunks[i].end(), rebuilt.begin() + offset);
        offset += chunks[i].size();
    }
    return rebuilt;
}
}

void test_dx11_decode_dxbc_bounds_regression()
{
    const std::string bytecode = read_f104_fixture("negate-positive.dxbc");
    const std::string assembly = read_f104_fixture("negate-positive.asm");
    check_literal_audit(bytecode.size() >= 32 && !assembly.empty(), "F104: native fixture is complete");

    std::fprintf(stderr,"F104 section G1\n"); std::fflush(stderr);
    // G1: the full, legal container must keep decoding exactly as before.
    Shader* shader = ::DecodeDXBC(bytecode.data(), bytecode.size());
    check_literal_audit(shader && shader->ui32MajorVersion == 5 && shader->eShaderType == PIXEL_SHADER &&
        shader->ui32ShaderLength >= 2 && !shader->asPhase[MAIN_PHASE].ppsInst[0].empty(),
        "F104: legal DXBC fixture still decodes to a ps_5_0 instruction stream");
    ::FreeShaderInfo(shader->sInfo);
    delete shader;

    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = bytecode.data();
    params.bytecodeSize = bytecode.size();
    params.decompiled = assembly.c_str();
    params.decompiledSize = assembly.size();
    params.G = &settings;
    bool patched = false, failed = false;
    std::string model;
    const auto hlsl = DecompileBinaryHLSL(params, patched, model, failed);
    check_literal_audit(!failed && !hlsl.empty() && model == "ps_5_0",
        "F104: legal fixture keeps its previous public decompiler output");

    std::fprintf(stderr,"F104 section T1\n"); std::fflush(stderr);
    // T1: one DWORD shorter than the declared totalSize must be rejected.
    const uint32_t declaredSize = read_f104_u32(bytecode, 24);
    check_literal_audit(declaredSize >= 32 && declaredSize <= bytecode.size(), "F104: fixture totalSize is usable");
    check_literal_audit(::DecodeDXBC(bytecode.data(), declaredSize - sizeof(uint32_t)) == nullptr,
        "F104: truncated buffer is rejected instead of reading the missing tail");
    check_literal_audit(::DecodeDXBC(nullptr, 0) == nullptr, "F104: null bytecode is rejected");

    std::fprintf(stderr,"F104 section T2\n"); std::fflush(stderr);
    // T2: an extra chunk-count entry with a hole after the table must be rejected.
    const auto extraSlot = add_f104_extra_chunk_slot(bytecode);
    check_literal_audit(::DecodeDXBC(extraSlot.data(), extraSlot.size()) == nullptr,
        "F104: chunk offset outside the table is rejected");

    std::fprintf(stderr,"F104 section T6\n"); std::fflush(stderr);
    // T6: an absurd reflection count must be rejected from the chunk size
    // before LoadShaderInfo allocates any array. A returned Shader is owned by
    // ShaderPtr, so the test never frees sInfo by hand.
    std::fprintf(stderr,"F104 T6: before damaging signature count\n"); std::fflush(stderr);
    std::string damaged = bytecode;
    uint32_t isgn = find_f104_chunk(damaged, 0x4e475349u); // 'ISGN'
    if (!isgn) isgn = find_f104_chunk(damaged, 0x31475349u); // 'ISG1'
    if (!isgn) isgn = find_f104_chunk(damaged, 0x4e47534fu); // 'OSGN'
    if (!isgn) isgn = find_f104_chunk(damaged, 0x3147534fu); // 'OSG1'
    check_literal_audit(isgn != 0 && size_t(isgn) + 12 <= damaged.size(),
        "F104: signature count is inside the fixture");
    const uint32_t bogusCount = 0xFFFFFFFFu;
    std::memcpy(&damaged[isgn + 8], &bogusCount, sizeof(bogusCount));
    std::fprintf(stderr,"F104 T6: damaged count at chunk offset %u\n",isgn); std::fflush(stderr);
    auto decode_damaged = [&]() -> bool {
        std::fprintf(stderr,"F104 T6: before DecodeDXBC size=%zu\n",damaged.size()); std::fflush(stderr);
        try {
            ShaderPtr owner(::DecodeDXBC(damaged.data(), damaged.size()));
            std::fprintf(stderr,"F104 T6: after DecodeDXBC returned %s\n",owner ? "shader" : "nullptr"); std::fflush(stderr);
            if (owner) std::fprintf(stderr,"F104 T6: before ShaderPtr/FreeShaderInfo destruction\n");
            std::fflush(stderr);
            return !owner;
        } catch (...) {
            std::fprintf(stderr,"F104 T6: DecodeDXBC threw; production owner already cleaned up\n"); std::fflush(stderr);
            return true;
        }
    };
    check_literal_audit(decode_damaged(), "F104: absurd reflection element count is rejected before allocation");
    const auto beforeDamaged = decompiler_live_bytes();
    for (unsigned i = 0; i < 64; ++i) (void)decode_damaged();
    const auto afterDamaged = decompiler_live_bytes();
    check_literal_audit(beforeDamaged == afterDamaged,
        "F104: rejected reflection counts leave no Shader or reflection storage behind");

    std::fprintf(stderr,"F104 section G5\n"); std::fflush(stderr);
    // G5/T8: the DX9 scan keeps a legal token stream and rejects one that ends
    // before END instead of reading the next free DWORD.
    std::vector<uint32_t> dx9Tokens{0xfffe0300, 0x02000001, 0x800f0000, 0xa0e40000, 0x0000ffff};
    Shader* dx9 = nullptr;
    try {
        dx9 = ::DecodeDX9BC(dx9Tokens.data(), dx9Tokens.size() * sizeof(uint32_t));
    } catch (...) {
    }
    check_literal_audit(dx9 && dx9->dx9Shader && dx9->ui32MajorVersion == 3,
        "F104: legal DX9 token stream still decodes");
    if (dx9) {
        ::FreeShaderInfo(dx9->sInfo);
        delete dx9;
    }

    bool dx9Rejected = false;
    try {
        Shader* truncated = ::DecodeDX9BC(dx9Tokens.data(), (dx9Tokens.size() - 1) * sizeof(uint32_t));
        dx9Rejected = (truncated == nullptr);
        if (truncated) {
            ::FreeShaderInfo(truncated->sInfo);
            delete truncated;
        }
    } catch (...) {
        dx9Rejected = true;
    }
    check_literal_audit(dx9Rejected, "F104: DX9 stream truncated before END is rejected");
}

namespace {

//F-101: minimal well-formed DXBC container around a raw SHDR/SHEX payload.
std::vector<uint32_t> make_f101_shader(SHADER_TYPE type, std::initializer_list<uint32_t> body)
{
    std::vector<uint32_t> shader;
    shader.push_back((static_cast<uint32_t>(type) << 16) | 0x50u); // SM 5.0
    shader.push_back(static_cast<uint32_t>(body.size() + 2));      // version + length
    shader.insert(shader.end(), body.begin(), body.end());
    return shader;
}

std::vector<uint32_t> make_f101_dxbc(const std::vector<uint32_t>& shader)
{
    // 32-byte DXBC header + one offset + 8-byte chunk header + payload.
    std::vector<uint32_t> bytes(11 + shader.size(), 0);
    std::memcpy(&bytes[0], "DXBC", 4);
    bytes[5] = 1;                                                   // header.one
    bytes[6] = static_cast<uint32_t>(bytes.size() * sizeof(uint32_t));
    bytes[7] = 1;                                                   // chunkCount
    bytes[8] = 36;                                                  // chunk table[0]
    std::memcpy(&bytes[9], "SHEX", 4);
    bytes[10] = static_cast<uint32_t>(shader.size() * sizeof(uint32_t));
    std::copy(shader.begin(), shader.end(), bytes.begin() + 11);
    return bytes;
}

size_t f101_shader_token_cap(const std::vector<uint32_t>& words)
{
    // DXBC words 11/12 are the shader payload's version/length DWORDs.
    return words.size() > 12 ? static_cast<size_t>(words[12]) : 0;
}

std::vector<uint32_t> f101_case_words(const char* name)
{
    const uint32_t L1 = 1u << 24;
    const uint32_t L127 = 127u << 24;
    if (!std::strcmp(name, "S2-missing-length"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_CUSTOMDATA)}));
    if (!std::strcmp(name, "S2-short-length"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_CUSTOMDATA), 1u}));
    if (!std::strcmp(name, "S2-overlong"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_NOP) | L1,
             static_cast<uint32_t>(OPCODE_CUSTOMDATA), 0xFFFFFFFFu}));
    if (!std::strcmp(name, "S4-extended-overrun"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_RET) | L1,
             static_cast<uint32_t>(OPCODE_RET) | L1 | 0x80000000u, 0u}));
    if (!std::strcmp(name, "S5-decl-zero"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_DCL_INPUT), 0u}));
    if (!std::strcmp(name, "S6-inst-zero"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_RET) | L1,
             static_cast<uint32_t>(OPCODE_NOP)}));
    if (!std::strcmp(name, "S7-hull-zero"))
        return make_f101_dxbc(make_f101_shader(HULL_SHADER,
            {static_cast<uint32_t>(OPCODE_NOP)}));
    if (!std::strcmp(name, "S7-hull-second"))
        return make_f101_dxbc(make_f101_shader(HULL_SHADER,
            {static_cast<uint32_t>(OPCODE_DCL_GLOBAL_FLAGS) | L1,
             static_cast<uint32_t>(OPCODE_NOP)}));
    if (!std::strcmp(name, "T5-overlong"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_NOP) | L127}));
    if (!std::strcmp(name, "T8-msad"))
        return make_f101_dxbc(make_f101_shader(PIXEL_SHADER,
            {static_cast<uint32_t>(OPCODE_NOP) | L1,
             static_cast<uint32_t>(OPCODE_MSAD) | L1}));
    return std::vector<uint32_t>();
}

unsigned f101_expected_passes(const char* name)
{
    if (!std::strcmp(name, "S6-inst-zero")) return 2;
    if (!std::strcmp(name, "S7-hull-second")) return 2;
    return 1;
}

int f101_run_child(const char* name)
{
    const std::vector<uint32_t> words = f101_case_words(name);
    if (words.empty()) {
        std::fprintf(stderr, "F-101 child: unknown case %s\n", name);
        return 44;
    }
    std::fprintf(stderr, "F-101 child %s: bad-token pass(es)=%u shader token cap=%zu\n",
        name, f101_expected_passes(name), f101_shader_token_cap(words));
    std::fflush(stderr);
    try {
        Shader* shader = ::DecodeDXBC(words.data(), words.size() * sizeof(uint32_t));
        if (shader) {
            ::FreeShaderInfo(shader->sInfo);
            delete shader;
        }
        std::fprintf(stderr, "F-101 child %s: decode returned without rejecting\n", name);
        return 43;
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "F-101 child %s: ran into the memory cap\n", name);
        return 42;
    } catch (...) {
        std::fprintf(stderr, "F-101 child %s: rejected by decompileError\n", name);
        return 0;
    }
}

class F101Handle {
public:
    explicit F101Handle(HANDLE value = nullptr) : value(value) {}
    ~F101Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    F101Handle(const F101Handle&) = delete;
    F101Handle& operator=(const F101Handle&) = delete;
    HANDLE get() const { return value; }
private:
    HANDLE value;
};

void f101_require_child_rejects(const char* name)
{
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    check_literal_audit(length != 0 && length < ARRAYSIZE(executable),
        "F-101: locate the regression host for the isolated child");
    check_literal_audit(SetEnvironmentVariableA("F101_DECODE_CASE", name) != FALSE,
        "F-101: publish the child case name");

    F101Handle job(CreateJobObjectW(nullptr, nullptr));
    check_literal_audit(job.get() != nullptr, "F-101: create owned child job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = 512ull * 1024 * 1024;
    check_literal_audit(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
        &limits, sizeof(limits)) != FALSE, "F-101: configure child memory cap");

    std::wstring command = L"\"" + std::wstring(executable) +
        L"\" --audit-item F-101 --fixture-isolated";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    check_literal_audit(CreateProcessW(executable, &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process) != FALSE,
        "F-101: launch isolated child");
    F101Handle child(process.hProcess);
    F101Handle thread(process.hThread);

    if (!AssignProcessToJobObject(job.get(), child.get()) ||
        ResumeThread(thread.get()) == DWORD(-1)) {
        TerminateProcess(child.get(), EXIT_FAILURE);
        WaitForSingleObject(child.get(), 5000);
        throw std::runtime_error("F-101: attach and resume the isolated child");
    }
    check_literal_audit(SetEnvironmentVariableA("F101_DECODE_CASE", nullptr) != FALSE,
        "F-101: clear the child case name");

    const DWORD waited = WaitForSingleObject(child.get(), 5000);
    if (waited != WAIT_OBJECT_0) {
        TerminateJobObject(job.get(), ERROR_TIMEOUT);
        WaitForSingleObject(child.get(), 5000);
    }
    DWORD code = EXIT_FAILURE;
    const BOOL collected = GetExitCodeProcess(child.get(), &code);
    std::fprintf(stderr, "F-101 child %s: exit=%lu wait=%lu\n", name, code, waited);
    check_literal_audit(waited == WAIT_OBJECT_0 && collected && code == 0,
        "F-101: a zero-length token must reject inside the 5s/512MiB child cap");
}

void f101_require_rejected(const char* name)
{
    const std::vector<uint32_t> words = f101_case_words(name);
    check_literal_audit(!words.empty(), "F-101: known decoder length fixture");
    bool threw = false;
    Shader* shader = nullptr;
    try { shader = ::DecodeDXBC(words.data(), words.size() * sizeof(uint32_t)); }
    catch (const std::bad_alloc&) { threw = false; }
    catch (...) { threw = true; }
    if (shader) {
        ::FreeShaderInfo(shader->sInfo);
        delete shader;
    }
    std::fprintf(stderr, "F-101 %s: threw=%d returned=%s shader token cap=%zu\n",
        name, threw ? 1 : 0, shader ? "shader" : "nullptr", f101_shader_token_cap(words));
    check_literal_audit(threw && shader == nullptr,
        "F-101: a zero or overlong token length must throw decompileError");
}

void f101_check_legal_fixture()
{
    const std::string bytecode = read_f104_fixture("negate-positive.dxbc");
    const std::string assembly = read_f104_fixture("negate-positive.asm");
    uint32_t shex = find_f104_chunk(bytecode, 0x58454853u); // 'SHEX'
    if (!shex) shex = find_f104_chunk(bytecode, 0x52444853u); // 'SHDR'
    check_literal_audit(shex != 0, "F-101: legal fixture has a shader chunk");
    const uint32_t declaredLength = read_f104_u32(bytecode, size_t(shex) + 12);
    check_literal_audit(declaredLength >= 2, "F-101: legal fixture declares a full shader length");

    Shader* shader = ::DecodeDXBC(bytecode.data(), bytecode.size());
    check_literal_audit(shader != nullptr, "F-101: legal native fixture still decodes");
    check_literal_audit(shader->ui32MajorVersion == 5 && shader->eShaderType == PIXEL_SHADER &&
        shader->ui32ShaderLength == declaredLength,
        "F-101: legal fixture keeps ps_5_0 and its declared length");
    const auto& decls = shader->asPhase[MAIN_PHASE].ppsDecl[0];
    const auto& insts = shader->asPhase[MAIN_PHASE].ppsInst[0];
    check_literal_audit(decls.size() == 2 && decls[0].eOpcode == OPCODE_DCL_GLOBAL_FLAGS &&
        decls[1].eOpcode == OPCODE_DCL_OUTPUT,
        "F-101: legal fixture keeps both declarations");
    check_literal_audit(insts.size() == 3 && insts[0].eOpcode == OPCODE_FTOU &&
        insts[1].eOpcode == OPCODE_MOV && insts[2].eOpcode == OPCODE_RET,
        "F-101: legal fixture keeps ftou/mov/ret in order");
    ::FreeShaderInfo(shader->sInfo);
    delete shader;

    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = bytecode.data();
    params.bytecodeSize = bytecode.size();
    params.decompiled = assembly.c_str();
    params.decompiledSize = assembly.size();
    params.G = &settings;
    bool patched = false, failed = false;
    std::string model;
    const std::string first = DecompileBinaryHLSL(params, patched, model, failed);
    check_literal_audit(!failed && !first.empty() && model == "ps_5_0",
        "F-101: legal fixture keeps its previous public decompiler output");
    DecompilerSettings settings2;
    params.G = &settings2;
    patched = false;
    failed = false;
    const std::string second = DecompileBinaryHLSL(params, patched, model, failed);
    check_literal_audit(!failed && second == first,
        "F-101: legal fixture output is stable across the checked length path");
}

void f101_check_free_shader_info()
{
    ShaderInfo zero{};
    ::FreeShaderInfo(&zero);
    check_literal_audit(zero.psInputSignatures == nullptr && zero.psOutputSignatures == nullptr &&
        zero.psPatchConstantSignatures == nullptr && zero.psResourceBindings == nullptr &&
        zero.psConstantBuffers == nullptr && zero.psClassTypes == nullptr &&
        zero.psClassInstances == nullptr,
        "F-101: FreeShaderInfo accepts an all-zero ShaderInfo");
    ::FreeShaderInfo(nullptr);

    Shader partial;
    partial.sInfo->psInputSignatures = new InOutSignature[2];
    partial.sInfo->ui32NumInputSignatures = 2;
    partial.sInfo->psConstantBuffers = new ConstantBuffer[1];
    partial.sInfo->ui32NumConstantBuffers = 1;
    ::FreeShaderInfo(partial.sInfo);
    check_literal_audit(partial.sInfo->psInputSignatures == nullptr &&
        partial.sInfo->psConstantBuffers == nullptr &&
        partial.sInfo->ui32NumInputSignatures == 0 &&
        partial.sInfo->ui32NumConstantBuffers == 0,
        "F-101: FreeShaderInfo accepts a partially populated ShaderInfo");
}

void f101_check_failed_decode_lifetime()
{
    const std::vector<uint32_t> words = f101_case_words("T8-msad");
    auto attempt = [&] {
        try {
            Shader* shader = ::DecodeDXBC(words.data(), words.size() * sizeof(uint32_t));
            if (shader) {
                ::FreeShaderInfo(shader->sInfo);
                delete shader;
            }
        } catch (...) {
        }
    };
    attempt(); // Exclude one-time logging/CRT initialization.
    const size_t before = decompiler_live_bytes();
    for (unsigned iteration = 0; iteration < 64; ++iteration)
        attempt();
    const size_t after = decompiler_live_bytes();
    std::fprintf(stderr, "F-101: failed decode live bytes %zu -> %zu (64 attempts)\n", before, after);
    check_literal_audit(before == after,
        "F-101: failed DecodeDXBC releases Shader and ShaderInfo");
}

void f101_check_public_failure_contract()
{
    const std::vector<uint32_t> words = f101_case_words("T5-overlong");
    const std::string assembly = read_f104_fixture("negate-positive.asm");
    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = words.data();
    params.bytecodeSize = words.size() * sizeof(uint32_t);
    params.decompiled = assembly.c_str();
    params.decompiledSize = assembly.size();
    params.G = &settings;
    bool patched = false, failed = false;
    std::string model;
    bool threw = false;
    std::string result;
    try { result = DecompileBinaryHLSL(params, patched, model, failed); }
    catch (...) { threw = true; }
    std::fprintf(stderr, "F-101 public: threw=%d failed=%d output=%zu\n",
        threw ? 1 : 0, failed ? 1 : 0, result.size());
    check_literal_audit(!threw && failed && result.empty(),
        "F-101: decode failure is caught at the public entry and reported as failed/empty");
}

static_assert(noexcept(ShaderOwnerDeleter{}(static_cast<Shader*>(nullptr))),
    "F-101: ShaderOwnerDeleter must be noexcept");
static_assert(std::is_nothrow_destructible<ShaderPtr>::value,
    "F-101: ShaderPtr destruction must be noexcept");

}

void test_dx11_decompiler_decode_bounds_regression()
{
    if (const char* child_case = std::getenv("F101_DECODE_CASE"))
        std::_Exit(f101_run_child(child_case));

    std::fputs("F-101: checked token length, bounded loops and owner cleanup\n", stderr);
    std::fflush(stderr);

    f101_check_legal_fixture();
    for (const char* name : {"S2-missing-length", "S2-short-length", "S2-overlong",
            "S4-extended-overrun", "S5-decl-zero", "S6-inst-zero", "S7-hull-zero",
            "S7-hull-second", "T5-overlong"}) {
        f101_require_rejected(name);
    }
    f101_check_free_shader_info();
    f101_check_failed_decode_lifetime();
    f101_check_public_failure_contract();
    for (const char* name : {"S5-decl-zero", "S6-inst-zero", "S7-hull-zero", "S7-hull-second"})
        f101_require_child_rejects(name);
}

void test_dx11_decompiler_negative_literal_regression()
{
    if (const char *corpus = std::getenv("BUNNY_F281_CORPUS_DIR")) {
        const char *output = std::getenv("BUNNY_F281_CORPUS_OUT");
        check_literal_audit(output && *output, "F281: corpus output directory is set");
        std::filesystem::create_directories(output);
        size_t count = 0;
        for (const auto &entry : std::filesystem::directory_iterator(corpus)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".dxbc") continue;
            std::ifstream binary(entry.path(), std::ios::binary);
            const std::string bytecode((std::istreambuf_iterator<char>(binary)), std::istreambuf_iterator<char>());
            if (bytecode.empty()) continue;
            Microsoft::WRL::ComPtr<ID3DBlob> assembly;
            std::string text;
            if (SUCCEEDED(D3DDisassemble(bytecode.data(), bytecode.size(), 0, nullptr, assembly.GetAddressOf())))
                text.assign(static_cast<const char *>(assembly->GetBufferPointer()));
            DecompilerSettings settings;
            ParseParameters params{};
            params.bytecode = bytecode.data();
            params.bytecodeSize = bytecode.size();
            params.decompiled = text.c_str();
            params.decompiledSize = text.size();
            params.G = &settings;
            bool patched = false, failed = false;
            std::string model;
            const auto hlsl = text.empty() ? std::string() : DecompileBinaryHLSL(params, patched, model, failed);
            std::ofstream result(std::filesystem::path(output) / (entry.path().stem().string() + ".hlsl"), std::ios::binary);
            result << "// failed=" << (failed ? 1 : 0) << " hlsl_bytes=" << hlsl.size() << "\n" << hlsl;
            ++count;
        }
        std::fprintf(stderr, "F281 corpus: %zu shader(s) dumped to %s\n", count, output);
        return;
    }
    const auto read_fixture = [](const std::string& name) {
        std::ifstream input(std::string(F281_FIXTURE_DIR) + "/" + name, std::ios::binary);
        check_literal_audit(input.good(), "F281: open the checked-in native fixture");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    for (const char* stem : {"negate-positive", "negate-negative"}) {
        const auto bytecode = read_fixture(std::string(stem) + ".dxbc");
        const auto assembly = read_fixture(std::string(stem) + ".asm");
        check_literal_audit(!bytecode.empty() && !assembly.empty(), "F281: fixture bytecode and assembly are complete");
        std::fprintf(stderr, "F281 %s: dxbc=%zuB asm=%zuB\n", stem, bytecode.size(), assembly.size());

        DecompilerSettings settings;
        ParseParameters params{};
        params.bytecode = bytecode.data();
        params.bytecodeSize = bytecode.size();
        params.decompiled = assembly.c_str();
        params.decompiledSize = assembly.size();
        params.G = &settings;
        bool patched = false, failed = false;
        std::string model;
        const auto hlsl = DecompileBinaryHLSL(params, patched, model, failed);
        std::fprintf(stderr, "F281 %s: model=%s failed=%d hlsl=%zuB\n", stem, model.c_str(), failed ? 1 : 0, hlsl.size());
        if (failed || hlsl.empty())
            std::fprintf(stderr, "F281 %s failed HLSL:\n%s\n", stem, hlsl.c_str());
        check_literal_audit(!failed && !hlsl.empty(),
            "F281: the native negated literal decompiles without the fallback error flag");
        check_literal_audit(hlsl.find("(uint6)") == std::string::npos,
            "F281: the -l( operand is not split into a bogus (uint6) cast");

        Microsoft::WRL::ComPtr<ID3DBlob> rebuilt, compile_errors;
        const HRESULT compiled = D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
            rebuilt.GetAddressOf(), compile_errors.GetAddressOf());
        if (FAILED(compiled)) {
            if (compile_errors)
                std::fprintf(stderr, "F281 %s compiler:\n%s\n", stem, static_cast<const char*>(compile_errors->GetBufferPointer()));
            std::fprintf(stderr, "F281 %s HLSL:\n%s\n", stem, hlsl.c_str());
        }
        check_literal_audit(SUCCEEDED(compiled),
            "F281: regenerated negated-literal HLSL compiles natively for ps_5_0");
    }
}

void check_dx11_dx9_dcl_const_without_ctab_lifetime(const uint32_t* tokens, size_t tokenCount)
{
    const auto decode = [&]() {
        try {
            Shader* shader = DecodeDX9BC(tokens, tokenCount * sizeof(uint32_t));
            if (!shader) return false;
            FreeShaderInfo(shader->sInfo);
            delete shader;
            return true;
        } catch (...) {
            return false;
        }
    };

    check_literal_audit(!decode(), "F126: the reviewed malformed stream is rejected before the lifetime loop");
    const auto before = decompiler_live_bytes();
    bool unexpectedly_decoded = false;
    for (unsigned iteration = 0; iteration < 64; ++iteration)
        unexpectedly_decoded = unexpectedly_decoded || decode();
    const auto after = decompiler_live_bytes();
    std::fprintf(stderr, "F126: live allocation bytes %zu -> %zu (64 rejected decodes)\n", before, after);
    check_literal_audit(!unexpectedly_decoded,
        "F126: every repeated decode of the malformed stream is rejected");
    check_literal_audit(before == after,
        "F126: rejected decodes release the Shader and ShaderInfo");
}

// ---------------------------------------------------------------------------
// F-103 RED: fixed-buffer bounds in DecompileHLSL.cpp.
//
// Current worktree line numbers (the design document's numbers are one line
// lower for the same code identities):
//   R1  1238-1239  ParseBufferDefinitions cbuffer line memcpy + NUL
//   R2  1285-1288  ParseBufferDefinitions struct-name hand-written loop
//   R3  3382-3383  WritePatches position-texture register memcpy + NUL
//   R4  3534-3535  WritePatches ObjectPos_ID1 memcpy + NUL
//   R5  3589-3590  WritePatches ObjectPos_ID2 memcpy + NUL
//   M1  old 1178 -> 1238-1239 (255-byte legal cbuffer member name)
//   M2  old 1228 -> 1285-1288  (258-byte struct-name span, 2-byte overflow)
//   M3  old 3328 -> 3534-3535 / 3589-3590 (128-byte target, NUL byte overflow)
//
// R1/R2/R3/M1/M2 write local stack arrays, so AddressSanitizer observes them.
// R4/R5/M3 write op1, a Decompiler member; ASan has no redzone between op1
// and op2, so those use the op2 0xCC canary defined at the top of this TU.
// ---------------------------------------------------------------------------
namespace {

void f103_require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string f103_text(const Decompiler& decoder)
{
    return std::string(decoder.mOutput.begin(), decoder.mOutput.end());
}

struct F103RunResult {
    bool threw = false;
    bool canary_hit = false;
    bool error = false;
    bool saw_world_pos = false;
    bool saw_stereo_pos = false;
    std::string output;
};

F103RunResult f103_run_write_patches(Decompiler& decoder, bool arm_canary)
{
    F103RunResult result;
    f103_red::canary_hit = false;
    if (arm_canary) {
        f103_require(reinterpret_cast<unsigned char*>(decoder.op2) ==
                reinterpret_cast<unsigned char*>(decoder.op1) + sizeof(decoder.op1),
            "F-103: the op2 canary requires op2 to immediately follow op1");
        decoder.op2[0] = static_cast<char>(0xCC);
        f103_red::canary = reinterpret_cast<unsigned char*>(decoder.op2);
    }
    try {
        decoder.WritePatches();
    } catch (const f103_red::CanaryHit&) {
        result.canary_hit = true;
    } catch (...) {
        result.threw = true;
    }
    f103_red::canary = nullptr;
    result.error = decoder.mErrorOccurred;
    result.output = f103_text(decoder);
    result.saw_world_pos = result.output.find("float3 worldPos") != std::string::npos;
    result.saw_stereo_pos = result.output.find("stereoPos") != std::string::npos;
    return result;
}

void f103_prepare_write_patches(Decompiler& decoder, DecompilerSettings& settings,
    const std::string& object_name, bool id2)
{
    settings = DecompilerSettings{};
    settings.ZRepair_PositionTexture = "F103PosTex";
    settings.ZRepair_WorldPosCalc = "%s";
    settings.InvTransforms.push_back("F103InvTransform");
    if (id2) settings.ObjectPos_ID2 = object_name;
    else settings.ObjectPos_ID1 = object_name;

    decoder.G = &settings;
    decoder.mErrorOccurred = false;
    decoder.mPatched = false;
    decoder.mUsesProjection = false;
    decoder.mShaderType = "ps_5_0";
    decoder.mTextureNames[0] = "F103PosTex";
    decoder.mCBufferData[0] = BufferEntry{DT_float4, 0, false, object_name};
    decoder.mCodeStartPos = 0;
}

void f103_set_object_pos_output(Decompiler& decoder, const std::string& dest,
    const std::string& object_name)
{
    const std::string text =
        "\nvoid main(\n  float4 p : SV_Position0,\n  float4 q : TEXCOORD0)\n{\n"
        "  o0.xyzw = F103PosTex.Sample(s, uv);\n"
        "  " + dest + " = " + object_name + ".xyz;\n"
        "}\n";
    decoder.mOutput.assign(text.begin(), text.end());
}

void f103_check_object_pos_case(const char* label, bool id2, const std::string& dest,
    bool expect_controlled_failure)
{
    DecompilerSettings settings;
    Decompiler decoder;
    const std::string object_name = id2 ? "F103ObjPos2" : "F103ObjPos1";
    f103_prepare_write_patches(decoder, settings, object_name, id2);
    f103_set_object_pos_output(decoder, dest, object_name);

    std::fprintf(stderr, "%s: target=%zu bytes, canary at op2[0], object=%s\n",
        label, dest.size(), object_name.c_str());
    std::fflush(stderr);
    const auto result = f103_run_write_patches(decoder, true);
    std::fprintf(stderr, "%s: canary_hit=%d threw=%d error=%d stereo=%d world=%d\n",
        label, static_cast<int>(result.canary_hit), static_cast<int>(result.threw),
        static_cast<int>(result.error), static_cast<int>(result.saw_stereo_pos),
        static_cast<int>(result.saw_world_pos));

    f103_require(!result.canary_hit,
        (std::string(label) + ": the target write crossed op1 and overwrote the op2 0xCC canary").c_str());
    if (expect_controlled_failure) {
        f103_require(result.threw || result.error,
            (std::string(label) + ": an overlong target must fail as a controlled decompile error").c_str());
        f103_require(!result.saw_stereo_pos,
            (std::string(label) + ": a controlled failure must not publish a truncated stereoPos patch").c_str());
    } else {
        f103_require(result.saw_world_pos && result.saw_stereo_pos,
            (std::string(label) + ": the production position/ObjectPos path must be reached").c_str());
    }
}

void f103_check_struct_name_case(const char* label, size_t name_length)
{
#ifdef DX11_AUDIT_ASAN_DECOMPILER
    DecompilerSettings settings;
    Decompiler decoder;
    decoder.G = &settings;
    decoder.mErrorOccurred = false;
    decoder.mCBufferNames["cb0"] = 0;
    const std::string name(name_length, 's');
    // Put the closing brace on its own short line so the memcpy at :1238
    // copies only "//   }\n"; the struct-name loop then crosses into the next
    // line and overflows buffer[256] without R1 masking it.
    const std::string text =
        "// cbuffer cb0\n// {\n"
        "//   struct\n//   {\n"
        "//     float4 a; // Offset:    0 Size:   16\n"
        "//   }\n" + name + "; // Offset:    0\n"
        "// }\n";
    std::fprintf(stderr, "%s: struct name=%zu bytes, span=%zu bytes, buffer[256]\n",
        label, name_length, 2 + name_length);
    std::fflush(stderr);
    bool threw = false;
    try {
        decoder.ParseBufferDefinitions(nullptr, text.c_str(), text.size());
    } catch (...) {
        threw = true;
    }
    const std::string output = f103_text(decoder);
    f103_require(!threw && !decoder.mErrorOccurred,
        (std::string(label) + ": the struct declaration must parse").c_str());
    f103_require(output.find(name) != std::string::npos,
        (std::string(label) + ": the complete struct name must be emitted").c_str());
#else
    (void)label;
    (void)name_length;
    throw std::runtime_error("F-103 struct-name case requires DX11_AUDIT_ASAN_DECOMPILER=ON");
#endif
}
}

void test_dx11_decompiler_f103_red_r1()
{
#ifdef DX11_AUDIT_ASAN_DECOMPILER
    DecompilerSettings settings;
    Decompiler decoder;
    decoder.G = &settings;
    decoder.mErrorOccurred = false;
    decoder.mCBufferNames["cb0"] = 0;
    const std::string name(600, 'n');
    const std::string line = "//   float4 " + name + ";// Offset:    0 Size:   16";
    const std::string text = "// cbuffer cb0\n// {\n" + line + "\n// }\n";
    std::fprintf(stderr, "F-103 RED R1 ParseBufferDefinitions (DecompileHLSL.cpp:1238): 600-byte cbuffer line\n");
    std::fflush(stderr);
    bool threw = false;
    try {
        decoder.ParseBufferDefinitions(nullptr, text.c_str(), text.size());
    } catch (...) {
        threw = true;
    }
    const std::string output = f103_text(decoder);
    const bool full = output.find(name) != std::string::npos;
    f103_require(threw || decoder.mErrorOccurred || full,
        "F-103 R1: a 600-byte cbuffer line must decompile completely or fail controlled");
#else
    throw std::runtime_error("F-103 R1 requires DX11_AUDIT_ASAN_DECOMPILER=ON");
#endif
}

void test_dx11_decompiler_f103_red_r2()
{
    f103_check_struct_name_case("F-103 RED R2 ParseBufferDefinitions (DecompileHLSL.cpp:1288)", 400);
}

void test_dx11_decompiler_f103_red_r3()
{
#ifdef DX11_AUDIT_ASAN_DECOMPILER
    DecompilerSettings settings;
    settings.ZRepair_PositionTexture = "F103PosTex";
    settings.ZRepair_WorldPosCalc = "%s";
    Decompiler decoder;
    decoder.G = &settings;
    decoder.mErrorOccurred = false;
    decoder.mPatched = false;
    decoder.mShaderType = "ps_5_0";
    decoder.mTextureNames[0] = "F103PosTex";
    decoder.mCodeStartPos = 0;
    const std::string dest(600, 'p');
    const std::string text =
        "\nvoid main(\n  float4 p : SV_Position)\n{\n  " + dest +
        " = F103PosTex.Sample(s, uv);\n}\n";
    decoder.mOutput.assign(text.begin(), text.end());
    std::fprintf(stderr, "F-103 RED R3 WritePatches position texture (DecompileHLSL.cpp:3382): 600-byte register token\n");
    std::fflush(stderr);
    bool threw = false;
    try {
        decoder.WritePatches();
    } catch (...) {
        threw = true;
    }
    const std::string output = f103_text(decoder);
    const bool full = output.find(dest) != std::string::npos &&
        output.find("float3 worldPos") != std::string::npos;
    f103_require(threw || decoder.mErrorOccurred || full,
        "F-103 R3: a 600-byte position-texture target must decompile completely or fail controlled");
    f103_require(!((threw || decoder.mErrorOccurred) && output.find("float3 worldPos") != std::string::npos),
        "F-103 R3: a controlled failure must not publish a partial worldPos patch");
#else
    throw std::runtime_error("F-103 R3 requires DX11_AUDIT_ASAN_DECOMPILER=ON");
#endif
}

void test_dx11_decompiler_f103_red_r4()
{
    f103_check_object_pos_case("F-103 RED R4 control", false, std::string(32, 'a'), false);
    f103_check_object_pos_case("F-103 RED R4 ObjectPos_ID1 (DecompileHLSL.cpp:3534)",
        false, std::string(600, 'b'), true);
}

void test_dx11_decompiler_f103_red_r5()
{
    f103_check_object_pos_case("F-103 RED R5 control", true, std::string(32, 'c'), false);
    f103_check_object_pos_case("F-103 RED R5 ObjectPos_ID2 (DecompileHLSL.cpp:3589)",
        true, std::string(600, 'd'), true);
}

void test_dx11_decompiler_f103_red_m1()
{
#ifdef DX11_AUDIT_ASAN_DECOMPILER
    DecompilerSettings settings;
    Decompiler decoder;
    decoder.G = &settings;
    decoder.mErrorOccurred = false;
    decoder.mCBufferNames["cb0"] = 0;
    const std::string name(255, 'm');
    const std::string line = "//   float4 " + name + ";// Offset:    0 Size:   16";
    const std::string text = "// cbuffer cb0\n// {\n" + line + "\n// }\n";
    std::fprintf(stderr, "F-103 RED M1 old 1178 = ParseBufferDefinitions:1238: legal 255-byte member name\n");
    std::fflush(stderr);
    bool threw = false;
    try {
        decoder.ParseBufferDefinitions(nullptr, text.c_str(), text.size());
    } catch (...) {
        threw = true;
    }
    const std::string output = f103_text(decoder);
    f103_require(!threw && !decoder.mErrorOccurred,
        "F-103 M1: a legal 255-byte cbuffer member name must not fail");
    f103_require(output.find(name) != std::string::npos,
        "F-103 M1: a legal 255-byte member name must be emitted complete");
#else
    throw std::runtime_error("F-103 M1 requires DX11_AUDIT_ASAN_DECOMPILER=ON");
#endif
}

void test_dx11_decompiler_f103_red_m2()
{
    f103_check_struct_name_case("F-103 RED M2 old 1228 = ParseBufferDefinitions:1288", 256);
}

void test_dx11_decompiler_f103_red_m3()
{
    f103_check_object_pos_case("F-103 RED M3 control", false, std::string(32, 'e'), false);
    f103_check_object_pos_case("F-103 RED M3 old 3328 = ObjectPos_ID1:3535 (128-byte NUL)",
        false, std::string(128, 'e'), true);
}
