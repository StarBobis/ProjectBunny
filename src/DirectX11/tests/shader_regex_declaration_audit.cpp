#include "slot_range_test_support.h"
#include "ShaderRegex.h"
#include "D3D_Shaders/stdafx.h"

bool probe_dx11_shader_regex_declaration_anchor(std::string*, size_t*);

namespace {
using namespace dx11_slot_test;

void require_native_regex_shader(BatchDevice& device, const std::string& text,
    const std::vector<unsigned char>& container)
{
    std::vector<char> assembly(text.begin(), text.end());
    const auto bytecode = assembler(&assembly, container);
    ComPtr<ID3D11PixelShader> shader;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, &shader)),
        "F296: native WARP accepts the actual assembled shader");
}

void compile_regex_ret_match(ShaderRegexGroup& group, bool replace = false)
{
    auto& pattern = group.patterns[L"ret"];
    std::string expression = "^ret[ \\t]*(?:\\r?\\n|$)";
    require_batch(pattern.compile(&expression), "F296: actual PCRE2 shader pattern compiles");
    pattern.do_replace = replace;
    if (replace) {
        pattern.replace = "mov $scratch.x, l(1)\\nret\\n";
        group.temp_regs.insert("scratch");
    }
}

void require_native_level9_shader(BatchDevice& device, const std::vector<unsigned char>& bytecode, bool vertex)
{
    if (vertex) {
        ComPtr<ID3D11VertexShader> shader;
        require_batch(SUCCEEDED(device.native->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr, &shader)),
            "F296v2: native WARP accepts the actual Level9 vertex shader");
    } else {
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, &shader)),
            "F296v2: native WARP accepts the actual Level9 pixel shader");
    }
}

void test_shader_regex_level9_declarations()
{
    BatchDevice device;
    bool all_paths = true;
    for (const auto* profile : {"vs_4_0_level_9_1", "vs_4_0_level_9_3", "ps_4_0_level_9_1", "ps_4_0_level_9_3"}) {
        const bool vertex = profile[0] == 'v';
        const char* source = vertex ? "float4 main(float4 p:POSITION):SV_Position{return p;}" :
            "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
        ComPtr<ID3DBlob> code, errors;
        require_batch(SUCCEEDED(D3DCompile(source,std::strlen(source),"F296-Level9",nullptr,nullptr,"main",profile,0,0,&code,&errors)),
            "F296v2: SDK compiles each actual VS/PS Level9 9_1/9_3 profile");
        const auto* data = static_cast<const unsigned char*>(code->GetBufferPointer());
        std::vector<unsigned char> binary(data,data+code->GetBufferSize()), disassembly;
        require_native_level9_shader(device,binary,vertex);
        require_batch(SUCCEEDED(disassembler(&binary,&disassembly,nullptr)), "F296v2: actual production Level9 disassembly");
        std::string original(disassembly.begin(),disassembly.end());
        std::vector<char> roundtrip_text(original.begin(),original.end());
        const auto roundtrip = assembler(&roundtrip_text,binary);
        require_batch(roundtrip == binary, "F296v2: unchanged Level9 production assembly round-trips every byte");
        require_native_level9_shader(device,roundtrip,vertex);
        size_t anchor = std::string::npos;
        const bool located = probe_dx11_shader_regex_declaration_anchor(&original,&anchor);
        std::fprintf(stderr,"F296v2 %s production locator: found=%d anchor=%zu\n",profile,located,anchor);
        all_paths &= located;

        auto declared = original;
        ShaderRegexGroup declarations; compile_regex_ret_match(declarations);
        declarations.declarations.push_back("dcl_temps 1");
        bool match = false, patch = false;
        declarations.apply_regex_patterns(&declared,&match,&patch);
        std::fprintf(stderr,"F296v2 %s declarations: match=%d patch=%d\n",profile,match,patch);
        all_paths &= match && patch;
        if (match && patch) {
            std::vector<char> text(declared.begin(),declared.end());
            require_native_level9_shader(device,assembler(&text,binary),vertex);
        } else {
            require_batch(declared == original, "F296v2: a rejected declaration locator leaves assembly unchanged");
        }
        auto temporary = original;
        ShaderRegexGroup temps; compile_regex_ret_match(temps,true);
        temps.apply_regex_patterns(&temporary,&match,&patch);
        std::fprintf(stderr,"F296v2 %s first temporary: match=%d patch=%d\n",profile,match,patch);
        all_paths &= match && patch;
        if (match && patch) {
            std::vector<char> text(temporary.begin(),temporary.end());
            require_native_level9_shader(device,assembler(&text,binary),vertex);
        }
    }
    require_batch(all_paths, "F296v2: every native Level9 shader reaches the DXBC model and both real insertion paths");

    // Locator-only token-stream controls; these do not claim a complete native HS fixture.
    for (const auto* control : {" \tps_5_0 \t// ordinary model\n  dcl_temps 1\nret\n",
            "hs_5_0\nhs_decls\ndcl_output_control_point_count 3\nhs_control_point_phase\n"
            "dcl_temps 1\nret\nhs_fork_phase\ndcl_hs_fork_phase_instance_count 1\nret\n"}) {
        std::string text = control;
        const auto last = text.find("dcl_hs_fork_phase_instance_count 1");
        const auto expected = last == std::string::npos ? text.find("dcl_temps 1") + std::strlen("dcl_temps 1") :
            last + std::strlen("dcl_hs_fork_phase_instance_count 1");
        size_t anchor = std::string::npos;
        require_batch(probe_dx11_shader_regex_declaration_anchor(&text,&anchor) && anchor == expected && text == control,
            "F296v2: ordinary indentation and last-declaration policy across later HS phases remain unchanged");
    }
}
}

void test_dx11_shader_regex_declaration_regression()
{
    using namespace dx11_slot_test;
    test_shader_regex_level9_declarations();
    const char source[] = "void main() {}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr,
        "main", "ps_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors)),
        "F296: native compiler creates a legal no-output pixel shader");
    const auto* data = static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> container(data, data + code->GetBufferSize());
    // This compiler emits refactoringAllowed even for an empty entry point.
    // Assemble the legal declaration-free instruction stream into its real
    // signature/container, then let native WARP and D3DDisassemble validate it.
    const std::string minimal = "ps_5_0\nret\n";
    std::vector<char> assembly(minimal.begin(), minimal.end());
    container = assembler(&assembly, container);
    BatchDevice device;
    ComPtr<ID3D11PixelShader> native;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(container.data(), container.size(), nullptr, &native)),
        "F296: native WARP validates the actual declaration-free no-output DXBC");
    ComPtr<ID3DBlob> disassembly;
    require_batch(SUCCEEDED(D3DDisassemble(container.data(), container.size(), 0, nullptr, &disassembly)),
        "F296: native disassembly supplies real leading comments around the model");
    const std::string native_text(static_cast<const char*>(disassembly->GetBufferPointer()));
    std::fprintf(stderr, "F296 native fixture:\n%s\n", native_text.c_str());
    require_batch(native_text.find("dcl_") == std::string::npos && native_text.find("ps_5_0") != std::string::npos,
        "F296: the real compiled fixture has a model and no declarations");
    for (const auto& original : {native_text, std::string("ps_5_0\nret\n"),
            std::string("// header\n\nps_5_0\n// before code\nret\n"),
            std::string("// header\r\n\t\r\nps_5_0 \t// model\r\n// before code\r\nret\r\n")}) {
        require_native_regex_shader(device, original, container);
        auto text = original;
        ShaderRegexGroup declarations;
        compile_regex_ret_match(declarations);
        declarations.declarations.push_back("dcl_temps 1");
        bool match = false, patch = false;
        declarations.apply_regex_patterns(&text, &match, &patch);
        require_batch(match && patch, "F296: actual ShaderRegex declarations path publishes the insertion");
        std::fprintf(stderr, "F296 declarations result:\n%s\n", text.c_str());
        require_native_regex_shader(device, text, container);
        const auto inserted = text;
        declarations.apply_regex_patterns(&text, &match, &patch);
        require_batch(match && !patch && text == inserted,
            "F296: a repeated declaration insertion remains unchanged");

        text = original;
        ShaderRegexGroup temporary;
        compile_regex_ret_match(temporary, true);
        temporary.apply_regex_patterns(&text, &match, &patch);
        require_batch(match && patch, "F296: the real regex replacement allocates a first temporary declaration");
        require_native_regex_shader(device, text, container);
    }

    std::string declared = "// header\nps_5_0\ndcl_globalFlags refactoringAllowed\n"
        "// between declarations\n\tdcl_temps 1 \t// existing declaration\nret\n// dcl_temps 99\n";
    require_native_regex_shader(device, declared, container);
    const auto old_declarations = declared;
    ShaderRegexGroup group;
    compile_regex_ret_match(group);
    group.declarations.push_back("dcl_indexableTemp x0[1], 4");
    bool match = false, patch = false;
    group.apply_regex_patterns(&declared, &match, &patch);
    require_batch(match && patch, "F296: existing declaration block accepts an additional declaration");
    auto restored = declared;
    const std::string addition = "\ndcl_indexableTemp x0[1], 4";
    const auto inserted_at = restored.find(addition);
    require_batch(inserted_at != std::string::npos, "F296: actual declaration insertion is present");
    restored.erase(inserted_at, addition.size());
    require_batch(restored == old_declarations && inserted_at > declared.find("dcl_temps 1") && inserted_at < declared.find("\nret"),
        "F296: preserve every existing byte and append after the last real declaration before code");
    require_native_regex_shader(device, declared, container);

    for (const auto* invalid : {"// no model\nret\n", "// comments only\n", "ps_5_0",
            "ps_5_0_bad\nret\n", "ps_5_0\ndcl_temps 1", "ps_6_0\nret\n", "ps_2_x\nret\n",
            "// Level9 shader bytecode:\n    ps_2_x\n    mov oC0, c0\n",
            "// Level9 shader bytecode:\n    vs_2_0\n    mov oPos, v0\n",
            "// Level9 shader bytecode:\n    ps_2_y\nps_4_0\nret\n",
            "// Level9 shader bytecode:\n    vs_2_x\nps_4_0\nret\n"}) {
        ShaderRegexGroup failure;
        failure.declarations.push_back("dcl_temps 2");
        std::string text = invalid;
        size_t anchor = std::string::npos;
        require_batch(!probe_dx11_shader_regex_declaration_anchor(&text,&anchor) && anchor == std::string::npos && text == invalid,
            "F296v2: malformed compatibility/missing-main text is rejected by the actual locator without mutation");
        failure.apply_regex_patterns(&text, &match, &patch);
        require_batch(match && !patch && text == invalid,
            "F296: no complete model/declaration anchor returns failure without modifying text");
    }
}
