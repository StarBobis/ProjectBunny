#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "vkeys.h"
#include "HLSLDecompiler/DecompileHLSL.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    for (const char* invalid : {"\n", "// no shader model\n", "", "ret\n"}) {
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
    void load(const char* constants, int hunting = 0, bool eager = false)
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n";
        if (eager) output << "config_initialization_delay=-1\n";
        output << "[Hunting]\nhunting=" << hunting << "\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants;
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

void test_f137()
{
    const auto saved = G->decompiler_settings;
    AuditConfigScope config;
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1=FirstDepth.w\nfix_ZRepair_DepthTexture2=SecondDepth.z\n");
    for (const auto* invalid : {"x", ".x", "ab", "Depth.q", "Depthx", "Depth.xy", ".w"}) {
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=") + invalid +
            "\nfix_ZRepair_DepthTexture2=" + invalid + "\n";
        std::fprintf(stderr, "F137: reject malformed texture selector %s before splitting\n", invalid); std::fflush(stderr);
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "FirstDepth" && G->decompiler_settings.ZRepair_DepthTextureReg1 == 'w' &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "SecondDepth" && G->decompiler_settings.ZRepair_DepthTextureReg2 == 'z',
            "F137: both invalid selectors are rejected without publishing partial settings");
    }
    for (const char component : {'x','y','z','w'}) {
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=  SceneDepthTexture.") + component +
            " \t\nfix_ZRepair_DepthTexture2=OtherDepth." + component + "\n";
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "SceneDepthTexture" &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "OtherDepth" &&
            G->decompiler_settings.ZRepair_DepthTextureReg1 == component && G->decompiler_settings.ZRepair_DepthTextureReg2 == component,
            "F137: preserve exact texture names and all four valid component suffixes");
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
