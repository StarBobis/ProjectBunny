#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "IniHandler.h"
#include "ShaderRegex.h"
#include "Input.h"
#include "Override.h"
#include "vkeys.h"
#include "HLSLDecompiler/DecompileHLSL.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include "D3D_Shaders/stdafx.h"

extern std::vector<CommandList*> pending_post_command_lists;
std::array<unsigned char,4> render_dx11_audit_constant_pixel(dx11_slot_test::BatchDevice&, ID3D11PixelShader*);

void test_dx11_dx9_constant_reset_regression();
void test_dx11_assembler_double_index_regression();
void test_dx11_assembler_interpolation_regression();
void test_dx11_signature_register_eof_regression();
void test_dx11_signature_feature_comment_regression();
void test_dx11_shader_replacement_ownership_regression();
void test_dx11_finish_failure_regression();
void test_dx11_shader_diagnostic_failure_regression();
void test_dx11_texture_initial_debug_regression();
void test_dx11_empty_hunting_present_regression();
void test_dx11_reload_shader_filename_regression();
void test_dx11_masked_flags_regression();
void test_dx11_crash_help_regression();
void test_dx11_ini_enumeration_regression();
void test_dx11_stencil_diagnostic_regression();
void test_dx11_view_reference_null_diagnostic_regression();
void test_dx11_ini_lookup_performance();
void test_dx11_file_publication_regression();
void test_dx11_device_initialization_regression();
void test_dx11_vertex_dump_bounds_regression();
void test_dx11_appended_vertex_layout_regression();
void test_dx11_decompiler_literal_regression();
void test_dx11_resource_filename_regression();
void test_dx11_pool_access_regression();
void test_dx11_decompile_file_diagnostic_regression();
void test_dx11_parameter_conversion_regression();
void test_dx11_assembler_declaration_regression();
void test_dx11_assembler_word_progress_regression();
void test_dx11_precision_delimiter_review();
void test_dx11_assembler_line_bounds_regression();
void test_dx11_shader_tool_read_regression();
void test_dx11_shader_tool_write_regression();
void test_dx11_automatic_draw_count_regression();
void test_dx11_shader_hash_miss_regression();
void test_dx11_shader_header_file_regression();
void test_dx11_resource_override_validation_regression();
void test_dx11_ini_upload_failure_regression();
void test_dx11_shader_model_bounds_regression();

#ifdef DX11_AUDIT_ASAN_LOCKS
void test_dx11_lock_cleanup_regression();
#endif

namespace {
using namespace dx11_slot_test;

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require_batch(input.good(), "audit: read actual output file");
    return std::string(std::istreambuf_iterator<char>(input), {});
}

class AuditDumpScope {
    std::wstring old_path = G->ANALYSIS_PATH;
    bool old_analysis = G->analyse_frame, old_registered = G->frame_analysis_registered;
    bool old_foreground = G->check_foreground_window;
    std::wstring old_title = G->additionalForegroundWindowTitle;
    int old_hunting = G->hunting;
    FrameAnalysisOptions old_current = G->cur_analyse_options, old_default = G->def_analyse_options;
public:
    std::filesystem::path path;
    AuditDumpScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "audit: locate test host");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool under_build = false;
        for (const auto& part : base) under_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(under_build, "audit: outputs remain under build");
        path = base / (L"audit-dump-" + std::to_wstring(GetCurrentProcessId()));
        require_batch(!std::filesystem::exists(path) && std::filesystem::create_directory(path), "audit: exclusive output directory");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        G->analyse_frame = G->frame_analysis_registered = true;
        G->check_foreground_window = true;
        G->additionalForegroundWindowTitle.clear();
        G->hunting = HUNTING_MODE_DISABLED;
        G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~AuditDumpScope()
    {
        G->analyse_frame = old_analysis; G->frame_analysis_registered = old_registered;
        G->check_foreground_window = old_foreground; G->additionalForegroundWindowTitle = std::move(old_title);
        G->hunting = old_hunting; G->cur_analyse_options = old_current; G->def_analyse_options = old_default;
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, old_path.c_str());
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    bool payload_contains(const char* expected)
    {
        for (const auto& file : std::filesystem::directory_iterator(path / L"deduped")) {
            if (file.path().extension() == L".txt" && read_file(file.path()).find(expected) != std::string::npos) return true;
        }
        return false;
    }
};

void test_f001()
{
    BatchDevice device;
    AuditDumpScope output;
    auto original = std::move(device.wrapper);
    auto analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
    analysis->SetHackerDevice(device.device);
    auto* context = analysis.get();
    device.wrapper = std::move(analysis);
    device.device->SetHackerContext(context);
    device.state.mHackerContext = context;
    try {
        auto vertices = buffer(device, 16, D3D11_BIND_VERTEX_BUFFER);
        const float values[] = {1, 2, 3, 4};
        device.context->UpdateSubresource(vertices.Get(), 0, nullptr, values, 0, 0);
        const auto options = static_cast<FrameAnalysisOptions>(static_cast<unsigned>(FrameAnalysisOptions::DUMP_VB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) | static_cast<unsigned>(FrameAnalysisOptions::MONO));
        std::fputs("F001: non-draw VB dump with null call_info\n", stderr); std::fflush(stderr);
        context->FrameAnalysisDump(vertices.Get(), options, L"audit-nondraw", DXGI_FORMAT_UNKNOWN, 4, 0);
        require_batch(output.payload_contains("stride: 4\r\n"), "F001: non-draw path writes the real VB header and payload");
        require_batch(!output.payload_contains("first instance:"), "F001: absent draw info does not invent instance metadata");

        ID3D11Buffer* bound = vertices.Get(); UINT stride = 4, offset = 0;
        context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->FrameAnalysisTrigger(options); context->Draw(4, 0);
        context->FrameAnalysisTrigger(options); context->DrawInstanced(4, 2, 0, 3);
        require_batch(output.payload_contains("first instance: 3\r\ninstance count: 2\r\n"),
            "F001: valid instanced draws retain both metadata values");
        context->FrameAnalysisDump(vertices.Get(), options, L"audit-zero-stride", DXGI_FORMAT_UNKNOWN, 0, 0);
        require_batch(output.payload_contains("stride: 0\r\n"), "F001: zero stride reaches its existing safe rejection");
    } catch (...) {
        device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get());
        device.state.mHackerContext = device.wrapper.get();
        throw;
    }
    device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get());
    device.state.mHackerContext = device.wrapper.get();
}

void test_f106()
{
    const char source[] = "float4 main():SV_Target{return float4(1,2,3,4);}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
        &code, &errors)), "F106: compile valid original DXBC using the real compiler");
    const auto* bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> original(bytes, bytes + code->GetBufferSize());
    std::vector<unsigned char> assembly;
    require_batch(SUCCEEDED(disassembler(&original, &assembly, nullptr)), "F106: disassemble real valid shader");
    std::vector<char> text(assembly.begin(), assembly.end());
    const auto rebuilt = assembler(&text, original);
    BatchDevice device;
    ComPtr<ID3D11PixelShader> shader;
    require_batch(!rebuilt.empty() && SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(), rebuilt.size(), nullptr, &shader)),
        "F106: valid round-trip bytecode remains accepted by WARP");
    for (const char* invalid : {"\n", "// no shader model\n", "", "ret\n", "ret\nret\nret\n", "unknown\nret\nret\n"}) {
        for (bool collect : {false, true}) {
            std::vector<char> input(invalid, invalid + std::strlen(invalid));
            std::vector<AssemblerParseError> warnings;
            bool rejected = false;
            std::fputs("F106: reject missing model before writing the token length\n", stderr); std::fflush(stderr);
            try { assembler(&input, original, collect ? &warnings : nullptr); }
            catch (const AssemblerParseError&) { rejected = true; }
            require_batch(rejected, "F106: missing model is a fatal parse error with or without warning collection");
        }
    }
    ComPtr<ID3DBlob> native_assembly;
    require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&native_assembly)),
        "F106 correction: actual native assembly for warning-collector recovery");
    const std::string prefixed=std::string("ret\n")+static_cast<const char*>(native_assembly->GetBufferPointer());
    std::vector<char> recover_input(prefixed.begin(),prefixed.end()); std::vector<AssemblerParseError> recover_warnings;
    const auto recovered=assembler(&recover_input,original,&recover_warnings);
    shader.Reset();
    require_batch(recover_warnings.size()==1 && recover_warnings.front().line_no==1 &&
        SUCCEEDED(device.native->CreatePixelShader(recovered.data(),recovered.size(),nullptr,&shader)),
        "F106 correction: a rejected prefix does not prevent a later valid model from recovering under the existing collector policy");
    wchar_t executable[MAX_PATH]{};
    require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "F106: locate owned shader-tool fixture");
    const auto fixture = std::filesystem::path(executable).parent_path() / L"shader-fixture";
    require_batch(std::filesystem::create_directories(fixture / L"ShaderCache"), "F106: exclusive shader-tool fixture");
    const std::pair<std::filesystem::path, const std::vector<unsigned char>*> outputs[] = {
        {fixture / L"ShaderCache/0123456789abcdef-ps.bin", &original}, {fixture / L"expected.cbo", &rebuilt}};
    for (const auto& item : outputs) {
        std::ofstream output(item.first, std::ios::binary);
        output.write(reinterpret_cast<const char*>(item.second->data()), item.second->size());
        output.close();
        require_batch(output.good(), "F106: persist exact native-validated shader bytes for CLI integration");
    }
}

class AuditConfigScope {
    HINSTANCE old_module = migoto_handle;
    LogVerbosity old_log_level = gLogVerbosity;
    bool old_debug = gLogDebug, old_warnings = G->gShowWarnings, old_initialized = G->gConfigInitialized;
    std::wstring old_user = G->user_config;
    int old_dirty = G->user_config_dirty;
    bool old_automatic = G->clear_unknown_settings, old_clear = G->clear_saved_persist_vars;
    int old_hunting = G->hunting;
    std::wstring old_shader_path = G->SHADER_PATH, old_shader_cache = G->SHADER_CACHE_PATH;
public:
    std::filesystem::path config, settings;
    AuditConfigScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "audit config: locate host");
        const auto base = std::filesystem::path(executable).parent_path();
        bool under_build = false;
        for (const auto& part : base) under_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(under_build, "audit config: owned directory remains under build");
        config = base / L"d3dx.ini";
        settings = base / (L"audit-user-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require_batch(file != INVALID_HANDLE_VALUE, "audit config: do not overwrite another fixture");
        CloseHandle(file);
        migoto_handle = nullptr;
    }
    ~AuditConfigScope()
    {
        ClearKeyBindings(); G->constants_command_list.clear(); G->post_constants_command_list.clear();
        registered_command_lists.clear(); explicitCommandListSections.clear();
        customResourcePools.clear(); customResources.clear(); persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = old_module; gLogVerbosity = old_log_level; gLogDebug = old_debug;
        G->gShowWarnings = old_warnings; G->gConfigInitialized = old_initialized;
        G->user_config = old_user; G->user_config_dirty = old_dirty;
        G->clear_unknown_settings = old_automatic; G->clear_saved_persist_vars = old_clear;
        G->hunting = old_hunting;
        wcscpy_s(G->SHADER_PATH, MAX_PATH, old_shader_path.c_str()); wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, old_shader_cache.c_str());
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants, int hunting = 0, bool eager = false, const char* include_lines = "")
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n";
        if (eager) output << "config_initialization_delay=-1\n";
        output << "[Hunting]\nhunting=" << hunting << "\n[Include]\nuser_config=" << settings.filename().string() << "\n" << include_lines << "[Constants]\n" << constants;
        output.close(); require_batch(output.good(), "audit config: write complete production INI");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};

void test_f114()
{
    AuditConfigScope config;
    std::fputs("F114: load all-flag Constants declarations without a variable name\n", stderr); std::fflush(stderr);
    config.load("global=1\nglobal persist=2\nGLOBAL LOCKED=3\nglobal persist\n"
        "global $audit_valid=7\nglobal persist $audit_persist=9\n");
    require_batch(command_list_globals.size() == 2 && command_list_globals.at(L"$audit_valid").fval == 7 &&
        command_list_globals.at(L"$audit_persist").fval == 9 && persistent_variables.size() == 1,
        "F114: reject missing names and continue parsing valid ordinary/persistent declarations");
    config.load("global $audit_valid=11\n");
    require_batch(command_list_globals.size() == 1 && command_list_globals.at(L"$audit_valid").fval == 11 &&
        persistent_variables.empty(), "F114: corrected config reload replaces declarations normally");
}

void test_f135()
{
    require_batch(ParseVKey(L"0x00") == 0 && ParseVKey(L"0xff") == 255 &&
        ParseVKey(L"0x000041") == 65 && ParseVKey(L"A") == 65 && ParseVKey(L"a") == 65 &&
        ParseVKey(L"VK_F1") == VK_F1 && ParseVKey(L"space") == VK_SPACE,
        "F135: valid hexadecimal, letter and named keys retain their values");
    for (const wchar_t* invalid : {L"0x100", L"0x", L"0xG", L"0x41junk", L"0x-1", L"0x10000000000000000"}) {
        std::fprintf(stderr, "F135: reject malformed or out-of-range hexadecimal VK\n"); std::fflush(stderr);
        require_batch(ParseVKey(invalid) == -1, "F135: reject missing digits, trailing garbage, negatives and overflow");
        bool rejected = false;
        try { VKInputButton button(invalid); }
        catch (const std::exception&) { rejected = true; }
        require_batch(rejected, "F135: the real input-button consumer rejects the invalid key");
    }
    VKInputButton valid(L"no_0x41");
}

#include "ini_parameter_audit.inl"
#include "key_cycle_audit.inl"

void test_f137()
{
    const auto saved = G->decompiler_settings;
    AuditConfigScope config;
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1=FirstDepth.w\nfix_ZRepair_DepthTexture2=SecondDepth.z\n");
    for (const auto* invalid : {"x", ".x", "ab", "Depth.q", "Depth.X", "Depth.R", "Depthx", "Depth.xy", ".w"}) {
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=") + invalid +
            "\nfix_ZRepair_DepthTexture2=" + invalid + "\n";
        std::fprintf(stderr, "F137: reject malformed texture selector %s before splitting\n", invalid); std::fflush(stderr);
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "FirstDepth" && G->decompiler_settings.ZRepair_DepthTextureReg1 == 'w' &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "SecondDepth" && G->decompiler_settings.ZRepair_DepthTextureReg2 == 'z',
            "F137: both invalid selectors are rejected without publishing partial settings");
    }
    for (const char component : {'x','y','z','w','r','g','b','a'}) {
        const auto shader_source=std::string("float4 main():SV_Target{float4 zpos4=float4(0.25,0.5,0.75,1);float zpos=zpos4.")+component+";return zpos;}";
        ComPtr<ID3DBlob> code,errors;
        require_batch(SUCCEEDED(D3DCompile(shader_source.data(),shader_source.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F137 correction: every accepted component is a native HLSL swizzle");
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=  SceneDepthTexture.") + component +
            " \t\nfix_ZRepair_DepthTexture2=OtherDepth." + component + "\n";
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "SceneDepthTexture" &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "OtherDepth" &&
            G->decompiler_settings.ZRepair_DepthTextureReg1 == component && G->decompiler_settings.ZRepair_DepthTextureReg2 == component,
            "F137: preserve exact texture names and all eight valid HLSL component suffixes");
    }
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1= \t\nfix_ZRepair_DepthTexture2=\n");
    require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "SceneDepthTexture" &&
        G->decompiler_settings.ZRepair_DepthTexture2 == "OtherDepth", "F137: empty or omitted values retain existing configuration semantics");
    G->decompiler_settings = saved;
}

void test_f138()
{
    test_dx11_parameter_conversion_regression();
    AuditConfigScope config;
    std::string overflow;
    for (int i = 0; i < 87; ++i) overflow += "\xe4\xb8\xad";
    std::string settings = "[Rendering]\n";
    for (const auto* key : {"fix_ZRepair_DepthTexture1", "fix_ZRepair_DepthTexture2", "fix_ZRepair_ZPosCalc1",
            "fix_ZRepair_Dependencies1", "fix_ZRepair_Dependencies2", "fix_InvTransform"})
        settings += std::string(key) + "=" + overflow + "\n";
    config.load(settings.c_str());
    require_batch(G->decompiler_settings.ZRepair_ZPosCalc1.empty() &&
        G->decompiler_settings.ZRepair_Dependencies1.empty() && G->decompiler_settings.ZRepair_Dependencies2.empty() &&
        G->decompiler_settings.InvTransforms.empty(), "F138: real INI loading rejects oversized UTF-8 parameter bytes");
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1=SceneDepthTexture.x\n"
        "fix_ZRepair_DepthTexture2=OtherDepth.y\nfix_ZRepair_ZPosCalc1= alpha beta \n"
        "fix_ZRepair_Dependencies1=a,b\nfix_ZRepair_Dependencies2=c,d\nfix_InvTransform=e,f\n");
    const auto& settings_ok = G->decompiler_settings;
    require_batch(settings_ok.ZRepair_DepthTexture1 == "SceneDepthTexture" && settings_ok.ZRepair_DepthTextureReg1 == 'x' &&
        settings_ok.ZRepair_DepthTexture2 == "OtherDepth" && settings_ok.ZRepair_DepthTextureReg2 == 'y' &&
        settings_ok.ZRepair_ZPosCalc1 == "alpha beta" && settings_ok.ZRepair_Dependencies1 == std::vector<std::string>{"a","b"} &&
        settings_ok.ZRepair_Dependencies2 == std::vector<std::string>{"c","d"} && settings_ok.InvTransforms == std::vector<std::string>{"e","f"},
        "F138: subsequent real configuration preserves normal depth selectors and all three lists");
}

void test_f143()
{
    test_dx11_assembler_declaration_regression();
    const char source[] = "Texture2D<float4> tex:register(t0);float4 main():SV_Target{return tex.Load(int3(0,0,0));}";
    ComPtr<ID3DBlob> code, errors, assembly;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) &&
        SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
        "F143: native compiler produces actual resource declaration fixture");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const std::string valid(static_cast<const char*>(assembly->GetBufferPointer()));
    std::vector<char> normal(valid.begin(),valid.end()); std::vector<AssemblerParseError> warnings;
    const auto rebuilt=assembler(&normal,original,&warnings);
    BatchDevice device; ComPtr<ID3D11PixelShader> shader;
    require_batch(warnings.empty() && SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
        "F143: native WARP accepts unchanged supported declaration round trip");
    const std::string token="(float,float,float,float)";
    const auto position=valid.find(token);
    require_batch(position!=std::string::npos, "F143: locate actual native resource type");
    std::string invalid=valid; invalid.replace(position,token.size(),"(invalid,invalid,invalid,invalid)");
    std::vector<char> bad(invalid.begin(),invalid.end()); bool rejected=false;
    try { assembler(&bad,original); } catch(const AssemblerParseError& error) {rejected=error.line_no>0;}
    require_batch(rejected, "F143: public assembly without warning collection reports a line-numbered parse error");
    warnings.clear();
    const auto with_warnings=assembler(&bad,original,&warnings);
    require_batch(warnings.size()==1 && warnings.front().line_no>0 && !with_warnings.empty(),
        "F143: preserve explicit warning-collection compatibility policy while reporting the rejected instruction");
}

void test_f142()
{
    test_dx11_assembler_word_progress_regression();
    const char source[]="float4 main():SV_Target{return 1;}";
    ComPtr<ID3DBlob> code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F142: actual native DXBC for the public shader replacement parser");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const std::string malformed="ps_5_0\nmov r0.x, l(1.0\nret\n";
    std::vector<char> text(malformed.begin(),malformed.end());
    bool rejected=false;
    try {assembler(&text,original);} catch(const AssemblerParseError& error) {rejected=error.line_no==2;}
    require_batch(rejected,"F142: user assembly missing a delimiter reports line 2 instead of hanging");
    std::vector<AssemblerParseError> warnings;
    const auto warned=assembler(&text,original,&warnings);
    require_batch(warnings.size()==1 && warnings.front().line_no==2 && !warned.empty(),
        "F142: explicit warning collection terminates and follows the existing continue policy");
}

std::vector<unsigned char> audit_shader_code(const std::vector<unsigned char>& binary)
{
    require_batch(binary.size()>=32,"shader code review: complete DXBC header");
    uint32_t count=0; std::memcpy(&count,binary.data()+28,4);
    require_batch(count<=(binary.size()-32)/4,"shader code review: complete chunk table");
    for(uint32_t i=0;i<count;++i) {
        uint32_t offset=0,length=0; std::memcpy(&offset,binary.data()+32+4*i,4);
        require_batch(offset<=binary.size()-8,"shader code review: complete chunk header");
        std::memcpy(&length,binary.data()+offset+4,4);
        require_batch(length<=binary.size()-offset-8,"shader code review: complete chunk payload");
        if(!std::memcmp(binary.data()+offset,"SHEX",4) || !std::memcmp(binary.data()+offset,"SHDR",4))
            return {binary.begin()+offset+8,binary.begin()+offset+8+length};
    }
    throw std::runtime_error("shader code review: native shader has no code chunk");
}

void test_f145_review()
{
    test_dx11_precision_delimiter_review();
    for(const auto* source : {"float4 main():SV_Target{return float4(0.02083333395421505,1.0000001192092896,-12345.6787109375,1.1754943508222875e-38);}",
            "float4 main(float4 p:SV_Position):SV_Target{const float4 values[2]={float4(0.123456789,2.23456789,3,4),float4(5.3456789,6,7,8)};return values[(uint)p.x%2];}"}) {
        ComPtr<ID3DBlob> code,errors;
        require_batch(SUCCEEDED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F145 review: actual native compiler creates scalar/vector and indexed constant shaders");
        const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
        std::vector<unsigned char> original(data,data+code->GetBufferSize()),text;
        require_batch(SUCCEEDED(disassembler(&original,&text,nullptr)),"F145 review: actual native disassembly and precision correction terminate");
        std::vector<char> input(text.begin(),text.end());
        const auto rebuilt=assembler(&input,original);
        require_batch(audit_shader_code(rebuilt)==audit_shader_code(original),"F145 review: all native instruction bits survive precision round trip");
        BatchDevice device; ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
            "F145 review: native WARP accepts the complete corrected shader");
    }
}

void test_f146()
{
    test_dx11_assembler_line_bounds_regression();
    const char source[]="float4 main():SV_Target{return float4(0.125,0.25,0.5,1);}";
    ComPtr<ID3DBlob> code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F146: actual original shader for bounded user assembly");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> original(data,data+code->GetBufferSize()),text;
    require_batch(SUCCEEDED(disassembler(&original,&text,nullptr)),"F146: actual assembly fixture");
    while(!text.empty() && (text.back()=='\n' || text.back()=='\r')) text.pop_back();
    std::vector<char> input(text.begin(),text.end());
    require_batch(!input.empty() && input.back()!=0 && input.back()!='\n',"F146: real user input has no NUL or final newline");
    const auto rebuilt=assembler(&input,original);
    require_batch(audit_shader_code(rebuilt)==audit_shader_code(original),"F146: no-newline assembly preserves exact native instruction bytes");
    BatchDevice device; ComPtr<ID3D11PixelShader> shader;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
        "F146: WARP accepts the complete bounded result");
    std::vector<char> empty; bool rejected=false;
    try {assembler(&empty,original);} catch(const AssemblerParseError&) {rejected=true;}
    require_batch(rejected,"F146: an empty public input reaches the existing missing-model error instead of reading null");
}

class AuditAnalysisDeviceScope {
    BatchDevice& device;
    std::unique_ptr<HackerContext> original;
public:
    FrameAnalysisContext* context=nullptr;
    explicit AuditAnalysisDeviceScope(BatchDevice& value):device(value)
    {
        auto analysis=std::make_unique<FrameAnalysisContext>(device.native.Get(),device.context.Get());
        analysis->SetHackerDevice(device.device); context=analysis.get();
        original=std::move(device.wrapper); device.wrapper=std::move(analysis);
        device.device->SetHackerContext(context); device.state.mHackerContext=context;
    }
    ~AuditAnalysisDeviceScope()
    {
        auto analysis=std::move(device.wrapper);
        device.wrapper=std::move(original);
        device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext=device.wrapper.get();
        analysis.reset();
    }
};

std::string audit_read_native_buffer(BatchDevice& device,ID3D11Buffer* input)
{
    D3D11_BUFFER_DESC desc{}; input->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc,nullptr,&staging)),"F169: real staging buffer");
    device.context->CopyResource(staging.Get(),input);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"F169: read the real uploaded bytes");
    const std::string bytes(static_cast<const char*>(mapped.pData),desc.ByteWidth);
    device.context->Unmap(staging.Get(),0);
    return bytes;
}

std::map<std::wstring,std::string> audit_update_dump_files(const std::filesystem::path& path)
{
    std::map<std::wstring,std::string> files;
    for(const auto& file:std::filesystem::directory_iterator(path)) {
        if(file.is_regular_file() && file.path().extension()==L".buf" && file.path().filename().wstring().find(L"-update")!=std::wstring::npos)
            files.emplace(file.path().filename().wstring(),read_file(file.path()));
    }
    return files;
}

void test_f169()
{
    AuditDumpScope output; BatchDevice device; AuditAnalysisDeviceScope analysis(device);
    auto target=buffer(device,16,D3D11_BIND_VERTEX_BUFFER);
    const auto options=static_cast<FrameAnalysisOptions>(FrameAnalysisOptions::DUMP_ON_UPDATE | FrameAnalysisOptions::FMT_BUF_BIN);
    G->cur_analyse_options=options;
    std::array<UINT,4> values{11,22,33,44};
    auto expected=[&] {return std::string(reinterpret_cast<const char*>(values.data()),sizeof(values));};
    auto verify_upload=[&] {require_batch(audit_read_native_buffer(device,target.Get())==expected(),"F169: capture controls never change native upload data");};
    auto files=audit_update_dump_files(output.path);
    auto verify_dump=[&] {
        const auto next=audit_update_dump_files(output.path);
        require_batch(next.size()==files.size()+1,"F169: exactly one new capture is emitted per enabled update");
        for(const auto& entry:files) require_batch(next.at(entry.first)==entry.second,"F169: previous captures remain unchanged");
        for(const auto& entry:next) if(!files.count(entry.first)) require_batch(entry.second==expected(),"F169: capture contains the complete updated bytes");
        files=next;
    };
    std::fputs("F169: UpdateSubresource1 must emit one dump of the completed native upload\n",stderr); std::fflush(stderr);
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0);
    verify_upload(); verify_dump();
    const UINT partial[]={77,88}; const D3D11_BOX box{4,0,0,12,1,1};
    values[1]=partial[0]; values[2]=partial[1];
    analysis.context->UpdateSubresource1(target.Get(),0,&box,partial,0,0,0);
    verify_upload(); verify_dump();
    G->analyse_frame=false; values[0]=99;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: disabled analysis adds no capture");
    G->analyse_frame=true; G->cur_analyse_options=FrameAnalysisOptions::FMT_BUF_BIN; values[0]=111;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: missing dump_on_update adds no capture");
    G->cur_analyse_options=options; values[0]=222;
    analysis.context->UpdateSubresource(target.Get(),0,nullptr,values.data(),0,0); verify_upload(); verify_dump();
    const auto blocked=output.path/L"blocked-parent";
    {std::ofstream file(blocked,std::ios::binary); file<<"not a directory"; file.close(); require_batch(file.good(),"F169: actual filesystem capture failure fixture");}
    wcscpy_s(G->ANALYSIS_PATH,MAX_PATH,blocked.c_str()); values[0]=333;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: failed capture does not replace earlier output");
    wcscpy_s(G->ANALYSIS_PATH,MAX_PATH,output.path.c_str()); values[0]=444;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload(); verify_dump();
}

void test_f186()
{
    AuditConfigScope config;
    const auto cache=config.config.parent_path()/L"regex-cache-audit";
    require_batch(std::filesystem::create_directory(cache),"F186: exclusive on-disk cache fixture");
    const auto settings=[](const char* alpha) {
        return std::string("[Rendering]\ncache_shaders=1\ncache_directory=regex-cache-audit\nshader_hash=fnv\n")+
            "[ShaderRegexAuditTail]\nshader_model=ps_5_0\nfilter_index=7.25\n"
            "[ShaderRegexAuditTail.Pattern]\nmov\\s+o0\\.xyzw,\\s*l\\(0\\.250000,\\s*0\\.500000,\\s*0\\.750000,\\s*1\\.000000\\)\n"
            "[ShaderRegexAuditTail.Pattern.Replace]\nmov o0.xyzw, l(0.250000, 0.500000, 0.750000, "+alpha+")\n";
    };
    config.load(settings("0.500000").c_str(),0,true);
    require_batch(shader_regex_groups.size()==1,"F186: production parser accepts the real regex group");
    const uint32_t first_hash=shader_regex_hash;
    config.load(settings("0.750000").c_str(),0,true);
    std::fputs("F186: changing only a replacement line's UTF-16 tail must invalidate its cache key\n",stderr); std::fflush(stderr);
    require_batch(shader_regex_hash!=first_hash,"F186: complete INI text contributes to the real configuration hash");

    BatchDevice device; ComPtr<ID3DBlob> code,errors;
    const char source[]="float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),"F186: real original shader");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const UINT64 shader_hash=fnv_64_buf(original.data(),original.size());
    wchar_t stem[64]{}; swprintf_s(stem,L"%016llx-ps_regex",shader_hash);
    const auto meta=cache/(std::wstring(stem)+L".dat");
    const auto create_patch=[&](const char* alpha,unsigned char expected_alpha) {
        config.load(settings(alpha).c_str(),0,true);
        auto binary=original; std::vector<unsigned char> assembly;
        require_batch(SUCCEEDED(disassembler(&binary,&assembly,nullptr)),"F186: production disassembly input");
        std::string text(assembly.begin(),assembly.end()),model="ps_5_0"; std::wstring tagline;
        const bool changed=apply_shader_regex_groups(&text,L"ps",&model,shader_hash,&tagline);
        if(!changed) {std::fprintf(stderr,"F186 native assembly:\n%s\n",text.c_str()); std::fflush(stderr);}
        require_batch(changed,"F186: real PCRE group patches the expected native instruction");
        std::vector<char> input(text.begin(),text.end());
        auto patched=assembler(&input,original);
        save_shader_regex_cache_bin(shader_hash,L"ps",&patched);
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(patched.data(),patched.size(),nullptr,&shader)),"F186: native WARP accepts the real regex output");
        require_batch(render_dx11_audit_constant_pixel(device,shader.Get())==std::array<unsigned char,4>{64,128,191,expected_alpha},
            "F186: real GPU output distinguishes the replacement tail");
        std::vector<unsigned char> cached; std::wstring cached_tagline;
        require_batch(load_shader_regex_cache(shader_hash,L"ps",&cached,&cached_tagline)==ShaderRegexCache::PATCH && cached==patched,
            "F186: current-version cache hit returns exact patched bytes");
        uint32_t version=0; std::ifstream file(meta,std::ios::binary); file.read(reinterpret_cast<char*>(&version),sizeof(version));
        require_batch(file.good() && version==2,"F186: writer persists the explicit new cache version");
        return patched;
    };
    const auto patched_a=create_patch("0.500000",128);
    config.load(settings("0.750000").c_str(),0,true);
    auto unchanged=original; std::wstring tagline=L"unchanged";
    require_batch(load_shader_regex_cache(shader_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE && unchanged==original && tagline==L"unchanged",
        "F186: reload rejects stale configuration cache without altering output");
    require_batch(!G->mShaderOverrideMap.count(shader_hash),"F186: stale cache rejection links no old group/filter");
    {
        const uint32_t legacy[]={1,shader_regex_hash,0,0};
        std::ofstream file(meta,std::ios::binary|std::ios::trunc); file.write(reinterpret_cast<const char*>(legacy),sizeof(legacy)); file.close();
        require_batch(file.good(),"F186: isolated legacy-version metadata fixture");
    }
    require_batch(load_shader_regex_cache(shader_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE && unchanged==original && tagline==L"unchanged",
        "F186: version 1 is rejected even when its hash field matches the current configuration");
    const auto patched_b=create_patch("0.750000",191);
    require_batch(patched_a!=patched_b,"F186: changed replacement produces distinct real bytecode");
    const char no_match_source[]="float4 main():SV_Target{return float4(0.125,0.25,0.5,1);}";
    code.Reset();
    require_batch(SUCCEEDED(D3DCompile(no_match_source,sizeof(no_match_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),"F186: genuine nonmatching original shader");
    data=static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> no_match(data,data+code->GetBufferSize()),assembly;
    const UINT64 no_match_hash=fnv_64_buf(no_match.data(),no_match.size());
    require_batch(SUCCEEDED(disassembler(&no_match,&assembly,nullptr)),"F186: real nonmatching disassembly");
    std::string text(assembly.begin(),assembly.end()),model="ps_5_0"; tagline.clear();
    require_batch(!apply_shader_regex_groups(&text,L"ps",&model,no_match_hash,&tagline),"F186: nonmatching shader is not patched");
    require_batch(load_shader_regex_cache(no_match_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_MATCH,"F186: new-version negative cache remains usable");
    wchar_t no_match_stem[64]{}; swprintf_s(no_match_stem,L"%016llx-ps_regex.dat",no_match_hash);
    {std::ofstream file(cache/no_match_stem,std::ios::binary|std::ios::trunc); const uint32_t truncated=2; file.write(reinterpret_cast<const char*>(&truncated),sizeof(truncated)); file.close(); require_batch(file.good(),"F186: owned truncated metadata fixture");}
    require_batch(load_shader_regex_cache(no_match_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE,"F186: incomplete metadata remains a cache miss");
    config.load("",0,true);
}

void test_f199_review()
{
    AuditConfigScope config;
    config.load("[ShaderRegexAuditSingle]\nshader_model=ps_5_0\n[ShaderRegexAuditSingle.Pattern]\nbefore\n"
        "[ShaderRegexAuditSingle.Pattern.Replace]\nafter\n",0,true);
    require_batch(shader_regex_groups.size()==1 && shader_regex_groups.begin()->second.patterns.size()==1,
        "F199 review: supported INI syntax creates exactly one real pattern");
    config.load("[ShaderRegexAuditCase]\nshader_model=ps_5_0\n[ShaderRegexAuditCase.Pattern]\nbefore\n"
        "[ShaderRegexAuditCase.pattern]\nafter\n[ShaderRegexAuditCase.Pattern.Replace]\nresult\n",0,true);
    require_batch(shader_regex_groups.size()==1 && shader_regex_groups.begin()->second.patterns.size()==1,
        "F199 review: case variants are one case-insensitive INI section, not multiple patterns");
    config.load("[ShaderRegexAuditNamed]\nshader_model=ps_5_0\n[ShaderRegexAuditNamed.PatternA]\nbefore\n"
        "[ShaderRegexAuditNamed.PatternA.Replace]\nafter\n[ShaderRegexAuditNamed.PatternB]\nsecond\n"
        "[ShaderRegexAuditNamed.PatternB.Replace]\nthird\n",0,true);
    require_batch(shader_regex_groups.empty(),"F199 review: named multiple patterns are rejected by the actual parser");
    config.load("[ShaderRegexAuditA]\nshader_model=ps_5_0\n[ShaderRegexAuditA.Pattern]\nbefore\n"
        "[ShaderRegexAuditB]\nshader_model=ps_5_0\n[ShaderRegexAuditB.Pattern]\nafter\n",0,true);
    require_batch(shader_regex_groups.size()==2,"F199 review: separate INI groups stay separate");
    for(const auto& group:shader_regex_groups)
        require_batch(group.second.patterns.size()==1,"F199 review: no production group gained a second pattern");
    ShaderRegexGroup synthetic;
    std::string first="before",missing="does_not_exist";
    require_batch(synthetic.patterns[L"first"].compile(&first) && synthetic.patterns[L"second"].compile(&missing),
        "F199 review: explicit synthetic multi-pattern local probe");
    synthetic.patterns[L"first"].do_replace=true; synthetic.patterns[L"first"].replace="after";
    std::string text="before"; bool matched=false,patched=false;
    synthetic.apply_regex_patterns(&text,&matched,&patched);
    std::fprintf(stderr,"F199 synthetic-only observation: match=%d patch=%d changed=%d; not a production reachability proof\n",matched,patched,text!="before");
    config.load("",0,true);
}

struct AuditCase { const char* id; void (*run)(); };
const AuditCase audit_cases[] = {{"F-003", test_dx11_file_publication_regression},
    {"F-005", test_dx11_device_initialization_regression},
    {"F-007", test_f007},
    {"F-008", test_dx11_vertex_dump_bounds_regression},
    {"F-324", test_dx11_appended_vertex_layout_regression},
    {"F-100", test_dx11_decompiler_literal_regression},
    {"F-108", test_dx11_resource_filename_regression},
    {"F-110", test_dx11_pool_access_regression},
    {"F-111", test_dx11_decompile_file_diagnostic_regression},
    {"F-137", test_f137},
    {"F-138", test_f138},
    {"F-143", test_f143},
    {"F-142", test_f142},
    {"F-145", test_f145_review},
    {"F-146", test_f146},
    {"F-148", test_dx11_shader_tool_read_regression},
    {"F-149", test_dx11_shader_tool_write_regression},
    {"F-155", test_dx11_automatic_draw_count_regression},
    {"F-169", test_f169},
    {"F-170", test_dx11_shader_hash_miss_regression},
    {"F-171", test_dx11_shader_header_file_regression},
    {"F-175", test_dx11_resource_override_validation_regression},
    {"F-186", test_f186},
    {"F-194", test_dx11_ini_upload_failure_regression},
    {"F-199", test_f199_review},
    {"F-201", test_dx11_shader_model_bounds_regression},
    {"F-001", test_f001}, {"F-106", test_f106}, {"F-114", test_f114},
    {"F-127", test_dx11_dx9_constant_reset_regression},
    {"F-135", test_f135},
    {"F-140", test_dx11_assembler_double_index_regression},
    {"F-144", test_dx11_assembler_interpolation_regression},
    {"F-150", test_dx11_signature_register_eof_regression},
    {"F-151", test_dx11_signature_feature_comment_regression},
    {"F-153", test_dx11_shader_replacement_ownership_regression},
    {"F-167", test_dx11_finish_failure_regression},
    {"F-172", test_dx11_shader_diagnostic_failure_regression},
    {"F-177", test_dx11_texture_initial_debug_regression},
    {"F-193", test_f193},
    {"F-179", test_dx11_empty_hunting_present_regression},
    {"F-184", test_dx11_reload_shader_filename_regression},
    {"F-187", test_dx11_masked_flags_regression},
    {"F-286", test_dx11_crash_help_regression},
    {"F-294", test_dx11_ini_enumeration_regression},
    {"F-295", test_dx11_stencil_diagnostic_regression},
    {"F-298", test_dx11_view_reference_null_diagnostic_regression},
    {"P-01", test_dx11_ini_lookup_performance},
#ifdef DX11_AUDIT_ASAN_LOCKS
    {"F-115", test_dx11_lock_cleanup_regression},
#endif
};
}

void with_dx11_audit_hunting_configuration(void (*scenario)())
{
    AuditConfigScope config;
    // Use the supported eager-initialization option and the actual loader;
    // do not fake the gConfigInitialized state used by Present.
    config.load("", HUNTING_MODE_ENABLED, true);
    require_batch(G->gConfigInitialized && G->hunting == HUNTING_MODE_ENABLED,
        "hunting audit: real eager configuration initialization completed");
    scenario();
}

bool test_dx11_audit_item(const char* id)
{
    for (const auto& test : audit_cases) {
        if (!std::strcmp(test.id, id)) { test.run(); return true; }
    }
    return false;
}

void test_dx11_audit_regressions()
{
    for (const auto& test : audit_cases) {
        test.run();
        std::printf("PASS audit item: %s\n", test.id);
    }
}

void test_dx11_vertex_dump_gpu_regression()
{
    using namespace dx11_slot_test;
    AuditDumpScope output; BatchDevice device;
    auto original = std::move(device.wrapper);
    auto analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
    analysis->SetHackerDevice(device.device);
    auto* context = analysis.get(); device.wrapper = std::move(analysis);
    device.device->SetHackerContext(context); device.state.mHackerContext = context;
    try {
        const char source[] = "float4 main(float4 p:DATA):SV_Position{return p;}";
        ComPtr<ID3DBlob> code, errors;
        require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)),
            "F008: compile actual input signature");
        ComPtr<ID3D11VertexShader> shader;
        require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "F008: native shader for actual public frame analysis");
        D3D11_INPUT_ELEMENT_DESC desc{"DATA",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        ComPtr<ID3D11InputLayout> layout;
        require_batch(SUCCEEDED(device.device->CreateInputLayout(&desc, 1, code->GetBufferPointer(), code->GetBufferSize(), &layout)),
            "F008: real cached production input layout");
        auto vertices = buffer(device, 32, D3D11_BIND_VERTEX_BUFFER);
        const float data[] = {1,2,3,4,5,6,7,8};
        device.context->UpdateSubresource(vertices.Get(), 0, nullptr, data, 0, 0);
        ID3D11Buffer* bound = vertices.Get(); UINT stride = 4, offset = 0;
        context->IASetInputLayout(layout.Get()); context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
        context->VSSetShader(shader.Get(), nullptr, 0); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        const auto options = static_cast<FrameAnalysisOptions>(static_cast<unsigned>(FrameAnalysisOptions::DUMP_VB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) | static_cast<unsigned>(FrameAnalysisOptions::MONO));
        context->FrameAnalysisTrigger(options); context->Draw(8, 0);
        require_batch(output.payload_contains("DATA: 1, 2, 3, 4") && output.payload_contains("DATA: 5, 6, 7, 8") &&
            output.payload_contains("exceeds mapped buffer"), "F008: actual WARP buffer staging and public dump preserve complete fields and diagnose the tail");
    } catch (...) {
        device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
        throw;
    }
    device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
}

void test_dx11_decompiler_public_literal_regression()
{
    using namespace dx11_slot_test;
    const char source[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> code, errors, assembly;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F100: native compiler produces a real literal shader");
    require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
        "F100: actual native assembly input");
    std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    DecompilerSettings settings;
    ParseParameters params{}; params.bytecode = code->GetBufferPointer(); params.decompiled = text.c_str();
    params.decompiledSize = text.size(); params.G = &settings;
    bool patched = false, failed = false; std::string model;
    const auto hlsl = DecompileBinaryHLSL(params,patched,model,failed);
    require_batch(!failed && !hlsl.empty(), "F100: real public decompiler preserves a normal four-component shader");
    ComPtr<ID3DBlob> rebuilt;
    require_batch(SUCCEEDED(D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors)),
        "F100: regenerated HLSL compiles with the actual compiler");
    BatchDevice device; ComPtr<ID3D11PixelShader> original, replacement;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&original)) &&
        SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&replacement)),
        "F100: native device accepts original and decompiled shaders");
    const auto expected = render_dx11_audit_constant_pixel(device, original.Get());
    require_batch(expected == std::array<unsigned char,4>{64,128,191,255} &&
        render_dx11_audit_constant_pixel(device,replacement.Get()) == expected, "F100: actual rendered literal values remain unchanged");
    const auto begin = text.find("l(");
    require_batch(begin != std::string::npos, "F100: real disassembly contains the fixture literal");
    const auto end = text.find(')',begin);
    require_batch(end != std::string::npos, "F100: fixture has a complete literal");
    text.replace(begin,end-begin+1,"l(1,2)");
    params.decompiled = text.c_str(); params.decompiledSize = text.size();
    failed = false; patched = false;
    (void)DecompileBinaryHLSL(params,patched,model,failed);
    require_batch(failed, "F100: unavailable components propagate the existing public error flag used by native shader fallback");
}
