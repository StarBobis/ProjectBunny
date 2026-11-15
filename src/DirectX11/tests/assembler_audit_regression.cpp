// The regression target replaces its Assembler.cpp object with this TU, so
// these tests call the real private operand parser without copying its logic.
#include "../../D3D_Shaders/Assembler.cpp"
#include <stdexcept>

namespace {
void check_assembler_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct IndexAddends { unsigned first, second; };
struct InterpolationCase { const char* text; unsigned mode; };
class GuardedAssemblySpan {
    void* allocation=nullptr;
    const char* view=nullptr;
public:
    explicit GuardedAssemblySpan(const std::string& text)
    {
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        check_assembler_audit(text.size()<=system.dwPageSize,"F146: guarded text fits the first page");
        allocation=VirtualAlloc(nullptr,2*system.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        check_assembler_audit(allocation!=nullptr,"F146: allocate guarded input");
        auto* boundary=static_cast<char*>(allocation)+system.dwPageSize;
        DWORD previous=0;
        if(!VirtualProtect(boundary,system.dwPageSize,PAGE_NOACCESS,&previous)) {
            VirtualFree(allocation,0,MEM_RELEASE); allocation=nullptr;
            throw std::runtime_error("F146: protect the byte immediately after the valid span");
        }
        view=boundary-text.size();
        if(!text.empty()) std::memcpy(const_cast<char*>(view),text.data(),text.size());
    }
    ~GuardedAssemblySpan() {if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
    const char* data() const {return view;}
};
}

void test_dx11_assembler_line_bounds_regression()
{
    const std::pair<std::string,std::vector<std::string>> cases[] = {
        {"ps_5_0",{"ps_5_0"}}, {"ps_5_0\nret",{"ps_5_0","ret"}}, {"ps_5_0\nret\n",{"ps_5_0","ret"}},
        {"",{}}, {"\n\n",{"",""}}, {"\r\n   \r\nret   ",{"","","ret"}},
        {std::string("\0ignored",8),{}}, {std::string("x\0y\nz",5),{std::string("x\0y",3),"z"}}};
    for(const auto& entry:cases) {
        GuardedAssemblySpan input(entry.first);
        std::fprintf(stderr,"F146: split %zu valid bytes directly before an inaccessible page\n",entry.first.size()); std::fflush(stderr);
        check_assembler_audit(stringToLines(input.data(),entry.first.size())==entry.second,
            "F146: bounded splitting preserves line, CR, space and embedded-NUL behavior");
    }
    check_assembler_audit(stringToLines(nullptr,0).empty(),"F146: an empty span does not require a dereferenceable pointer");
}

void test_dx11_assembler_word_progress_regression()
{
    for (const auto* malformed : {"mov r0.x, l(1.0", "add r0.xyzw, r1.xyzw, l(1.0, 2.0, 3.0, 4.0",
            "dmov r0.xyzw, d(1.0l, 2.0l", "printf \"unterminated", "errorf \""}) {
        bool rejected=false;
        std::fprintf(stderr,"F142: tokenization must terminate and reject %s\n",malformed); std::fflush(stderr);
        try { (void)strToWords(malformed); } catch(const AssemblerParseError&) {rejected=true;}
        check_assembler_audit(rejected,"F142: missing operand delimiters use the normal parse-error channel");
        rejected=false;
        try { (void)assembleIns(malformed); } catch(const AssemblerParseError&) {rejected=true;}
        check_assembler_audit(rejected,"F142: actual instruction assembly propagates missing-delimiter failures");
    }
    for (const auto* text : {"mov r0.x, r1.x", "mov r0.x, l(1.0)",
            "add r0.xyzw, r1.xyzw, l(1.0, 2.0, 3.0, 4.0)",
            "printf \"a message with \"inner\" quotes\", r0.x", "printf \"\"", "dmov r0.xyzw, d(1.0l, 2.0l)"}) {
        const auto words=strToWords(text);
        check_assembler_audit(!words.empty() && !assembleIns(text).empty(),"F142: preserve ordinary final words, closed literals and right-most quote matching");
    }
    check_assembler_audit(strToWords("mov r0.x, r1.x")==std::vector<std::string>{"mov","r0.x","r1.x"},
        "F142: the previously disputed ordinary final-word case retains its correct tokenization");
}

void test_dx11_precision_delimiter_review()
{
    const auto original=assembleIns("dcl_immediateConstantBuffer { { 1.0, 2.0, 3.0, 5.0} }");
    bool local_exception=false;
    try { (void)assembleAndCompare("dcl_immediateConstantBuffer { { 1.0, 2.0, 3.0, 4.0 missing",original); }
    catch(const std::out_of_range&) {local_exception=true;}
    check_assembler_audit(local_exception, "F145 review: malformed synthetic ICB text exposes a local delimiter exception");
    std::fputs("F145 observation: local missing-brace exception confirmed; actual native producer reachability remains unproved\n",stderr);
    for (const auto* text : {"mov r0.x, l(1.0)", "add r0.xyzw, l(1.0, 2.0, 3.0, 4.0), l(5.0, 6.0, 7.0, 8.0)",
            "dmov r0.xyzw, d(1.0l, 2.0l)", "dcl_immediateConstantBuffer { { 1.0, 2.0, 3.0, 4.0} }"}) {
        const auto words=assembleIns(text);
        check_assembler_audit(assembleAndCompare(text,words)==text, "F145 review: preserve supported canonical literal formats");
    }
    for (const auto& pair : {std::pair<const char*,const char*>{"  mov r0.x, l(1.0)","mov r0.x, l(2.0)"},
            {"mov r0.xyzw, l(1.0, 2.0, 3.0, 4.0)","mov r0.xyzw, l(2.0, 4.0, 6.0, 8.0)"},
            {"dmov r0.xyzw, d(1.0l, 2.0l)","dmov r0.xyzw, d(3.0l, 4.0l)"}}) {
        const auto bits=assembleIns(pair.second);
        const auto corrected=assembleAndCompare(pair.first,bits);
        check_assembler_audit(assembleIns(corrected)==bits, "F145 review: scalar/vector/double precision repair preserves exact binary words");
    }
}

void test_dx11_assembler_declaration_regression()
{
    const std::pair<const char*, DWORD> types[] = {
        {"(float,float,float,float)",0x5555}, {"(uint,uint,uint,uint)",0x4444},
        {"(sint,sint,sint,sint)",0x3333}, {"(snorm,snorm,snorm,snorm)",0x2222},
        {"(unorm,unorm,unorm,unorm)",0x1111}, {"(double,<continued>,<unused>,<unused>)",0x9987},
        {"(double,<continued>,double,<continued>)",0x8787}, {"(mixed,mixed,mixed,mixed)",0x6666}};
    for (const auto& entry : types) {
        std::string text = entry.first; std::vector<DWORD> words{0x12345678};
        assembleResourceDeclarationType(&text,&words);
        check_assembler_audit(words == std::vector<DWORD>{0x12345678,entry.second}, "F143: preserve every supported resource return-type token");
    }
    const char* values[] = {"position","clip_distance","cull_distance","rendertarget_array_index","viewport_array_index",
        "vertex_id","primitive_id","instance_id","is_front_face","sampleIndex","finalQuadUeq0EdgeTessFactor",
        "finalQuadVeq0EdgeTessFactor","finalQuadUeq1EdgeTessFactor","finalQuadVeq1EdgeTessFactor","finalQuadUInsideTessFactor",
        "finalQuadVInsideTessFactor","finalTriUeq0EdgeTessFactor","finalTriVeq0EdgeTessFactor","finalTriWeq0EdgeTessFactor",
        "finalTriInsideTessFactor","finalLineDetailTessFactor","finalLineDensityTessFactor"};
    for (size_t i=0; i<ARRAYSIZE(values); ++i) {
        std::string text=values[i]; std::vector<DWORD> words;
        assembleSystemValue(&text,&words);
        check_assembler_audit(words == std::vector<DWORD>{static_cast<DWORD>(i+1)}, "F143: preserve all 22 system-value encodings");
    }
    for (const bool resource : {true,false}) {
        for (const char* invalid : {"", "unknown", "(float,uint,sint,double)"}) {
            std::string text=invalid; std::vector<DWORD> words{0x12345678}; bool rejected=false;
            std::fputs("F143: unsupported declaration must report an actual parser error\n",stderr); std::fflush(stderr);
            try { if(resource) assembleResourceDeclarationType(&text,&words); else assembleSystemValue(&text,&words); }
            catch(const AssemblerParseError&) {rejected=true;}
            check_assembler_audit(rejected && words == std::vector<DWORD>{0x12345678}, "F143: unknown declaration cannot silently omit a required token");
        }
    }
    for (const auto* text : {"dcl_resource_texture2d (unknown,unknown,unknown,unknown) t0", "dcl_input_sgv v0.x, unknown"}) {
        bool rejected=false;
        try { assembleIns(text); } catch(const AssemblerParseError&) {rejected=true;}
        check_assembler_audit(rejected, "F143: complete instruction parser propagates unsupported declaration errors");
    }
}

void test_dx11_assembler_interpolation_regression()
{
    const InterpolationCase valid[] = {
        {"dcl_input_ps v0.xy", 0}, {"dcl_input_ps constant v0.x", 1},
        {"dcl_input_ps linear v0.xy", 2}, {"dcl_input_ps linear centroid v0.xy", 3},
        {"dcl_input_ps linear noperspective v0.xy", 4}, {"dcl_input_ps linear noperspective centroid v0.xy", 5},
        {"dcl_input_ps linear sample v0.xy", 6}, {"dcl_input_ps linear noperspective sample v0.xy", 7},
        {"dcl_input_ps_sgv v6.x, is_front_face", 1},
        {"dcl_input_ps_sgv constant v6.x, is_front_face", 1},
        {"dcl_input_ps_siv v0.xy, position", 0},
        {"dcl_input_ps_siv linear noperspective sample v0.xy, position", 7}};
    for (const auto& test : valid) {
        const auto words = assembleIns(test.text);
        check_assembler_audit(!words.empty(), "F144: valid declaration emits actual instruction tokens");
        shader_ins instruction{}; instruction.op = words[0];
        check_assembler_audit(instruction._11_23 == test.mode && instruction.length == words.size(),
            "F144: valid/legacy interpolation modes and token lengths are preserved");
    }
    for (const char* invalid : {"dcl_input_ps linear", "dcl_input_ps", "dcl_input_ps constant",
            "dcl_input_ps linear noperspective", "dcl_input_ps linear noperspective centroid",
            "dcl_input_ps linear centroid", "dcl_input_ps linear sample",
            "dcl_input_ps_sgv constant is_front_face", "dcl_input_ps_siv linear noperspective sample position"}) {
        bool rejected = false;
        std::fprintf(stderr, "F144: reject incomplete declaration: %s\n", invalid); std::fflush(stderr);
        try { assembleIns(invalid); }
        catch (const AssemblerParseError&) { rejected = true; }
        check_assembler_audit(rejected, "F144: missing input/system-value operands use the real parse-error channel");
    }
}

void test_dx11_assembler_double_index_regression()
{
    for (const IndexAddends pair : {IndexAddends{3,7}, {0,7}, {3,0}, {0,0}, {15,2}}) {
        const auto text = "v[r0.x + " + std::to_string(pair.first) + "][r1.y + " + std::to_string(pair.second) + "].xyzw";
        std::fprintf(stderr, "F140: encode independent index addends %u/%u\n", pair.first, pair.second); std::fflush(stderr);
        const auto words = assembleOp(text);
        check_assembler_audit(words.size() == 5u + bool(pair.first) + bool(pair.second), "F140: exact operand length reflects each independent addend");
        token_operand operand{}; operand.op = words[0];
        check_assembler_audit(operand.file == 1 && operand.num_indices == 2 &&
            operand.index0_repr == (pair.first ? 3u : 2u) && operand.index1_repr == (pair.second ? 3u : 2u),
            "F140: both index representation fields independently describe immediate-plus-relative or relative-only");
        size_t cursor = 1;
        if (pair.first) check_assembler_audit(words[cursor++] == pair.first, "F140: first immediate matches the first source addend");
        token_operand first{}; first.op = words[cursor++];
        check_assembler_audit(first.file == 0 && first.num_indices == 1 && words[cursor++] == 0, "F140: first index still references r0");
        if (pair.second) check_assembler_audit(words[cursor++] == pair.second, "F140: second immediate matches the second source addend");
        token_operand second{}; second.op = words[cursor++];
        check_assembler_audit(second.file == 0 && second.num_indices == 1 && words[cursor++] == 1 && cursor == words.size(),
            "F140: second index still references r1 and consumes exactly its tokens");
    }
    const auto fixed = assembleOp("v[1][2].xyzw");
    check_assembler_audit(fixed.size() == 3 && fixed[1] == 1 && fixed[2] == 2, "F140: fixed-index encoding remains unchanged");
}
