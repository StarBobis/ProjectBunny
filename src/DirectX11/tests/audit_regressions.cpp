#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "IniHandler.h"
#include "Input.h"
#include "vkeys.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include "D3D_Shaders/stdafx.h"

void test_dx11_dx9_constant_reset_regression();
void test_dx11_assembler_double_index_regression();
void test_dx11_assembler_interpolation_regression();
void test_dx11_signature_register_eof_regression();
void test_dx11_signature_feature_comment_regression();
void test_dx11_shader_replacement_ownership_regression();
void test_dx11_finish_failure_regression();

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
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants)
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Hunting]\nhunting=0\n[Include]\nuser_config=" << settings.filename().string() << "\n[Constants]\n" << constants;
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

struct AuditCase { const char* id; void (*run)(); };
const AuditCase audit_cases[] = {{"F-001", test_f001}, {"F-106", test_f106}, {"F-114", test_f114},
    {"F-127", test_dx11_dx9_constant_reset_regression},
    {"F-135", test_f135},
    {"F-140", test_dx11_assembler_double_index_regression},
    {"F-144", test_dx11_assembler_interpolation_regression},
    {"F-150", test_dx11_signature_register_eof_regression},
    {"F-151", test_dx11_signature_feature_comment_regression},
    {"F-153", test_dx11_shader_replacement_ownership_regression},
    {"F-167", test_dx11_finish_failure_regression},
#ifdef DX11_AUDIT_ASAN_LOCKS
    {"F-115", test_dx11_lock_cleanup_regression},
#endif
};
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
