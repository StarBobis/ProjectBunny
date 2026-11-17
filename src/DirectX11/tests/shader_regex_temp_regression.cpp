#include "slot_range_test_support.h"
#include "ShaderRegex.h"
#include "D3D_Shaders/stdafx.h"
#include <array>
#include <cstdio>

std::array<unsigned char,4> render_dx11_audit_constant_pixel(dx11_slot_test::BatchDevice&, ID3D11PixelShader*);

namespace {
using namespace dx11_slot_test;

const char temp_replacement[] =
    "mov $scratch.xyzw, l(0.125, 0.125, 0.125, 0.125)\nadd o0.xyzw, ${value}, $scratch.xyzw";

class RegexTempScope {
    ShaderRegexGroups previous_groups = std::move(shader_regex_groups);
    std::vector<ShaderRegexGroup*> previous_index = std::move(shader_regex_group_index);
    bool previous_cache = G->CACHE_SHADERS, previous_export = G->EXPORT_FIXED;
public:
    ShaderRegexGroup& group;
    RegexTempScope() : group(shader_regex_groups[L"ShaderRegexF297"])
    {
        G->CACHE_SHADERS = G->EXPORT_FIXED = false;
        group.ini_section = L"ShaderRegexF297";
        group.shader_models.insert("ps_5_0");
        group.temp_regs.insert("scratch");
        shader_regex_group_index = {&group};
        auto& pattern = group.patterns[L"Pattern"];
        std::string expression = R"(^[ \t]*mov o0\.xyzw, (?<value>[^\r\n]+))";
        require_batch(pattern.compile(&expression), "F297: compile the actual PCRE2 replacement pattern");
        pattern.do_replace = true;
        pattern.replace = temp_replacement;
    }
    ~RegexTempScope()
    {
        shader_regex_groups = std::move(previous_groups);
        shader_regex_group_index = std::move(previous_index);
        G->CACHE_SHADERS = previous_cache; G->EXPORT_FIXED = previous_export;
    }
};

ComPtr<ID3DBlob> compile_temp_shader(const char* source)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT result = D3DCompile(source, std::strlen(source), "F297-native-reference", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &code, &errors);
    if (FAILED(result) && errors) std::fprintf(stderr, "F297 native compiler: %s\n", static_cast<const char*>(errors->GetBufferPointer()));
    require_batch(SUCCEEDED(result), "F297: compile a real native signature and pixel oracle");
    return code;
}

std::string temp_shader_text(const std::string& count, unsigned source_reg, bool use_reg)
{
    const std::string value = "l(0.25, 0.5, 0.75, 1.0)";
    const std::string reg = "r" + std::to_string(source_reg) + ".xyzw";
    return "ps_5_0\ndcl_globalFlags refactoringAllowed\ndcl_output o0.xyzw\ndcl_temps " + count + "\n" +
        (use_reg ? "mov " + reg + ", " + value + "\n" : "") +
        "mov o0.xyzw, " + (use_reg ? reg : value) + "\nret\n";
}

std::vector<unsigned char> assemble_temp_fixture(const std::string& text, const std::vector<unsigned char>& seed)
{
    std::vector<char> input(text.begin(), text.end());
    std::vector<AssemblerParseError> errors;
    auto code = assembler(&input, seed, &errors);
    require_batch(errors.empty() && !code.empty(), "F297: assemble a complete valid shader with its real native signature");
    return code;
}

ComPtr<ID3D11PixelShader> native_temp_shader(BatchDevice& device, const std::vector<unsigned char>& code)
{
    ComPtr<ID3D11PixelShader> shader;
    const HRESULT result = device.native->CreatePixelShader(code.data(), code.size(), nullptr, &shader);
    if (FAILED(result)) std::fprintf(stderr, "F297 native CreatePixelShader: 0x%08lx\n", result);
    require_batch(SUCCEEDED(result), "F297: WARP accepts the real large temporary declaration and register accesses");
    return shader;
}

void check_native_temp_count(const std::vector<unsigned char>& code, unsigned count)
{
    ComPtr<ID3DBlob> assembly;
    require_batch(SUCCEEDED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &assembly)),
        "F297: native disassembler validates the produced DXBC");
    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    require_batch(text.find("\ndcl_temps " + std::to_string(count) + "\n") != std::string::npos,
        "F297: native decoding preserves the complete temporary-register declaration");
}
}

void test_dx11_shader_regex_temp_count_regression()
{
    RegexTempScope configuration;
    BatchDevice device;
    auto seed_blob = compile_temp_shader("float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}");
    const auto* seed_begin = static_cast<const unsigned char*>(seed_blob->GetBufferPointer());
    const std::vector<unsigned char> seed(seed_begin, seed_begin + seed_blob->GetBufferSize());
    auto expected_blob = compile_temp_shader("float4 main():SV_Target{return float4(0.375,0.625,0.875,1.125);}");
    ComPtr<ID3D11PixelShader> expected_shader;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(expected_blob->GetBufferPointer(), expected_blob->GetBufferSize(), nullptr, &expected_shader)),
        "F297: native compiled pixel oracle");
    const auto expected = render_dx11_audit_constant_pixel(device, expected_shader.Get());

    for (unsigned count : {0u, 1u, 4095u}) {
        // r409 is live in the 4095 case: truncating ' 4095' to ' 409'
        // aliases the injected scratch register with original shader data.
        const auto original = assemble_temp_fixture(temp_shader_text(std::to_string(count), count == 4095 ? 409 : 0, count != 0), seed);
        auto original_shader = native_temp_shader(device, original);
        check_native_temp_count(original, count);
        std::string text = BinaryToAsmText(original.data(), original.size(), false);
        std::string model;
        require_batch(!text.empty() && get_shader_model_from_bytecode(original.data(), original.size(), &model),
            "F297: run the real bytecode-to-ShaderRegex input path");
        std::wstring tagline;
        require_batch(apply_shader_regex_groups(&text, L"ps", &model, count, &tagline),
            "F297: real global ShaderRegex application allocates its symbolic temporary");
        std::vector<char> input(text.begin(), text.end());
        std::vector<unsigned char> patched;
        std::vector<AssemblerParseError> errors;
        require_batch(SUCCEEDED(AssembleFluganWithSignatureParsing(&input, &patched, &errors)) && errors.empty(),
            "F297: use the production ShaderRegex assembly/signature pipeline");
        auto shader = native_temp_shader(device, patched);
        const auto actual = render_dx11_audit_constant_pixel(device, shader.Get());
        std::fprintf(stderr, "F297 count=%u -> %u: pixel=%u,%u,%u,%u expected=%u,%u,%u,%u\n", count, count+1,
            actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);
        require_batch(actual == expected, "F297: four-digit temp counts cannot alias existing live registers");
        check_native_temp_count(patched, count + 1);
    }

    const auto full = assemble_temp_fixture(temp_shader_text("4096", 4095, true), seed);
    auto full_shader = native_temp_shader(device, full);
    check_native_temp_count(full, 4096);
    auto& pattern = configuration.group.patterns.at(L"Pattern");
    pattern.do_replace = false;
    std::string full_text = BinaryToAsmText(full.data(), full.size(), false);
    const auto full_before = full_text;
    bool match = false, patch = true;
    configuration.group.apply_regex_patterns(&full_text, &match, &patch);
    require_batch(match && !patch && full_text == full_before, "F297: a legal 4096 declaration remains usable by a match-only pattern");
    pattern.do_replace = true;

    for (const auto* token : {"4096", "4097", "10000", "4294967296", "9999999999999999999999999", "-1", "+1", "1junk", "0x10", ""}) {
        std::string text = temp_shader_text(token, 0, false);
        const auto before = text;
        match = true; patch = true;
        configuration.group.apply_regex_patterns(&text, &match, &patch);
        require_batch(!match && !patch && text == before,
            "F297: invalid or exhausted counts reject before replacing code or publishing an out-of-range declaration");
    }
    configuration.group.temp_regs.insert("second");
    std::string overflow = temp_shader_text("4095", 409, true);
    const auto before = overflow;
    configuration.group.apply_regex_patterns(&overflow, &match, &patch);
    require_batch(!match && !patch && overflow == before, "F297: two new temporaries cannot exceed the SM5 total-register limit");
}
