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
